#include <cstdio>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include <QTimer>

#include "playlistwindow.h"
#include "skinutils.h"
#include "translator.h"
#include "winampbitmaps.h"
#include "winampwindow.h"
#include "qt5compat.h"

#ifdef WINAMP_HAVE_WASABIQT
#  include <WasabiQt/SkinXml.h>
#  include <WasabiQt/SkinView.h>
#  include <WasabiQt/SkinRuntime.h>
#  include <WasabiQt/Layout.h>
#  include <WasabiQt/BitmapRegistry.h>
#  include <WasabiQt/Host.h>
#  include <QAudioBuffer>
#  include <QAudioBufferOutput>
#  include <QAudioOutput>
#  include <QImage>
#  include <QKeyEvent>
#  include <QMediaPlayer>
#  include <QMouseEvent>
#  include <QUrl>
#  include <QWindow>
#  include <cmath>
namespace {
// Resolver passed to qtWasabi's Layout::hitTest so widgets that
// take their pixel size from a named bitmap (typical for buttons)
// hit-test correctly.
QSize qtampImageSize(const QString &bitmapId, void *userdata) {
    auto *registry = static_cast<WasabiQt::BitmapRegistry *>(userdata);
    if (!registry) return QSize();
    const auto *def = registry->find(bitmapId);
    if (!def) return QSize();
    if (!def->srcRect.isEmpty()) return def->srcRect.size();
    // Whole-image: load the image once and return its size.
    QImage img = registry->imageFor(*def);
    return img.size();
}
}  // namespace
#endif

#ifdef WINAMP_HAVE_WASABIQT

// QtampHost — Qtamp's WasabiQt::Host implementation.  Shovels live
// QMediaPlayer state through the abstract Host interface qtWasabi
// expects, so qtWasabi's default DisplayResolver + dispatchAction
// helpers can do the actual skin-format-convention work.
class QtampPlayerWindow;
class QtampHost : public QObject, public WasabiQt::Host {
public:
    QtampHost() {
        m_player.setAudioOutput(&m_audio);
        m_audio.setVolume(qreal(0.7));

        // Audio-buffer tap so <vis> bars can bounce with the audio.
        // Qt 6.7+: QMediaPlayer routes raw PCM to a connected
        // QAudioBufferOutput in addition to the audio sink.
        m_player.setAudioBufferOutput(&m_bufOut);
        QObject::connect(&m_bufOut,
                         &QAudioBufferOutput::audioBufferReceived,
                         this, &QtampHost::onAudioBuffer);
    }

    void bindWindow(QtampPlayerWindow *w) { m_window = w; }

    QMediaPlayer       &player()       { return m_player; }
    const QMediaPlayer &player() const { return m_player; }

    // ── Read state ─────────────────────────────────────────────
    qint64  positionMs() const override { return m_player.position(); }
    qint64  durationMs() const override { return m_player.duration(); }
    bool    isPlaying() const override {
        return m_player.playbackState() == QMediaPlayer::PlayingState;
    }
    bool    isPaused() const override {
        return m_player.playbackState() == QMediaPlayer::PausedState;
    }
    int     volume() const override {
        return int(qBound(qreal(0), m_audio.volume(), qreal(1)) * 100);
    }
    QString songTitle() const override {
        const QUrl src = m_player.source();
        if (src.isLocalFile())
            return QFileInfo(src.toLocalFile()).completeBaseName();
        return src.toString();
    }
    QString songFilename() const override {
        const QUrl src = m_player.source();
        return src.isLocalFile()
            ? QFileInfo(src.toLocalFile()).fileName()
            : src.toString();
    }

    // ── Transport ──────────────────────────────────────────────
    void play()  override { m_player.play(); }
    void pause() override { m_player.pause(); }
    void stop()  override { m_player.stop(); }
    void seekMs(qint64 ms) override { m_player.setPosition(ms); }
    void setVolume(int v) override {
        m_audio.setVolume(qBound(0, v, 100) / qreal(100));
    }

    // ── EJECT — pick a file AND start playing it.  Overrides the
    //    base default (which only picks) so the EJECT action does
    //    what users expect.
    QUrl pickFile(QWidget *embedder) override;

    // ── Visualisation: smoothed RMS of the most recent audio
    //    buffer in [0..1].
    double audioLevel() const override { return m_audioLevel; }

    // ── Window control — implemented via the bound window.
    bool close()    override;
    bool minimize() override;

private:
    void onAudioBuffer(const QAudioBuffer &buf) {
        if (!buf.isValid() || buf.frameCount() <= 0) return;
        const QAudioFormat fmt = buf.format();
        const int frames = buf.frameCount();
        const int channels = fmt.channelCount();
        const int total = frames * channels;
        if (total <= 0 || channels <= 0) return;

        double sumSq = 0.0;
        switch (fmt.sampleFormat()) {
        case QAudioFormat::Float: {
            const float *d = buf.constData<float>();
            for (int i = 0; i < total; ++i)
                sumSq += double(d[i]) * d[i];
            break;
        }
        case QAudioFormat::Int16: {
            const qint16 *d = buf.constData<qint16>();
            for (int i = 0; i < total; ++i) {
                const double v = d[i] / 32768.0;
                sumSq += v * v;
            }
            break;
        }
        case QAudioFormat::Int32: {
            const qint32 *d = buf.constData<qint32>();
            for (int i = 0; i < total; ++i) {
                const double v = d[i] / 2147483648.0;
                sumSq += v * v;
            }
            break;
        }
        case QAudioFormat::UInt8: {
            const quint8 *d = buf.constData<quint8>();
            for (int i = 0; i < total; ++i) {
                const double v = (int(d[i]) - 128) / 128.0;
                sumSq += v * v;
            }
            break;
        }
        default: return;
        }
        const double rms = std::sqrt(sumSq / total);
        // Asymmetric smoothing — fast attack, slower decay so the
        // bars peak quickly with the audio and fall back gradually.
        const double alpha = (rms > m_audioLevel) ? 0.5 : 0.15;
        m_audioLevel = m_audioLevel * (1.0 - alpha) + rms * alpha;
    }

    QMediaPlayer  m_player;
    QAudioOutput  m_audio;
    QAudioBufferOutput m_bufOut;
    double        m_audioLevel = 0.0;
    QtampPlayerWindow *m_window = nullptr;
};

// Player-window wrapper around qtWasabi's SkinView.  Modern skins
// paint their own chrome (titlebar, buttons, borders), so the host
// window has to be frameless and the click-on-empty-area drag has
// to be implemented by the embedder.  ESC closes; drag-and-move
// works via QWindow::startSystemMove() on Wayland and a manual
// move() fallback elsewhere.  All transport / display logic lives
// in QtampHost; this class is just window shell + input routing.
class QtampPlayerWindow : public WasabiQt::SkinView {
public:
    explicit QtampPlayerWindow(QtampHost *host, QWidget *parent = nullptr)
        : WasabiQt::SkinView(parent), m_host(host) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);

        // Hand the Host to SkinView — paintEvent pulls live
        // display strings AND <slider> thumb positions straight
        // from it.  Supersedes setDisplayResolver().
        setHost(host);

        // 200ms repaint cadence so the time text ticks visibly
        // while playback is running.
        auto *tick = new QTimer(this);
        tick->setInterval(200);
        connect(tick, &QTimer::timeout, this,
                qOverload<>(&QWidget::update));
        tick->start();

        // Immediate repaint when transport state / source changes.
        connect(&host->player(), &QMediaPlayer::sourceChanged, this,
                [this](const QUrl &) { update(); });
        connect(&host->player(), &QMediaPlayer::playbackStateChanged,
                this, [this](QMediaPlayer::PlaybackState) { update(); });
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            const QPoint p = e->position().toPoint();
            QRect hitBbox;
            const auto *hit = WasabiQt::Layout::hitTest(
                tree(), p, /*actionOnly=*/true,
                qtampImageSize, &registry(), &hitBbox);
            if (hit) {
                // <slider> widgets — drag-to-set instead of
                // dispatchAction.  Click jumps the thumb to the
                // click position; mouseMove follows.
                if (hit->tag == QLatin1String("slider")) {
                    m_sliderAction = hit->attrs
                        .value(QStringLiteral("action")).toUpper();
                    m_sliderTrack = hitBbox;
                    applySliderDrag(p.x());
                    update();
                    return;
                }
                // Buttons / togglebuttons → action dispatch.
                const QString action =
                    hit->attrs.value(QStringLiteral("action"));
                fprintf(stderr, "[qtamp] action: %s\n",
                        action.toUpper().toLocal8Bit().constData());
                if (WasabiQt::dispatchAction(action, m_host, this))
                    return;
            }
            // Empty-area click — start a window drag.
            if (windowHandle() && windowHandle()->startSystemMove())
                return;
            m_dragOrigin = e->globalPosition().toPoint() -
                           frameGeometry().topLeft();
            m_dragging = true;
        }
        WasabiQt::SkinView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_sliderAction.isEmpty() &&
            (e->buttons() & Qt::LeftButton)) {
            applySliderDrag(e->position().toPoint().x());
            update();
            return;
        }
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            move(e->globalPosition().toPoint() - m_dragOrigin);
        }
        WasabiQt::SkinView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        m_dragging = false;
        m_sliderAction.clear();
        WasabiQt::SkinView::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        if (e->key() == Qt::Key_Escape) { close(); return; }
        if (ctrl && (e->key() == Qt::Key_O || e->key() == Qt::Key_L)) {
            const QUrl u = m_host->pickFile(this);
            if (!u.isEmpty()) {
                m_host->player().setSource(u);
                m_host->player().play();
            }
            return;
        }
        if (e->key() == Qt::Key_Space) {
            m_host->isPlaying() ? m_host->pause() : m_host->play();
            return;
        }
        if (e->key() == Qt::Key_MediaPlay)  { m_host->play();  return; }
        if (e->key() == Qt::Key_MediaPause) { m_host->pause(); return; }
        if (e->key() == Qt::Key_MediaStop)  { m_host->stop();  return; }
        WasabiQt::SkinView::keyPressEvent(e);
    }

private:
    void applySliderDrag(int xInWindow) {
        if (m_sliderTrack.width() <= 0) return;
        const double v = double(xInWindow - m_sliderTrack.x()) /
                         double(m_sliderTrack.width());
        m_host->setSliderPosition(m_sliderAction,
                                   qBound(0.0, v, 1.0));
    }

    QtampHost *m_host = nullptr;
    QPoint     m_dragOrigin;
    bool       m_dragging = false;
    QString    m_sliderAction;     // empty when not dragging a slider
    QRect      m_sliderTrack;
};

// ── QtampHost methods that need QtampPlayerWindow to be defined ──
inline QUrl QtampHost::pickFile(QWidget *embedder) {
    const QUrl u = WasabiQt::Host::pickFile(embedder);
    if (!u.isEmpty()) {
        m_player.setSource(u);
        m_player.play();
    }
    return u;
}

inline bool QtampHost::close() {
    if (m_window) m_window->close();
    return m_window != nullptr;
}

inline bool QtampHost::minimize() {
    if (m_window) m_window->showMinimized();
    return m_window != nullptr;
}
#endif

namespace {
// Pull --<flag> <path> out of argv before the rest of the player's
// arg parsing runs.  The flag + value get stripped from argv so the
// existing classic-skin code never sees them.
QString takeStringArg(int &argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String(flag) && i + 1 < argc) {
            const QString value = QString::fromLocal8Bit(argv[i + 1]);
            for (int j = i; j + 2 < argc; ++j) argv[j] = argv[j + 2];
            argc -= 2;
            argv[argc] = nullptr;
            return value;
        }
    }
    return {};
}
QString takeModernSkinArg(int &argc, char **argv) {
    return takeStringArg(argc, argv, "--modern-skin");
}
QString takeScreenshotArg(int &argc, char **argv) {
    return takeStringArg(argc, argv, "--screenshot");
}
bool takeFlag(int &argc, char **argv, const char *flag) {
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String(flag)) {
            for (int j = i; j + 1 < argc; ++j) argv[j] = argv[j + 1];
            argc -= 1;
            argv[argc] = nullptr;
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char *argv[]) {
  QString modernSkinPath = takeModernSkinArg(argc, argv);
  QString screenshotPath = takeScreenshotArg(argc, argv);
  const bool listActions = takeFlag(argc, argv, "--list-actions");

  // Modern skins paint their own rounded chrome with alpha-cut
  // corners — the host surface needs an alpha channel for those
  // pixels to actually composite transparently.  Must be set
  // BEFORE QApplication or Wayland gives us an opaque surface
  // and the corners come out as black squares.
  {
      QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
      fmt.setAlphaBufferSize(8);
      QSurfaceFormat::setDefaultFormat(fmt);
  }

  QApplication app(argc, argv);
  app.setApplicationName("Qtamp");
  app.setApplicationVersion("0.5 BETA");
  app.setOrganizationName("Qtamp");

#ifdef WINAMP_HAVE_WASABIQT
  // Modern-skin path — bypass the classic-skin chrome entirely and
  // hand the rendering over to qtWasabi's SkinView.  Accepts either
  // a path to skin.xml or a directory containing one.
  if (!modernSkinPath.isEmpty()) {
    QString skinXml = modernSkinPath;
    if (QFileInfo(skinXml).isDir()) {
      skinXml = QDir(skinXml).filePath("skin.xml");
    }
    if (!QFile::exists(skinXml)) {
      qWarning() << "qtamp: modern skin not found at" << skinXml;
      return 2;
    }

    WasabiQt::SkinXml::Document doc;
    QString err;
    if (!WasabiQt::SkinXml::parse(skinXml, doc, &err)) {
      fprintf(stderr, "qtamp: parse failed: %s\n", err.toLocal8Bit().constData());
      return 3;
    }

    auto *host = new QtampHost();
    auto *view = new QtampPlayerWindow(host);
    host->bindWindow(view);
    if (!view->load(doc, "main", "normal", &err)) {
      fprintf(stderr, "qtamp: layout load failed: %s\n", err.toLocal8Bit().constData());
      return 4;
    }

    // Drive the chrome.  Two paths:
    //  1) Apply the static well-known-script equivalents
    //     (titlebar.m's resizeObjects → streak geometry, etc.) so
    //     the chrome lays out correctly straight away.
    //  2) Hand the resolved tree to qtWasabi's SkinRuntime so the
    //     real .maki bytecode runs through the embedded VM —
    //     onScriptLoaded handlers get to do their thing, future
    //     onResize / onSetXuiParam events flow through too.
    // Set WASABIQT_NO_STATIC_SCRIPTS=1 to skip step 1 (useful for
    // testing whether Maki dispatch alone produces a sane chrome).
    {
        auto &mutableTree = const_cast<WasabiQt::Layout::ResolvedWidget &>(
            view->tree());
        if (!::getenv("WASABIQT_NO_STATIC_SCRIPTS")) {
            const int layoutW = view->layoutNativeSize().width();
            WasabiQt::Layout::runKnownScripts(mutableTree, layoutW);
        }

        // Fire the actual Maki scripts.  Errors are non-fatal — if
        // a binding is missing the runtime logs a guru and moves
        // on; the static-fallback chrome is still visible.
        static WasabiQt::SkinRuntime runtime;
        runtime.loadScripts(doc, mutableTree);
        runtime.dispatchOnScriptLoaded();
        runtime.dispatchXuiParams(mutableTree);
        view->update();
    }

    if (listActions) {
      // Probe hit-test at known coords for each transport button —
      // verifies the qtWasabi hit-test + image-size resolver chain
      // headlessly, no display required.
      static const struct { QPoint p; const char *expect; } probes[] = {
          {{50, 110},  "PLAY"},
          {{15, 9},    "SYSMENU"},
          {{340, 9},   "CLOSE"},
          {{50, 200},  "(no hit)"},  // empty area
          {{20, 110},  "PREV"},
          {{80, 110},  "PAUSE"},
          {{110, 110}, "STOP"},
          {{140, 110}, "NEXT"},
          {{268, 92},  "EJECT"},
      };
      int passed = 0, failed = 0;
      for (const auto &probe : probes) {
        const auto *hit = WasabiQt::Layout::hitTest(
            view->tree(), probe.p, /*actionOnly=*/true,
            qtampImageSize, &view->registry());
        const QByteArray got = hit
            ? hit->attrs.value(QStringLiteral("action")).toLocal8Bit()
            : QByteArray("(no hit)");
        const bool ok = got == QByteArray(probe.expect);
        fprintf(stderr, "  %s probe (%d,%d) -> %s  (expected %s)\n",
                ok ? "PASS" : "FAIL",
                probe.p.x(), probe.p.y(),
                got.constData(), probe.expect);
        ok ? ++passed : ++failed;
      }
      fprintf(stderr, "qtamp: %d/%lu probes passed\n",
              passed, (unsigned long)(sizeof(probes) / sizeof(probes[0])));
      delete view;
      return failed == 0 ? 0 : 6;
    }
    view->setWindowTitle("Qtamp — " + QFileInfo(modernSkinPath).fileName());
    view->resize(view->layoutNativeSize());
    view->show();

    // Visual-debug pipeline mirroring qtWasabi's render_layout: when
    // --screenshot is set, wait for the first paintEvent to land,
    // grab the widget, save PNG, exit.  Use a 0-ms timer + a small
    // delay so AUTOMOC and the initial repaint complete first.
    if (!screenshotPath.isEmpty()) {
      QTimer::singleShot(150, view, [view, screenshotPath]() {
        QPixmap shot = view->grab();
        if (shot.save(screenshotPath)) {
          qInfo() << "qtamp: wrote" << screenshotPath
                  << "(" << shot.width() << "x" << shot.height() << ")";
          QCoreApplication::exit(0);
        } else {
          qWarning() << "qtamp: failed to save" << screenshotPath;
          QCoreApplication::exit(5);
        }
      });
    }
    return app.exec();
  }
#else
  if (!modernSkinPath.isEmpty()) {
    qWarning() << "qtamp: --modern-skin requires building with "
                  "QTAMP_USE_QTWASABI=ON; ignoring.";
  }
#endif

  // Load the Winamp icon from the source resource directory
  QString appDir = QCoreApplication::applicationDirPath();
  QStringList iconCandidates = {
      appDir + "/../share/winamp/resource/WinampIcon.ico",
      appDir + "/../share/icons/hicolor/256x256/apps/winamp.png",
      "/usr/share/winamp/resource/WinampIcon.ico",
      "/usr/share/icons/hicolor/256x256/apps/winamp.png",
      appDir + "/../../Src/Winamp/resource/WinampIcon.ico",
      appDir + "/../Src/Winamp/resource/WinampIcon.ico",
      appDir + "/WinampIcon.ico"};
  for (const QString &iconPath : iconCandidates) {
    if (QFile::exists(iconPath)) {
      app.setWindowIcon(QIcon(iconPath));
      break;
    }
  }

  // Load bitmaps — try saved skin, then project skins/default, then resource
  // dir
  QSettings settings(configPath(), QSettings::IniFormat);
  QString skinPath = settings.value("skin").toString();

  // Load language pack
  QString langCode = settings.value("language", "en").toString();
  Translator::instance().loadLanguage(langCode);

  // Build a list of candidate paths
  QStringList candidates;
  if (!skinPath.isEmpty())
    candidates << skinPath;

  // FHS-standard paths for system-wide install (/usr/bin -> /usr/share/winamp)
  candidates << appDir + "/../share/winamp/skins/default"
             << appDir + "/../share/winamp/resource";

  // Absolute FHS paths (fallback if appDir is not /usr/bin)
  candidates << "/usr/share/winamp/skins/default"
             << "/usr/share/winamp/resource"
             << "/usr/local/share/winamp/skins/default"
             << "/usr/local/share/winamp/resource";

  // Paths relative to the executable (development builds)
  candidates << appDir + "/../skins/default" << appDir + "/../../skins/default"
             << QDir::homePath() + "/.winamp/skins/default";

  // Also try the source resource directory (has MAIN.BMP etc.)
  candidates << appDir + "/../Src/Winamp/resource"
             << appDir + "/../../Src/Winamp/resource";

  // Modern skins are not yet supported on Linux — always load classic skin.
  // bool savedIsModern = !skinPath.isEmpty() && isModernSkinDir(skinPath);

  bool loaded = false;
  for (const QString &path : candidates) {
    QDir d(path);
    // Skip modern skin paths when loading classic bitmaps
    if (isModernSkinDir(d.absolutePath()))
      continue;
    if (d.exists() && WinampBitmaps::instance().loadAll(d.absolutePath())) {
      qDebug() << "Successfully loaded authentic Winamp bitmaps from:"
               << d.absolutePath();
      loaded = true;
      break;
    }
  }

  // If no classic skin loaded yet and saved skin was modern, load default
  // classic as fallback
  if (!loaded) {
    QStringList fallbackClassic = {appDir + "/../share/winamp/skins/default",
                                   appDir + "/../share/winamp/resource",
                                   "/usr/share/winamp/skins/default",
                                   "/usr/share/winamp/resource",
                                   "/usr/local/share/winamp/skins/default",
                                   "/usr/local/share/winamp/resource",
                                   appDir + "/../skins/default",
                                   appDir + "/../../skins/default",
                                   QDir::homePath() + "/.winamp/skins/default",
                                   appDir + "/../Src/Winamp/resource",
                                   appDir + "/../../Src/Winamp/resource"};
    for (const QString &path : fallbackClassic) {
      QDir d(path);
      if (d.exists() && WinampBitmaps::instance().loadAll(d.absolutePath())) {
        loaded = true;
        break;
      }
    }
  }

  // Also try loading any missing bitmaps from all other candidate paths
  if (loaded) {
    for (const QString &path : candidates) {
      QDir d(path);
      if (d.exists()) {
        WinampBitmaps::instance().loadMissing(d.absolutePath());
      }
    }
  }

  if (!loaded) {
    qWarning() << "Could not load Winamp skin bitmaps from any candidate path.";
  }

  // Splash screen (matches Windows SPLASH.cpp)
  QPixmap splashPix;
  QSplashScreen *splash = nullptr;
  QStringList splashCandidates = candidates;
  splashCandidates << appDir + "/../Src/resources"
                   << appDir + "/../../Src/resources";
  for (const QString &path : splashCandidates) {
    QString splashFile = QDir(path).filePath("SPLASH.BMP");
    if (QFile::exists(splashFile)) {
      splashPix.load(splashFile);
      break;
    }
    splashFile = QDir(path).filePath("splash.bmp");
    if (QFile::exists(splashFile)) {
      splashPix.load(splashFile);
      break;
    }
  }
  if (!splashPix.isNull()) {
    splash = new QSplashScreen(splashPix);
    splash->show();
    app.processEvents();
  }

  WinampWindow w;

  // Modern skins are not yet supported on Linux — skip modern skin loading.
  // if (savedIsModern) {
  //     w.onSkinChanged(skinPath);
  // }

  // Process command-line arguments (matches Windows cmdline.cpp)
  QStringList filesToPlay;
  bool enqueueMode = false;
  for (int i = 1; i < argc; i++) {
    QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == "-enqueue" || arg == "--enqueue") {
      enqueueMode = true;
    } else if (arg == "-play" || arg == "--play") {
      // Will auto-play after adding files
    } else if (arg == "-pause" || arg == "--pause") {
      QTimer::singleShot(100, &w, [&w]() {
        if (PLAYBACK_STATE(w.getPlayer()) == QMediaPlayer::PlayingState)
          w.getPlayer()->pause();
      });
    } else if (arg == "-stop" || arg == "--stop") {
      QTimer::singleShot(100, &w, [&w]() { w.getPlayer()->stop(); });
    } else if (arg.startsWith("-")) {
      // Unknown flag — ignore
    } else {
      QFileInfo fi(arg);
      if (fi.isDir()) {
        QDir dir(arg);
        QStringList audioExts = {"*.mp3", "*.wav", "*.ogg", "*.flac",
                                 "*.m4a", "*.aac", "*.wma", "*.opus"};
        for (const QFileInfo &entry :
             dir.entryInfoList(audioExts, QDir::Files, QDir::Name))
          filesToPlay << entry.absoluteFilePath();
      } else if (fi.exists()) {
        filesToPlay << fi.absoluteFilePath();
      }
    }
  }

  // Add CLI files to playlist and optionally play
  if (!filesToPlay.isEmpty()) {
    PlaylistWindow *pl = w.getPlaylistWindow();
    if (pl) {
      for (const QString &f : filesToPlay)
        pl->addTrack(f);
      if (!enqueueMode && pl->trackCount() > 0) {
        pl->setCurrentTrackIndex(0);
        w.playTrack(pl->trackAt(0));
      }
    }
  }

  if (splash) {
    QTimer::singleShot(1500, splash, &QSplashScreen::close);
  }

  w.show();

  return app.exec();
}

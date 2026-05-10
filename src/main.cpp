#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QSplashScreen>
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
#  include <QKeyEvent>
#  include <QMouseEvent>
#  include <QWindow>
#endif

#ifdef WINAMP_HAVE_WASABIQT
// Player-window wrapper around qtWasabi's SkinView.  Modern skins
// paint their own chrome (titlebar, buttons, borders), so the host
// window has to be frameless and the click-on-empty-area drag has
// to be implemented by the embedder.  ESC closes; drag-and-move
// works via QWindow::startSystemMove() on Wayland and a manual
// move() fallback elsewhere.
class QtampPlayerWindow : public WasabiQt::SkinView {
public:
    explicit QtampPlayerWindow(QWidget *parent = nullptr)
        : WasabiQt::SkinView(parent) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            // Wayland / native system-move when available.
            if (windowHandle() && windowHandle()->startSystemMove())
                return;
            m_dragOrigin = e->globalPosition().toPoint() -
                           frameGeometry().topLeft();
            m_dragging = true;
        }
        WasabiQt::SkinView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            move(e->globalPosition().toPoint() - m_dragOrigin);
        }
        WasabiQt::SkinView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        m_dragging = false;
        WasabiQt::SkinView::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) {
            close();
            return;
        }
        WasabiQt::SkinView::keyPressEvent(e);
    }

private:
    QPoint m_dragOrigin;
    bool   m_dragging = false;
};
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
}  // namespace

int main(int argc, char *argv[]) {
  QString modernSkinPath = takeModernSkinArg(argc, argv);
  QString screenshotPath = takeScreenshotArg(argc, argv);

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
      qWarning() << "qtamp: parse failed:" << err;
      return 3;
    }

    auto *view = new QtampPlayerWindow();
    if (!view->load(doc, "main", "normal", &err)) {
      qWarning() << "qtamp: layout load failed:" << err;
      return 4;
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

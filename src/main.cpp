#include <cstdio>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QProcess>
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
#  include <WasabiQt/TreePainter.h>
#  include <QAudioBuffer>
#  include <QAudioBufferOutput>
#  include <QAudioOutput>
#  include <QImage>
#  include <QInputDialog>
#  include <QKeyEvent>
#  include <QMediaPlayer>
#  include <QMenu>
#  include <QMessageBox>
#  include <QMouseEvent>
#  include <QPushButton>
#  include <QStandardPaths>
#  include <QListWidget>
#  include <QListWidgetItem>
#  include <QStackedWidget>
#  include <QTreeWidget>
#  include <QTreeWidgetItem>
#  include <QVBoxLayout>
#  include <QHBoxLayout>
#  include <QDialog>
#  include <QLabel>
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

#include "dialogs.h"
#include "bookmarkmanager.h"
#include "recentfilesmanager.h"
#include "skinutils.h"

#ifdef WINAMP_HAVE_WASABIQT

// (Preferences + About + Jump-to-File + Play-Location dialogs live
// in dialogs.{h,cpp} — ported verbatim from lord3nd3r/winamp-linux
// so the UI matches the upstream player exactly.)
#if 0  /* legacy inline-prefs stub, kept for reference */
class QtampPreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit QtampPreferencesDialog(QWidget *parent = nullptr)
        : QDialog(parent) {
        setWindowTitle(tr("Preferences"));
        resize(640, 420);

        treeWidget = new QTreeWidget(this);
        treeWidget->setHeaderHidden(true);
        treeWidget->setMaximumWidth(180);
        auto *general    = new QTreeWidgetItem({ tr("General") });
        auto *skins      = new QTreeWidgetItem({ tr("Skins") });
        auto *modernSkin = new QTreeWidgetItem(skins, { tr("Modern Skins") });
        auto *playback   = new QTreeWidgetItem({ tr("Playback") });
        auto *bookmarks  = new QTreeWidgetItem({ tr("Bookmarks") });
        auto *vis        = new QTreeWidgetItem({ tr("Visualization") });
        treeWidget->addTopLevelItem(general);
        treeWidget->addTopLevelItem(skins);
        treeWidget->addTopLevelItem(playback);
        treeWidget->addTopLevelItem(bookmarks);
        treeWidget->addTopLevelItem(vis);
        skins->setExpanded(true);

        stackedWidget = new QStackedWidget(this);
        m_pageGeneral   = stackedWidget->addWidget(makePlaceholder(tr("General settings")));
        m_pageSkins     = stackedWidget->addWidget(makePlaceholder(tr("Pick a sub-category.")));
        m_pageModern    = stackedWidget->addWidget(createModernSkinsPage());
        m_pagePlayback  = stackedWidget->addWidget(makePlaceholder(tr("Playback")));
        m_pageBookmarks = stackedWidget->addWidget(makePlaceholder(tr("Bookmarks")));
        m_pageVis       = stackedWidget->addWidget(makePlaceholder(tr("Visualization")));

        QHash<QTreeWidgetItem *, int> pageOf;
        pageOf[general]    = m_pageGeneral;
        pageOf[skins]      = m_pageSkins;
        pageOf[modernSkin] = m_pageModern;
        pageOf[playback]   = m_pagePlayback;
        pageOf[bookmarks]  = m_pageBookmarks;
        pageOf[vis]        = m_pageVis;
        connect(treeWidget, &QTreeWidget::currentItemChanged,
                this, [this, pageOf](QTreeWidgetItem *cur, QTreeWidgetItem *) {
            const int idx = pageOf.value(cur, -1);
            if (idx >= 0) stackedWidget->setCurrentIndex(idx);
        });
        treeWidget->setCurrentItem(modernSkin);   // open on skin picker

        auto *closeBtn = new QPushButton(tr("Close"), this);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        auto *row = new QHBoxLayout;
        row->addWidget(treeWidget);
        row->addWidget(stackedWidget, 1);

        auto *col = new QVBoxLayout(this);
        col->addLayout(row, 1);
        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch(1);
        btnRow->addWidget(closeBtn);
        col->addLayout(btnRow);
    }

signals:
    void skinChanged(const QString &skinPath);

private:
    QWidget *makePlaceholder(const QString &text) {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        auto *lbl = new QLabel(text, w);
        lbl->setAlignment(Qt::AlignCenter);
        l->addWidget(lbl);
        return w;
    }

    QWidget *createModernSkinsPage() {
        auto *w = new QWidget;
        auto *l = new QVBoxLayout(w);
        l->addWidget(new QLabel(tr("Modern Skins"), w));
        m_modernList = new QListWidget(w);
        l->addWidget(m_modernList, 1);
        populateModernSkins();
        connect(m_modernList, &QListWidget::itemActivated,
                this, &QtampPreferencesDialog::onModernSkinSelected);
        auto *applyBtn = new QPushButton(tr("Apply"), w);
        connect(applyBtn, &QPushButton::clicked, this, [this]() {
            if (m_modernList->currentItem())
                onModernSkinSelected(m_modernList->currentItem());
        });
        l->addWidget(applyBtn);
        return w;
    }

    void populateModernSkins() {
        // Scan ~/.winamp/skins/<name>/skin.xml — the upstream
        // location every Modern-skin distribution drops into.
        const QString skinsRoot = QDir::homePath() +
                                  QStringLiteral("/.winamp/skins");
        QDir d(skinsRoot);
        if (!d.exists()) {
            m_modernList->addItem(tr("(no ~/.winamp/skins directory)"));
            return;
        }
        const auto entries = d.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : entries) {
            const QString path = d.absoluteFilePath(name) +
                                  QStringLiteral("/skin.xml");
            if (QFile::exists(path)) {
                auto *item = new QListWidgetItem(name, m_modernList);
                item->setData(Qt::UserRole, path);
            }
        }
    }

    void onModernSkinSelected(QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) emit skinChanged(path);
    }

    QTreeWidget    *treeWidget;
    QStackedWidget *stackedWidget;
    QListWidget    *m_modernList = nullptr;
    int m_pageGeneral=0, m_pageSkins=0, m_pageModern=0,
        m_pagePlayback=0, m_pageBookmarks=0, m_pageVis=0;
};
#endif  /* legacy inline-prefs stub */

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
    int     channelCount() const override { return m_lastChannels; }
    int     sampleRate()   const override { return m_lastSampleRate; }
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
        m_lastChannels   = channels;
        m_lastSampleRate = fmt.sampleRate();

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
    int           m_lastChannels   = 0;
    int           m_lastSampleRate = 0;
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
    // Keep the parsed skin Document around so we can spin off
    // secondary windows (EQ / Playlist) that load other containers
    // from the same skin on demand.
    void setSkinDocument(WasabiQt::SkinXml::Document doc) {
        m_doc = std::move(doc);
    }

    // Toggle a secondary container window (EQ / Playlist / etc.).
    // Creates the SkinView lazily on first call.  Layout id matches
    // the skin XML convention — modern skins almost always use
    // "normal" as the default layout name.
    void toggleSubwindow(const QString &containerId) {
        WasabiQt::SkinView *&slot = m_subwindows[containerId.toLower()];
        if (!slot) {
            slot = new WasabiQt::SkinView();
            slot->setWindowTitle(
                "Qtamp — " + QFileInfo(windowTitle().mid(8)).fileName()
                + " · " + containerId);
            slot->setWindowFlags(slot->windowFlags() | Qt::FramelessWindowHint);
            slot->setAttribute(Qt::WA_TranslucentBackground);
            slot->setHost(m_host);
            QString err;
            if (!slot->load(m_doc, containerId, QStringLiteral("normal"),
                            &err)) {
                fprintf(stderr,
                    "[qtamp] failed to open container %s: %s\n",
                    containerId.toLocal8Bit().constData(),
                    err.toLocal8Bit().constData());
                slot->deleteLater();
                slot = nullptr;
                return;
            }
            slot->resize(slot->layoutNativeSize());
            slot->show();
            return;
        }
        if (slot->isVisible()) slot->hide();
        else                   slot->show();
    }

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

    // ── Visualisation mode (app-level) ────────────────────────
    int  visMode() const { return m_visMode; }
    void setVisMode(int m) {
        m_visMode = m;
        update();
    }

    // ── Colour-themes list state (app-level) ──────────────────
    int     colorThemesSelectedRow() const { return m_ctSelectedRow; }
    void    setColorThemesSelectedRow(int row) {
        m_ctSelectedRow = row;
        update();
    }
    int     colorThemesTopRow() const { return m_ctTopRow; }
    void    setColorThemesTopRow(int row) {
        m_ctTopRow = row;
        update();
    }
    QRect   colorThemesListRect() const { return m_ctListRect; }

protected:
    // Re-implement SkinView's paint so the app's colour-themes
    // list state (selectedRow / topRow / out-bbox) threads into
    // qtWasabi's TreePainter without the engine having to know
    // about that state.  qtWasabi stays a pure renderer.
    void paintEvent(QPaintEvent *) override {
        QImage buf(size(), QImage::Format_ARGB32_Premultiplied);
        buf.fill(Qt::transparent);
        {
            QPainter bp(&buf);
            if (!windowRegion().isEmpty())
                bp.setClipRegion(windowRegion());
            m_ctListRect = QRect();
            WasabiQt::TreePainter::paintTree(
                &bp, tree(), registry(), fonts(),
                size(), host(), &gammasets(), &colors(),
                m_ctSelectedRow, m_ctTopRow,
                &m_ctListRect, &m_ctTopRow, m_visMode);
        }
        QPainter p(this);
        p.setClipping(false);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(0, 0, buf);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) {
            // Right-click anywhere → Winamp-style context menu.
            // Ported from winamp-linux's WinampWindow::showContext
            // Menu (originally written by 3nd3r) and adapted to
            // qtamp's WasabiQt::Host transport surface.
            showContextMenu(e->globalPosition().toPoint());
            return;
        }
        if (e->button() == Qt::LeftButton) {
            const QPoint p = e->position().toPoint();

            // Drawer toggle — handled before the regular hit-test
            // because player.main paints (and hit-tests) on top of
            // the drawer toggle button when the drawer is closed.
            // The button lives at drawer-relative (-72, 118); drawer
            // y flips between 17 (closed) and 133 (open).
            {
                const int btnX = layoutNativeSize().width() - 72;
                const int btnY = m_drawerOpen ? 251 : 135;
                const QRect toggle =
                    QRect(btnX, btnY, 25, 7).adjusted(-3, -3, 3, 3);
                if (toggle.contains(p)) {
                    setDrawerOpen(!m_drawerOpen);
                    return;
                }
            }

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
                // `action="TOGGLE" param="<container-id>"` opens (or
                // toggles) a secondary container window from the same
                // skin doc — EQ, Playlist, etc.
                if (action.compare(QStringLiteral("TOGGLE"),
                                   Qt::CaseInsensitive) == 0) {
                    QString param = hit->attrs.value(
                        QStringLiteral("param"));
                    if (!param.isEmpty()) {
                        // Strip "guid:" / "GUID:" prefix —
                        // DeClassified writes `param="guid:pl"`.
                        if (param.startsWith(QStringLiteral("guid:"),
                                             Qt::CaseInsensitive))
                            param = param.mid(5);
                        toggleSubwindow(param);
                        return;
                    }
                }
                if (WasabiQt::dispatchAction(action, m_host, this))
                    return;
            }
            // Drawer tab switcher: the three `mousetrapTab*` layers
            // sit on top of each tab.  They don't carry `action=`
            // attrs so the action-only hit-test above ignores them
            // — fall through to a wider hit-test and dispatch
            // tab-switch internally.  Region is unaffected (the
            // tab cluster has no sysregion), so we just toggle the
            // on/off variants and the content pages and repaint.
            const auto *hit2 = WasabiQt::Layout::hitTest(
                tree(), p, /*actionOnly=*/false,
                qtampImageSize, &registry(), nullptr);
            if (hit2) {
                // The `.off` tab variants ship with a top-level
                // `mousetrapTab*` layer, but the `.on` variants
                // don't — clicking the active tab lands on its
                // text label or Grid bg instead.  Match the tab
                // by id pattern so any widget inside a tab group
                // (mousetrap, label, or grid) counts as a click
                // on that tab.
                const QString id = hit2->id;
                int newTab = 0;
                if (id.contains(QLatin1String("TabEQ")) ||
                    id.contains(QLatin1String("eq.on")) ||
                    id.contains(QLatin1String("eq.off")))           newTab = 1;
                else if (id.contains(QLatin1String("TabOPTIONS")) ||
                         id.contains(QLatin1String("options.on")) ||
                         id.contains(QLatin1String("options.off"))) newTab = 2;
                else if (id.contains(QLatin1String("TabCOLORTHEMES")) ||
                         id.contains(QLatin1String("colorthemes.on")) ||
                         id.contains(QLatin1String("colorthemes.off"))) newTab = 3;
                if (newTab != 0) {
                    switchDrawerTab(newTab);
                    update();
                    return;
                }
            }
            // ColorThemes list — clicking a row selects it; the
            // Switch button (action=colorthemes_switch) then
            // applies the chosen gammaset.  Hit-test the bbox the
            // painter cached.  Also detect clicks in the
            // scrollbar column on the right edge.
            const QRect ctRect = colorThemesListRect();
            if (ctRect.isValid()) {
                if (ctRect.contains(p)) {
                    const int rowH = 10;
                    const int row = (p.y() - ctRect.y() - 2) / rowH;
                    if (row >= 0) {
                        setColorThemesSelectedRow(
                            colorThemesTopRow() + row);
                        return;
                    }
                }
                // Scrollbar column sits in the 14 px to the right
                // of the list rect (the painter reserves it).  The
                // top 17 px is the up-arrow, the bottom 17 px is
                // the down-arrow, and the 31 px thumb floats in
                // the middle (drag to scroll).
                const QRect sb(ctRect.right() + 1, ctRect.y(),
                                14, ctRect.height());
                if (sb.contains(p)) {
                    const int arrowH = 17;
                    const int thumbH = 31;
                    // Top arrow: scroll up one row.
                    if (p.y() < sb.y() + arrowH) {
                        setColorThemesTopRow(qMax(0,
                            colorThemesTopRow() - 1));
                        return;
                    }
                    // Bottom arrow: scroll down one row.
                    if (p.y() >= sb.y() + sb.height() - arrowH) {
                        setColorThemesTopRow(
                            colorThemesTopRow() + 1);
                        return;
                    }
                    // Middle area: capture the start of a thumb
                    // drag.  Compute the thumb's current rect so
                    // the click point can be inside or outside it
                    // (page jump on outside).
                    const int trackTop = sb.y() + arrowH;
                    const int trackBot = sb.y() + sb.height() - arrowH;
                    const int travel   = qMax(0, (trackBot - trackTop) - thumbH);
                    int nrows = gammasets().names().size();
                    const int maxTop = qMax(0, nrows - 8);
                    const double frac = maxTop > 0
                        ? double(colorThemesTopRow()) / double(maxTop)
                        : 0.0;
                    const int thumbY = trackTop + int(frac * travel);
                    if (p.y() >= thumbY && p.y() < thumbY + thumbH) {
                        // Start drag tracking — store the y offset
                        // from thumb's top so subsequent moves
                        // keep the cursor aligned.
                        m_ctDragging  = true;
                        m_ctDragOffset = p.y() - thumbY;
                        m_ctTrackTop  = trackTop;
                        m_ctTrackBot  = trackBot;
                        m_ctThumbH    = thumbH;
                        m_ctMaxTop    = maxTop;
                        return;
                    }
                    // Click in the empty track: page up/down.
                    if (p.y() < thumbY)
                        setColorThemesTopRow(qMax(0,
                            colorThemesTopRow() - 4));
                    else
                        setColorThemesTopRow(
                            colorThemesTopRow() + 4);
                    return;
                }
            }
            // Colour-themes drawer actions — qtamp-specific
            // (Audacious and other Wasabi 2-style hosts simply
            // wouldn't bind handlers for these).  Three buttons
            // ship in the skin XML as:
            //   action="colorthemes_switch"  - apply selected
            //   action="colorthemes_previous" - select prev row
            //   action="colorthemes_next"     - select next row
            if (hit2) {
                const QString a = hit2->attrs.value(QStringLiteral("action")).toLower();
                QStringList names = gammasets().names();
                std::sort(names.begin(), names.end(),
                          [](const QString &x, const QString &y){
                              return x.compare(y, Qt::CaseInsensitive) < 0;
                          });
                int row = colorThemesSelectedRow();
                if (row < 0 || row >= names.size()) {
                    // Resolve "no explicit selection" to the active
                    // gammaset's row so prev/next have something to
                    // start from.
                    const auto *act = gammasets().active();
                    const QString actName = act ? act->name : QString();
                    row = qMax(0, names.indexOf(actName));
                }
                if (a == QLatin1String("colorthemes_switch")) {
                    if (row >= 0 && row < names.size())
                        setActiveGammaset(names[row]);
                    return;
                }
                if (a == QLatin1String("colorthemes_previous")) {
                    setColorThemesSelectedRow(qMax(0, row - 1));
                    return;
                }
                if (a == QLatin1String("colorthemes_next")) {
                    setColorThemesSelectedRow(
                        qMin(names.size() - 1, row + 1));
                    return;
                }
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
        if (m_ctDragging && (e->buttons() & Qt::LeftButton)) {
            // Map cursor y to thumb top, then to a row fraction.
            const int y = e->position().toPoint().y();
            const int wantThumbY = y - m_ctDragOffset;
            const int travel = qMax(0,
                (m_ctTrackBot - m_ctTrackTop) - m_ctThumbH);
            const double frac = travel > 0
                ? qBound(0.0,
                    double(wantThumbY - m_ctTrackTop) / double(travel),
                    1.0)
                : 0.0;
            setColorThemesTopRow(int(frac * m_ctMaxTop + 0.5));
            return;
        }
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            move(e->globalPosition().toPoint() - m_dragOrigin);
        }
        WasabiQt::SkinView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        m_dragging = false;
        m_ctDragging = false;
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
        // Up/Down arrows scroll the colour-themes list when its
        // tab is open.  Works regardless of wheel-event delivery
        // (frameless + translucent backgrounds sometimes swallow
        // wheel events on Wayland).
        if (e->key() == Qt::Key_Down) {
            setColorThemesTopRow(colorThemesTopRow() + 1);
            return;
        }
        if (e->key() == Qt::Key_Up) {
            setColorThemesTopRow(qMax(0, colorThemesTopRow() - 1));
            return;
        }
        if (e->key() == Qt::Key_PageDown) {
            setColorThemesTopRow(colorThemesTopRow() + 5);
            return;
        }
        if (e->key() == Qt::Key_PageUp) {
            setColorThemesTopRow(qMax(0, colorThemesTopRow() - 5));
            return;
        }
        WasabiQt::SkinView::keyPressEvent(e);
    }
    void wheelEvent(QWheelEvent *e) override {
        // Wheel scroll inside the colour-themes list moves the
        // top-row offset.  Outside the list area, fall through.
        const QPoint p = e->position().toPoint();
        const QRect lr = colorThemesListRect();
        fprintf(stderr, "[wheel] at (%d,%d) ct_rect=%dx%d+%d+%d valid=%d "
                "contains=%d delta=%d\n",
                p.x(), p.y(),
                lr.width(), lr.height(), lr.x(), lr.y(),
                lr.isValid()?1:0, lr.contains(p)?1:0,
                e->angleDelta().y());
        fflush(stderr);
        if (lr.isValid() && lr.contains(p)) {
            const int steps = e->angleDelta().y() / 120;  // 1 notch = 120
            setColorThemesTopRow(qMax(0,
                colorThemesTopRow() - steps));
            return;
        }
        WasabiQt::SkinView::wheelEvent(e);
    }

public:
    // Reload the modern skin at runtime — re-parses the document,
    // re-expands the layout, replays the static well-known-scripts
    // and re-renders.  Mirrors the live-skin-swap path that the
    // upstream winamp-linux PreferencesDialog drives through its
    // `skinChanged(path)` signal.
    void reloadSkin(const QString &skinXmlPath) {
        WasabiQt::SkinXml::Document doc;
        QString err;
        if (!WasabiQt::SkinXml::parse(skinXmlPath, doc, &err)) {
            QMessageBox::warning(this, tr("Skin load failed"),
                tr("Could not parse %1:\n%2").arg(skinXmlPath, err));
            return;
        }
        if (!load(doc, "main", "normal", &err)) {
            QMessageBox::warning(this, tr("Skin load failed"),
                tr("Layout expand failed: %1").arg(err));
            return;
        }
        setSkinDocument(doc);
        // Any previously-open subwindows belong to the old skin doc.
        for (auto *w : std::as_const(m_subwindows)) if (w) w->deleteLater();
        m_subwindows.clear();
        auto &mutableTree = const_cast<WasabiQt::Layout::ResolvedWidget &>(
            tree());
        WasabiQt::Layout::runKnownScripts(mutableTree,
                                          layoutNativeSize().width());
        // Re-apply our drawer state — the fresh tree has both
        // drawer.button.close and drawer.button.open visible by
        // default, so without this the down-arrow paints on top of
        // the up-arrow even though the drawer is open.
        const bool want = m_drawerOpen;
        m_drawerOpen = !want;
        setDrawerOpen(want);
    }

private:
    // Winamp-style right-click context menu.  Ported from
    // winamp-linux's WinampWindow::showContextMenu (original
    // author: 3nd3r <lord3nd3r@gmail.com>); items that don't yet
    // have a backend in qtamp (preferences dialog, equalizer
    // window, playlist window, video, media library, milkdrop,
    // recent files, bookmarks) are intentionally left as enabled
    // placeholders that surface a status — they wire up later as
    // those subsystems land.  Items that do map onto qtamp's
    // host surface (transport, jump-to-time, exit) are fully
    // functional.
    // Ported verbatim from lord3nd3r/winamp-linux's
    // WinampWindow::showContextMenu (originally written by 3nd3r
    // <lord3nd3r@gmail.com>).  Same submenus, same labels, same
    // hotkey hints, same green-on-navy stylesheet, same PreferencesDialog
    // and AboutDialog.  Items that map onto qtamp's host surface
    // (Play file, Recent files, Bookmarks, Preferences, Jump to time,
    // About, Exit, Colour Theme) are fully wired; the remainder are
    // present so the menu tree is identical to upstream and they
    // light up as those subsystems land.
    void showContextMenu(QPoint globalPos) {
        static const char *menuStyle =
            "QMenu { background-color: #2b2d3d; color: #00ff00; border: 1px solid #555; font-size: 9pt; }"
            "QMenu::item:selected { background-color: #0000c6; }"
            "QMenu::item:checked { font-weight: bold; }"
            "QMenu::item:disabled { color: #666; }"
            "QMenu::separator { height: 1px; background: #555; margin: 2px 4px; }";

        QMenu menu;
        menu.setStyleSheet(menuStyle);

        // === Winamp main menu (matching Windows main.cpp top_menu) ===

        // -- Play submenu --
        QMenu *playMenu = menu.addMenu("Play");
        playMenu->setStyleSheet(menuStyle);
        QAction *playFileAct = playMenu->addAction("Play file...\tL");
        QAction *playLocAct  = playMenu->addAction("Play location...\tCtrl+L");
        playMenu->addSeparator();

        // -- Recent files submenu --
        QMenu *recentMenu = playMenu->addMenu("Recent files");
        recentMenu->setStyleSheet(menuStyle);
        auto &recent = RecentFilesManager::instance();
        QHash<QAction *, QString> recentOf;
        if (recent.recentFiles.isEmpty()) {
            QAction *empty = recentMenu->addAction("(no recent files)");
            empty->setEnabled(false);
        } else {
            for (int i = 0; i < recent.recentFiles.size(); i++) {
                const QString f = recent.recentFiles[i];
                QAction *a = recentMenu->addAction(
                    QString("%1. %2").arg(i + 1).arg(QFileInfo(f).fileName()));
                recentOf.insert(a, f);
            }
        }

        // -- Bookmarks submenu --
        QMenu *bmMenu = menu.addMenu("Bookmarks");
        bmMenu->setStyleSheet(menuStyle);
        QAction *addBmAct = bmMenu->addAction("Add current as bookmark");
        const QUrl currentSrc = m_host->player().source();
        const QString currentFile = currentSrc.isLocalFile()
            ? currentSrc.toLocalFile() : QString();
        addBmAct->setEnabled(!currentFile.isEmpty());
        bmMenu->addSeparator();
        auto &bmMgr = BookmarkManager::instance();
        QHash<QAction *, int> bmOf;
        for (int i = 0; i < bmMgr.bookmarks.size(); i++) {
            QAction *a = bmMenu->addAction(bmMgr.bookmarks[i].title);
            bmOf.insert(a, i);
        }
        if (bmMgr.bookmarks.isEmpty()) {
            QAction *empty = bmMenu->addAction("(no bookmarks)");
            empty->setEnabled(false);
        }

        menu.addSeparator();

        // -- Options submenu --
        QMenu *optMenu = menu.addMenu("Options");
        optMenu->setStyleSheet(menuStyle);
        QAction *aotAct = optMenu->addAction("Always on top\tCtrl+T");
        aotAct->setCheckable(true);
        aotAct->setChecked(windowFlags() & Qt::WindowStaysOnTopHint);

        QAction *dsizeAct = optMenu->addAction("Double size\tCtrl+D");
        dsizeAct->setCheckable(true);
        dsizeAct->setEnabled(false);

        QAction *shadeAct = optMenu->addAction("Windowshade mode\tCtrl+W");
        shadeAct->setCheckable(true);
        shadeAct->setEnabled(false);

        optMenu->addSeparator();
        QAction *prefsAct = optMenu->addAction("Preferences...\tCtrl+P");

        optMenu->addSeparator();
        QAction *stopAfterAct = optMenu->addAction("Stop after current");
        stopAfterAct->setCheckable(true);
        stopAfterAct->setEnabled(false);

        // -- Playback submenu --
        QMenu *pbMenu = menu.addMenu("Playback");
        pbMenu->setStyleSheet(menuStyle);
        QAction *jumpTimeAct = pbMenu->addAction("Jump to time...\tJ");
        QAction *jumpFileAct = pbMenu->addAction("Jump to file...\tCtrl+J");
        jumpFileAct->setEnabled(false);
        pbMenu->addSeparator();

        QAction *shuffAct = pbMenu->addAction("Shuffle");
        shuffAct->setCheckable(true);
        shuffAct->setEnabled(false);

        QMenu *repMenu = pbMenu->addMenu("Repeat");
        repMenu->setStyleSheet(menuStyle);
        QAction *repOffAct = repMenu->addAction("Off");
        repOffAct->setCheckable(true);
        repOffAct->setChecked(true);
        QAction *repAllAct = repMenu->addAction("Repeat all");
        repAllAct->setCheckable(true);
        repAllAct->setEnabled(false);
        QAction *repOneAct = repMenu->addAction("Repeat track");
        repOneAct->setCheckable(true);
        repOneAct->setEnabled(false);

        // -- Windows submenu --
        QMenu *winMenu = menu.addMenu("Windows");
        winMenu->setStyleSheet(menuStyle);
        QAction *eqTogAct = winMenu->addAction("Equalizer\tAlt+G");
        eqTogAct->setCheckable(true);
        eqTogAct->setEnabled(false);
        QAction *plTogAct = winMenu->addAction("Playlist editor\tAlt+E");
        plTogAct->setCheckable(true);
        plTogAct->setEnabled(false);
        QAction *vidTogAct = winMenu->addAction("Video window");
        vidTogAct->setCheckable(true);
        vidTogAct->setEnabled(false);
        QAction *mlTogAct = winMenu->addAction("Media library\tAlt+L");
        mlTogAct->setCheckable(true);
        mlTogAct->setEnabled(false);
        winMenu->addSeparator();
        QAction *milkdropAct = winMenu->addAction("Milkdrop visualization");
        milkdropAct->setEnabled(false);

        // -- Visualization submenu --
        QMenu *visMenu = menu.addMenu("Visualization");
        visMenu->setStyleSheet(menuStyle);
        QAction *visOffAct = visMenu->addAction("Off");
        visOffAct->setCheckable(true);
        visOffAct->setChecked(m_visMode == 0);
        QAction *visSpecAct = visMenu->addAction("Spectrum analyzer");
        visSpecAct->setCheckable(true);
        visSpecAct->setChecked(m_visMode == 1);
        QAction *visOscAct = visMenu->addAction("Oscilloscope");
        visOscAct->setCheckable(true);
        visOscAct->setChecked(m_visMode == 2);
        QAction *visVuAct = visMenu->addAction("VU meter");
        visVuAct->setCheckable(true);
        visVuAct->setChecked(m_visMode == 3);
        visMenu->addSeparator();
        QAction *visMilkdropAct = visMenu->addAction("Milkdrop visualization...");
        visMilkdropAct->setEnabled(false);

        menu.addSeparator();

        QAction *aboutAct = menu.addAction("About Winamp...");
        menu.addSeparator();
        QAction *quitAct = menu.addAction("Exit");

        // === Handle selection ===
        QAction *sel = menu.exec(globalPos);
        if (!sel) return;

        if (sel == playFileAct) {
            const QUrl u = m_host->pickFile(this);
            if (!u.isEmpty()) {
                m_host->player().setSource(u);
                m_host->player().play();
                RecentFilesManager::instance().addFile(u.toLocalFile());
            }
        }
        else if (sel == playLocAct) {
            PlayLocationDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted) {
                QString url = dlg.getUrl();
                if (!url.isEmpty()) {
                    m_host->player().setSource(QUrl(url));
                    m_host->player().play();
                }
            }
        }
        else if (recentOf.contains(sel)) {
            const QString f = recentOf.value(sel);
            m_host->player().setSource(QUrl::fromLocalFile(f));
            m_host->player().play();
        }
        else if (sel == addBmAct) {
            bool ok;
            const QString title = QInputDialog::getText(this,
                tr("Add Bookmark"), tr("Bookmark title:"),
                QLineEdit::Normal, QFileInfo(currentFile).fileName(), &ok);
            if (ok && !title.isEmpty())
                BookmarkManager::instance().addBookmark(title, currentFile);
        }
        else if (bmOf.contains(sel)) {
            const auto &bm = bmMgr.bookmarks[bmOf.value(sel)];
            m_host->player().setSource(QUrl::fromLocalFile(bm.path));
            m_host->player().play();
        }
        else if (sel == aotAct) {
            Qt::WindowFlags f = windowFlags();
            if (sel->isChecked()) f |=  Qt::WindowStaysOnTopHint;
            else                  f &= ~Qt::WindowStaysOnTopHint;
            setWindowFlags(f);
            show();
        }
        else if (sel == prefsAct) {
            auto *prefs = new PreferencesDialog(this);
            connect(prefs, &PreferencesDialog::skinChanged,
                    this, [this](const QString &path){
                // Persist the picked skin.  Modern skins reload in
                // place via qtWasabi; classic skins require a renderer
                // swap (this window is qtWasabi-only), so we restart
                // the process — main() routes by skin type at boot.
                QSettings s(configPath(), QSettings::IniFormat);
                s.setValue("skin", path);
                s.sync();
                if (isModernSkinDir(path)) {
                    const QString xml = path + "/skin.xml";
                    if (QFile::exists(xml)) reloadSkin(xml);
                } else {
                    QProcess::startDetached(
                        QApplication::applicationFilePath(), {});
                    QApplication::quit();
                }
            });
            prefs->setAttribute(Qt::WA_DeleteOnClose);
            prefs->exec();
        }
        else if (sel == jumpTimeAct) {
            bool ok;
            QString timeStr = QInputDialog::getText(this,
                "Jump to Time",
                "Enter time (MM:SS or seconds):",
                QLineEdit::Normal, "", &ok);
            if (ok && !timeStr.isEmpty()) {
                qint64 jumpMs = 0;
                if (timeStr.contains(':')) {
                    QStringList parts = timeStr.split(':');
                    if (parts.size() >= 2)
                        jumpMs = (parts[0].toInt() * 60 + parts[1].toInt()) * 1000;
                } else {
                    jumpMs = timeStr.toInt() * 1000;
                }
                m_host->seekMs(qBound(qint64(0), jumpMs, m_host->durationMs()));
            }
        }
        else if (sel == aboutAct) {
            // The animated demoscene-style AboutDialog from the
            // upstream dialogs.cpp — same look as winamp-linux.
            QString skinPath;
            const QUrl src = m_host->player().source();
            if (src.isLocalFile()) skinPath = QFileInfo(src.toLocalFile()).absolutePath();
            AboutDialog about(skinPath, this);
            about.exec();
        }
        else if (sel == visOffAct)  setVisMode(0);
        else if (sel == visSpecAct) setVisMode(1);
        else if (sel == visOscAct)  setVisMode(2);
        else if (sel == visVuAct)   setVisMode(3);
        else if (sel == quitAct) close();
    }

    void applySliderDrag(int xInWindow) {
        if (m_sliderTrack.width() <= 0) return;
        const double v = double(xInWindow - m_sliderTrack.x()) /
                         double(m_sliderTrack.width());
        m_host->setSliderPosition(m_sliderAction,
                                   qBound(0.0, v, 1.0));
    }

public:
    // Programmatic equivalent of clicking a drawer tab.
    void mousePressEventForTab(int tab) {
        switchDrawerTab(tab);
        update();
    }

    // Mirror configtabs.m::setTabs(int): show the .on variant of
    // the selected tab + its content page, hide the others.  Only
    // touches `visible` attrs inside the drawer — the drawer's own
    // sysregion-bearing widgets are untouched, so the window
    // region clip stays in sync without needing a rebuild.
    // Mirror configtabs.m's OpenDrawer / closeDrawer.  When closed
    // the drawer slides up to y=17 so it sits behind player.main
    // (out of view); the drawer.button.open is then the only
    // child still visible — it pokes through the player chrome's
    // CONFIG notch.  When open the drawer sits at y=133 (below
    // player.main, the position we already use as the default).
    void setDrawerOpen(bool open) {
        if (open == m_drawerOpen) return;
        m_drawerOpen = open;
        auto &mut = const_cast<WasabiQt::Layout::ResolvedWidget &>(tree());
        std::function<void(WasabiQt::Layout::ResolvedWidget &)> walk =
            [&](WasabiQt::Layout::ResolvedWidget &w) {
            if (w.id == QStringLiteral("player.normal.drawer")) {
                w.attrs.insert(QStringLiteral("y"),
                    open ? QStringLiteral("133") : QStringLiteral("17"));
                w.attrs.remove(QStringLiteral("relaty"));
            } else if (w.id == QStringLiteral("player.normal.drawer.shadow")) {
                w.attrs.insert(QStringLiteral("visible"),
                    open ? QStringLiteral("1") : QStringLiteral("0"));
                w.attrs.insert(QStringLiteral("y"),
                    open ? QStringLiteral("121") : QStringLiteral("0"));
            } else if (w.id == QStringLiteral("player.normal.drawer.content")) {
                // The chrome/inner-borders/list live inside content;
                // hide it when closed so only the open-button area
                // (rendered later in the tree) shows through.
                w.attrs.insert(QStringLiteral("visible"),
                    open ? QStringLiteral("1") : QStringLiteral("0"));
            } else if (w.id == QStringLiteral("drawer.button.close")) {
                // Single toggle button: always visible, image swapped
                // to flip the arrow.  When the drawer is open it shows
                // the up-arrow ("close"); when closed it shows the
                // down-arrow ("open").
                w.attrs.insert(QStringLiteral("visible"),
                    QStringLiteral("1"));
                w.attrs.insert(QStringLiteral("image"),
                    open ? QStringLiteral("drawer.button.close")
                         : QStringLiteral("drawer.button.open"));
                w.attrs.insert(QStringLiteral("hoverImage"),
                    open ? QStringLiteral("drawer.button.close.hover")
                         : QStringLiteral("drawer.button.open.hover"));
                w.attrs.insert(QStringLiteral("downImage"),
                    open ? QStringLiteral("drawer.button.close.pressed")
                         : QStringLiteral("drawer.button.open.pressed"));
            } else if (w.id == QStringLiteral("drawer.button.open")) {
                // The duplicate is never used as a separate widget.
                w.attrs.insert(QStringLiteral("visible"),
                    QStringLiteral("0"));
            }
            for (auto &c : w.children) walk(c);
        };
        walk(mut);
        // Shrink the window when the drawer is closed so the chrome
        // sits on its own footprint with no transparent strip below.
        // Compact height covers chrome + the open-button tab that
        // pokes out at the bottom: drawer.y(=17) + button.relY(=118)
        // + button.h(~7) + a couple px of padding.
        const QSize full = layoutNativeSize();
        const int compactH = 17 + 118 + 7 + 2;  // 144 px
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        resize(full.width(), open ? full.height() : compactH);
        rebuildWindowRegion();
        update();
    }

    void switchDrawerTab(int tab) {
        struct Apply {
            const char *id;
            const char *onIfThisTab;
        };
        const QString onEQ   = (tab == 1) ? QStringLiteral("1") : QStringLiteral("0");
        const QString offEQ  = (tab == 1) ? QStringLiteral("0") : QStringLiteral("1");
        const QString onOPT  = (tab == 2) ? QStringLiteral("1") : QStringLiteral("0");
        const QString offOPT = (tab == 2) ? QStringLiteral("0") : QStringLiteral("1");
        const QString onCT   = (tab == 3) ? QStringLiteral("1") : QStringLiteral("0");
        const QString offCT  = (tab == 3) ? QStringLiteral("0") : QStringLiteral("1");
        auto &mut = const_cast<WasabiQt::Layout::ResolvedWidget &>(tree());
        std::function<void(WasabiQt::Layout::ResolvedWidget &)> walk =
            [&](WasabiQt::Layout::ResolvedWidget &w) {
            // Tab on/off variants.
            if (w.id == QStringLiteral("config.tab.eq.on"))           w.attrs.insert("visible", onEQ);
            else if (w.id == QStringLiteral("config.tab.eq.off"))     w.attrs.insert("visible", offEQ);
            else if (w.id == QStringLiteral("config.tab.options.on")) w.attrs.insert("visible", onOPT);
            else if (w.id == QStringLiteral("config.tab.options.off"))w.attrs.insert("visible", offOPT);
            else if (w.id == QStringLiteral("config.tab.colorthemes.on"))   w.attrs.insert("visible", onCT);
            else if (w.id == QStringLiteral("config.tab.colorthemes.off")) w.attrs.insert("visible", offCT);
            // Content pages.
            else if (w.id == QStringLiteral("player.normal.drawer.eq"))           w.attrs.insert("visible", onEQ);
            else if (w.id == QStringLiteral("player.normal.drawer.options"))      w.attrs.insert("visible", onOPT);
            else if (w.id == QStringLiteral("player.normal.drawer.colorthemes"))  w.attrs.insert("visible", onCT);
            for (auto &c : w.children) walk(c);
        };
        walk(mut);
    }

    QtampHost *m_host = nullptr;
    QPoint     m_dragOrigin;
    bool       m_dragging = false;
    QString    m_sliderAction;     // empty when not dragging a slider
    QRect      m_sliderTrack;
    WasabiQt::SkinXml::Document m_doc;
    QHash<QString, WasabiQt::SkinView *> m_subwindows;
    bool m_drawerOpen = true;
    // Visualisation mode (right-click → Visualization submenu).
    // 0=Off, 1=Spectrum (default), 2=Oscilloscope, 3=VU meter.
    int  m_visMode    = 1;
    // Colour-themes list state — app-level, threaded through
    // qtWasabi's TreePainter on each paint.  qtWasabi itself stays
    // a pure rendering engine (no application state) so the
    // selection/scroll/bbox knowledge lives here.
    int          m_ctSelectedRow = -1;   // -1 = use active gammaset
    mutable int  m_ctTopRow      = 0;
    mutable QRect m_ctListRect;
    // Scrollbar drag state for the colour-themes list.
    bool m_ctDragging  = false;
    int  m_ctDragOffset = 0;
    int  m_ctTrackTop  = 0;
    int  m_ctTrackBot  = 0;
    int  m_ctThumbH    = 31;
    int  m_ctMaxTop    = 0;
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

// Q_OBJECT classes defined inline need their MOC output included
// at the bottom of the same TU so AUTOMOC can pick them up.
#include "main.moc"
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
  QString cliModernSkin  = takeModernSkinArg(argc, argv);
  QString cliClassicSkin = takeStringArg(argc, argv, "--classic-skin");
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

  // Resolve skin path + renderer kind.  CLI flags win; then saved
  // setting; then a sensible default.  Modern skins are rendered by
  // qtWasabi; classic skins by the legacy WinampWindow renderer
  // ported from lord3nd3r/winamp-linux.
  QString modernSkinPath;
  QString classicSkinPath;
  {
    QSettings s(configPath(), QSettings::IniFormat);
    if (!cliModernSkin.isEmpty()) {
      modernSkinPath = cliModernSkin;
    } else if (!cliClassicSkin.isEmpty()) {
      classicSkinPath = cliClassicSkin;
    } else {
      const QString saved = s.value("skin").toString();
      if (!saved.isEmpty()) {
        if (isModernSkinDir(saved)) modernSkinPath  = saved;
        else                        classicSkinPath = saved;
      } else {
        const QString defModern = QDir::homePath()
                                  + "/.winamp/skins/Winamp Modern";
        if (QFile::exists(defModern + "/skin.xml"))
          modernSkinPath = defModern;
      }
    }
  }

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
    view->setSkinDocument(doc);

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
            // The static path mutates widget positions (drawer y,
            // titlebar streaks, …); recompute the window region so
            // sysregion cutouts land where the chrome actually
            // paints, not where the original XML put it.
            view->rebuildWindowRegion();
            // Drawer opens by default in qtamp.  Configtabs.m's
            // setup hides drawer.button.open in that state; since
            // we don't run scripts on first load, do it ourselves
            // so the two toggle buttons don't overlap on startup.
            std::function<void(WasabiQt::Layout::ResolvedWidget &)> walk =
                [&](WasabiQt::Layout::ResolvedWidget &w) {
                if (w.id == QStringLiteral("drawer.button.open"))
                    w.attrs.insert(QStringLiteral("visible"),
                                   QStringLiteral("0"));
                for (auto &c : w.children) walk(c);
            };
            walk(mutableTree);
        }

        // Fire the actual Maki scripts.  Errors are non-fatal — if
        // a binding is missing the runtime logs a guru and moves
        // on; the static-fallback chrome is still visible.
        // Set WASABIQT_NO_RUNTIME=1 to skip Maki dispatch entirely
        // (useful for visual diffs that should reflect ONLY the
        // static well-known-script path).
        if (!::getenv("WASABIQT_NO_RUNTIME")) {
            static WasabiQt::SkinRuntime runtime;
            runtime.loadScripts(doc, mutableTree);
            runtime.dispatchOnScriptLoaded();
            runtime.dispatchXuiParams(mutableTree);
        }
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
    {
      QSettings s(configPath(), QSettings::IniFormat);
      s.setValue("skin", modernSkinPath);
    }
    view->setWindowTitle("Qtamp — " + QFileInfo(modernSkinPath).fileName());
    view->resize(view->layoutNativeSize());
    view->show();

    // Visual-debug pipeline mirroring qtWasabi's render_layout: when
    // --screenshot is set, wait for the first paintEvent to land,
    // grab the widget, save PNG, exit.  Use a 0-ms timer + a small
    // delay so AUTOMOC and the initial repaint complete first.
    if (!screenshotPath.isEmpty()) {
      // Debug knob: WASABIQT_FORCE_TAB=2|3 pre-selects the
      // Options / Color Themes tab before the screenshot so the
      // visual harness can verify their content without a click.
      if (const char *t = ::getenv("WASABIQT_FORCE_TAB")) {
        const int tn = QByteArray(t).toInt();
        if (tn >= 1 && tn <= 3)
            QTimer::singleShot(0, view,
              [view, tn]() { view->mousePressEventForTab(tn); });
      }
      if (::getenv("WASABIQT_DRAWER_CLOSED")) {
        QTimer::singleShot(0, view,
          [view]() { view->setDrawerOpen(false); });
      }
      // Optional: queue + play a file before the screenshot grab.
      // Used to verify the chrome at "real" host state (time digits,
      // kbps/kHz numbers, songticker, vis level) instead of the
      // bare zero-state default.
      int screenshotDelayMs = 250;
      if (const char *f = ::getenv("WASABIQT_PLAY_FILE")) {
        screenshotDelayMs = 2500;
        QTimer::singleShot(0, view, [host, f]() {
          host->player().setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(f)));
          host->player().play();
        });
      }
      QTimer::singleShot(screenshotDelayMs, view, [view, screenshotPath]() {
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
  // CLI --classic-skin overrides the saved one; otherwise fall back
  // to whatever was last picked (which we already know is classic
  // because the modern branch above didn't take it).
  QString skinPath = !classicSkinPath.isEmpty() ? classicSkinPath
                                                : settings.value("skin").toString();
  if (!classicSkinPath.isEmpty())
    settings.setValue("skin", classicSkinPath);

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

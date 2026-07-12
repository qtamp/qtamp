#include <cstdio>
#include <memory>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QHoverEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWindow>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QStandardPaths>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QSGRendererInterface>
#ifndef Q_OS_WASM
#include <QProcess>
#endif
#include <QScreen>
#include <QSettings>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include <QTimer>

#include "audiometa.h"
#include "eq10dsp.h"
#include "wavreader.h"
#include "medialibraryindex.h"
#include "playlistwindow.h"
#include "playerhost.h"
#ifdef QTAMP_HAVE_SIDECAR
#include <qtWasabi/serve/SidecarService.h>
#endif
#include <qtWasabi/remote/RemoteHost.h>
#include <qtWasabi/remote/RemoteTransport.h>
#include <qtWasabi/remote/GraphQLTransport.h>
#include <qtWasabi/FakeHost.h>
#include <qtWasabi/head/HeadChrome.h>
#include <qtWasabi/head/HeadWindow.h>
#include "skinutils.h"
#include "translator.h"
#include "winampbitmaps.h"
#include "winampwindow.h"
#include "qt5compat.h"

#ifdef QTAMP_WITH_MILKDROP
#  include "MilkdropItem.h"
#endif

#ifdef WINAMP_HAVE_WASABIQT
#  include <qtWasabi/SkinXml.h>
#  include <qtWasabi/SkinView.h>
#  include <qtWasabi/SkinQuickItem.h>
#  include <qtWasabi/SkinRuntime.h>
#  include <qtWasabi/Layout.h>
#  include <qtWasabi/BitmapRegistry.h>
#  include <qtWasabi/Host.h>
#  include <qtWasabi/PaintCtx.h>
#  include <qtWasabi/WindowHolderRegistry.h>
#  include <qtWasabi/TreePainter.h>
#  include <qtWasabi/Widget.h>
#  include <qtWasabi/MilkdropWidget.h>
#  include <qtWasabi/CfgAttribStore.h>
#  include <QAudioBuffer>
#  include <QAudioBufferOutput>
#  include <QAudioDecoder>
#  include <QAudioOutput>
#  include <QAudioSink>
#  include <QMediaDevices>
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
    auto *registry = static_cast<qtWasabi::BitmapRegistry *>(userdata);
    if (!registry) return QSize();
    const auto *def = registry->find(bitmapId);
    // NStatesButton convention: when the bare image id isn't a
    // registered bitmap, fall back to `<id>0` (e.g. `repeat` →
    // `repeat0`).  Without this, NStates buttons with no explicit
    // w/h are invisible to hit-test and click-through never reaches
    // them.
    if (!def && !bitmapId.isEmpty()) {
        def = registry->find(bitmapId + QStringLiteral("0"));
    }
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

#ifdef QTAMP_REMOTE_ONLY
#include <emscripten.h>

#include <cstdlib>

// Read a query parameter of the embedding page ("__origin" returns
// location.origin).  Strings cross the JS boundary through our own
// KEEPALIVE allocator + manual UTF-8 on HEAPU8 — no Emscripten
// runtime-method exports needed (same technique as the EventSource
// glue in remotetransport.cpp).
extern "C" {
EMSCRIPTEN_KEEPALIVE void *qtamp_query_alloc(int n) { return std::malloc(n); }
}
// clang-format off
EM_JS(char *, qtamp_query_param_js, (const char *namePtr), {
    let end = namePtr;
    while (HEAPU8[end]) end++;
    const name = new TextDecoder().decode(HEAPU8.subarray(namePtr, end));
    let v = '';
    try {
        if (name === '__origin') v = location.origin;
        else v = new URLSearchParams(location.search).get(name) || '';
    } catch (e) { v = ''; }
    const data = new TextEncoder().encode(v);
    const ptr = _qtamp_query_alloc(data.length + 1);
    HEAPU8.set(data, ptr);
    HEAPU8[ptr + data.length] = 0;
    return ptr;
});
// clang-format on
static QString wasmQueryParam(const char *name) {
    char *p = qtamp_query_param_js(name);
    QString v = QString::fromUtf8(p);
    std::free(p);
    return v;
}
#endif  // QTAMP_REMOTE_ONLY

#ifdef WINAMP_HAVE_WASABIQT

// (Preferences + About + Jump-to-File + Play-Location dialogs live
// in dialogs.{h,cpp}.)
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

// QtampHost — Qtamp's qtWasabi::Host implementation.  Shovels live
// QMediaPlayer state through the abstract Host interface qtWasabi
// expects, so qtWasabi's default DisplayResolver + dispatchAction
// helpers can do the actual skin-format-convention work.
class QtampPlayerWindow;
class QtampHost : public PlayerHost {
public:
    QtampHost() {
        reloadVisPrefs();

        // Bridge the live QMediaPlayer signals onto the PlayerHost
        // notification surface the window listens to, so the same repaint
        // machinery works whether the host is local or remote.
        connect(&m_player, &QMediaPlayer::sourceChanged, this,
                [this](const QUrl &) { emit sourceChanged(); });
        connect(&m_player, &QMediaPlayer::playbackStateChanged, this,
                [this](QMediaPlayer::PlaybackState) { emit playbackStateChanged(); });
        connect(&m_player, &QMediaPlayer::metaDataChanged, this,
                [this]() { emit metaDataChanged(); });

        // Single, always-on audio path.  QMediaPlayer's QAudioOutput
        // is INTENTIONALLY left unset — there's exactly one sink,
        // `m_eqSink`, which we feed from `onAudioBuffer`.  Reasoning
        // is in the plan: toggling between QAudioOutput and a
        // separate QAudioSink left the audio device in a stale
        // state on PulseAudio/PipeWire after every EQ on/off flip
        // (and the QAudioBufferOutput buffer-receive callbacks
        // stopped firing too, so the spectrum went dead).  By
        // never connecting a parallel sink, the device exclusivity
        // stays stable across the whole session.
        // Audio is decoded up front with QAudioDecoder into m_pcm, then the
        // pump feeds the one always-on QAudioSink from a play cursor at real
        // time (paced by the sink's free space) through the EQ + spectrum
        // pipeline.  This is backend-independent (QMediaPlayer's
        // QAudioBufferOutput isn't implemented by macOS's AVFoundation
        // backend), and a play cursor over decoded PCM makes seek instant
        // and track changes gapless.  m_player is kept purely for metadata,
        // duration and cover art (it never plays).
        QObject::connect(&m_decoder, &QAudioDecoder::bufferReady,
                         this, &QtampHost::onDecodedBuffer);
        QObject::connect(&m_decoder, &QAudioDecoder::finished, this,
                         [this]{ m_decodeDone = true; });
        m_pumpTimer.setInterval(10);
        QObject::connect(&m_pumpTimer, &QTimer::timeout,
                         this, &QtampHost::pumpFromPcm);

        // Subscribe to the canonical EQ-enable cfgattrib key
        // (synthesised by ButtonWidget for any button with
        // action="EQ_TOGGLE" + activeImage, so it's already shared
        // across the EQ ON button, its drawer LED, and the songinfo
        // "eq" badge).  When the user clicks any of them, the store
        // value flips and we toggle `m_eqEnabled` — the audio path
        // is unchanged either way, the flag only switches between
        // user-supplied band gains and 0 dB.
        m_eqToggleSub = qtWasabi::CfgAttribStore::instance().subscribe(
            QStringLiteral("__action:EQ_TOGGLE"),
            [this](int v) { m_eqEnabled = (v != 0); });
    }

    ~QtampHost() override {
        if (m_eqToggleSub)
            qtWasabi::CfgAttribStore::instance().unsubscribe(
                m_eqToggleSub);
    }

    QMediaPlayer       &player()       { return m_player; }
    const QMediaPlayer &player() const { return m_player; }

    // Decode a path from the top (PlayerHost surface; the internal call
    // sites keep using openAndDecode directly).
    void openPath(const QUrl &u) override { openAndDecode(u); }
    QUrl currentSourceUrl() const override { return m_player.source(); }
    AudioAnalyzer *analyzerPtr() override { return &m_analyzer; }

    // ── Read state ─────────────────────────────────────────────
    // Position is the play cursor over the decoded PCM.
    qint64  positionMs() const override {
        const int r = m_pcmFormat.sampleRate();
        return r > 0 ? (m_cursor * 1000) / r : 0;
    }
    qint64  durationMs() const override { return m_player.duration(); }
    bool    isPlaying() const override { return m_playing && !m_paused; }
    bool    isPaused() const override { return m_paused; }
    int     volume() const override {
        // Volume is owned by the always-on EQ sink (m_eqSink); the
        // legacy QAudioOutput is no longer used.  Before the sink
        // exists (first buffer not yet arrived) fall back to the
        // cached user value so the slider doesn't snap to 0.
        return int(qBound(qreal(0), m_userVolume, qreal(1)) * 100);
    }
    int     channelCount() const override { return m_pcmFormat.channelCount(); }
    int     sampleRate()   const override { return m_pcmFormat.sampleRate(); }
    int     bitrate() const override {
        // The QAudioDecoder pipeline exposes no container bitrate, so
        // report the average = file size / duration (what players show
        // for lossless/VBR anyway).  bytes*8/durationMs is kbps directly.
        const QUrl src = m_player.source();
        const qint64 durMs = m_player.duration();
        if (src.isLocalFile() && durMs > 0) {
            const qint64 bytes = QFileInfo(src.toLocalFile()).size();
            if (bytes > 0) return int((bytes * 8) / durMs);
        }
        // PCM path: the decoded format IS the bitrate (sampleRate *
        // channels * bits).  The browser demo track is a qrc: resource
        // (not a local file) and a WAV carries no AudioBitRate metadata,
        // so without this the kbps field read 0; this reports the true
        // 1411 kbps for the bundled 44.1 kHz/16-bit stereo demo.
        if (m_pcmFrames > 0 && m_pcmFormat.isValid()) {
            const int rate  = m_pcmFormat.sampleRate();
            const int chans = m_pcmFormat.channelCount();
            const int bytes = m_pcmFormat.bytesPerFrame();
            if (rate > 0 && chans > 0 && bytes > 0)
                return int((qint64(rate) * bytes * 8) / 1000);
        }
        // Fallback: the container bitrate from Qt metadata, if present.
        bool ok = false;
        const int bps = m_player.metaData()
            .value(QMediaMetaData::AudioBitRate).toInt(&ok);
        return ok ? bps / 1000 : 0;
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

    // Full path/URL — what System.getPlayItemString() returns (the
    // skin's fileinfo.maki does removePath() on it).
    QString songPath() const override {
        const QUrl src = m_player.source();
        return src.isLocalFile() ? src.toLocalFile() : src.toString();
    }

    // Per-field track metadata for the file-info display.  Maps the
    // skin's canonical lower-case field names onto Qt's QMediaMetaData
    // keys.  Empty when the tag isn't present (line stays hidden).
    QString playItemMetaData(const QString &field) const override {
        const QMediaMetaData md = m_player.metaData();
        const QString f = field.toLower();
        auto s = [&](QMediaMetaData::Key k) { return md.value(k).toString(); };
        if (f == QLatin1String("title"))    return s(QMediaMetaData::Title);
        if (f == QLatin1String("artist")) {
            const QString a = s(QMediaMetaData::ContributingArtist);
            return a.isEmpty() ? s(QMediaMetaData::AlbumArtist) : a;
        }
        if (f == QLatin1String("album"))       return s(QMediaMetaData::AlbumTitle);
        if (f == QLatin1String("albumartist")) return s(QMediaMetaData::AlbumArtist);
        if (f == QLatin1String("length")) {
            // Track length in milliseconds — Winamp's "length" metadata
            // scale (scripts StringToInteger it and compare against ms).
            // Empty when unknown (stream/endless), the script cue for
            // their endless-source fallbacks.
            const qint64 ms = m_player.duration();
            return ms > 0 ? QString::number(ms) : QString();
        }
        if (f == QLatin1String("track"))       return s(QMediaMetaData::TrackNumber);
        if (f == QLatin1String("genre"))       return s(QMediaMetaData::Genre);
        if (f == QLatin1String("composer"))    return s(QMediaMetaData::Composer);
        if (f == QLatin1String("publisher"))   return s(QMediaMetaData::Publisher);
        if (f == QLatin1String("comment"))     return s(QMediaMetaData::Comment);
        if (f == QLatin1String("year")) {
            const QVariant dv = md.value(QMediaMetaData::Date);
            if (dv.canConvert<QDateTime>()) {
                const int y = dv.toDateTime().date().year();
                if (y > 0) return QString::number(y);
            }
            return dv.toString().left(4);
        }
        return QString();
    }
    // "Artist - Title" primary label; falls back to the plain title.
    QString playItemDisplayTitle() const override {
        const QMediaMetaData md = m_player.metaData();
        const QString title = md.value(QMediaMetaData::Title).toString();
        if (title.isEmpty()) return songTitle();
        const QString artist =
            md.value(QMediaMetaData::ContributingArtist).toString();
        return artist.isEmpty() ? title : (artist + QStringLiteral(" - ") + title);
    }
    // Qt doesn't expose the decoder string; derive a readable name from
    // the container so the "Decoder:" line isn't blank for known types.
    QString decoderName() const override {
        const QUrl src = m_player.source();
        if (!src.isValid() || src.isEmpty()) return QString();
        const QString ext =
            QFileInfo(src.isLocalFile() ? src.toLocalFile() : src.path())
                .suffix().toLower();
        if (ext == QLatin1String("mp3"))  return QStringLiteral("MPEG Audio Decoder");
        if (ext == QLatin1String("flac")) return QStringLiteral("FLAC Decoder");
        if (ext == QLatin1String("ogg") || ext == QLatin1String("opus"))
            return QStringLiteral("Ogg Vorbis Decoder");
        if (ext == QLatin1String("m4a") || ext == QLatin1String("aac"))
            return QStringLiteral("AAC Decoder");
        if (ext == QLatin1String("wav"))  return QStringLiteral("WAV Decoder");
        return ext.isEmpty() ? QString() : ext.toUpper() + QStringLiteral(" Decoder");
    }

    // Album cover for <albumart> widgets.  Qt 6's QMediaPlayer
    // surfaces embedded cover art (ID3 APIC, FLAC PICTURE, MP4
    // covr, etc.) through QMediaMetaData::CoverArtImage.  Cached
    // implicitly because QImage uses CoW.
    QImage albumArt() const override {
        QVariant v = m_player.metaData()
            .value(QMediaMetaData::CoverArtImage);
        if (!v.canConvert<QImage>() || v.value<QImage>().isNull()) {
            // Qt 6's ffmpeg backend exposes the embedded MP3 APIC /
            // FLAC PICTURE frame as ThumbnailImage rather than
            // CoverArtImage.  Fall back so the cover lights up
            // regardless of which slot the backend populated.
            v = m_player.metaData()
                .value(QMediaMetaData::ThumbnailImage);
        }
        if (v.canConvert<QImage>()) return v.value<QImage>();
        return QImage();
    }

    // ── Transport ──────────────────────────────────────────────
    void play()  override {
        if (m_paused) {                        // resume
            m_paused = false;
            if (m_eqSink) m_eqSink->resume();
            m_pumpTimer.start();
        } else if (!m_playing && !m_curSource.isEmpty()) {
            openAndDecode(m_curSource);         // (re)start from the top
        }
    }
    void pause() override {
        if (!m_playing || m_paused) return;
        m_paused = true;
        m_pumpTimer.stop();
        if (m_eqSink) m_eqSink->suspend();
    }
    void stop()  override {
        m_playing = false; m_paused = false;
        m_pumpTimer.stop();
        m_decoder.stop();
        if (m_eqSink) m_eqSink->suspend();
        m_cursor = 0;
    }
    // Next/Prev were base-Host no-ops, so the transport Next/Prev buttons
    // did nothing.  Route them through the playlist model (which emits
    // trackDoubleClicked → setSource+play for the new current track).
    void next()  override { if (m_playlist) m_playlist->nextTrack(); }
    void prev()  override { if (m_playlist) m_playlist->prevTrack(); }
    // Seek is instant: move the cursor over the already-decoded PCM.  If the
    // decoder has not reached the target yet, clamp to what is decoded.
    void seekMs(qint64 ms) override {
        const int r = m_pcmFormat.sampleRate();
        if (r <= 0) return;
        const qint64 f = (qMax<qint64>(0, ms) * r) / 1000;
        m_cursor = qBound<qint64>(0, f, m_pcmFrames);
    }

    // Central play entry: decode `u` into m_pcm from the start and play it.
    // All open call sites funnel through here.  The sink is left in place so
    // track changes stay gapless (runEqPipeline rebuilds it only on a format
    // change).
    void openAndDecode(const QUrl &u) {
        if (u.isEmpty()) return;
        // Play-stats feed for the Library's smart views (Most Played /
        // Recently Played / Never Played) — every decode start counts
        // as one play, matching ml_local bumping playcount on start.
        if (u.isLocalFile()) m_mlIndex.recordPlay(u.toLocalFile());
        m_curSource = u;
        m_player.setSource(u);                 // metadata / duration / cover only
        m_decoder.stop();
        m_pcm.clear();
        m_pcmFrames = 0; m_cursor = 0; m_decodeDone = false;
        m_playing = true; m_paused = false;
        if (m_eqSink) m_eqSink->resume();      // undo a prior stop's suspend
#ifdef QTAMP_WASM
        // Qt's WebAssembly multimedia backend ships no QAudioDecoder
        // ("Not available"), and the browser demo only ever plays its
        // bundled PCM WAV.  Parse the RIFF header directly into the
        // existing m_pcm pipeline; everything downstream (pump, EQ DSP,
        // QAudioSink via Web Audio) is unchanged.
        if (loadWavIntoPcm(u)) { m_pumpTimer.start(); return; }
#endif
        m_decoder.setSource(u);
        m_decoder.start();
        m_pumpTimer.start();
    }

#ifdef QTAMP_WASM
    // Read the bundled demo track through the dependency-free RIFF parser
    // (src/wavreader.h, unit-tested in tests/wavreader_test.cpp) and fill
    // m_pcm / m_pcmFormat / m_pcmFrames the same way onDecodedBuffer does.
    bool loadWavIntoPcm(const QUrl &u) {
        QString path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        if (path.startsWith(QStringLiteral("qrc:")))
            path = path.mid(3);                 // qrc:/x -> :/x
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray all = f.readAll();
        const qtamp::WavPcm w = qtamp::parseWavPcm16(
            reinterpret_cast<const uint8_t *>(all.constData()),
            static_cast<size_t>(all.size()));
        if (!w.ok) return false;
        m_pcm = QByteArray(reinterpret_cast<const char *>(w.data),
                           static_cast<int>(w.dataLen));
        m_pcmFormat.setSampleRate(w.sampleRate);
        m_pcmFormat.setChannelCount(w.channels);
        m_pcmFormat.setSampleFormat(QAudioFormat::Int16);
        m_pcmFrames = m_pcm.size() / m_pcmFormat.bytesPerFrame();
        m_decodeDone = true;
        return m_pcmFrames > 0;
    }
#endif

    void teardownSink() {
        if (m_eqSink) { m_eqSink->stop(); m_eqSink->deleteLater(); m_eqSink = nullptr; }
        m_eqSinkDevice = nullptr;
        m_eqSampleRate = 0; m_eqChannels = 0;   // force runEqPipeline to rebuild
    }

    // Decoder -> m_pcm.  Runs ahead of playback so the whole track lands in
    // memory, making seeks random-access.
    void onDecodedBuffer() {
        while (m_decoder.bufferAvailable()) {
            const QAudioBuffer b = m_decoder.read();
            if (!b.isValid() || b.frameCount() <= 0) continue;
            if (m_pcmFrames == 0) m_pcmFormat = b.format();
            m_pcm.append(static_cast<const char *>(b.constData<char>()),
                         b.byteCount());
            m_pcmFrames += b.frameCount();
        }
    }

    // Feed the sink from the cursor at real time (paced by the sink's free
    // space), wrapping each PCM chunk in a QAudioBuffer so the existing EQ +
    // spectrum pipeline processes it.
    void pumpFromPcm() {
        if (!m_playing || m_paused) return;
        const int bpf = m_pcmFormat.bytesPerFrame();
        if (bpf <= 0) return;
        const qint64 chunk = 4096;              // frames per write
        while (m_cursor < m_pcmFrames) {
            if (m_eqSink && m_eqSink->bytesFree() < m_eqSink->bufferSize() / 2)
                break;                          // sink busy -> next tick (real-time pace)
            const qint64 n = qMin(chunk, m_pcmFrames - m_cursor);
            const QByteArray slice(m_pcm.constData() + m_cursor * bpf, int(n * bpf));
            runEqPipeline(QAudioBuffer(slice, m_pcmFormat));
            m_cursor += n;
        }
        if (m_decodeDone && m_cursor >= m_pcmFrames) {   // reached the end
            m_playing = false;
            m_pumpTimer.stop();
        }
    }
    void setVolume(int v) override {
        m_userVolume = qBound(0, v, 100) / qreal(100);
        if (m_eqSink) m_eqSink->setVolume(m_userVolume);
    }

    // ── EJECT — pick a file AND start playing it.  Overrides the
    //    base default (which only picks) so the EJECT action does
    //    what users expect.
    QUrl pickFile(QWidget *embedder) override;

    // ── Visualisation: smoothed RMS of the most recent audio
    //    buffer in [0..1] (level scalar), plus the 19-band spectrum,
    //    75-sample oscilloscope, and L/R VU peaks computed by the
    //    shared AudioAnalyzer.
    double audioLevel() const override { return m_analyzer.level(); }
    const float *spectrumData() const override { return m_analyzer.spectrum(); }
    const float *peakData()     const override { return m_analyzer.peaks();    }
    const float *oscData()      const override { return m_analyzer.osc(); }
    float vuLeft()  const override { return m_analyzer.vuLeft();  }
    float vuRight() const override { return m_analyzer.vuRight(); }
    bool  peaksVisible() const override { return m_peaksVisible; }

    // Raw analyzer access for the MilkDrop overlay, which needs the
    // native-rate PCM ring buffer (not the analyzer's 75-sample
    // oscilloscope summary).  Non-const because the overlay drains
    // the ring buffer on read.
    AudioAnalyzer &analyzer() { return m_analyzer; }

    // Reload viz prefs from QSettings (called on startup and when
    // Preferences emits settingChanged).
    void reloadVisPrefs() override {
        QSettings s(configPath(), QSettings::IniFormat);
        m_peaksVisible = s.value("visualization/peaks", true).toBool();
        m_analyzer.setPeakFalloff(
            s.value("visualization/peakFalloff", 1).toInt());
    }

    // ── Window control — implemented via the bound window.
    bool close()    override;
    bool minimize() override;
    bool maximize() override;
    bool toggleShade() override;
    bool showSystemMenu(QWidget *embedder = nullptr) override;

    // ── EQ slider routing.  EQ_BAND with param="1".."10" or
    //    "preamp" reads/writes per-band gain values in the
    //    [0..63] Winamp slider scale.  Stored on the host so the
    //    sliders can read back their state (e.g. on skin reload).
    //    Applied to audio in `onAudioBuffer` via the eq10 DSP.
    //
    // Pull the no-param Host overloads into scope so qtamp call
    // sites (e.g. applySliderDrag) keep compiling against the
    // 2-arg form.  Without this `using` the override below would
    // hide them and the 2-arg form becomes ambiguous.
    using Host::sliderPosition;
    using Host::setSliderPosition;

    double sliderPosition(const QString &action,
                           const QString &param) const override {
        if (action.compare(QLatin1String("EQ_BAND"),
                            Qt::CaseInsensitive) == 0) {
            const int sliderVal = eqBandSliderValue(param);
            // Return on the slider's 0..1 axis, with INVERTED y:
            // Winamp's slider value 0 = +12 dB (top), 63 = -12 dB
            // (bottom).  qtWasabi's vertical slider semantics are
            // pos=0 at top, pos=1 at bottom — matches Winamp's
            // direction.
            return double(sliderVal) / 63.0;
        }
        // Balance/pan on the engine-wide 0..1 axis (0.5 = centre).
        if (action.compare(QLatin1String("PAN"), Qt::CaseInsensitive) == 0)
            return qBound(0.0, double(m_balance) / 127.0 * 0.5 + 0.5, 1.0);
        return Host::sliderPosition(action);
    }
    void setSliderPosition(const QString &action, double v,
                            const QString &param) override {
        if (action.compare(QLatin1String("EQ_BAND"),
                            Qt::CaseInsensitive) == 0) {
            const int sliderVal = qBound(0,
                int(v * 63.0 + 0.5), 63);
            setEqBandSliderValue(param, sliderVal);
            enableEqIfBandActive(sliderVal);
            return;
        }
        if (action.compare(QLatin1String("PAN"), Qt::CaseInsensitive) == 0) {
            // 0..1 (centre 0.5) → -127..+127 (the Winamp API balance scale).
            // Repaint is driven by the caller (slider drag / the slider
            // bridge lambda), as with the EQ_BAND case above.
            m_balance = qBound(-127.0f, float((v - 0.5) * 2.0 * 127.0), 127.0f);
            if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                fprintf(stderr, "[balance] PAN v=%.3f -> m_balance=%.1f\n",
                        v, m_balance);
            return;
        }
        Host::setSliderPosition(action, v);
    }

    // EQ enabled / auto toggle.  Wired from EQ_TOGGLE / EQ_AUTO
    // action dispatch — qtamp's mousePressEvent calls these when
    // the user clicks the corresponding buttons.
    void setEqEnabled(bool on) { m_eqEnabled = on; }
    bool eqEnabled() const     { return m_eqEnabled; }
    // EQ "auto" (per-song preset auto-load).  qtamp has no preset
    // library yet, so the flag is pure stored state: it round-trips
    // through the channel/GraphQL and the skin's AUTO indicator.
    void setEqAuto(bool on) { m_eqAuto = on; }
    bool eqAuto() const     { return m_eqAuto; }

    // Maki System.setEqBand(band,val)/getEqBand(band): Wasabi's signed gain
    // (-127..127, 0 = flat, +127 = full boost) mapped onto the same 0..63
    // slider store that drives the EQ sliders AND the audio DSP — so the EQ
    // reset/+/- buttons move the sliders and change the sound in lockstep.
    // Slider 0 = top (+12 dB), 63 = bottom (-12 dB), 31 = flat.
    void setEqBandValue(int band, int val) override {
        if (band < 0 || band > 9) return;
        m_eqBandSlider[band] =
            qBound(0, qRound(31.0 - val * 31.0 / 127.0), 63);
        enableEqIfBandActive(m_eqBandSlider[band]);
        if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
            fprintf(stderr, "[eqband] setEqBand(%d, %d) -> slider %d (eq=%d)\n",
                    band, val, m_eqBandSlider[band], m_eqEnabled ? 1 : 0);
    }

    // Moving a band off flat means the EQ is in use — turn it on (through the
    // shared EQ-enable cfgattrib, so a skin's EQ LED/button tracks it).  This
    // is what makes the EQ audible on skins like HeadAMP that expose NO EQ
    // on/off control: without it m_eqEnabled stays false and onAudioBuffer
    // forces every band to 0 dB no matter where the sliders sit.
    void enableEqIfBandActive(int sliderVal) {
        if (sliderVal != 31 && !m_eqEnabled)
            qtWasabi::CfgAttribStore::instance().set(
                QStringLiteral("__action:EQ_TOGGLE"), 1);
    }
    int eqBandValue(int band) const override {
        if (band < 0 || band > 9) return 0;
        return qBound(-127,
            qRound((31 - m_eqBandSlider[band]) * 127.0 / 31.0), 127);
    }

    // ── Hook the existing PlaylistWindow / library root into the
    //    Host playlist + library accessors.  Wired from main()
    //    after both objects exist.  The library root is a
    //    filesystem path used as the QDir root for libraryRow*().
    void setPlaylist(PlaylistWindow *pl) { m_playlist = pl; }
    void setLibraryRoot(const QString &root) {
        m_libraryRoot = root;
        // Build the tag-indexed Media Library (DuckDB + Parquet) for the
        // gen_ml artist/album/track panes.  Persisted under the app cache
        // so a relaunch loads instantly; rescanned each launch to pick up
        // added files.  A no-op when qtamp is built without DuckDB.
        if (!root.isEmpty()) {
            const QString cacheDir = QStandardPaths::writableLocation(
                QStandardPaths::CacheLocation) + QStringLiteral("/medialibrary");
            if (m_mlIndex.open(cacheDir))
                m_mlIndex.rescan(root);
        }
    }

    // Single funnel for "open a track" intent.  Appends to the Host
    // playlist model (the same model the pledit renderer reads via
    // playlistRowCount/playlistRowText) AND starts playback, so opening
    // a song through the UI actually shows up in the playlist instead of
    // only swapping the audio source.  enqueueOnly=true adds without
    // changing what's playing.  Engine-agnostic; all open/enqueue call
    // sites should route through here rather than poking m_player.
    void enqueueAndPlay(const QUrl &u, bool enqueueOnly = false) override {
        if (u.isEmpty()) return;
        const QString path = u.isLocalFile() ? u.toLocalFile() : u.toString();
        int row = -1;
        if (m_playlist) { row = m_playlist->trackCount(); m_playlist->addTrack(path); }
        if (!enqueueOnly) {
            if (m_playlist && row >= 0) m_playlist->setCurrentTrackIndex(row);
            openAndDecode(u);
        }
    }

    // Add a batch of URLs: the first starts playing, the rest enqueue (unless
    // the caller wants everything enqueued).  Used by the multi-file and
    // folder open paths so a whole album lands in the playlist at once.
    void enqueueUrls(const QList<QUrl> &urls, bool enqueueOnly = false) {
        bool first = true;
        for (const QUrl &u : urls) {
            enqueueAndPlay(u, enqueueOnly || !first);
            first = false;
        }
    }
    static const QStringList &audioExts() {
        static const QStringList e = {
            QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"),
            QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("m4a"),
            QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("aiff"),
            QStringLiteral("alac") };
        return e;
    }
    // Multi-file open — the user can select MANY tracks at once (not just one),
    // including everything in a folder via Ctrl+A.  Returns the chosen URLs.
    QList<QUrl> openFilesAndEnqueue(QWidget *embedder, bool enqueueOnly = false) override {
#ifdef QTAMP_WASM
        // No synchronous native dialogs in the browser: QFileDialog::exec()
        // needs asyncify, which is incompatible with the function-pointer
        // cast emulation the Maki VM dispatch requires.  EJECT / open are
        // no-ops in the demo (it plays its bundled track).
        Q_UNUSED(embedder); Q_UNUSED(enqueueOnly);
        return {};
#else
        const QStringList paths = QFileDialog::getOpenFileNames(
            embedder, QObject::tr("Open file(s)"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            QObject::tr("Audio (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac "
                        "*.wma *.aiff *.alac);;All files (*)"));
        QList<QUrl> urls;
        for (const QString &p : paths) urls << QUrl::fromLocalFile(p);
        enqueueUrls(urls, enqueueOnly);
        return urls;
#endif
    }
    // Folder open — recursively collect audio files (sorted) and enqueue them.
    QList<QUrl> openFolderAndEnqueue(QWidget *embedder, bool enqueueOnly = false) override {
#ifdef QTAMP_WASM
        Q_UNUSED(embedder); Q_UNUSED(enqueueOnly);
        return {};
#else
        const QString dir = QFileDialog::getExistingDirectory(
            embedder, QObject::tr("Open folder"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
        QList<QUrl> urls;
        if (dir.isEmpty()) return urls;
        QStringList files;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (audioExts().contains(QFileInfo(f).suffix().toLower()))
                files << f;
        }
        // Order by the embedded album sequence (track/disc tags), not by
        // filename — an album whose files are named "Artist - Title" with
        // no leading number would otherwise enqueue alphabetically instead
        // of in album order.  Falls back to filename when no tags exist.
        audiometa::sortByTrack(files);
        for (const QString &f : files) urls << QUrl::fromLocalFile(f);
        enqueueUrls(urls, enqueueOnly);
        return urls;
#endif
    }

    int  playlistRowCount() const override {
        return m_playlist ? m_playlist->trackCount() : 0;
    }
    QString playlistRowText(int row) const override {
        return m_playlist ? m_playlist->trackDisplayAt(row) : QString();
    }
    qint64 playlistRowDurationMs(int row) const override {
        return m_playlist ? m_playlist->trackDurationAt(row) : 0;
    }
    int  playlistCurrentRow() const override {
        return m_playlist ? m_playlist->currentTrackIndex() : -1;
    }
    void playlistSetCurrentRow(int row) override {
        if (m_playlist) m_playlist->setCurrentTrackIndex(row);
    }
    void playlistPlayRow(int row) override {
        if (!m_playlist) return;
        m_playlist->setCurrentTrackIndex(row);
        m_playlist->playCurrentTrack();
    }
    bool pleditCommand(const QString &verb) override {
        if (!m_playlist) return false;
        m_playlist->pleditButtonMenu(verb);   // pops Add/Rem/Sel/Misc/Manage
        return true;
    }

    // ── ML panel player-state sections (moved out of the engine's
    // MediaLibraryPanel in Wasabi 2 V2 — the identical file code, now
    // behind the Host seam; the engine no longer touches app data). ──
    QList<qtWasabi::Host::MlPanelItem> mlPanelChildren(
        const QString &ns) const override {
        QList<qtWasabi::Host::MlPanelItem> out;
        const QString appData = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
        if (ns == QLatin1String("playlists")) {
            QDir d(appData + QStringLiteral("/playlists"));
            if (!d.exists()) return out;
            const QStringList filters{
                QStringLiteral("*.m3u"), QStringLiteral("*.m3u8"),
                QStringLiteral("*.pls"), QStringLiteral("*.xspf")};
            for (const QFileInfo &fi :
                 d.entryInfoList(filters, QDir::Files, QDir::Name))
                out.append({fi.completeBaseName(), fi.fileName()});
        } else if (ns == QLatin1String("bookmarks")) {
            QFile f(appData + QStringLiteral("/bookmarks.txt"));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString line = ts.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QChar('#'))) continue;
                out.append({line, line});
            }
        } else if (ns == QLatin1String("history")) {
            QFile f(appData + QStringLiteral("/history.txt"));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
            QStringList lines;
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                const QString line = ts.readLine().trimmed();
                if (!line.isEmpty() && !line.startsWith(QChar('#')))
                    lines.prepend(line);  // newest first
                if (lines.size() >= 50) break;
            }
            for (const QString &line : lines) {
                const QString label = QFileInfo(line).fileName();
                out.append({label.isEmpty() ? line : label, line});
            }
        } else if (ns == QLatin1String("devices")) {
            const QString user = qEnvironmentVariable("USER");
            if (user.isEmpty()) return out;
            QDir d(QStringLiteral("/run/media/") + user);
            if (!d.exists()) return out;
            for (const QFileInfo &fi : d.entryInfoList(
                     QDir::AllDirs | QDir::NoDotAndDotDot, QDir::Name))
                out.append({fi.fileName(), fi.fileName()});
        }
        return out;
    }

    int libraryRowCount(const QString &parent) const override {
        const QString p = parent.isEmpty() ? m_libraryRoot : parent;
        if (p.isEmpty()) return 0;
        return libraryEntries(p).size();
    }
    QString libraryRowLabel(const QString &parent, int row) const override {
        const QString p = parent.isEmpty() ? m_libraryRoot : parent;
        const auto entries = libraryEntries(p);
        return (row >= 0 && row < entries.size())
            ? entries[row].fileName() : QString();
    }
    QString libraryRowPath(const QString &parent, int row) const override {
        const QString p = parent.isEmpty() ? m_libraryRoot : parent;
        const auto entries = libraryEntries(p);
        return (row >= 0 && row < entries.size())
            ? entries[row].absoluteFilePath() : QString();
    }
    bool libraryRowHasChildren(const QString &parent, int row) const override {
        const QString p = parent.isEmpty() ? m_libraryRoot : parent;
        const auto entries = libraryEntries(p);
        return (row >= 0 && row < entries.size())
            ? entries[row].isDir() : false;
    }

    // ── Media Library artist/album/track queries, delegating to the
    //    DuckDB + Parquet index.  Convert the index's value structs to
    //    the Host row types the gen_ml renderer consumes.
    QList<MlArtistRow> mlArtists() const override {
        QList<MlArtistRow> out;
        for (const auto &a : m_mlIndex.artists())
            out.append({a.name, a.albumCount, a.trackCount});
        return out;
    }
    QList<MlAlbumRow> mlAlbums(const QString &artist) const override {
        QList<MlAlbumRow> out;
        for (const auto &a : m_mlIndex.albums(artist))
            out.append({a.name, a.year, a.trackCount});
        return out;
    }
    QList<MlTrackRow> mlTracks(const QString &artist, const QString &album) const override {
        QList<MlTrackRow> out;
        for (const auto &t : m_mlIndex.tracks(artist, album))
            out.append({t.artist, t.album, t.title, t.genre,
                        t.track, t.year, t.lengthMs, t.path});
        return out;
    }
    int mlTotalTracks() const override { return m_mlIndex.totalTracks(); }
    void mlPlayTracks(const QList<QString> &paths, int startRow,
                      bool enqueueOnly) override {
        if (paths.isEmpty() || !m_playlist) return;
        const int base = m_playlist->trackCount();
        for (const QString &p : paths) m_playlist->addTrack(p);
        if (!enqueueOnly) {
            const int i = qBound(0, startRow, int(paths.size()) - 1);
            m_playlist->setCurrentTrackIndex(base + i);
            openAndDecode(QUrl::fromLocalFile(paths.at(i)));
        }
    }
    QList<MlFilterRow> mlFilterValues(
        const QString &field, const QString &countField,
        const QList<QPair<QString, QString>> &equals) const override {
        QList<MlFilterRow> out;
        for (const auto &v : m_mlIndex.filterValues(field, countField, equals))
            out.append({v.name, v.count});
        return out;
    }
    QList<MlTrackRow> mlTracksQuery(
        const QList<QPair<QString, QString>> &equals,
        int smartView) const override {
        QList<MlTrackRow> out;
        for (const auto &t : m_mlIndex.tracksQuery(equals, smartView))
            out.append({t.artist, t.album, t.title, t.genre,
                        t.track, t.year, t.lengthMs, t.path});
        return out;
    }
    // gen_ml's Library button → "Media Library Preferences...".  The
    // dialog lives on the player window, which registers the base
    // PlayerHost::showPreferencesFn callback.
    void mlShowPreferences() override {
        if (showPreferencesFn) showPreferencesFn();
    }

private:
    QFileInfoList libraryEntries(const QString &dirPath) const {
        if (dirPath.isEmpty()) return {};
        return QDir(dirPath).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::Name);
    }

    PlaylistWindow *m_playlist     = nullptr;
    QString         m_libraryRoot;
    mutable qtamp::MediaLibraryIndex m_mlIndex;


    // 10 bands + 1 preamp, stored as Winamp 0..63 slider values
    // (0 = +12 dB / top, 31 = 0 dB / middle, 63 = -12 dB / bottom).
    int  m_eqBandSlider[10] = {31,31,31,31,31,31,31,31,31,31};
    int  m_eqPreampSlider   = 31;
    bool m_eqEnabled        = false;
    bool m_eqAuto           = false;

    int eqBandSliderValue(const QString &param) const {
        if (param.compare(QLatin1String("preamp"),
                            Qt::CaseInsensitive) == 0) {
            return m_eqPreampSlider;
        }
        bool ok = false;
        const int n = param.toInt(&ok);
        if (!ok || n < 1 || n > 10) return 31;          // 0 dB default
        return m_eqBandSlider[n - 1];
    }
    void setEqBandSliderValue(const QString &param, int v) {
        v = qBound(0, v, 63);
        if (param.compare(QLatin1String("preamp"),
                            Qt::CaseInsensitive) == 0) {
            m_eqPreampSlider = v;
            return;
        }
        bool ok = false;
        const int n = param.toInt(&ok);
        if (!ok || n < 1 || n > 10) return;
        m_eqBandSlider[n - 1] = v;
    }

    void onAudioBuffer(const QAudioBuffer &buf) {
        if (!buf.isValid() || buf.frameCount() <= 0) return;
        m_lastChannels   = buf.format().channelCount();
        m_lastSampleRate = buf.format().sampleRate();
        // ALWAYS-ON pipeline.  We process every buffer through the
        // single `m_eqSink`, regardless of whether the user has
        // toggled EQ on or off.  The flag `m_eqEnabled` only
        // controls which band-gain values feed the eq10 filter:
        //   • on  → user-supplied gains from the sliders
        //   • off → 0 dB across the board + preamp = 1.0
        //          (Q-asymmetric IIR at unity is bit-identical
        //          passthrough)
        // Net result: no device-exclusivity churn, no transient
        // sink lifecycle, no hardcoded volume restoration.
        runEqPipeline(buf);
    }

    // Per-buffer DSP + sink pump.  Owns the lazy creation of
    // `m_eqSink` (first valid buffer arrives → sink built at that
    // sample rate / channel count).  Body in main.cpp below the
    // class.
    void runEqPipeline(const QAudioBuffer &buf);

    QMediaPlayer  m_player;          // metadata / duration / album-art only
    QAudioDecoder m_decoder;         // decodes the whole track into m_pcm
    QTimer        m_pumpTimer;       // feeds the sink from m_pcm at real time
    QUrl          m_curSource;
    QByteArray    m_pcm;             // whole-track decoded PCM (m_pcmFormat)
    QAudioFormat  m_pcmFormat;       // format of the samples in m_pcm
    qint64        m_pcmFrames  = 0;  // frames decoded into m_pcm so far
    qint64        m_cursor     = 0;  // playback cursor (frames) — drives position + seek
    bool          m_playing    = false;
    bool          m_paused     = false;
    bool          m_decodeDone = false;
    AudioAnalyzer m_analyzer;
    bool          m_peaksVisible = true;
    int           m_lastChannels   = 0;
    int           m_lastSampleRate = 0;
    // (m_window lives on the PlayerHost base, set via bindWindow.)
    // EQ DSP state — single, always-on pipeline.  Sink is lazy-
    // initialised on the first buffer at that buffer's format; we
    // never tear it down until QtampHost is destroyed.
    eq10_t        m_eqState[2] = {};
    int           m_eqSampleRate = 0;
    int           m_eqChannels   = 0;
    QAudioSink   *m_eqSink       = nullptr;
    QIODevice    *m_eqSinkDevice = nullptr;
    qreal         m_userVolume   = qreal(0.7);
    float         m_balance      = 0.0f;  // -127..+127, 0 = centre (Winamp API balance)
    int           m_eqToggleSub  = 0;
    // Cached height before entering shade mode so we can restore on
    // the next SWITCH toggle.  0 = not currently shaded.
    int           m_savedHeight    = 0;
};

// Always-on EQ pipeline for the modern-skin path.  Mirrors the
// classic-skin DSP (10-band Q-asymmetric IIR + preamp lookup) but
// runs unconditionally:
//   • The flag `m_eqEnabled` decides whether the band gains come
//     from the user sliders (when true) or are forced to 0 dB /
//     preamp = 1.0 (when false).  The eq10 filter at 0 dB is
//     mathematically a passthrough — no audible difference vs the
//     un-EQ'd buffer.
//   • There's exactly one output sink (`m_eqSink`) for the whole
//     session, created lazily at the first buffer's format.  This
//     was the regression that killed audio + spectrum when EQ was
//     toggled off: tearing the sink down + relying on QAudioOutput
//     to take over again was unreliable on PulseAudio/PipeWire.
//   • The analyzer is always fed the post-DSP samples, so the
//     spectrum reflects EQ when on and is the input as-is when off
//     (because the DSP is unity).
// Falls back silently when the buffer format is unsupported (Int32 /
// UInt8 / etc.) — the analyzer still gets the raw buffer.
void QtampHost::runEqPipeline(const QAudioBuffer &buf) {
    const QAudioFormat fmt = buf.format();
    const int frames   = buf.frameCount();
    const int channels = fmt.channelCount();
    const int sampleRate = fmt.sampleRate();
    if (frames <= 0 || channels <= 0 || sampleRate <= 0) return;
    if (fmt.sampleFormat() != QAudioFormat::Int16 &&
        fmt.sampleFormat() != QAudioFormat::Float) {
        // Fall back to passthrough analyzer feed for unsupported
        // formats (Int32 / UInt8 / etc.).  Better than dropping
        // audio entirely.
        m_analyzer.feed(buf);
        return;
    }

    // Lazy sink setup, or rebuild on sample-format change.
    const int eqChannels = qMin(channels, 2);
    if (!m_eqSink || sampleRate != m_eqSampleRate ||
        eqChannels != m_eqChannels) {
        m_eqSampleRate = sampleRate;
        m_eqChannels   = eqChannels;
        eq10_setup(m_eqState, m_eqChannels, double(sampleRate));
        if (m_eqSink) {
            m_eqSink->stop();
            m_eqSink->deleteLater();
            m_eqSinkDevice = nullptr;
        }
        const QAudioDevice outDev = QMediaDevices::defaultAudioOutput();
        QAudioFormat outFmt;
        outFmt.setSampleRate(sampleRate);
        outFmt.setChannelCount(m_eqChannels);
        outFmt.setSampleFormat(QAudioFormat::Float);
#ifdef QTAMP_WASM
        // Qt's WebAssembly (Web Audio) sink rejects a format the device
        // does not natively support with "Failed to open audio device
        // Invalid Operation".  Fall back to the device's nearest format
        // so the sink actually opens; the pump already resamples/copies
        // per buffer, so a non-Float sink is fine.
        if (!outDev.isFormatSupported(outFmt))
            outFmt = outDev.preferredFormat();
#endif
        m_eqSink = new QAudioSink(outDev, outFmt, this);
        // ~200 ms buffer matches the classic-skin path.
        m_eqSink->setBufferSize(
            sampleRate * m_eqChannels * int(sizeof(float)) / 5);
        m_eqSink->setVolume(m_userVolume);
        m_eqSinkDevice = m_eqSink->start();
    }
    if (!m_eqSinkDevice) return;

    // Pick the per-band gains.  When EQ is OFF we feed zeros — the
    // eq10 IIR at 0 dB is a unity filter, so the output is
    // bit-identical to the input modulo float roundoff.  When ON
    // the gains come from the user sliders.  Refresh every buffer
    // (cheap) so slider drags are immediately audible.
    if (m_eqEnabled) {
        for (int b = 0; b < 10; ++b)
            eq10_setgain(m_eqState, m_eqChannels, b,
                eq10_valtodb(m_eqBandSlider[b]));
    } else {
        for (int b = 0; b < 10; ++b)
            eq10_setgain(m_eqState, m_eqChannels, b, 0.0);
    }
    const float preampGain = m_eqEnabled
        ? eq_preamp_table[qBound(0, m_eqPreampSlider, 63)]
        : 1.0f;

    const int total = frames * m_eqChannels;
    QVector<float> in(total), out(total);
    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        const qint16 *src = buf.constData<qint16>();
        for (int i = 0; i < frames; ++i)
            for (int ch = 0; ch < m_eqChannels; ++ch)
                in[i * m_eqChannels + ch] =
                    (src[i * channels + ch] / 32768.0f) * preampGain;
    } else {
        const float *src = buf.constData<float>();
        for (int i = 0; i < frames; ++i)
            for (int ch = 0; ch < m_eqChannels; ++ch)
                in[i * m_eqChannels + ch] =
                    src[i * channels + ch] * preampGain;
    }
    for (int ch = 0; ch < m_eqChannels; ++ch)
        eq10_processf(&m_eqState[ch], in.data(), out.data(),
                      frames, ch, m_eqChannels);
    // Snapshot the EQ'd-but-PRE-balance samples for the visualizer.  Balance
    // is an output-stage pan; it must not gate the spectrum/Milkdrop, else
    // panning fully to one side silences the channel the analyzer reads and
    // the visualizer goes dark.  The spectrum still reflects the EQ.
    QByteArray visBytes(reinterpret_cast<const char *>(out.data()),
                        qint64(total) * qint64(sizeof(float)));
    // Stereo balance (the Winamp API PAN): linear opposite-channel
    // attenuation — panning left fades the right channel, and vice versa.
    // Centre (m_balance==0) and mono are no-ops, so output stays unchanged.
    if (m_eqChannels >= 2) {
        const int pan = qRound(m_balance);   // -127..+127
        if (pan != 0) {
            const float balL = pan > 0 ? float(128 - pan) / 128.0f : 1.0f;
            const float balR = pan < 0 ? float(128 + pan) / 128.0f : 1.0f;
            for (int i = 0; i < frames; ++i) {
                out[i * m_eqChannels + 0] *= balL;
                out[i * m_eqChannels + 1] *= balR;
            }
        }
    }
    // Output to the sink (volume already applied via setVolume).
    m_eqSinkDevice->write(reinterpret_cast<const char *>(out.data()),
                           qint64(total) * qint64(sizeof(float)));
    // Feed the EQ'd (pre-balance) samples into the analyzer so the spectrum
    // reflects the EQ but not the output pan.  Float format, lossless.
    QAudioFormat fbufFmt;
    fbufFmt.setSampleRate(sampleRate);
    fbufFmt.setChannelCount(m_eqChannels);
    fbufFmt.setSampleFormat(QAudioFormat::Float);
    QAudioBuffer processed(visBytes, fbufFmt);
    m_analyzer.feed(processed);
}

// All transport / display logic lives in QtampHost; this class is
// just the window shell + input routing.  Modern skins paint their
// own chrome, so the host window is frameless and click-on-empty-area
// drag is implemented here via QWindow::startSystemMove() (Wayland)
// with a manual move() fallback elsewhere.
// QtampPlayerWindow is a QQuickItem subclass hosted in a QQuickView
// (frameless transparent QQuickWindow).  All the same input handling
// / state / paint as a plain widget — just on top of the Qt Quick
// Scene Graph instead of QWidget paint events.  The renderer chain
// (TreePainter -> QImage -> QSGTexture) lives in SkinQuickItem's
// updatePaintNode + the paintInto virtual we override here.
// Auxiliary sub-windows (EQ / Playlist / etc.) stay on the QWidget
// SkinView path.

// Event filter for the WA5 menu-bar popups: Left/Right arrow keys walk
// the prev/next sibling chain (mirrors Wasabi's nextMenu/_previousMenu),
// while leaving QMenu's own submenu open/close on the arrows intact —
// Right still opens a highlighted submenu, Left still closes an open one.
class MenuArrowFilter : public QObject {
public:
    QMenu *menu = nullptr;
    std::function<void()> onPrev, onNext;
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() != QEvent::KeyPress) return false;
        auto *ke = static_cast<QKeyEvent *>(e);
        if (::getenv("WASABIQT_TRACE_MAKI"))
            fprintf(stderr, "[menukey] key=0x%x on %s\n", ke->key(),
                    o ? o->metaObject()->className() : "?");
        if (ke->key() == Qt::Key_Right) {
            // A highlighted submenu opens on Right — don't navigate then.
            if (menu && menu->activeAction() && menu->activeAction()->menu())
                return false;
            if (onNext) onNext();
            return true;
        }
        if (ke->key() == Qt::Key_Left) {
            // An open submenu closes on Left — don't navigate then.
            if (menu)
                for (QMenu *sub : menu->findChildren<QMenu *>())
                    if (sub->isVisible()) return false;
            if (onPrev) onPrev();
            return true;
        }
        return false;
    }
};

class QtampPlayerWindow : public qtWasabi::head::HeadWindow {
public:
    // A fresh skin document was adopted (initial load or reload):
    // rewire the MilkDrop overlay against the new widget tree.
    void skinDocumentChanged() override { wireMilkdrop(); }

    // Subwindow titles carry the qtamp brand + skin name.
    QString subwindowTitle(const QString &containerId) const override {
        const QString hostTitle = window() ? window()->title() : QString();
        return "Qtamp — " + QFileInfo(hostTitle.mid(8)).fileName()
               + " · " + containerId;
    }

    // Perform a widget's `action=` exactly as a real click would.  The engine
    // calls this (registerSkinWidgetClickCallback) from Maki's
    // GuiObject.leftClick()/rightClick() when a script delegates a click and
    // the target didn't consume it with an onLeftClick/onRightClick handler —
    // so scripted click-delegation drives transport/toggle/page buttons on ANY
    // skin, not just ones whose buttons carry a script handler.  Mirrors this
    // window's real mousePressEvent dispatch (TOGGLE, builtin verbs,
    // action_target) minus the hit-test/press-visual, since the target widget
    // is named directly (and may be hidden, which a point hit-test can't reach).
    bool triggerWidgetActionById(const QString &id, bool /*right*/) {
        const qtWasabi::Layout::ResolvedWidget *w = nullptr;
        std::function<void(const qtWasabi::Layout::ResolvedWidget &)> find =
            [&](const qtWasabi::Layout::ResolvedWidget &n) {
                if (w) return;
                if (n.id.compare(id, Qt::CaseInsensitive) == 0) { w = &n; return; }
                for (const auto &c : n.children) if (c) find(*c);
            };
        find(tree());
        if (!w) return false;
        const QString action = w->attrs.value(QStringLiteral("action"));
        if (action.isEmpty()) return false;
        if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
            fprintf(stderr, "[clickaction] %s -> action=%s\n",
                    w->id.toLocal8Bit().constData(),
                    action.toLocal8Bit().constData());
        if (action.compare(QStringLiteral("TOGGLE"), Qt::CaseInsensitive) == 0) {
            const QString param = w->attrs.value(QStringLiteral("param"));
            if (!param.isEmpty()) { toggleSubwindow(param); return true; }
        }
        if (qtWasabi::dispatchAction(action, m_host, nullptr)) return true;
        const QString target = w->attrs.value(QStringLiteral("action_target"));
        if (!target.isEmpty()) {
            const QString param = w->attrs.value(QStringLiteral("param"));
            if (action.startsWith(QStringLiteral("switchto;"),
                                  Qt::CaseInsensitive)) {
                const QString grp = action.section(QChar(';'), 1, 1);
                if (!grp.isEmpty()) {
                    qtWasabi::fireWidgetAttrSet(
                        target, QStringLiteral("groupid"), grp);
                    update();
                    return true;
                }
            }
            if (qtWasabi::fireWidgetActionEvent(
                    target, action, param, 0, 0, 0, 0, w->id) > 0) {
                update();
                return true;
            }
        }
        return false;
    }

    explicit QtampPlayerWindow(qtWasabi::PlayerHost *host, QQuickItem *parent = nullptr)
        : qtWasabi::head::HeadWindow(host, parent) {
        // QQuickItem is hosted by a QQuickView; the view sets
        // FramelessWindowHint + transparent color itself (main()
        // creates the view).  No setAttribute/setWindowFlags here —
        // those are QWidget APIs that don't apply to items.

        // Head-local state (colour theme, vis prefs) lives in
        // winamp.conf.
        setSettingsFile(configPath());

        // The ML Library button's "Media Library Preferences..." routes
        // to the same Preferences dialog as the player's own menu.
        host->showPreferencesFn = [this]() { openPreferences(); };

        // Load persisted visualization mode (0=off, 1=spectrum,
        // 2=oscilloscope, 3=VU).  Default to spectrum analyzer.
        {
            QSettings s(configPath(), QSettings::IniFormat);
            m_visMode = s.value("visualization/mode", 1).toInt();
        }

        // 50 ms repaint cadence (20 fps) — fast enough for the
        // FFT-driven spectrum bars to animate fluidly, slow enough
        // that the QSGSimpleTextureNode re-upload doesn't dominate
        // CPU when the visualizer is off.  QQuickItem::update queues
        // a node update on the scene graph.
        auto *tick = new QTimer(this);
        tick->setInterval(50);
        connect(tick, &QTimer::timeout, this, [this]() {
            // Only force the 20fps full-tree re-raster when something is
            // actually animating frame-to-frame: the visualizer bars, or
            // playback advancing the clock/seekbar.  When the player is
            // idle with the visualizer off the chrome is static, so we
            // leave repaints event-driven (transport signals + Maki
            // setAttr both call update()).  Re-rasterising the entire
            // skin 20x/sec at idle otherwise pins a CPU core for nothing.
            if (m_visMode != 0 || m_host->isPlaying())
                update();
        });
        connect(tick, &QTimer::timeout, this,
                &QtampPlayerWindow::syncMilkdropOverlay);
        tick->start();

        // Immediate repaint when transport state / source changes.  The
        // PlayerHost signals abstract over the audio backend: QtampHost
        // forwards its QMediaPlayer signals, RemoteHost fires them from
        // synced events, so this machinery is backend-agnostic.
        connect(host, &PlayerHost::sourceChanged, this,
                [this]() { fireTitleChange(); update(); });
        connect(host, &PlayerHost::playbackStateChanged, this,
                [this]() {
            // The Maki System playback callbacks (onPlay/onResume/
            // onPause/onStop) are fired by the engine itself from the
            // same host status System.getStatus() reads, so we only
            // repaint here.
            update();
        });
        // Track metadata arrives asynchronously after the source opens;
        // re-fire System.onTitleChange so the skin's fileinfo.maki
        // (re)populates the Title/Artist/Album/… display lines.
        connect(host, &PlayerHost::metaDataChanged, this,
                [this] { fireTitleChange(); update(); });
        // A remote playlist change invalidates the pledit render cache.
        connect(host, &PlayerHost::playlistChanged, this,
                [this] { update(); });

        // Pick up keyboard events (Esc to close, Ctrl-L for open
        // file, arrow keys for colorthemes scroll).  QQuickItems
        // don't receive key events by default.
        setFlag(QQuickItem::ItemIsFocusScope, true);
        setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    }

    // ── Visualisation mode (app-level) ────────────────────────
    //    0 = off, 1 = spectrum analyzer, 2 = oscilloscope,
    //    3 = VU meter.  Driven by the context-menu visualization
    //    submenu AND the Preferences dialog; persisted to
    //    QSettings("visualization/mode").
    int  visMode() const { return m_visMode; }
    void setVisMode(int m) {
        m_visMode = qBound(0, m, 3);
        QSettings s(configPath(), QSettings::IniFormat);
        s.setValue("visualization/mode", m_visMode);
        update();
    }

    // ── Time-display mode (skin-level) ─────────────────────────
    //    1 = elapsed, 2 = remaining (countdown).  One state shared
    //    with the skin scripts: reads/writes the per-skin
    //    TimerElapsedRemaining slot wa2songtimer.m keys on
    //    getSkinName(), then nudges the scripts through
    //    onTitleChange — the event they re-apply the mode on.
    //    Driven by the context-menu Time display submenu, the
    //    skin's own time-click toggle, and the Preferences dialog.
    int  timeDisplayMode() const {
        return qtWasabi::privateConfigInt(
            qtWasabi::activeSkinName(),
            QStringLiteral("TimerElapsedRemaining"), 1);
    }
    void setTimeDisplayMode(int mode) {
        qtWasabi::setPrivateConfigInt(
            qtWasabi::activeSkinName(),
            QStringLiteral("TimerElapsedRemaining"), mode == 2 ? 2 : 1);
        if (m_runtime && m_host)
            m_runtime->dispatchTitleChange(m_host->playItemDisplayTitle());
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
    // Override SkinQuickItem's paint hook so the app's colour-themes
    // list state (selectedRow / topRow / out-bbox) threads into
    // qtWasabi's TreePainter without the engine having to know about
    // that state.  qtWasabi stays a pure renderer; per-skin state is
    // a property of the qtamp consumer.  Called from updatePaintNode
    // into a transparent QImage buffer that gets uploaded as a
    // QSGSimpleTextureNode.
    void paintInto(QPainter *p, const QSize &canvas) override {
        // Do NOT clip the chrome paint to the window region.  The
        // user explicitly wants the chrome bitmap painted fully
        // (rectangular) — the rounded corners come from setMask on
        // the QQuickWindow shaping the OS-level surface, not from
        // alpha-zero pixels in the QImage (Wayfire renders those as
        // white instead of desktop-transparent, producing the
        // staircase-of-white visual the user has been calling out).
        m_ctListRect = QRect();
        qtWasabi::TreePainter::paintTree(
            p, tree(), registry(), fonts(),
            canvas, host(), &gammasets(), &colors(),
            m_ctSelectedRow, m_ctTopRow,
            &m_ctListRect, &m_ctTopRow, m_visMode);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) {
            // Skin scripts get first refusal on right-clicks — real
            // Winamp routes the button events to the widget under the
            // cursor and only shows the default menu when nothing
            // consumed them.  wa2songtimer.m binds TimerTrigger.
            // onRightButtonUp to pop its own elapsed/remaining menu.
            // Down and Up are dispatched back-to-back here (the skin
            // popup opens a nested loop that swallows the real release
            // anyway); receiver-gated, so widgets without handlers
            // cost nothing and fall through to qtamp's menu.
            const QPoint rp = e->position().toPoint();
            int consumed = 0;
            const QList<const qtWasabi::Layout::ResolvedWidget *> rHits =
                alphaHitTestList(rp, /*actionOnly=*/false,
                                 qtampImageSize, &registry());
            for (const auto *w : rHits) {
                if (!w || w->id.isEmpty()) continue;
                consumed += qtWasabi::fireWidgetXYEventOn(
                    w, L"onRightButtonDown", rp.x(), rp.y());
                consumed += qtWasabi::fireWidgetXYEventOn(
                    w, L"onRightButtonUp", rp.x(), rp.y());
                consumed += qtWasabi::fireWidgetEvent(
                    w->id, L"onRightClick");
                if (consumed > 0) break;
            }
            if (consumed > 0) { update(); return; }
            // Right-click anywhere → Winamp-style context menu,
            // built against qtamp's qtWasabi::Host transport surface.
            showContextMenu(e->globalPosition().toPoint());
            return;
        }
        if (e->button() == Qt::LeftButton) {
            const QPoint p = e->position().toPoint();

            // (The old hardcoded drawer-toggle that intercepted clicks
            // at the fixed drawer.button position used to live here.
            // It bypassed Maki dispatch — clicking it ran qtamp's own
            // setDrawerOpen which forced drawer.button.open visible=0
            // permanently, breaking configtabs.maki's btnOpen.show()
            // path on the next close.  Removed: clicks now fall
            // through to the hit-test + fireWidgetEvent path so
            // configtabs.m's btnOpen.onLeftClick / btnClose.onLeftClick
            // handlers run normally.)

            QRect hitBbox;
            const auto *hit = qtWasabi::Layout::hitTest(
                tree(), p, /*actionOnly=*/true,
                qtampImageSize, &registry(), &hitBbox);
            if (hit) {
                // Buttons / togglebuttons → action dispatch.
                // Sliders are intentionally NOT handled here — they
                // flow through the alphaHitTestList button-claim loop
                // below so SliderWidget's own onLeftButtonDown
                // (which handles vertical sliders, lastCanvasRect-
                // relative drag, and host-setSliderPosition writes)
                // owns the entire drag lifecycle.
                // Show the pressed-state bitmap (downImage) so an action
                // button visibly depresses.  This fast-path dispatches the
                // action and returns, otherwise skipping the press
                // lifecycle that script-driven buttons get via the
                // alphaHitTestList claim loop below — leaving transport
                // buttons looking dead even though they fire.  Plain
                // <button> only: togglebuttons cycle their state on
                // release and own their own press handling.  m_activeWidget
                // routes the matching onLeftButtonUp from mouseReleaseEvent.
                if (hit->tag == QLatin1String("button")) {
                    auto *bw = const_cast<qtWasabi::Widget *>(hit);
                    qtWasabi::PaintCtx bctx{};
                    bctx.bmp  = &registry();
                    bctx.host = host();
                    bw->onLeftButtonDown(p, bctx);
                    setActiveWidget(bw);
                }
                const QString action =
                    hit->attrs.value(QStringLiteral("action"));
                fprintf(stderr, "[qtamp] action: %s\n",
                        action.toUpper().toLocal8Bit().constData());
                // `action="TOGGLE" param="<container-id>"` opens (or
                // toggles) a secondary container window from the same
                // skin doc — EQ, Playlist, etc.
                if (action.compare(QStringLiteral("TOGGLE"),
                                   Qt::CaseInsensitive) == 0) {
                    const QString param = hit->attrs.value(
                        QStringLiteral("param"));
                    if (!param.isEmpty()) {
                        // Pass the raw param through — toggleSubwindow
                        // resolves "guid:pl" / "guid:ml" / a literal
                        // GUID / a literal container id to the actual
                        // <container id> via the engine's
                        // SkinXml::resolveContainerId().
                        toggleSubwindow(param);
                        return;
                    }
                }
#ifdef QTAMP_WITH_MILKDROP
                // VIS_Prev / VIS_Next — route to the MilkDrop
                // overlay if one is wired.  The default
                // dispatchAction has no idea what these are; without
                // this hook the prev/next buttons in the AVS drawer
                // fire but nothing happens.
                //
                // Note: there is intentionally no VIS_Random action
                // here — real Winamp's Random control is a
                // `<togglebutton cfgattrib="…;Random">`, not an
                // action button.  Toggle state propagates through
                // CfgAttribStore → MilkdropItem::setShuffle (wired in
                // wireMilkdrop()), exactly mirroring Wasabi's model.
                if (m_milkdropItem) {
                    const QString A = action.toUpper();
                    if (A == QLatin1String("VIS_PREV")) {
                        m_milkdropItem->selectPrev();   return;
                    }
                    if (A == QLatin1String("VIS_NEXT")) {
                        m_milkdropItem->selectNext();   return;
                    }
                }
#endif
                // EQ_TOGGLE used to be handled here as a special
                // action that flipped QtampHost::m_eqEnabled.  That
                // path returned early and prevented the button-
                // claim loop from running cycleOnRelease, so the
                // LEDs and the songinfo "eq" badge never lit up.
                // It's gone now: the EQ ON button has action=
                // EQ_TOGGLE + activeImage, so ButtonWidget gives it
                // a synthetic __action:EQ_TOGGLE cfgattrib; clicks
                // flow through the button-claim path to
                // cycleOnRelease which writes the store; the
                // QtampHost subscription updates m_eqEnabled, and
                // all three EQ indicators (button, drawer LED,
                // songinfo badge) read the same store and update in
                // lockstep.  Single source of truth.
                // dispatchAction wants a QWidget* embedder for file
                // dialogs; QQuickItem isn't a QWidget so pass nullptr.
                if (qtWasabi::dispatchAction(action, m_host, nullptr))
                    return;
                // `action="X" action_target="Y"` — Wasabi's action-
                // dispatch protocol.  Scripts that own widget Y
                // implement `Y.onAction(action, param, x, y, p1, p2,
                // source)` and pivot on the action string (e.g.
                // configtarget.m's `target.onAction("switchto;…")`
                // swapping option pages).  Without this dispatch the
                // option-bucket buttons fire onLeftClick on themselves
                // but nothing happens because the actual logic lives
                // on the target widget.
                // `action="cb_prevpage|cb_nextpage" cbtarget="..."` —
                // componentbucket scroll arrows.  Look up the bucket
                // (we pick the only componentbucket in the parent
                // chain, since the `cbtarget="bucket"` alias doesn't
                // necessarily match the bucket's actual id) and bump
                // its `_scroll` attr.  TreePainter then translates the
                // bucket's children by -scroll * entry_step on each
                // paint so a different window of entries becomes visible.
                if (action.compare(QStringLiteral("cb_prevpage"),
                                   Qt::CaseInsensitive) == 0 ||
                    action.compare(QStringLiteral("cb_nextpage"),
                                   Qt::CaseInsensitive) == 0) {
                    const QString cbtarget = hit->attrs.value(
                        QStringLiteral("cbtarget"));
                    auto &mut = const_cast<qtWasabi::Layout::ResolvedWidget &>(
                        tree());
                    std::function<qtWasabi::Layout::ResolvedWidget *(
                        qtWasabi::Layout::ResolvedWidget &)> findBucket =
                        [&](qtWasabi::Layout::ResolvedWidget &w)
                              -> qtWasabi::Layout::ResolvedWidget * {
                        if (w.tag == QStringLiteral("componentbucket")) {
                            if (cbtarget.isEmpty() ||
                                w.id.compare(cbtarget,
                                              Qt::CaseInsensitive) == 0 ||
                                w.id.endsWith(
                                    QChar('.') + cbtarget,
                                    Qt::CaseInsensitive))
                                return &w;
                        }
                        for (auto &c : w.children)
                            if (c) if (auto *r = findBucket(*c)) return r;
                        return nullptr;
                    };
                    auto *buck = findBucket(mut);
                    if (buck) {
                        int sc = buck->attrs.value(
                            QStringLiteral("_scroll")).toInt();
                        const int cnt = buck->attrs.value(
                            QStringLiteral("_entry_count")).toInt();
                        // Page size derived from bucket.h / entry_step:
                        const int step = qMax(1, buck->attrs.value(
                            QStringLiteral("_entry_step")).toInt());
                        const int viewport = buck->attrs.value(
                            QStringLiteral("h")).toInt() / step;
                        const int maxScroll = qMax(0, cnt - viewport);
                        sc += (action.compare(
                            QStringLiteral("cb_nextpage"),
                            Qt::CaseInsensitive) == 0) ? 1 : -1;
                        sc = qBound(0, sc, maxScroll);
                        // Route through setXmlParam so
                        // ComponentBucketWidget can shadow the value
                        // on its typed state member.
                        buck->setXmlParam(QStringLiteral("_scroll"),
                                           QString::number(sc));
                        update();
                        return;
                    }
                }
                if (!action.isEmpty()) {
                    const QString target = hit->attrs.value(
                        QStringLiteral("action_target"));
                    if (!target.isEmpty()) {
                        // Handle the universal switchto;GROUPID
                        // protocol directly so the option pages work
                        // regardless of whether the script-side
                        // onAction dispatch reads its args correctly.
                        // Wasabi's contract is the same in real
                        // Winamp: a button with
                        // action="switchto;<groupdef-id>" populates
                        // its action_target widget with that groupdef.
                        if (action.startsWith(
                                QStringLiteral("switchto;"),
                                Qt::CaseInsensitive)) {
                            const QString grp =
                                action.section(QChar(';'), 1, 1);
                            if (!grp.isEmpty()) {
                                qtWasabi::fireWidgetAttrSet(
                                    target, QStringLiteral("groupid"), grp);
                                update();
                                return;
                            }
                        }
                        const QString param = hit->attrs.value(
                            QStringLiteral("param"));
                        int fired = qtWasabi::fireWidgetActionEvent(
                            target, action, param,
                            p.x(), p.y(), 0, 0, hit->id);
                        if (fired > 0) {
                            update();
                            return;
                        }
                    }
                }
                // Universal Maki click dispatch: when no built-in
                // action fired, broadcast onLeftClick to any handler
                // that bound to this widget id.  This is how skin
                // scripts wire their own button behaviour (e.g.
                // videoavs.m's btnOpen.onLeftClick → openDrawer())
                // without us needing per-skin glue.
                if (!hit->id.isEmpty()) {
                    int fired = qtWasabi::fireWidgetEvent(
                        hit->id, L"onLeftClick");
                    if (fired > 0) {
                        update();
                        return;
                    }
                }
            }
            // Alpha-aware hit-test list: every widget at the click
            // point that's opaque in the composite alpha, ordered
            // topmost-first.  Wasabi's event model bubbles unhandled
            // clicks down the z-order, so we iterate: the first widget
            // whose Maki handler dispatches (fired > 0) consumes the
            // click.  Chrome layers without handlers are skipped over
            // — they're opaque pixels with no script binding, so the
            // click should reach the button visually behind them.
            const QList<const qtWasabi::Layout::ResolvedWidget *> hits =
                alphaHitTestList(p, /*actionOnly=*/false,
                                  qtampImageSize, &registry());
            const qtWasabi::Layout::ResolvedWidget *hit2 = nullptr;
            // For button-family widgets, fire onLeftButtonDown on the
            // topmost interactive hit BEFORE Maki dispatch so the
            // pressed-state bitmap shows immediately.  The widget is
            // remembered in m_activeWidget so mouseReleaseEvent can
            // route the matching onLeftButtonUp (clearing m_pressed).
            // Menu is handled as a special case below — it short-
            // circuits because its onLeftButtonDown is meant to claim
            // the click entirely (popup spawn, no Maki dispatch).
            bool buttonClaimed = false;
            for (const auto *w : hits) {
                if (!w) continue;
                // Capture-style widgets take the press for the whole
                // press→move→release lifecycle: button/slider families by
                // tag, plus any widget that reports capturesMouse() — the
                // playlist / library list holders, whose onLeftButtonDown
                // selects the row under the cursor and arms scrollbar drag.
                // Claiming here (buttonClaimed) is also what stops the
                // press from falling through to the window-move drag.
                if (w->tag == QLatin1String("button") ||
                    w->tag == QLatin1String("togglebutton") ||
                    w->tag == QLatin1String("nstatesbutton") ||
                    w->tag == QLatin1String("slider") ||
                    w->capturesMouse()) {
                    auto *bw = const_cast<qtWasabi::Widget *>(w);
                    qtWasabi::PaintCtx bctx{};
                    bctx.bmp  = &registry();
                    bctx.host = host();
                    bw->onLeftButtonDown(p, bctx);
                    setActiveWidget(bw);
                    buttonClaimed = true;
                    break;
                }
            }
            for (const auto *w : hits) {
                if (!w || w->id.isEmpty()) continue;
                // Compiled widget behaviours first — these widgets have
                // built-in onLeftButtonDown handlers (Menu's hover/
                // down state swap, Slider drag init, ScrollBar thumb
                // grab) that take precedence over Maki onLeftClick.
                // Dispatch through the Widget virtual; if it claims
                // the click we remember the widget so mouseRelease
                // can route the corresponding onLeftButtonUp.
                if (w->tag == QLatin1String("menu")) {
                    qtWasabi::PaintCtx mctx{};
                    const QString firstId =
                        w->attrs.value(QStringLiteral("menu"));
                    if (firstId.isEmpty()) {
                        // No menu= target: engine down-state toggle only.
                        auto *mw = const_cast<qtWasabi::Widget *>(w);
                        mw->onLeftButtonDown(p, mctx);
                        setActiveWidget(mw);
                        update();
                        return;
                    }
                    // Open the popup, showing the down face while it's up.
                    // showWa5Menu returns the next menu widget to chain to
                    // when the cursor swept onto a sibling in the same
                    // menugroup — loop until a menu closes normally.
                    const qtWasabi::Widget *cur = w;
                    while (cur) {
                        const QString menuId =
                            cur->attrs.value(QStringLiteral("menu"));
                        if (menuId.isEmpty()) break;
                        const QString wid = cur->id;
                        auto *mw = const_cast<qtWasabi::Widget *>(cur);
                        mw->onLeftButtonDown(p, mctx);   // → down
                        update();
                        // Anchor at the menu button's bottom-left.
                        QPoint anchor =
                            mapToGlobal(QPointF(p.x(), p.y())).toPoint();
                        const QRect cr = cur->lastCanvasRect;
                        if (cr.isValid())
                            anchor = mapToGlobal(QPointF(
                                cr.x(), cr.y() + cr.height())).toPoint();
                        const qtWasabi::Widget *next =
                            showWa5Menu(menuId, anchor, cur);
                        // A menu action (e.g. switching skins via
                        // Options > Preferences) can rebuild the whole
                        // widget tree, freeing mw/next.  Only touch a
                        // pointer still registered as the live widget for
                        // its id; otherwise the tree was replaced and these
                        // pointers dangle — stop, the fresh tree is normal.
                        const bool stillLive = !wid.isEmpty() &&
                            qtWasabi::Widget::findById(wid) == mw;
                        if (stillLive) {
                            mw->onLeftButtonDown(p, mctx);   // → normal
                            cur = next;
                        } else {
                            cur = nullptr;
                        }
                        update();
                    }
                    setActiveWidget(nullptr);
                    return;
                }
                // Standard window-control buttons (maximize/restore in
                // the shared standardframe) carry no action= attribute;
                // the skin's own simplemaximize.maki onLeftClick drives
                // them through the Maki resize() binding, so no per-skin
                // interception is needed here — the click falls through
                // to the generic onLeftClick dispatch below.
                applyDrawerModeFixup(w->id);
                // GuiObject press event first — scripts that bind
                // onLeftButtonDown(x, y) (wa2songtimer.m's elapsed/
                // remaining toggle on the invisible TimerTrigger
                // layer) get real button semantics; the widget id is
                // remembered so mouseReleaseEvent routes the matching
                // onLeftButtonUp.  onLeftClick stays the second try
                // for the (far more common) click-bound handlers.
                const int downFired = qtWasabi::fireWidgetXYEventOn(
                    w, L"onLeftButtonDown", p.x(), p.y());
                if (downFired > 0) { m_makiPressId = w->id; m_makiPressWidget = w; }
                int fired = qtWasabi::fireWidgetEvent(
                    w->id, L"onLeftClick");
                if (::getenv("WASABIQT_TRACE_MAKI"))
                    fprintf(stderr,
                        "[click] (%d,%d) alpha hit id=%s fired=%d down=%d\n",
                        p.x(), p.y(),
                        w->id.toLocal8Bit().constData(), fired, downFired);
                if (fired > 0 || downFired > 0) {
                    update();
                    return;
                }
                // Event bubbling: a click on a nested widget whose own id has
                // no onLeftClick handler (a tab's `bento.tabbutton.mousetrap`)
                // should reach a handler bound on an ENCLOSING group — the
                // Maki tab flow binds `switch.X.onLeftClick` and the click
                // lands on the inner mousetrap.  Walk up the parent chain and
                // fire onLeftClick on each id'd ancestor; receiver-gated, so
                // passive ancestors cost nothing.  This is what drives
                // suicore's switchToX (replacing the removed wireTabs system).
                for (const qtWasabi::Widget *a = w->parentWidget;
                     a; a = a->parentWidget) {
                    if (a->id.isEmpty()) continue;
                    if (qtWasabi::fireWidgetEvent(a->id, L"onLeftClick") > 0) {
                        update();
                        return;
                    }
                }
                // First widget with an id becomes the tab-switcher
                // candidate even if it had no Maki handler.
                if (!hit2) hit2 = w;
            }
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
                    if (row >= 0 && row < names.size()) {
                        setActiveGammaset(names[row]);
                        // Remember the choice (real Winamp persists the
                        // colour theme across restarts).
                        QSettings(configPath(), QSettings::IniFormat)
                            .setValue(QStringLiteral("player/colortheme"),
                                      names[row]);
                    }
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
            // Empty-area click — start a window drag.  Skip when a
            // button widget already claimed the press: the button's
            // mouseReleaseEvent path needs to see the matching
            // release to fire onLeftButtonUp (and let togglebutton /
            // nstatesbutton cycle their state).  Without this skip,
            // clicks on auto-cycling state widgets (Repeat / Shuffle
            // / Random — none of them have a Maki onLeftClick
            // handler that would fire>0 above) fell through to
            // startSystemMove and the window dragged instead.
            if (buttonClaimed) {
                update();
                return;
            }
            // Empty area near an edge/corner → resize (native or manual
            // fallback) BEFORE window-move, so dragging the border resizes
            // instead of moving.  This must precede startSystemMove because
            // on Wayland startSystemMove always succeeds and would win.
            if (beginEdgeResize(e->position(), e->globalPosition().toPoint())) {
                e->accept();
                return;
            }
            if (::getenv("WASABIQT_TRACE_MAKI"))
                fprintf(stderr,
                    "[click] (%d,%d) falling through to window drag "
                    "(hit=%s hit2=%s)\n",
                    p.x(), p.y(),
                    hit ? hit->id.toLocal8Bit().constData() : "(null)",
                    hit2 ? hit2->id.toLocal8Bit().constData() : "(null)");
            if (window() && window()->startSystemMove())
                return;
            m_dragOrigin = e->globalPosition().toPoint() -
                           (window() ? window()->position() : QPoint(0,0));
            m_dragging = true;
        }
        qtWasabi::SkinQuickItem::mousePressEvent(e);
    }
    // m_activeWidget can be freed under us if the widget tree is rebuilt
    // (theme/skin reload, Maki relayout) between press and release.  Detect
    // that by id — a pointer compare against the live registry, never a
    // deref of the (possibly freed) widget — and drop the stale pointer.
    void setActiveWidget(qtWasabi::Widget *w) {
        m_activeWidget   = w;
        m_activeWidgetId = w ? w->id : QString();
    }
    bool activeWidgetStale() const {
        return m_activeWidget && !m_activeWidgetId.isEmpty() &&
               qtWasabi::Widget::findById(m_activeWidgetId) != m_activeWidget;
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (activeWidgetStale()) setActiveWidget(nullptr);
        // Slider / list-holder drag — m_activeWidget owns the press,
        // forward every move to it until release.  Slider's onMouseMove
        // updates the host position; a playlist/library holder's
        // onMouseMove drags its scrollbar thumb (capturesMouse() covers
        // those — without this the scrollbar couldn't be dragged).
        if (m_activeWidget && (e->buttons() & Qt::LeftButton) &&
            (m_activeWidget->tag == QLatin1String("slider") ||
             m_activeWidget->capturesMouse())) {
            qtWasabi::PaintCtx ctx{};
            ctx.bmp  = &registry();
            ctx.host = host();
            m_activeWidget->onMouseMove(e->position().toPoint(), ctx);
            update();
            return;
        }
        // (Legacy m_sliderAction drag removed — SliderWidget's own
        // onMouseMove handles drag now, via the m_activeWidget block
        // above.  The legacy path only handled horizontal sliders
        // and used a wrong coord system for vertical EQ bands.)
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
        if (m_dragging && (e->buttons() & Qt::LeftButton) && window()) {
            window()->setPosition(e->globalPosition().toPoint() - m_dragOrigin);
        }
        qtWasabi::SkinQuickItem::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        m_dragging = false;
        m_ctDragging = false;
        // Route the matching GuiObject onLeftButtonUp to the script
        // receiver whose onLeftButtonDown claimed the press.  Guarded
        // by findById: a Down handler can rebuild the tree (skin
        // switch), freeing the remembered pointer.
        if (!m_makiPressId.isEmpty() && e->button() == Qt::LeftButton) {
            const QPoint rp = e->position().toPoint();
            if (m_makiPressWidget &&
                qtWasabi::Widget::findById(m_makiPressId) ==
                    m_makiPressWidget) {
                qtWasabi::fireWidgetXYEventOn(
                    m_makiPressWidget, L"onLeftButtonUp", rp.x(), rp.y());
            }
            m_makiPressId.clear();
            m_makiPressWidget = nullptr;
            update();
        }
        if (activeWidgetStale()) setActiveWidget(nullptr);
        if (m_activeWidget && e->button() == Qt::LeftButton) {
            qtWasabi::PaintCtx mctx{};
            mctx.bmp  = &registry();
            mctx.host = host();
            m_activeWidget->onLeftButtonUp(
                e->position().toPoint(), mctx);
            setActiveWidget(nullptr);
            update();
        }
        qtWasabi::SkinQuickItem::mouseReleaseEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        const bool ctrl = e->modifiers() & Qt::ControlModifier;
        if (e->key() == Qt::Key_Escape) {
            if (window()) window()->close();
            return;
        }
        if (ctrl && (e->key() == Qt::Key_O || e->key() == Qt::Key_L)) {
            // pickFile takes a QWidget* for the file-dialog parent.
            // Pass nullptr so the dialog parents to QGuiApplication;
            // the QQuickWindow itself isn't a QWidget.
            m_host->pickFile(nullptr);   // enqueues + plays internally
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
        qtWasabi::SkinQuickItem::keyPressEvent(e);
    }
    void wheelEvent(QWheelEvent *e) override {
        // Wheel scroll inside the colour-themes list moves the
        // top-row offset.  Outside the list area, fall through.
        const QPoint p = e->position().toPoint();
        const QRect lr = colorThemesListRect();
        if (::getenv("WASABIQT_TRACE_MAKI")) {
            fprintf(stderr, "[wheel] at (%d,%d) ct_rect=%dx%d+%d+%d valid=%d "
                    "contains=%d delta=%d\n",
                    p.x(), p.y(),
                    lr.width(), lr.height(), lr.x(), lr.y(),
                    lr.isValid()?1:0, lr.contains(p)?1:0,
                    e->angleDelta().y());
            fflush(stderr);
        }
        if (lr.isValid() && lr.contains(p)) {
            const int steps = e->angleDelta().y() / 120;  // 1 notch = 120
            setColorThemesTopRow(qMax(0,
                colorThemesTopRow() - steps));
            return;
        }
        qtWasabi::SkinQuickItem::wheelEvent(e);
    }

public:
    // Reload the modern skin at runtime — re-parses the document,
    // re-expands the layout, replays the static well-known-scripts,
    // re-runs the Maki VM, and re-renders.  Drives the live-skin-swap
    // path off the PreferencesDialog's `skinChanged(path)` signal.
    // Also used by the hot-reload watcher when a skin XML file changes
    // on disk.
private:
    // Winamp-style right-click context menu.  Items that map onto
    // qtamp's host surface (Play file, Recent files, Bookmarks,
    // Preferences, Jump to time, About, Exit, Colour Theme) are fully
    // wired.  Items that don't yet have a backend in qtamp (equalizer
    // window, playlist window, video, media library, milkdrop) are
    // left as enabled placeholders that surface a status and wire up
    // as those subsystems land.  Same submenus, labels, hotkey hints,
    // and green-on-navy stylesheet as the classic Winamp menu, plus
    // the PreferencesDialog and AboutDialog.
    // Q_INVOKABLE so the SYSMENU action (via Host::showSystemMenu)
    // can pop this same menu from a non-mouse path via
    // QMetaObject::invokeMethod("showContextMenu", Q_ARG(QPoint, ...)).
public:
    Q_INVOKABLE void showContextMenu(QPoint globalPos) {
        const QString menuStyle = themedMenuStyle();

        QMenu menu;
        menu.setStyleSheet(menuStyle);

        // === Winamp-style main menu (mirrors the classic top menu) ===

        // -- Play submenu --
        QMenu *playMenu = menu.addMenu("Play");
        playMenu->setStyleSheet(menuStyle);
        QAction *playFileAct = playMenu->addAction("Play file(s)...\tL");
        QAction *playFolderAct = playMenu->addAction("Play folder...\tShift+L");
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
        const QUrl currentSrc = m_host->currentSourceUrl();
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
        aotAct->setChecked(window() &&
            (window()->flags() & Qt::WindowStaysOnTopHint));

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

        // -- Time display submenu --
        // Elapsed vs remaining (countdown), the two modes real Winamp
        // offers.  Same per-skin slot the skin's own time-click toggle
        // and the Preferences radios use, so all three stay one state.
        QMenu *timeMenu = menu.addMenu("Time display");
        timeMenu->setStyleSheet(menuStyle);
        const int timeMode = timeDisplayMode();
        QAction *timeElapsedAct = timeMenu->addAction("Time elapsed");
        timeElapsedAct->setCheckable(true);
        timeElapsedAct->setChecked(timeMode != 2);
        QAction *timeRemainAct = timeMenu->addAction("Time remaining");
        timeRemainAct->setCheckable(true);
        timeRemainAct->setChecked(timeMode == 2);

        menu.addSeparator();

        QAction *aboutAct = menu.addAction("About Winamp...");
        menu.addSeparator();
        QAction *quitAct = menu.addAction("Exit");

        // === Handle selection ===
        prepareMenuForWayland(menu);
        QAction *sel = menu.exec(globalPos);
        if (!sel) return;

        if (sel == playFileAct) {
            // QQuickItem isn't a QWidget; pass nullptr so the file
            // dialog parents to QGuiApplication.  Multi-select.
            const QList<QUrl> us = m_host->openFilesAndEnqueue(nullptr);
            for (const QUrl &u : us)
                if (!u.isEmpty())
                    RecentFilesManager::instance().addFile(u.toLocalFile());
        }
        else if (sel == playFolderAct) {
            const QList<QUrl> us = m_host->openFolderAndEnqueue(nullptr);
            if (!us.isEmpty())
                RecentFilesManager::instance().addFile(us.first().toLocalFile());
        }
        else if (sel == playLocAct) {
            PlayLocationDialog dlg(nullptr);
            if (dlg.exec() == QDialog::Accepted) {
                QString url = dlg.getUrl();
                if (!url.isEmpty())
                    m_host->enqueueAndPlay(QUrl(url));
            }
        }
        else if (recentOf.contains(sel)) {
            const QString f = recentOf.value(sel);
            m_host->enqueueAndPlay(QUrl::fromLocalFile(f));
        }
        else if (sel == addBmAct) {
            bool ok;
            const QString title = QInputDialog::getText(nullptr,
                tr("Add Bookmark"), tr("Bookmark title:"),
                QLineEdit::Normal, QFileInfo(currentFile).fileName(), &ok);
            if (ok && !title.isEmpty())
                BookmarkManager::instance().addBookmark(title, currentFile);
        }
        else if (bmOf.contains(sel)) {
            const auto &bm = bmMgr.bookmarks[bmOf.value(sel)];
            m_host->enqueueAndPlay(QUrl::fromLocalFile(bm.path));
        }
        else if (sel == aotAct) {
            if (auto *w = window()) {
                Qt::WindowFlags f = w->flags();
                if (sel->isChecked()) f |=  Qt::WindowStaysOnTopHint;
                else                  f &= ~Qt::WindowStaysOnTopHint;
                w->setFlags(f);
                w->show();
            }
        }
        else if (sel == prefsAct) {
            openPreferences();
        }
        else if (sel == jumpTimeAct) {
            bool ok;
            QString timeStr = QInputDialog::getText(nullptr,
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
            // The animated demoscene-style AboutDialog.
            QString skinPath;
            const QUrl src = m_host->currentSourceUrl();
            if (src.isLocalFile()) skinPath = QFileInfo(src.toLocalFile()).absolutePath();
            AboutDialog about(skinPath, nullptr);
            about.exec();
        }
        else if (sel == visOffAct)  setVisMode(0);
        else if (sel == visSpecAct) setVisMode(1);
        else if (sel == visOscAct)  setVisMode(2);
        else if (sel == visVuAct)   setVisMode(3);
        else if (sel == timeElapsedAct) setTimeDisplayMode(1);
        else if (sel == timeRemainAct)  setTimeDisplayMode(2);
        else if (sel == quitAct) { if (window()) window()->close(); }
    }

    // Open the Preferences dialog (shared by the context menu and the
    // skin's Options menu).  Persists a picked skin + applies vis prefs.
    void openPreferences() {
        auto *prefs = new PreferencesDialog(nullptr);
        prefs->setStyleSheet(themedDialogStyle());   // tint to the skin
        // Feed the active skin's Color Themes into the dialog's picker, and
        // apply a chosen one live — setActiveGammaset re-tints the skin and
        // (via our override) this very dialog.  Show "Default colors"
        // selected when the active theme is the skin's own default / none.
        {
            const QString activeName =
                gammasets().active() ? gammasets().active()->name : QString();
            const bool atDefault = activeName.isEmpty() ||
                activeName == gammasets().defaultThemeName();
            prefs->setColorThemes(gammasets().names(),
                                  atDefault ? QString() : activeName);
        }
        // Reflect the skin scripts' persisted time-display mode (the
        // same slot wa2songtimer.m toggles on a time-display click).
        prefs->setTimeDisplayMode(qtWasabi::privateConfigInt(
            qtWasabi::activeSkinName(),
            QStringLiteral("TimerElapsedRemaining"), 1));
        connect(prefs, &PreferencesDialog::colorThemeChanged, this,
                [this](const QString &name) {
            // The "Default colors" entry isn't a real gammaset — revert to
            // the skin's own default (its native default theme, or no tint).
            const QString applied = gammasets().find(name)
                ? name : gammasets().defaultThemeName();
            if (::getenv("WASABIQT_TRACE_MAKI"))
                fprintf(stderr, "[preftheme] pick '%s' -> apply '%s'\n",
                        name.toLocal8Bit().constData(),
                        applied.toLocal8Bit().constData());
            setActiveGammaset(applied);
            // The skin window is behind the (modal) dialog; make sure it
            // actually re-renders with the new tint rather than waiting.
            if (window()) window()->requestUpdate();
            update();
            QSettings(configPath(), QSettings::IniFormat)
                .setValue(QStringLiteral("player/colortheme"), applied);
        });
        connect(prefs, &PreferencesDialog::skinChanged,
                this, [this, prefs](const QString &path){
            QSettings s(configPath(), QSettings::IniFormat);
            s.setValue("skin", path);
            s.sync();
            if (isModernSkinDir(path)) {
                const QString xml = path + "/skin.xml";
                if (QFile::exists(xml)) {
                    reloadSkin(xml);
                    // The dialog stays open across the switch — refresh the
                    // Color Theme picker (and the dialog tint) for the NEW
                    // skin, otherwise it keeps showing the previous skin's
                    // themes.
                    prefs->setColorThemes(gammasets().names(),
                        gammasets().active() ? gammasets().active()->name
                                             : QString());
                    prefs->setStyleSheet(themedDialogStyle());
                }
            } else {
#ifndef Q_OS_WASM
                QProcess::startDetached(
                    QApplication::applicationFilePath(), {});
                QApplication::quit();
#endif
            }
        });
        connect(prefs, &PreferencesDialog::settingChanged,
                this, [this](const QString &key, const QVariant &v){
            if (key == QStringLiteral("visMode")) {
                setVisMode(v.toInt());
            } else if (key == QStringLiteral("timeDisplayMode")) {
                setTimeDisplayMode(v.toInt());
            } else if (key == QStringLiteral("saPeaks") ||
                       key == QStringLiteral("saPeakFalloff") ||
                       key == QStringLiteral("saFalloff")) {
                m_host->reloadVisPrefs();
                update();
            }
        });
        prefs->setAttribute(Qt::WA_DeleteOnClose);
        prefs->exec();
    }

    // React to a colour-theme switch: re-tint our own (non-skin) Qt chrome
    // so an already-open dialog or popup follows the new theme live, not
    // just the next one opened.  The skin re-tints itself through the base
    // class; we add the chrome on top.
    // The visible <Menu> widget whose canvas rect contains `itemPos`, if
    // any.  Menus paint nothing themselves but cacheResolvedRects fills
    // their lastCanvasRect, so the bar's button bounds are available for
    // the hover-switch hit-test.
    const qtWasabi::Widget *menuWidgetAt(QPoint itemPos) const {
        const qtWasabi::Widget *found = nullptr;
        std::function<void(const qtWasabi::Widget &)> walk =
            [&](const qtWasabi::Widget &w) {
            if (found) return;
            if (w.tag == QLatin1String("menu") &&
                w.attrs.value(QStringLiteral("visible")) !=
                    QStringLiteral("0") &&
                w.lastCanvasRect.contains(itemPos)) {
                found = &w;
                return;
            }
            for (const auto &c : w.children) if (c) walk(*c);
        };
        walk(tree());
        return found;
    }

    // Spawn the popup for a skin menu-bar button (`<Menu menu="WA5:File">`).
    // The WA5:* ids are Winamp's standard top-bar menus; we build a focused
    // popup per id, wired to the same Host/window actions the right-click
    // context menu uses.  While open it polls the cursor — moving onto a
    // SIBLING <Menu> in the same `menugroup` cancels this popup and returns
    // that widget so the caller can chain to it (the menu-bar sweep, per
    // the Wasabi switchToMenu timer model).  Returns the next menu widget
    // to open, or nullptr when the popup closed normally.
    const qtWasabi::Widget *showWa5Menu(const QString &menuId,
                                        QPoint globalPos,
                                        const qtWasabi::Widget *source) {
        const QString menuStyle = themedMenuStyle();
        QMenu menu;
        menu.setStyleSheet(menuStyle);
        // Match on the trailing menu name so "WA5:File" and a bare "File"
        // resolve the same.
        const QString m = menuId.section(QChar(':'), -1).toLower();
        if (::getenv("WASABIQT_TRACE_MAKI"))
            fprintf(stderr, "[wa5menu] spawn '%s' (-> '%s') at (%d,%d)\n",
                    menuId.toLocal8Bit().constData(),
                    m.toLocal8Bit().constData(), globalPos.x(), globalPos.y());

        QHash<QAction *, std::function<void()>> act;
        if (m == QLatin1String("file")) {
            act[menu.addAction("Play file...")] = [this]{
                const QUrl u = m_host->pickFile(nullptr);
                if (!u.isEmpty())
                    RecentFilesManager::instance().addFile(u.toLocalFile());
            };
            act[menu.addAction("Play location...")] = [this]{
                PlayLocationDialog dlg(nullptr);
                if (dlg.exec() == QDialog::Accepted && !dlg.getUrl().isEmpty())
                    m_host->enqueueAndPlay(QUrl(dlg.getUrl()));
            };
            menu.addSeparator();
            act[menu.addAction("Exit")] =
                [this]{ if (window()) window()->close(); };
        } else if (m == QLatin1String("play")) {
            const bool playing = m_host->isPlaying();
            act[menu.addAction(playing ? "Pause" : "Play")] =
                [this, playing]{ if (playing) m_host->pause();
                                 else m_host->play(); };
            act[menu.addAction("Stop")] = [this]{ m_host->stop(); };
            menu.addSeparator();
            act[menu.addAction("Previous")] = [this]{ m_host->prev(); };
            act[menu.addAction("Next")]     = [this]{ m_host->next(); };
        } else if (m == QLatin1String("options")) {
            act[menu.addAction("Preferences...")] = [this]{ openPreferences(); };
            menu.addSeparator();
            // Real Winamp's Options menu carries the two time-display
            // modes; same per-skin slot as the context-menu submenu.
            const int tm = timeDisplayMode();
            QAction *te = menu.addAction("Time elapsed");
            te->setCheckable(true);
            te->setChecked(tm != 2);
            act[te] = [this]{ setTimeDisplayMode(1); };
            QAction *tr = menu.addAction("Time remaining");
            tr->setCheckable(true);
            tr->setChecked(tm == 2);
            act[tr] = [this]{ setTimeDisplayMode(2); };
        } else if (m == QLatin1String("view") ||
                   m == QLatin1String("windows")) {
            act[menu.addAction("Playlist editor")] =
                [this]{ toggleSubwindow(QStringLiteral("pl")); };
            act[menu.addAction("Media library")] =
                [this]{ toggleSubwindow(QStringLiteral("ml")); };
            act[menu.addAction("Video")] =
                [this]{ toggleSubwindow(QStringLiteral("vid")); };
            menu.addSeparator();
            QMenu *visM = menu.addMenu("Visualization");
            visM->setStyleSheet(menuStyle);
            const char *vl[] = { "Off", "Spectrum analyzer",
                                 "Oscilloscope", "VU meter" };
            for (int i = 0; i < 4; ++i) {
                QAction *a = visM->addAction(vl[i]);
                a->setCheckable(true);
                a->setChecked(m_visMode == i);
                act[a] = [this, i]{ setVisMode(i); };
            }
        } else if (m == QLatin1String("help")) {
            act[menu.addAction("About...")] =
                [this]{ AboutDialog about(QString(), nullptr); about.exec(); };
        } else {
            showContextMenu(globalPos);
            return nullptr;
        }

        // menugroup hover-switch: while this popup is open, poll the cursor
        // and chain to a sibling menu button it enters.  Record where the
        // cursor was at spawn and only switch once it has actually MOVED
        // (Wasabi's xuimenu timerCheck does the same) — otherwise a
        // stationary cursor still hovering the just-clicked / arrow-key'd
        // button would immediately yank the popup back to it.
        const qtWasabi::Widget *chainTo = nullptr;
        const QString grp = source
            ? source->attrs.value(QStringLiteral("menugroup")) : QString();
        const QPoint origCursor = QCursor::pos();
        QTimer poll;
        poll.setInterval(60);
        connect(&poll, &QTimer::timeout, &menu, [&]{
            if (!source || grp.isEmpty()) return;
            const QPoint gc = QCursor::pos();
            if (gc == origCursor) return;   // cursor hasn't moved yet
            const QPoint ip = mapFromGlobal(gc).toPoint();
            const qtWasabi::Widget *sib = menuWidgetAt(ip);
            if (sib && sib != source &&
                sib->attrs.value(QStringLiteral("menugroup"))
                    .compare(grp, Qt::CaseInsensitive) == 0) {
                chainTo = sib;
                menu.close();
            }
        });
        // Left/Right arrow keys walk the prev/next menu chain.
        MenuArrowFilter navFilter;
        navFilter.menu = &menu;
        auto chainByAttr = [&](const QString &attr) {
            if (!source) return;
            const QString id = source->attrs.value(attr);
            if (id.isEmpty()) return;
            qtWasabi::Widget *nw = qtWasabi::Widget::findById(id);
            if (nw && nw->tag == QLatin1String("menu")) {
                chainTo = nw;
                menu.close();
            }
        };
        navFilter.onPrev = [&]{ chainByAttr(QStringLiteral("prev")); };
        navFilter.onNext = [&]{ chainByAttr(QStringLiteral("next")); };
        // App-level filter (not menu-level): on Wayland the xdg-popup may
        // not hold keyboard focus, so a filter on the menu can miss the
        // arrows.  qApp sees the key wherever Qt delivers it.  MUST be
        // removed before navFilter (a local) is destroyed.
        if (source) qApp->installEventFilter(&navFilter);

        if (source) poll.start();
        prepareMenuForWayland(menu);
        QAction *sel = menu.exec(globalPos);
        poll.stop();
        if (source) qApp->removeEventFilter(&navFilter);
        if (chainTo) return chainTo;          // chain; don't run the action
        if (sel && act.contains(sel)) act.value(sel)();
        return nullptr;
    }

    void applySliderDrag(int xInWindow) {
        if (m_sliderTrack.width() <= 0) return;
        const double v = double(xInWindow - m_sliderTrack.x()) /
                         double(m_sliderTrack.width());
        m_host->setSliderPosition(m_sliderAction,
                                   qBound(0.0, v, 1.0));
    }

    // AVS-drawer mode-switch fixup.  The Maki chain in qtwasabi
    // doesn't propagate drawer_showVideo's getVideoWindowHolder()
    // .show() through to the WindowHolder widget — onShowVideo()'s
    // button-bar swap (vis ↔ video) fires fine but the holder
    // visibility never flips.  Recognise the two switchto clicks by
    // widget id and toggle myviswnd/myvideownd visibility here in
    // lockstep with the Maki dispatch.  By the Wasabi model this is
    // handled entirely in script; this hook is a pragmatic interim
    // until the VM correctly caches the holder lookup.  Case-tolerant
    // because skin XML uses lowercase ids while Maki globals are
    // bound with CamelCase aliases.
    void applyDrawerModeFixup(const QString &clickedId) {
        auto setVis = [](const char *id, bool on) {
            if (auto *wnd = qtWasabi::Widget::findById(
                    QString::fromLatin1(id)))
                wnd->setXmlParam(QStringLiteral("visible"),
                    on ? QStringLiteral("1") : QStringLiteral("0"));
        };
        // Per-mode visibility set covering BOTH the render slots
        // (myviswnd / myvideownd) and the corresponding button bars
        // (buttons.vis* / buttons.video*).  Wasabi's onShowVis /
        // onShowVideo drives all of these in script, but our Timer
        // port refuses the script's hides at SkinRuntimeBridge (the
        // Timer-gated re-show would otherwise leave them invisible
        // forever).  We bypass that by setting attrs directly here.
        auto applyMode = [&](bool vis) {
            // Render slot.
            setVis("myviswnd",                vis);
            setVis("myvideownd",             !vis);
            // Button bar — "Switch" + "Detach" labels + the parent
            // container groups that hold the secondary controls
            // (Prev/Next/Random for vis, 1x/2x/FS for video).
            setVis("buttons.vis",             vis);
            setVis("buttons.vis.detach",      vis);
            setVis("buttons.vis.switchto",    vis);
            setVis("buttons.video",          !vis);
            setVis("buttons.video.detach",   !vis);
            setVis("buttons.video.switchto", !vis);
        };
        if (clickedId.compare(QLatin1String("button.vis.switchto"),
                               Qt::CaseInsensitive) == 0) {
            applyMode(false);   // switch to video mode
        } else if (clickedId.compare(QLatin1String("button.vid.switchto"),
                                      Qt::CaseInsensitive) == 0) {
            applyMode(true);    // switch to visualiser mode
        } else if (clickedId.compare(QLatin1String("videoavs.open"),
                                      Qt::CaseInsensitive) == 0) {
            // Drawer open — initial mode is visualiser.  Without
            // this, myviswnd stays visible="0" (XML default), the
            // milkdrop placeholder inside it never paints, and the
            // MilkdropItem GL overlay never wakes up.  User would
            // see a black drawer until they ping-pong through video
            // mode and back.  Mirrors the Timer-driven Maki
            // drawer_showVis path that our partial Timer port skips.
            applyMode(true);
        }
    }

    // Walk the freshly-loaded skin tree for the first <milkdrop>
    // placeholder.  When found AND the build has QTAMP_WITH_MILKDROP,
    // construct a MilkdropItem as a sibling overlay of this
    // SkinQuickItem.  The per-tick `syncMilkdropOverlay` then snaps
    // its geometry to the placeholder's canvas rect.
    void wireMilkdrop() {
        // Order matters: clear the stale placeholder pointer BEFORE
        // any other code runs.  reloadSkin() rebuilds m_tree before
        // calling us; the old m_milkdropPlaceholder dangled into the
        // freed tree until this line.  syncMilkdropOverlay (50 ms
        // GUI-thread tick) wouldn't actually fire mid-reload because
        // we're single-threaded, but other re-entrant code paths
        // (e.g. Maki scripts loaded by the runtime reset) could.
        m_milkdropPlaceholder = nullptr;
        m_milkdropPlaceholder = findAVSPlaceholder(&tree());
#ifdef QTAMP_WITH_MILKDROP
        // We deliberately do NOT delete MilkdropItem when the new
        // skin has no <milkdrop> placeholder.  Deleting an item that
        // owns a dedicated GL context + offscreen surface + projectM
        // mid-reload races against the scene-graph render thread,
        // which may still be inside our `beforeFrameBegin` slot
        // (Qt::DirectConnection).  Instead we just clear the
        // placeholder pointer above; the per-tick syncMilkdropOverlay
        // hides the item when the placeholder is null, and the item
        // stays alive across skin reloads.
        auto *localHost = dynamic_cast<PlayerHost *>(m_host);
        if (m_milkdropPlaceholder && !m_milkdropItem && localHost &&
            localHost->analyzerPtr()) {
            m_milkdropItem = new MilkdropItem(this);
            m_milkdropItem->setAnalyzer(localHost->analyzerPtr());
            m_milkdropItem->setVisible(false);  // until first sync
            // Stack above chrome so the GL surface isn't masked.
            m_milkdropItem->setZ(1000.0);
            // Feed the offscreen MilkDrop frame to any AVS windowholder
            // that lives in a window WITHOUT a GL surface (a detached
            // SkinView).  The GL overlay still serves the main window;
            // syncMilkdropOverlay flips readback on and sizes the FBO to
            // the detached slot, so copyFrame() yields a frame exactly
            // matching the slot's request — the size guard keeps a second
            // AVS slot (e.g. the chrome's small album-art vis) from
            // blitting a wrong-size copy.
            qtWasabi::registerHolderFrameProvider(
                [this](const QString &guidKey, const QSize &size) -> QImage {
                    if (!m_milkdropItem) return QImage();
                    if (guidKey != QStringLiteral(
                            "{0000000a-000c-0010-ff7b-01014263450c}"))
                        return QImage();
                    QImage f;
                    if (!m_milkdropItem->copyFrame(f)) return QImage();
                    if (f.size() != size) return QImage();
                    return f;
                });
        } else if (!m_milkdropPlaceholder && m_milkdropItem) {
            m_milkdropItem->setVisible(false);
        }
        // Subscribe to the shuffle/random cfgattrib so the
        // togglebutton's state propagates to projectM via
        // projectm_playlist_set_shuffle().  Canonical Wasabi key
        // suffix is `;Random` (case-insensitive); the GUID prefix
        // can vary per skin (it's the vis-service GUID), so match
        // by suffix to stay skin-agnostic.  On a skin reload the
        // old subscription may point to the previous skin's key —
        // unsubscribe and re-bind to whatever the current tree
        // declares.
        if (m_randomCfgSub != 0) {
            qtWasabi::CfgAttribStore::instance().unsubscribe(
                m_randomCfgSub);
            m_randomCfgSub = 0;
        }
        if (m_milkdropItem) {
            const QString key = findCfgAttribKeyForSuffix(
                &tree(), QStringLiteral(";Random"));
            if (!key.isEmpty()) {
                auto &store = qtWasabi::CfgAttribStore::instance();
                // Seed from the current store value (the togglebutton
                // populated it during onAttrsInitialized).
                m_milkdropItem->setShuffle(store.get(key) != 0);
                m_randomCfgSub = store.subscribe(key,
                    [this](int v) {
                        if (m_milkdropItem)
                            m_milkdropItem->setShuffle(v != 0);
                    });
            }
        }
#endif
    }

    // DFS the tree for the first widget whose `cfgattrib` ends with
    // the given suffix (case-insensitive match).  Used to find the
    // Random toggle's binding key without hard-coding the GUID.
    QString findCfgAttribKeyForSuffix(
            const qtWasabi::Widget *w, const QString &suffix) const {
        if (!w) return QString();
        const QString cfg = w->attrs.value(QStringLiteral("cfgattrib"));
        if (!cfg.isEmpty() &&
            cfg.endsWith(suffix, Qt::CaseInsensitive))
            return cfg;
        for (const auto &c : w->children) {
            const QString k = findCfgAttribKeyForSuffix(c.get(), suffix);
            if (!k.isEmpty()) return k;
        }
        return QString();
    }

    // DFS the tree for the AVS slot *currently being painted* with the
    // largest canvas area.  A skin commonly declares several AVS host
    // slots — Bento ships a tiny 84x84 `<component id="vis">` in the
    // player chrome AND a full-size `<windowholder id="wdh.vis.object">`
    // for the Visualization tab, both with the AVS GUID.  Only the slot
    // the user has open keeps repainting (lastPaintedAtMs refreshes);
    // others go stale.  A static first-match placeholder snapped the GL
    // overlay to the 84x84 one, so the big tab stayed black.  Picking
    // the freshest+largest alive slot each tick is skin-agnostic and
    // follows the visualizer as the user switches chrome-vis ↔ vis tab.
    const qtWasabi::Widget *
    bestAliveAvsSlot(const qtWasabi::Widget *w, qint64 nowMs,
                     const qtWasabi::Widget *best = nullptr) const {
        if (!w) return best;
        // PLACEMENT predicate is stricter than the load-time BUILD
        // predicate (isAvsTag): the MilkDrop GL overlay may only land on
        // a real visualization *window* (`windowholder`/`wmh`), never on
        // an embedded `<component id="vis">` chrome slot.  Bento puts a
        // tiny 84x74 `<component id="vis">` over the player display /
        // album-art area (player-normal-mcv.xml) AND the full-size
        // `<windowholder id="wdh.vis.object">` on the Visualization tab
        // (player-normal-sui.xml).  Letting the overlay fall back to the
        // component slot when the big tab is closed painted a small
        // visualizer over the album art — exactly what the user reported.
        // Excluding `component` here means: tab open → overlay on the
        // windowholder; tab closed → no alive windowholder → overlay
        // hides (album art shows through).  Skin-agnostic, no per-skin id.
        const bool isAvs =
            w->tag == QLatin1String("milkdrop") ||
            (isAvsWindowSlot(w->tag) &&
             isAvsGuidRef(w->attrs.value(QStringLiteral("hold")))) ||
            // An embedded <component param="guid:avs"> is the skin's
            // chosen inline AVS host (e.g. HeadAMP's InlineAVS).  This is
            // distinct from Bento's hold=-based mini-vis component (kept
            // excluded above): param= is Wasabi's spelling for the GUID
            // of the component a <component> embeds, so a param-AVS
            // component is an explicit "render AVS here" slot.
            (w->tag == QLatin1String("component") &&
             isAvsGuidRef(w->attrs.value(QStringLiteral("param"))));
        if (isAvs) {
            const bool alive = w->lastPaintedAtMs > 0 &&
                               (nowMs - w->lastPaintedAtMs) < 150;
            const QRect r = w->lastCanvasRect;
            if (alive && r.width() > 0 && r.height() > 0) {
                const qint64 area = qint64(r.width()) * r.height();
                const qint64 bestArea = best
                    ? qint64(best->lastCanvasRect.width()) *
                          best->lastCanvasRect.height()
                    : -1;
                if (area > bestArea) best = w;
            }
        }
        for (const auto &c : w->children)
            best = bestAliveAvsSlot(c.get(), nowMs, best);
        return best;
    }

    // Per-tick geometry + visibility update for the MilkDrop overlay.
    // Follows whichever AVS slot the active layout/tab is currently
    // painting (freshest+largest), hiding when none is alive.
    void syncMilkdropOverlay() {
#ifdef QTAMP_WITH_MILKDROP
        if (!m_milkdropItem) return;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        // Find the globally best-alive AVS slot across the main window
        // AND every visible detached subwindow.  There is ONE projectM
        // instance; it serves whichever window owns the freshest+largest
        // slot.  The main window gets it via the GL overlay item; a
        // detached SkinView (a software QWidget with no GL surface of its
        // own) gets it via CPU readback blitted by its windowholder.
        auto areaOf = [](const qtWasabi::Widget *w) -> qint64 {
            if (!w) return -1;
            return qint64(w->lastCanvasRect.width()) *
                   w->lastCanvasRect.height();
        };
        const qtWasabi::Widget *bestSlot =
            m_milkdropPlaceholder ? bestAliveAvsSlot(&tree(), nowMs)
                                  : nullptr;
        qtWasabi::SkinView *bestOwner = nullptr;   // nullptr = main window
        for (qtWasabi::SkinView *sv : std::as_const(m_subwindows)) {
            if (!sv || !sv->isVisible()) continue;
            const qtWasabi::Widget *s = bestAliveAvsSlot(&sv->tree(), nowMs);
            if (s && areaOf(s) > areaOf(bestSlot)) {
                bestSlot = s;
                bestOwner = sv;
            }
        }

        if (std::getenv("WASABIQT_TRACE_MILKDROP")) {
            if (bestSlot)
                std::fprintf(stderr,
                    "[milkdrop] overlay -> owner=%s tag=%s id=%s rect=%dx%d+%d+%d\n",
                    bestOwner ? "detached" : "main",
                    bestSlot->tag.toUtf8().constData(),
                    bestSlot->id.toUtf8().constData(),
                    bestSlot->lastCanvasRect.width(),
                    bestSlot->lastCanvasRect.height(),
                    bestSlot->lastCanvasRect.x(),
                    bestSlot->lastCanvasRect.y());
            else
                std::fprintf(stderr,
                    "[milkdrop] overlay -> HIDDEN (no alive window-vis slot)\n");
        }

        if (!bestSlot) {
            if (m_milkdropItem->isVisible())
                m_milkdropItem->setVisible(false);
            m_milkdropItem->setCpuReadbackEnabled(false);
            m_milkdropItem->setRenderSize(QSize());
            return;
        }
        const QRect r = bestSlot->lastCanvasRect;
        if (r.width() <= 0 || r.height() <= 0) return;

        if (bestOwner) {
            // Detached path: the GL overlay can't render into another
            // window, so render projectM offscreen at the detached slot's
            // size and let the slot's windowholder blit the readback
            // frame.  Repaint the owner so the fresh frame lands.
            if (m_milkdropItem->isVisible())
                m_milkdropItem->setVisible(false);
            m_milkdropItem->setRenderSize(r.size());
            m_milkdropItem->setCpuReadbackEnabled(true);
            bestOwner->update();
            return;
        }

        // Main path: GL overlay renders directly into this window; no
        // readback, FBO follows the on-screen item size.
        m_milkdropItem->setCpuReadbackEnabled(false);
        m_milkdropItem->setRenderSize(QSize());
        m_milkdropItem->setX(r.x());
        m_milkdropItem->setY(r.y());
        m_milkdropItem->setWidth(r.width());
        m_milkdropItem->setHeight(r.height());
        if (!m_milkdropItem->isVisible())
            m_milkdropItem->setVisible(true);
#endif
    }

private:
    // DFS into a Widget subtree looking for the first widget that
    // declares an AVS visualizer slot.  Cheap (called only at
    // skin-load time).  Two forms are recognised — both produce the
    // same observable behaviour because `lastCanvasRect` and
    // `lastPaintedAtMs` live on the base Widget class:
    //   1. Explicit `<milkdrop>` tag (qtamp-specific, used by
    //      skins that want to opt in regardless of GUID convention).
    //   2. `<windowholder hold="guid:avs">` (or the `wmh` alias) or
    //      the resolved-GUID form — the canonical Wasabi pattern used
    //      by Winamp Modern / WinampModernPP.
    //   3. `<component hold="guid:...">` — Wasabi's OTHER spelling for
    //      an HWND-host slot.  Bento + Big Bento declare their vis
    //      panel this way (`<component id="vis" hold="guid:{0000000A-…}">`).
    //      Widget.cpp already maps the `component` tag to a
    //      WindowHolderWidget, so the slot paints + bumps
    //      lastPaintedAtMs; the only gap was THIS tag check not knowing
    //      the alias, so m_milkdropItem was never built and the Bento /
    //      Big Bento Visualization tab stayed black.
    // True when an attribute value (a windowholder's `hold=` or a
    // component's `param=`) refers to the AVS visualization component —
    // either by the `guid:avs` alias or its canonical GUID.
    static bool isAvsGuidRef(const QString &ref) {
        // Normalise any spelling — `guid:avs`, the `guid:`-prefixed GUID,
        // or a bare `{0000000A…}` with no prefix (Winamp Modern's detached
        // vis component uses the bare form) — to the same bare lowercase
        // key before comparing.  A prefix-only match misses the detached
        // window's holder and leaves the visualizer black.
        QString bare = ref.trimmed();
        if (bare.startsWith(QLatin1String("guid:"), Qt::CaseInsensitive))
            bare = bare.mid(5).trimmed();
        bare = bare.toLower();
        return bare == QLatin1String("avs") ||
               bare == QLatin1String("{0000000a-000c-0010-ff7b-01014263450c}");
    }
    static bool isAvsTag(const QString &tag) {
        return tag == QLatin1String("windowholder") ||
               tag == QLatin1String("wmh") ||
               tag == QLatin1String("component");
    }
    // Stricter than isAvsTag: a real visualization *window* slot, used
    // for OVERLAY PLACEMENT only (see bestAliveAvsSlot).  Excludes
    // `component`, which is Wasabi's embedded-chrome HWND host (Bento's
    // small player-display vis) — the GL overlay must never land there.
    static bool isAvsWindowSlot(const QString &tag) {
        return tag == QLatin1String("windowholder") ||
               tag == QLatin1String("wmh");
    }
    const qtWasabi::Widget *
    findAVSPlaceholder(const qtWasabi::Widget *w) const {
        if (!w) return nullptr;
        if (w->tag == QLatin1String("milkdrop"))
            return w;
        if (isAvsTag(w->tag)) {
            // Windowholders host the AVS window via `hold=`; <component>
            // embeds it via `param=` — accept either spelling.
            if (isAvsGuidRef(w->attrs.value(QStringLiteral("hold"))) ||
                isAvsGuidRef(w->attrs.value(QStringLiteral("param"))))
                return w;
        }
        for (const auto &c : w->children) {
            if (auto *hit = findAVSPlaceholder(c.get())) return hit;
        }
        return nullptr;
    }
public:

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
        auto &mut = const_cast<qtWasabi::Layout::ResolvedWidget &>(tree());
        std::function<void(qtWasabi::Layout::ResolvedWidget &)> walk =
            [&](qtWasabi::Layout::ResolvedWidget &w) {
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
            for (auto &c : w.children) if (c) walk(*c);
        };
        walk(mut);
        // Shrink the window when the drawer is closed so the chrome
        // sits on its own footprint with no transparent strip below.
        // Compact height covers chrome + the open-button tab that
        // pokes out at the bottom: drawer.y(=17) + button.relY(=118)
        // + button.h(~7) + a couple px of padding.
        //
        // Only the small player layouts (WACUP-style, ~185/105 px tall)
        // shrink like this.  The big Bento layout (h≈600, with the docked
        // Media Library below) keeps the drawer INTERNAL — collapsing that
        // window to the drawer-tab height clipped off the whole player.
        // So leave a large layout's window size alone.
        const QSize full = layoutNativeSize();
        const int compactH = 17 + 118 + 7 + 2;  // 144 px
        if (auto *w = window(); w && full.height() < 300) {
            w->setMinimumSize(QSize(0, 0));
            w->setMaximumSize(QSize(16777215, 16777215));
            w->resize(full.width(), open ? full.height() : compactH);
        }
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
        auto &mut = const_cast<qtWasabi::Layout::ResolvedWidget &>(tree());
        std::function<void(qtWasabi::Layout::ResolvedWidget &)> walk =
            [&](qtWasabi::Layout::ResolvedWidget &w) {
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
            for (auto &c : w.children) if (c) walk(*c);
        };
        walk(mut);
    }

    QPoint     m_dragOrigin;
    bool       m_dragging = false;
    // Script receiver whose onLeftButtonDown claimed the press —
    // mouseReleaseEvent routes the matching onLeftButtonUp to it.
    // Pointer for the instance-exact dispatch, id for the liveness
    // re-check (findById) in case a handler rebuilt the tree.
    QString    m_makiPressId;
    const qtWasabi::Widget *m_makiPressWidget = nullptr;
    QString    m_sliderAction;     // empty when not dragging a slider
    QRect      m_sliderTrack;
    // Widget currently holding the left mouse button — receives
    // onLeftButtonUp / onMouseMove until release.  Used by widgets
    // with compiled built-in interaction (Menu hover/down state,
    // future Slider drag, ScrollBar thumb grab).
    qtWasabi::Widget *m_activeWidget = nullptr;
    // Id of m_activeWidget, captured when it's set, so a press→rebuild→release
    // sequence can tell a live widget from a freed one without dereferencing.
    QString           m_activeWidgetId;
    bool m_drawerOpen = true;
    // Previous player transport state — lets the playbackStateChanged
    // handler tell a fresh start (onPlay) from un-pausing (onResume).
    QMediaPlayer::PlaybackState m_prevPlaybackState =
        QMediaPlayer::StoppedState;
    // MilkDrop overlay — populated by wireMilkdrop() when the skin
    // declares an AVS slot.  Recognised forms:
    //   • Explicit `<milkdrop>` widget tag (qtamp-specific opt-in).
    //   • `<windowholder hold="guid:avs">` (canonical Wasabi, used by
    //     Winamp Modern + WinampModernPP and most modern skins).
    //   • `<windowholder hold="guid:{0000000A-000C-0010-FF7B-…}">`
    //     (the resolved AVS plugin GUID, used by Bento family).
    // Type is the base `Widget*` because the placeholder can be any
    // of the above — the embedder only needs `lastCanvasRect` and
    // `lastPaintedAtMs`, both inherited from `Widget`.  All pointers
    // are nullable (skin without an AVS slot, or build without
    // QTAMP_WITH_MILKDROP).
    const qtWasabi::Widget         *m_milkdropPlaceholder = nullptr;
#ifdef QTAMP_WITH_MILKDROP
    MilkdropItem                   *m_milkdropItem        = nullptr;
    int                             m_randomCfgSub        = 0;
#endif
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
    // The "open content" prompt (PLAY on empty / EJECT) is multi-select:
    // pick one file, many files, or a whole folder's worth via Ctrl+A — not
    // just a single track.  Returns the first chosen URL for the caller's
    // recent-files bookkeeping.
    const QList<QUrl> us = openFilesAndEnqueue(embedder);
    return us.isEmpty() ? QUrl() : us.first();
}

// QQuickWindow that hosts the QtampPlayerWindow item.  QtampHost
// reaches it via m_window->window().
inline bool QtampHost::close() {
    if (m_window && m_window->window()) m_window->window()->close();
    return m_window != nullptr;
}

inline bool QtampHost::minimize() {
    if (m_window && m_window->window()) m_window->window()->showMinimized();
    return m_window != nullptr;
}

inline bool QtampHost::maximize() {
    if (!m_window || !m_window->window()) return false;
    QQuickWindow *w = m_window->window();
    if (w->visibility() == QWindow::Maximized) w->showNormal();
    else                                       w->showMaximized();
    return true;
}

inline bool QtampHost::toggleShade() {
    // "Shade" mode collapses the player to a thin strip — Winamp's
    // titlebar middle button.  Modern skins normally swap to the
    // `<container id="main"><layout id="shade">...</layout>` layout,
    // but our window hosts only the "normal" layout, so we toggle
    // between the painted-extent-only height (drawer hidden) and a
    // 30-px-tall preview by manipulating the QQuickWindow's height
    // directly.  Falls back to no-op-true so the click is consumed.
    if (!m_window || !m_window->window()) return true;
    QQuickWindow *w = m_window->window();
    const int curH = w->height();
    const int shadeH = 30;
    if (curH > shadeH + 16) {
        m_savedHeight = curH;
        w->resize(w->width(), shadeH);
    } else if (m_savedHeight > 0) {
        w->resize(w->width(), m_savedHeight);
        m_savedHeight = 0;
    }
    return true;
}

inline bool QtampHost::showSystemMenu(QWidget *embedder) {
    // Route SYSMENU action to the same right-click context menu the
    // QtampPlayerWindow item builds.  showContextMenu is Q_INVOKABLE
    // on the QQuickItem subclass so QMetaObject::invokeMethod can
    // dispatch even though the QQuickItem isn't a QWidget.  Sub-windows
    // (EQ / Playlist / ...) still pass a QWidget embedder for parent.
    Q_UNUSED(embedder);
    if (!m_window) return true;
    const QPoint pos = QCursor::pos();
    QMetaObject::invokeMethod(m_window, "showContextMenu",
                               Qt::QueuedConnection,
                               Q_ARG(QPoint, pos));
    return true;
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

// gen_ml host installer.  Forward-declared here so we don't drag in
// the full ml/MlHostWidget.h umbrella from main.cpp.
namespace qtWasabi { namespace ml { void installMlHostFactory(); } }
// Real in-player playlist renderer for the Playlist GUID.
namespace qtWasabi { void installPleditHostFactory(); }

// --connect scheme -> transport factory.  Wasabi 2 schemes:
//   graphql+http://host/...   GraphQL over TCP (the pylon)
//   graphql+https://host/...  GraphQL over TLS/edge
//   graphql+unix:///path.sock GraphQL over a local unix socket
// plain http(s):// stays the legacy control channel during migration.
static qtWasabi::remote::RemoteTransport *makeRemoteTransport(QString &connectUrl) {
    if (connectUrl.startsWith(QLatin1String("graphql+unix://"))) {
        const QString path =
            connectUrl.mid(int(qstrlen("graphql+unix://")));
        connectUrl = QStringLiteral("http://qtwasabi.local");  // placeholder
        return new qtWasabi::remote::GraphQLLocalTransport(path);
    }
    if (connectUrl.startsWith(QLatin1String("unix://"))) {
        const QString path = connectUrl.mid(int(qstrlen("unix://")));
        connectUrl = QStringLiteral("http://qtwasabi.local");  // placeholder
        return new qtWasabi::remote::GraphQLLocalTransport(path);
    }
    // GraphQL is the only head data path; plain http(s):// means the
    // pylon's GraphQL endpoint (the graphql+ prefix stays accepted).
    if (connectUrl.startsWith(QLatin1String("graphql+")))
        connectUrl = connectUrl.mid(int(qstrlen("graphql+")));
    return new qtWasabi::remote::GraphQLHttpTransport();
}

int main(int argc, char *argv[]) {
  // Register the gen_ml-shaped MlHostRenderer as the canonical ML
  // windowholder GUID's default backing.  Skin-agnostic — replaces
  // the flat MediaLibraryPanel substitute for any modern skin
  // (Bento, Big Bento, WinampModernPP) whose XML references
  // hold="{6B0EDF80-…}".  A future ported ml.dll-equivalent can
  // supersede by registering its own factory under the same GUID
  // before any windowholder paints.
  qtWasabi::ml::installMlHostFactory();
  // Register the real playlist renderer for the Playlist component
  // GUID {45F3F7C1-…}, retiring the PlaylistPro substitute.
  qtWasabi::installPleditHostFactory();

  QString cliModernSkin  = takeModernSkinArg(argc, argv);
  QString cliClassicSkin = takeStringArg(argc, argv, "--classic-skin");
  QString screenshotPath = takeScreenshotArg(argc, argv);
  // Optional: grab a CONTAINER window (e.g. the Playlist Editor) instead of
  // the main player, so the offscreen harness can verify subwindow fidelity.
  QString screenshotContainerRef = takeStringArg(argc, argv,
                                                 "--screenshot-container");
  const bool listActions = takeFlag(argc, argv, "--list-actions");
  // --qml-renderer used to open a parallel SkinQuickItem preview
  // while QWidget was the primary path.  The QML SkinQuickItem path
  // is now primary; the flag is silently accepted for backwards
  // compatibility but is a no-op.
  (void)takeFlag(argc, argv, "--qml-renderer");
  // --serve-player <socketPath>: run the headless player (audio +
  // playlist) serving the Wasabi 2 player protocol (api/player.proto,
  // gRPC) on a unix socket, with no skin and no windows.  The
  // framework's pylon pairs with it as the GraphQL server.
  const QString servePlayerArg = takeStringArg(argc, argv, "--serve-player");
  const bool backendMode = !servePlayerArg.isEmpty();
  // --connect <url>: run the normal player UI but backed by a RemoteHost
  // synced to a networked backend at <url> (the docs/PROTOCOL.md root).
  QString connectUrl = takeStringArg(argc, argv, "--connect");
  // --probe <field>: headless connectivity check (tests). Connect a
  // RemoteHost to --connect, wait for the first snapshot, print one
  // field and exit. No skin, no window.
  const QString probeField = takeStringArg(argc, argv, "--probe");
  // --container <ref>: render this container as the window's ROOT
  // instead of "main" — e.g. `--container pl` presents just the
  // Playlist Editor.  Accepts the same refs as the TOGGLE action
  // (container id, component GUID, or the pl/ml/vid aliases).  A
  // non-main root disables subwindow toggles: one container per
  // process, which is how each browser iframe hosts a single window.
  QString rootContainerArg = takeStringArg(argc, argv, "--container");
  // --fakehost: render against the deterministic scripted host — the
  // frontend gates itself without any player (Wasabi 2 V2 harness).
  const bool fakeHostMode = takeFlag(argc, argv, "--fakehost");
#ifdef QTAMP_REMOTE_ONLY
  // The remote-only browser head is configured by its embedding page:
  //   /player/?window=player|pledit&graphql=/api/music/graphql
  // CLI args (tests) win over query params.  `graphql=` may point at
  // the pylon's GraphQL endpoint — the control channel lives at the
  // pylon root, so a trailing /graphql is stripped; a relative path
  // resolves against the page origin (the iframe's same-origin proxy).
  if (connectUrl.isEmpty()) {
      QString v = wasmQueryParam("connect");
      if (v.isEmpty()) v = wasmQueryParam("graphql");
      if (v.endsWith(QLatin1String("/graphql"))) v.chop(8);
      if (!v.isEmpty() && !v.startsWith(QLatin1String("http")))
          v = wasmQueryParam("__origin")
              + (v.startsWith(QLatin1Char('/')) ? v
                                                : QLatin1Char('/') + v);
      connectUrl = v.isEmpty() ? wasmQueryParam("__origin") : v;
      // Wasabi 2: the browser head speaks the GraphQL API.
      if (!connectUrl.startsWith(QLatin1String("graphql+")))
          connectUrl = QStringLiteral("graphql+") + connectUrl;
  }
  if (rootContainerArg.isEmpty()) {
      QString c = wasmQueryParam("container");
      if (c.isEmpty()) {
          const QString w = wasmQueryParam("window").toLower();
          if (w == QLatin1String("pledit") || w == QLatin1String("playlist")
              || w == QLatin1String("pl"))
              c = QStringLiteral("pl");
      }
      rootContainerArg = c;
  }
#endif
  if (backendMode && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
      // Headless by default: the QWidget-based playlist model still wants
      // a QPA, offscreen satisfies it without a display server.
      qputenv("QT_QPA_PLATFORM", "offscreen");
  }

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

  // Force the Qt Quick scene graph onto the OpenGL backend.  Qt 6
  // defaults to RHI with Vulkan/Metal/D3D, but the MilkDrop overlay
  // (`src/MilkdropItem`) bridges projectM into the scene graph by
  // sharing OpenGL textures with the scene graph's GL context — so
  // the scene graph MUST be running OpenGL for the share to work.
  // Setting this before QApplication ensures the choice is locked in
  // before any QQuickWindow gets constructed.
#ifdef QTAMP_WITH_MILKDROP
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif

  QApplication app(argc, argv);
  app.setApplicationName("Qtamp");
  app.setApplicationVersion("0.5 BETA");
  app.setOrganizationName("Qtamp");

  if (!probeField.isEmpty() && !connectUrl.isEmpty()) {
      // Headless probe: connect, wait for the first snapshot, print the
      // requested field, exit. Used by tests/remote/sync_test.sh.
      qtWasabi::remote::RemoteTransport *transport = makeRemoteTransport(connectUrl);
      auto *host = new qtWasabi::remote::RemoteHost(QUrl(connectUrl), transport);
      auto printAndExit = [host, probeField]() {
          QString out;
          if (probeField == QLatin1String("playing"))
              out = host->isPlaying() ? QStringLiteral("true")
                                      : QStringLiteral("false");
          else if (probeField == QLatin1String("paused"))
              out = host->isPaused() ? QStringLiteral("true")
                                     : QStringLiteral("false");
          else if (probeField == QLatin1String("playlistCount"))
              out = QString::number(host->playlistRowCount());
          else if (probeField == QLatin1String("title"))
              out = host->songTitle();
          else if (probeField == QLatin1String("volume"))
              out = QString::number(host->volume());
          printf("%s\n", out.toLocal8Bit().constData());
          QCoreApplication::exit(0);
      };
      // The snapshot arrives on the first repaint-driving signal; give a
      // short settle then print (a fixed delay keeps the probe simple and
      // is plenty on loopback).
      QTimer::singleShot(600, &app, printAndExit);
      QTimer::singleShot(5000, &app, []() { QCoreApplication::exit(2); });
      return app.exec();
  }

  if (backendMode) {
      // The whole backend: the local host (audio pipeline), the hidden
      // playlist model, and the control channel.  No skin, no windows.
      auto *host = new QtampHost();
      auto *pl = new PlaylistWindow(nullptr);
      QObject::connect(pl, &PlaylistWindow::trackDoubleClicked,
                       [host](const QString &filePath) {
                           host->openAndDecode(QUrl::fromLocalFile(filePath));
                       });
      host->setPlaylist(pl);
      QObject::connect(pl, &PlaylistWindow::changed, host,
                       &PlayerHost::notifyPlaylistChanged);

      // The music root confines open/playlistAddPaths (the pylon forwards
      // viewer input into this channel).
      QString musicRoot = qEnvironmentVariable("QTAMP_MUSIC_ROOT");
      if (musicRoot.isEmpty()) {
          musicRoot = QStandardPaths::writableLocation(
              QStandardPaths::MusicLocation);
      }
      host->setLibraryRoot(musicRoot.isEmpty() ? QDir::homePath()
                                               : musicRoot);

      // CLI positional media seeds the playlist (paths need not be under
      // the music root — the operator launched them, not a viewer).
      for (int i = 1; i < argc; ++i) {
          const QString a = QString::fromLocal8Bit(argv[i]);
          if (a.startsWith(QLatin1Char('-'))) continue;
          if (QFileInfo(a).isFile()) pl->addTrack(a);
      }

#ifdef QTAMP_HAVE_SIDECAR
      qtWasabi::serve::ServeHooks hooks;
      hooks.playlistClear = [pl]() { pl->clearPlaylist(); };
      hooks.playlistRemoveRows = [pl](const QList<int> &rows) {
          pl->removeRows(rows);
      };
      hooks.eqOn = [host]() { return host->eqEnabled(); };
      hooks.setEqOn = [host](bool on) { host->setEqEnabled(on); };
      hooks.eqAuto = [host]() { return host->eqAuto(); };
      hooks.setEqAuto = [host](bool on) { host->setEqAuto(on); };
      hooks.musicRoot = musicRoot;
      hooks.playerName = QStringLiteral("qtamp-player ") +
                         QCoreApplication::applicationVersion();

      auto *sidecar =
          new qtWasabi::serve::SidecarService(host, std::move(hooks));
      if (!sidecar->listen(servePlayerArg)) {
          fprintf(stderr, "qtamp: --serve-player: cannot bind %s\n",
                  servePlayerArg.toLocal8Bit().constData());
          return 6;
      }
      const int rc = app.exec();
      delete sidecar;
      return rc;
#else
      fprintf(stderr,
              "qtamp: --serve-player needs the grpc++ build "
              "(qtwasabi_serve target)\n");
      return 6;
#endif
  }

  // Resolve skin path + renderer kind.  CLI flags win; then saved
  // setting; then a sensible default.  Modern skins are rendered by
  // qtWasabi; classic skins by the legacy WinampWindow renderer.
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

#ifdef QTAMP_WASM
  // The browser build ships one skin, baked into the binary as a Qt
  // resource (wasm/qtamp_wasm.qrc).  Qt's resource paths work with
  // QFile/QDir, so the engine loads it exactly like a filesystem skin.
  modernSkinPath  = QStringLiteral(":/skin/QTAMP-WinampModernPP");
  classicSkinPath.clear();
#endif

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

    qtWasabi::SkinXml::Document doc;
    QString err;
    if (!qtWasabi::SkinXml::parse(skinXml, doc, &err)) {
      fprintf(stderr, "qtamp: parse failed: %s\n", err.toLocal8Bit().constData());
      return 3;
    }

    // The host factory: local (the full audio pipeline + playlist model)
    // or remote (a RemoteHost synced to a networked backend, --connect).
    // Everything below this block depends only on the PlayerHost base.
    qtWasabi::PlayerHost *host = nullptr;
    PlaylistWindow *modernPl = nullptr;
    if (fakeHostMode) {
        host = new qtWasabi::FakeHost();
    } else if (!connectUrl.isEmpty()) {
        qtWasabi::remote::RemoteTransport *transport = makeRemoteTransport(connectUrl);
        host = new qtWasabi::remote::RemoteHost(QUrl(connectUrl), transport);
        fprintf(stderr, "qtamp: remote head connected to %s\n",
                connectUrl.toLocal8Bit().constData());
    } else {
        auto *localHost = new QtampHost();
        // Reuse PlaylistWindow as the modern path's playlist data model.
        // The widget is never `show()`n — it's purely a track-data holder
        // that backs the engine-level <playlistpro> renderer through the
        // qtWasabi::Host playlist accessors.  Library root defaults to
        // the user's Music dir, again hidden behind the Host abstraction.
        modernPl = new PlaylistWindow(nullptr);
        QObject::connect(modernPl, &PlaylistWindow::trackDoubleClicked,
            [localHost](const QString &filePath) {
                localHost->openAndDecode(QUrl::fromLocalFile(filePath));
            });
        localHost->setPlaylist(modernPl);
        QObject::connect(modernPl, &PlaylistWindow::changed, localHost,
                         &PlayerHost::notifyPlaylistChanged);
        const QString musicDir = QStandardPaths::writableLocation(
            QStandardPaths::MusicLocation);
        if (!musicDir.isEmpty() && QDir(musicDir).exists())
            localHost->setLibraryRoot(musicDir);
        else
            localHost->setLibraryRoot(QDir::homePath());
        host = localHost;
    }

    // QtampPlayerWindow is a QQuickItem hosted inside a QQuickWindow
    // declared from QML.  A raw C++-constructed
    // QQuickWindow doesn't get a valid wl_surface.commit on Wayfire/
    // wlroots (the toplevel never appears in `wlrctl toplevel list`),
    // even though Qt reports the window as visible.  The QML Window
    // element handles the full xdg-shell setup correctly because it
    // goes through the QML engine's window-lifecycle path.  We let
    // QML create the Window, then attach our manually-constructed
    // QtampPlayerWindow item to its contentItem.
    auto *view = new QtampPlayerWindow(host);
    QString rootContainerId = QStringLiteral("main");
    if (!rootContainerArg.isEmpty()) {
        rootContainerId =
            qtWasabi::SkinXml::resolveContainerId(doc, rootContainerArg);
        if (rootContainerId.isEmpty()) rootContainerId = rootContainerArg;
        view->setRootContainerId(rootContainerId);
    }
    if (!view->load(doc, rootContainerId, "normal", &err)) {
      fprintf(stderr, "qtamp: layout load failed (container '%s'): %s\n",
              rootContainerId.toLocal8Bit().constData(),
              err.toLocal8Bit().constData());
      return 4;
    }
    view->setSkinDocument(doc);
    // Apply the saved / preferred colour theme so the player chrome
    // (frame borders, file-info|playlist divider) renders grey instead
    // of the flat-black "*Default" identity theme.
    view->applyPreferredColorTheme();

    // Qt.Window | Qt.CustomizeWindowHint = no OS decoration, but
    // still registers as a proper xdg-toplevel.  Qt.FramelessWindow
    // Hint on QQuickWindow + Wayfire/wlroots silently prevents the
    // surface from mapping (the toplevel never appears in
    // `wlrctl toplevel list`), even though Qt reports the window
    // as visible.  The QWidget path didn't hit this because
    // QWidget's setWindowFlags interacts with the wayland-platform
    // differently than QQuickWindow's setFlag.  CustomizeWindowHint
    // strips all the title/min/max/close hints without going
    // through that bad QQuickWindow code path.
    // Qt.Window | Qt.CustomizeWindowHint = no OS decoration, but
    // still registers as a proper xdg-toplevel.  Qt.FramelessWindow
    // Hint on QQuickWindow + Wayfire/wlroots silently prevents the
    // surface from mapping.  Start with visible: false so we can
    // setParentItem(contentItem()) on our QtampPlayerWindow BEFORE
    // the window attempts its first render — otherwise the first
    // frame has no content and Wayfire never sees a buffer attach.
    QQmlApplicationEngine engine;
    // Frameless — Bento paints its own titlebar via standardframe's
    // `<wasabi.frame.layout>` groupdef (WINAMP wordmark + File/Play/
    // Options/View/Help menu + restore/maximize/close buttons).
    // The reference Bento screenshot shows both X11 chrome AND the
    // skin's own internal titlebar stacked, but on Linux/Wayland we
    // pick frameless because the OS chrome would overlap the skin's
    // rendered top region and most users prefer the pure-skin look.
    const QByteArray windowQml =
        "import QtQuick\n"
        "import QtQuick.Window\n"
        "Window {\n"
        "    flags: Qt.Window | Qt.FramelessWindowHint\n"
        "    color: 'transparent'\n"
        "    width: 354; height: 280\n"
        "    visible: true\n"
        "}\n";
    engine.loadData(windowQml);
    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "qtamp: failed to instantiate QML Window\n");
        return 4;
    }
    auto *qwin = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!qwin) {
        fprintf(stderr, "qtamp: QML root is not a QQuickWindow\n");
        return 4;
    }
    view->setParentItem(qwin->contentItem());
    // displaySize applies the render ratio (basewnd::setRenderRatio):
    // XML-unit layout scaled by the user's chosen factor.
    view->setSize(QSizeF(view->displaySize()));
    qwin->resize(view->displaySize());
    // Force the QQuickWindow's wl_surface to use an alpha buffer.
    // Wayfire/Asahi composites alpha=0 pixels correctly ONLY when
    // the surface was created with an ARGB visual; the default
    // RGB(no-alpha) path renders cleared pixels as opaque white.
    // QSurfaceFormat::setDefaultFormat in main() sets the default
    // but QQuickWindow ignores defaults if it's created with
    // QQmlApplicationEngine — so set it explicitly here.
    {
        QSurfaceFormat fmt = qwin->format();
        fmt.setAlphaBufferSize(8);
        qwin->setFormat(fmt);
    }
    qwin->setColor(QColor(0, 0, 0, 0));
    if (auto *localHost = dynamic_cast<PlayerHost *>(host))
        localHost->bindWindow(view);

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
        auto &mutableTree = const_cast<qtWasabi::Layout::ResolvedWidget &>(
            view->tree());
        if (!::getenv("WASABIQT_NO_STATIC_SCRIPTS")) {
            const int layoutW = view->layoutNativeSize().width();
            qtWasabi::Layout::runKnownScripts(mutableTree, layoutW);
            // Engine-level stepper wiring (Decrease/Increase
            // buttons + Display text → sibling cfgattrib slider).
            qtWasabi::Layout::wireSteppers(mutableTree);
            // The Maki VM drives the playlist enlarge itself (pledit.m's
            // playlist_enlarge_attrib + g_playlist.onResize +
            // playlistpro.frameGroup.onResize), settled to a fixpoint by
            // SkinRuntime::dispatchInitialResize.  See the
            // dispatchInitialResize call below.
            if (::getenv("WASABIQT_TRACE_LAYOUTROOT")) {
                qtWasabi::Layout::dumpResolved(
                    mutableTree, view->layoutNativeSize());
            }
            // The static path mutates widget positions (drawer y,
            // titlebar streaks, …); recompute the window region so
            // sysregion cutouts land where the chrome actually
            // paints, not where the original XML put it.
            view->rebuildWindowRegion();
            // (Skin-specific widget force-visibility used to live
            // here.  Both pieces — drawer.button.open hide and
            // posbarbg visible — are now driven by their respective
            // Maki scripts: configtabs.maki via DrawerOpen=1 default,
            // setposbarvisibility.maki via the wired getStatus()
            // callback to QMediaPlayer state.)
        }

        // Fire the actual Maki scripts.  Errors are non-fatal — if
        // a binding is missing the runtime logs a guru and moves
        // on; the static-fallback chrome is still visible.
        // Set WASABIQT_NO_RUNTIME=1 to skip Maki dispatch entirely
        // (useful for visual diffs that should reflect ONLY the
        // static well-known-script path).
        if (!::getenv("WASABIQT_NO_RUNTIME")) {
            static qtWasabi::SkinRuntime runtime;
            view->setSkinRuntime(&runtime);
            // Maki GuiObject.leftClick()/rightClick() action fallback: run the
            // delegated widget's action through this window's real dispatch.
            qtWasabi::registerSkinWidgetClickCallback(
                [view](const QString &id, bool right) -> bool {
                    return view->triggerWidgetActionById(id, right);
                });
            // Maki System.showWindow / hideNamedWindow / isNamedWindowVisible
            // → the same subwindow machinery the TOGGLE action uses (the
            // vis/video drawer DETACH buttons in Winamp Modern route here).
            qtWasabi::registerNamedWindowCallback(
                [view](const QString &ref, int op) -> int {
                    int result = 0;
                    if (op == 1) {                       // show
                        if (qtWasabi::SkinView *sv = view->ensureSubwindow(ref)) {
                            sv->show();
                            sv->raise();
                            result = 1;
                        }
                    } else {
                        qtWasabi::SkinView *sv = view->peekSubwindow(ref);
                        if (op == 0 && sv) sv->hide();   // hide
                        if (sv && sv->isVisible()) {
                            result = 1;
                        } else if (op == 2) {
                            // A DOCKED component (the vis/video hosted in
                            // the player's drawer holder) counts as visible
                            // — real Wasabi semantics; the drawer scripts'
                            // detach flow gates on isNamedWindowVisible
                            // before showWindow.
                            const qint64 last =
                                qtWasabi::holderLastPaintedMs(ref);
                            if (last > 0 &&
                                QDateTime::currentMSecsSinceEpoch() - last
                                    < 400)
                                result = 1;
                        }
                    }
                    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                        fprintf(stderr, "[namedwindow] op=%d ref=%s -> %d\n",
                                op, ref.toLocal8Bit().constData(), result);
                    return result;
                });
            // Maki System.setEqBand/getEqBand → host EQ store (drives the EQ
            // sliders + audio), so a skin's EQ reset / +/- buttons work.
            qtWasabi::registerSkinEqCallbacks(
                [h = host, view](int band, int val) {
                    h->setEqBandValue(band, val);
                    if (view) view->update();
                },
                [h = host](int band) -> int { return h->eqBandValue(band); });
            // Maki Slider.setPosition/getPosition → host slider axis (drives
            // scripted balance/volume buttons), keyed by the slider's action=.
            qtWasabi::registerSkinSliderCallbacks(
                [h = host, view](const QString &action, const QString &param,
                                 int v255) {
                    h->setSliderPosition(
                        action, qBound(0.0, double(v255) / 255.0, 1.0), param);
                    if (view) view->update();
                },
                [h = host](const QString &action,
                           const QString &param) -> int {
                    const double p = h->sliderPosition(action, param);
                    return p < 0.0 ? -1 : qRound(p * 255.0);
                });
            // Maki System.setVolume/getVolume (0..255) → host volume (0..100).
            qtWasabi::registerSkinVolumeCallbacks(
                [h = host, view](int v255) {
                    h->setVolume(qRound(double(v255) / 255.0 * 100.0));
                    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_MAKI") == 1)
                        fprintf(stderr, "[volume] System.setVolume(%d) -> %d%%\n",
                                v255, h->volume());
                    if (view) view->update();
                },
                [h = host]() -> int {
                    return qRound(double(h->volume()) / 100.0 * 255.0);
                });
            // Route Maki getStatus() through to the live QMediaPlayer
            // state so playback-state-driven scripts (setposbarvisibility.
            // maki, classicplaystatus.maki, drawer.m) see the real
            // host status instead of a hardcoded default.
            qtWasabi::registerSkinPlaybackStatusCallback(
                [h = host]() -> int {
                    if (h->isPlaying()) return 1;
                    if (h->isPaused())  return -1;
                    return 0;
                });
            // Maki Layout.setTarget* → gotoTarget chain resizes the
            // window.  Used by drawer.m's openDrawer/closeDrawer for
            // the upper video/vis drawer — without this the drawer
            // becomes visible inside the original-sized window and
            // pushes the chrome off-screen.
            // Skin scripts fire startup resize callbacks during their
            // onScriptLoaded handlers — Bento's maximize.m calls
            // setWndToScreen() on first launch, which resizes the
            // window to fill the user's display.  That makes the
            // chrome (fixed-position widgets) look tiny inside a
            // huge window and doesn't match what the reference
            // screenshots show (Bento's native 800x600 size).
            // Block resize callbacks for the first ~750 ms after
            // skin load — covers the entire onScriptLoaded /
            // dispatchInitialResize pass.  User-initiated resize
            // events (maximize button click, etc.) fire later and
            // pass through normally.
            auto resizeUnblockAt =
                std::make_shared<qint64>(
                    QDateTime::currentMSecsSinceEpoch() + 750);
            qtWasabi::registerSkinResizeCallback(
                [view, resizeUnblockAt](int w, int h) {
                    if (QDateTime::currentMSecsSinceEpoch() <
                        *resizeUnblockAt) {
                        return;  // startup resize — ignore
                    }
                    // Maximize: the skin's resize() (e.g. simplemaximize.maki)
                    // targets ~the screen work-area.  On Wayland a client
                    // can't position itself, so a plain resize grows the
                    // window but leaves it off-origin (bottom-right runs off
                    // screen).  Hand the maximize to the COMPOSITOR, which
                    // fills AND positions it.  General: any skin whose
                    // maximize script resizes to the viewport hits this.
                    if (QQuickWindow *win = view->window()) {
                        const QRect avail =
                            QGuiApplication::primaryScreen()
                                ? QGuiApplication::primaryScreen()
                                      ->availableGeometry()
                                : QRect();
                        if (avail.isValid() && w >= avail.width() - 8 &&
                            h >= avail.height() - 8) {
                            if (win->visibility() != QWindow::Maximized)
                                win->showMaximized();
                            return;
                        }
                        // Restore: leave maximized state before applying the
                        // smaller size, else the compositor keeps the
                        // maximized geometry and ignores the resize.
                        if (win->visibility() == QWindow::Maximized)
                            win->showNormal();
                    }
                    // WASABIQT_NO_ANIM=1 forces the snap path (handy
                    // for offscreen test pipelines that grab a single
                    // frame and don't want to wait out a tween).
                    if (::getenv("WASABIQT_NO_ANIM"))
                        view->resizeLayoutTo(QSize(w, h));
                    else
                        // 350 ms matches the widget-level setTarget
                        // tween (configtabs.m's drawer slide) so the
                        // config drawer + the video/vis drawer feel
                        // consistent.
                        view->animatedResizeLayoutTo(QSize(w, h), 350);
                });
            // Plug the bitmap registry into the Maki bridge so layer
            // widgets' `getAutoWidth/Height` can resolve their bound
            // bitmaps' intrinsic dimensions (mainmenu.maki et al.).
            runtime.setBitmapRegistry(&view->registry());
            runtime.loadScripts(doc, mutableTree);
            // Cache effective resolved rects so Maki getWidth/getHeight
            // report real pixels for relat-sized widgets (logo holder).
            mutableTree.cacheResolvedRects(QPoint(0, 0),
                                           view->layoutNativeSize());
            runtime.dispatchOnScriptLoaded();
            runtime.dispatchXuiParams(mutableTree);
            // Fire the initial onResize on every script that bound a
            // handler.  configtabs.m's `main.onResize(x, y, w, h) { ...
            // DrawerContent.setXmlParam("x", w/2 - 163); }` now
            // correctly evaluates to 14 inside the Maki VM (was 0 prior
            // to the SOM::makeDouble type-aware fix), so the static
            // drawer.content hardcode in Layout.cpp is dropped and
            // centring runs through real Maki dispatch.
            // WASABIQT_NO_FIRE_RESIZE=1 skips this for the offscreen
            // visual baselines that want the pre-resize chrome.
            if (!::getenv("WASABIQT_NO_FIRE_RESIZE")) {
                const QSize ls = view->layoutNativeSize();
                runtime.dispatchInitialResize(ls.width(), ls.height());
            }
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
        const auto *hit = qtWasabi::Layout::hitTest(
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
    // Reference Bento titlebar says "Winamp" (window-attribute, not
    // skin chrome).  Keep it short so X11/Wayfire's titlebar text
    // doesn't wrap.
    qwin->setTitle(QStringLiteral("Winamp"));
    qwin->resize(view->displaySize());
    // Auto-shrink to painted-region extent after Maki mutations.
    // Off by default in qtWasabi so other embedders preserve explicit
    // Maki sizing; qtamp opts in.
    view->setAutoShrinkToRegion(true);
    // (visible: true is set from QML; window is already mapped.)

    // Resizeable window: when the OS resizes the toplevel, re-resolve the
    // skin's relat-sized layout to the new size so the chrome scales to
    // fill it, and re-run the layout passes (album-art square, tabs,
    // search-header offset).  Guarded against re-entry; resizeLayoutTo
    // resizes the window to the same size it already is, so no feedback.
    // This also re-resolves the tree after a Maki layout switch resizes
    // the window (the shade-mode toggle) — without it the shade layout
    // paints against stale rects (a memory-access-out-of-bounds on WASM).
    {
        auto relayout = [view]() {
            static bool busy = false;
            auto *w = view->window();
            if (busy || !w) return;
            // The window is in display units; the layout speaks XML
            // units.  Convert through the render ratio, otherwise a
            // ratio != 1 feeds the scaled size back into the layout
            // and halves (or doubles) the window on every pass.
            const double rr = view->renderRatio();
            const QSize wpx(w->width(), w->height());
            if (wpx.width() <= 0 || wpx.height() <= 0 ||
                wpx == view->displaySize())
                return;
            const QSize ws(int(wpx.width()  / rr + 0.5),
                           int(wpx.height() / rr + 0.5));
            if (ws == view->layoutNativeSize()) return;
            busy = true;
            // Free resize must not snap back to the painted extent.
            view->setAutoShrinkToRegion(false);
            view->resizeLayoutTo(ws);
            auto &t = const_cast<qtWasabi::Layout::ResolvedWidget &>(view->tree());
            // Resolve to the new size, then re-fire the Maki onResize
            // cascade (faithful Wasabi: a window resize fires onResize, which
            // re-runs pledit/playlistpro to maintain the enlarged column +
            // reflow the search header / button bar).
            t.cacheResolvedRects(QPoint(0, 0), ws);
            if (auto *rt = view->skinRuntime())
                rt->dispatchInitialResize(ws.width(), ws.height());
            view->rebuildWindowRegion();
            view->update();
            busy = false;
        };
        QObject::connect(qwin, &QQuickWindow::widthChanged,  view,
                         [relayout](int) { relayout(); });
        QObject::connect(qwin, &QQuickWindow::heightChanged, view,
                         [relayout](int) { relayout(); });
    }

#ifdef QTAMP_WASM
    // Qt for WebAssembly expands the frameless window to fill its
    // browser container, so the skin item (its native ~354x280) sits at
    // the window's top-left.  Centre the ITEM within the window (the
    // window itself already fills the container, so moving the window
    // does nothing), and re-centre when the container resizes (a phone
    // rotating, the modal reflowing) or the item settles its size.
    {
        auto centerItem = [view, qwin]() {
            const qreal iw = view->width(),  ih = view->height();
            const qreal ww = qwin->width(),  wh = qwin->height();
            if (iw <= 0 || ih <= 0 || ww <= 0 || wh <= 0) return;
            view->setPosition(QPointF(qMax(0.0, (ww - iw) / 2.0),
                                      qMax(0.0, (wh - ih) / 2.0)));
        };
        centerItem();
        QObject::connect(qwin, &QQuickWindow::widthChanged,  view,
                         [centerItem](int) { centerItem(); });
        QObject::connect(qwin, &QQuickWindow::heightChanged, view,
                         [centerItem](int) { centerItem(); });
        QObject::connect(view, &QQuickItem::widthChanged,  view,
                         [centerItem]() { centerItem(); });
        QObject::connect(view, &QQuickItem::heightChanged, view,
                         [centerItem]() { centerItem(); });
    }
#endif

    // Hot-reload: opt-in via WASABIQT_HOT_RELOAD=1.  Watches every
    // XML/Maki source under the skin directory and triggers a full
    // reload (Maki VM reset + script reload + chrome repaint) after a
    // 250 ms debounce.  Off by default so offscreen tests and prod
    // sessions don't pay for inotify watchers + extra reload surface.
    if (::getenv("WASABIQT_HOT_RELOAD")) {
        const QString xml = QFileInfo(modernSkinPath).isDir()
            ? QDir(modernSkinPath).filePath("skin.xml")
            : modernSkinPath;
        view->installHotReloadWatcher(xml);
        fprintf(stderr,
            "[hot-reload] watching %s\n",
            QFileInfo(xml).absolutePath().toLocal8Bit().constData());
    }

    // Headless chrome self-test: WASABIQT_SELFTEST_CHROME=<themeName>
    // verifies the menu-bar prev/next ring and the menu/dialog re-tint,
    // then quits.  Runs without --screenshot (the chrome is Qt widgets,
    // not part of the offscreen skin grab).
    if (const char *st = ::getenv("WASABIQT_SELFTEST_CHROME")) {
      const QString theme = QString::fromLocal8Bit(st);
      QTimer::singleShot(400, view, [view, theme]() {
        view->runChromeSelfTest(theme);
        QCoreApplication::quit();
      });
    }

    // Load positional file/folder args (and --play-file) into modernPl —
    // the data model the on-skin <playlistpro>/Pledit reads.  This MUST run
    // in BOTH the live app and the --screenshot path; gating it on the
    // screenshot branch left `qtamp --modern-skin … <folder>` with an empty
    // playlist in the live window.  Folders expand to their audio files in
    // album order (track/disc tags).
    QStringList modernCliFiles;
    {
      const QStringList cliArgs = QCoreApplication::arguments();
      for (int i = 1; i < cliArgs.size(); ++i) {
        const QString a = cliArgs.at(i);
        if (a == QLatin1String("--play-file") && i + 1 < cliArgs.size()) {
          modernCliFiles << cliArgs.at(++i);
          continue;
        }
        if (a.startsWith(QLatin1Char('-'))) continue;
        QFileInfo fi(a);
        if (fi.isDir()) {
          const QStringList exts = {"*.mp3","*.wav","*.ogg","*.flac",
                                    "*.m4a","*.aac","*.wma","*.opus"};
          QStringList dirFiles;
          for (const QFileInfo &e :
               QDir(a).entryInfoList(exts, QDir::Files, QDir::Name))
            dirFiles << e.absoluteFilePath();
          audiometa::sortByTrack(dirFiles);
          modernCliFiles << dirFiles;
        } else if (fi.exists()) {
          modernCliFiles << fi.absoluteFilePath();
        }
      }
#if defined(QTAMP_WASM) && !defined(QTAMP_REMOTE_ONLY)
      // Queue the bundled demo loop like a CLI file argument; the user's
      // Play click inside the skin doubles as the browser's audio gesture.
      // The remote head bundles no track — audio lives on the backend.
      if (modernCliFiles.isEmpty())
          modernCliFiles << QStringLiteral(":/demo.wav");
#endif
    }
    // A remote head has no local playlist model or file access; the
    // backend owns the playlist, so CLI media is ignored (with a note).
    if (!modernPl && (!modernCliFiles.isEmpty() ||
                      ::getenv("WASABIQT_PLAY_FILE"))) {
      fprintf(stderr,
              "qtamp: --connect: ignoring local media args (the backend "
              "owns the playlist)\n");
    }
    const bool modernHasCliMedia =
        modernPl && (!modernCliFiles.isEmpty() || ::getenv("WASABIQT_PLAY_FILE"));
    if (const char *f = modernPl ? ::getenv("WASABIQT_PLAY_FILE") : nullptr) {
      QTimer::singleShot(0, view, [host, modernPl, f]() {
        const QString path = QString::fromLocal8Bit(f);
        modernPl->addTrack(path);
        modernPl->setCurrentTrackIndex(0);
        host->openPath(QUrl::fromLocalFile(path));
      });
    } else if (modernPl && !modernCliFiles.isEmpty()) {
      QTimer::singleShot(0, view, [host, modernPl, modernCliFiles]() {
        for (const QString &f : modernCliFiles) modernPl->addTrack(f);
        modernPl->setCurrentTrackIndex(0);
        host->openPath(QUrl::fromLocalFile(modernCliFiles.first()));
      });
    }

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
      // Engine-tab override (Bento family): write a value directly
      // to the __tab:active CfgAttribStore key so the visual
      // harness can screenshot every tab page without simulating
      // clicks on the (often-invisible-without-Maki) Bento:TabButton
      // XUI instances.  Index matches the suffix-sorted order
      // produced by Layout::wireTabs.
      if (const char *t = ::getenv("WASABIQT_FORCE_ACTIVE_TAB")) {
        const int tn = QByteArray(t).toInt();
        QTimer::singleShot(50, view, [tn]() {
            qtWasabi::CfgAttribStore::instance().set(
                QStringLiteral("__tab:active"), tn);
        });
      }
      // Debug knob: WASABIQT_FORCE_ATTR="id1:k1=v1;id2:k2=v2;..." —
      // semicolon-separated triples that force setXmlParam on the
      // named widget before screenshot.  Lets the visual harness
      // probe state branches the Maki scripts gate (e.g. force the
      // video window holder visible to verify the album-art slot
      // renders without driving the switchto button).
      if (const char *spec = ::getenv("WASABIQT_FORCE_ATTR")) {
        const QString s = QString::fromLocal8Bit(spec);
        QTimer::singleShot(150, view, [s]() {
            for (const QString &t : s.split(QChar(';'), Qt::SkipEmptyParts)) {
                const int colon = t.indexOf(QChar(':'));
                const int eq    = t.indexOf(QChar('='), colon);
                if (colon < 0 || eq < 0) continue;
                const QString id  = t.left(colon);
                const QString k   = t.mid(colon + 1, eq - colon - 1);
                const QString v   = t.mid(eq + 1);
                if (auto *w = qtWasabi::Widget::findById(id))
                    w->setXmlParam(k, v);
            }
        });
      }
      if (::getenv("WASABIQT_DRAWER_CLOSED")) {
        QTimer::singleShot(0, view,
          [view]() { view->setDrawerOpen(false); });
      }
      // Switch the colour theme AFTER the skin has loaded + painted, to
      // verify a live gammaset change re-tints the already-rendered skin
      // (the Preferences picker path), not just the load-time tint.
      if (const char *lt = ::getenv("WASABIQT_LIVE_THEME")) {
        const QString th = QString::fromLocal8Bit(lt);
        QTimer::singleShot(180, view,
          [view, th]() { view->setActiveGammaset(th); });
      }
      // CLI media (positional files/folder, --play-file) is queued into
      // modernPl unconditionally above; give the grab time to let it load
      // and the chrome reach "real" host state (time digits, songticker).
      int screenshotDelayMs = modernHasCliMedia ? 2500 : 250;
      // Test hook: force a specific colour-theme (gammaset) to probe how
      // the player frame bevels/dividers render under a boost=1 grey theme
      // vs the auto-selected *Default (boost=0, near-black borders).
      if (const char *gs = ::getenv("WASABIQT_GAMMASET")) {
          const QString name = QString::fromLocal8Bit(gs);
          QTimer::singleShot(400, view, [view, name]() {
              fprintf(stderr, "qtamp: setActiveGammaset -> %s\n",
                      name.toLocal8Bit().constData());
              view->setActiveGammaset(name);
          });
          screenshotDelayMs = qMax(screenshotDelayMs, 900);
      }
      // Test hook: switch skins at runtime to reproduce reloadSkin bugs.
      // Semicolon-separated list switches sequentially (1.2s apart) — a
      // multi-switch is what arms teardown-order bugs: the first switch's
      // deleteLater'd subwindows destruct between switches and the second
      // switch dispatches into whatever state they left behind.
      if (const char *sw = ::getenv("WASABIQT_SWITCH_TO")) {
          const QStringList xmls =
              QString::fromLocal8Bit(sw).split(';', Qt::SkipEmptyParts);
          int at = 800;
          for (const QString &xml : xmls) {
              QTimer::singleShot(at, view, [view, xml]() {
                  fprintf(stderr, "qtamp: reloadSkin -> %s\n",
                          xml.toLocal8Bit().constData());
                  view->reloadSkin(xml);
                  fprintf(stderr, "qtamp: reloadSkin returned OK\n");
              });
              at += 1200;
          }
          screenshotDelayMs = qMax(screenshotDelayMs, at + 1200);
      }
      // Test hook: synthesise hover moves (use with WASABIQT_TRACE_HOVER
      // to see which widget the hover hit-test resolves to).
      if (const char *hv = ::getenv("WASABIQT_HOVER_AT")) {
          const QString s = QString::fromLocal8Bit(hv);
          int delay = 60;
          for (const QString &pt : s.split(';', Qt::SkipEmptyParts)) {
              const QStringList xy = pt.split(',');
              if (xy.size() != 2) continue;
              const QPointF pos(xy[0].toInt(), xy[1].toInt());
              QTimer::singleShot(delay, view, [view, pos]() {
                  QHoverEvent he(QEvent::HoverMove, pos, pos, pos);
                  QCoreApplication::sendEvent(view, &he);
                  view->update();
              });
              delay += 150;
          }
          screenshotDelayMs = qMax(screenshotDelayMs, delay + 250);
      }
      // Optional: fire onLeftClick on a widget id before the
      // screenshot.  Lets the visual-test pipeline exercise script-
      // defined button handlers (drawer toggles, mute, etc.)
      // without an actual click event.  Comma-separated lets us
      // chain multiple actions.
      // Test hook: WASABIQT_TEST_SWITCH_SKIN=<skin.xml or skin dir> —
      // switch the skin in-app shortly after boot (the Preferences-picker
      // path), BEFORE any WASABIQT_CLICK_AT fires.  Reproduces live
      // switch-then-interact flows offscreen (e.g. the HeadAMP drawer
      // arrows after switching away from Winamp Modern).
      if (const char *sw = ::getenv("WASABIQT_TEST_SWITCH_SKIN")) {
        // Comma-separated list switches in sequence (700ms apart) — the
        // Preferences picker flow, where repeated switches historically
        // crash (stale per-root globals).
        const QStringList paths =
            QString::fromLocal8Bit(sw).split(',', Qt::SkipEmptyParts);
        int at = 400;
        for (const QString &raw : paths) {
          QString p = raw.trimmed();
          if (QFileInfo(p).isDir()) p += QStringLiteral("/skin.xml");
          QTimer::singleShot(at, view, [view, p]() { view->reloadSkin(p); });
          at += 700;
        }
      }
      // Test hook: WASABIQT_TEST_TIMEMODE=1|2 — run the context-menu /
      // Options time-display action offscreen (the QMenu::exec path
      // itself needs real input).  Fires after the load settle so the
      // skin scripts are bound, exactly like a user pick would.
      if (const char *tm = ::getenv("WASABIQT_TEST_TIMEMODE")) {
        const int mode = QString::fromLocal8Bit(tm).toInt();
        QTimer::singleShot(2500, view, [view, mode]() {
            fprintf(stderr, "qtamp: test time-display mode -> %d\n", mode);
            view->setTimeDisplayMode(mode);
        });
        screenshotDelayMs = qMax(screenshotDelayMs, 2500 + 400);
      }
      // WASABIQT_CLICK_AT="x,y;x,y" — synthesise full Qt QMouseEvent
      // press+release at the given coords so the click path runs
      // through mousePressEvent (hit-test, fireWidgetEvent, drag
      // fallthrough).  Lets us verify the full click pipeline in
      // offscreen tests instead of bypassing it with WASABIQT_FIRE_CLICK.
      // An "R" prefix on a point ("R60,70") sends a RIGHT click, so the
      // script-first right-button dispatch (onRightButtonDown/Up +
      // PopupMenu, with WASABIQT_TEST_MENU_PICK selecting the item) is
      // testable offscreen too.
      if (const char *c = ::getenv("WASABIQT_CLICK_AT")) {
        const QString s = QString::fromLocal8Bit(c);
        const QStringList pts = s.split(';', Qt::SkipEmptyParts);
        // First synth click is delayed past the skin's load-time settle +
        // debounce timers (e.g. suicore's 100ms tempDisable that gates
        // switchToX), so a tab/tab-drawer click isn't swallowed as a
        // load-debounce no-op the way a real (seconds-later) click never is.
        // Override with WASABIQT_CLICK_DELAY.
        int delay = qEnvironmentVariableIntValue("WASABIQT_CLICK_DELAY");
        if (delay <= 0) delay = 600;
        for (const QString &pt : pts) {
            QString spec = pt.trimmed();
            Qt::MouseButton btn = Qt::LeftButton;
            if (spec.startsWith(QLatin1Char('R'), Qt::CaseInsensitive)) {
                btn = Qt::RightButton;
                spec.remove(0, 1);
            }
            const QStringList xy = spec.split(',');
            if (xy.size() != 2) continue;
            const int px = xy[0].toInt();
            const int py = xy[1].toInt();
            QTimer::singleShot(delay, view, [view, px, py, btn]() {
                fprintf(stderr, "qtamp: synth %s click at (%d,%d)\n",
                        btn == Qt::RightButton ? "right" : "left", px, py);
                const QPointF pos(px, py);
                // Offscreen QQuickItems don't receive synthesised QMouseEvents
                // through the platform layer, so drive a full press+release
                // through the REAL handlers (the exact path a live click
                // takes — capture check, dispatchClickAt, drag fallthrough).
                view->testClick(pos, btn);
                view->update();
            });
            delay += 200;
        }
        screenshotDelayMs = qMax(screenshotDelayMs, delay + 250);
      }
      auto fireMouseClickAtForClick = [view](QPointF pos) {
          QMouseEvent down(QEvent::MouseButtonPress, pos, pos,
              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(view, &down);
          QMouseEvent up(QEvent::MouseButtonRelease, pos, pos,
              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
          QCoreApplication::sendEvent(view, &up);
          view->update();
      };
if (const char *c = ::getenv("WASABIQT_FIRE_CLICK")) {
        const QString s = QString::fromLocal8Bit(c);
        const QStringList specs = s.split(QChar('|'), Qt::SkipEmptyParts);
        // Spec is either a widget id (existing behaviour) or "x,y"
        // canvas coords (NEW — exercises the full mousePressEvent
        // pipeline including hit-test).  Multiple specs separated
        // by '|' fire in sequence with 200ms between, so the prior
        // state mutation gets a paint pass before the next click.
        int delay = 50;
        for (const QString &spec : specs) {
            QTimer::singleShot(delay, view, [view, spec, fireMouseClickAtForClick]() {
                if (spec.contains(QChar(','))) {
                    const auto parts = spec.split(QChar(','));
                    if (parts.size() == 2) {
                        const QPointF pt(parts[0].toInt(), parts[1].toInt());
                        qInfo().noquote()
                            << "qtamp: firing real click at" << pt;
                        fireMouseClickAtForClick(pt);
                        return;
                    }
                }
                qInfo().noquote()
                    << "qtamp: firing onLeftClick on" << spec;
                if (auto *w = qtWasabi::Widget::findById(spec)) {
                    qtWasabi::PaintCtx ctx{};
                    w->onLeftButtonDown(QPoint(0, 0), ctx);
                    w->onLeftButtonUp  (QPoint(0, 0), ctx);
                }
                // Same drawer-mode fixup the real mousePressEvent
                // path applies, so the offscreen pipeline exercises
                // the full visible flip when WASABIQT_FIRE_CLICK is
                // used to click a switchto button by id.
                view->applyDrawerModeFixup(spec);
                qtWasabi::fireWidgetEvent(spec, L"onLeftClick");
                view->update();
            });
            delay += 200;
        }
        screenshotDelayMs = qMax(screenshotDelayMs, delay + 250);
      }
      // WASABIQT_FIRE_HOVER=<id> or =x,y: synthesize a Qt hover event
      // at the named widget's bbox center (or the literal x,y coords)
      // so the resulting screenshot exercises the FULL hover pipeline
      // (Qt hoverMoveEvent -> SkinQuickItem::hoverMoveEvent ->
      // topmostWidgetAt -> Widget::onMouseMove).  Use this to verify
      // the real interactive path offscreen, not just the
      // widget-virtual fast path.
      auto fireHoverAt = [view](QPointF pos) {
          QHoverEvent ev(QEvent::HoverMove, pos, pos, QPointF(-1, -1));
          QCoreApplication::sendEvent(view, &ev);
          view->update();
      };
      auto fireMousePressAt = [view](QPointF pos) {
          QMouseEvent ev(QEvent::MouseButtonPress, pos, pos,
              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(view, &ev);
          view->update();
      };
      auto fireMouseClickAt = [view](QPointF pos) {
          QMouseEvent down(QEvent::MouseButtonPress, pos, pos,
              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
          QCoreApplication::sendEvent(view, &down);
          QMouseEvent up(QEvent::MouseButtonRelease, pos, pos,
              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
          QCoreApplication::sendEvent(view, &up);
          view->update();
      };
      auto resolveTarget = [view](const QString &spec) -> QPointF {
          // "x,y" → literal coords; otherwise treat as widget id and
          // look up bbox center via Layout::hitTest's collect mode.
          if (spec.contains(',')) {
              const auto parts = spec.split(',');
              if (parts.size() == 2)
                  return QPointF(parts[0].toInt(), parts[1].toInt());
          }
          auto *w = qtWasabi::Widget::findById(spec);
          if (!w) return QPointF(-1, -1);
          // Find absolute bbox by re-walking the tree in collect mode.
          // Easier: use the topmostWidgetAt by stepping through the
          // canvas — but cheaper: ask the widget for its rect via
          // resolveRect (parent-local) and then walk parents.  Cheapest
          // of all: scan a small grid for the widget id.
          const QSize sz = view->size().toSize();
          for (int y = 0; y < sz.height(); y += 2) {
              for (int x = 0; x < sz.width(); x += 2) {
                  if (view->topmostWidgetAt(QPoint(x, y), false) == w)
                      return QPointF(x, y);
              }
          }
          return QPointF(-1, -1);
      };
      if (const char *c = ::getenv("WASABIQT_FIRE_HOVER")) {
        const QString spec = QString::fromLocal8Bit(c);
        QTimer::singleShot(150, view, [view, spec, fireHoverAt, resolveTarget]() {
            const QPointF pos = resolveTarget(spec);
            fprintf(stderr, "qtamp: FIRE_HOVER spec=%s -> pos=(%g,%g)\n",
                spec.toLocal8Bit().constData(), pos.x(), pos.y());
            if (pos.x() >= 0) fireHoverAt(pos);
        });
        screenshotDelayMs = qMax(screenshotDelayMs, 800);
      }
      if (const char *c = ::getenv("WASABIQT_FIRE_PRESS_HOLD")) {
        const QString spec = QString::fromLocal8Bit(c);
        QTimer::singleShot(150, view, [view, spec, fireMousePressAt, resolveTarget]() {
            const QPointF pos = resolveTarget(spec);
            fprintf(stderr, "qtamp: FIRE_PRESS_HOLD spec=%s -> pos=(%g,%g)\n",
                spec.toLocal8Bit().constData(), pos.x(), pos.y());
            if (pos.x() >= 0) fireMousePressAt(pos);
        });
        screenshotDelayMs = qMax(screenshotDelayMs, 800);
      }
      QTimer::singleShot(screenshotDelayMs, view,
                         [view, qwin, screenshotPath, screenshotContainerRef]() {
        // QQuickWindow::grabWindow returns the rendered QImage from
        // the scene graph; equivalent of QWidget::grab() for the new
        // QML render path.
        Q_UNUSED(view);
        // Container capture: build the named subwindow (without showing it)
        // and grab it via QWidget::grab() — SkinView::paintEvent is
        // self-contained software compositing, so it renders offscreen with
        // no compositor.
        if (!screenshotContainerRef.isEmpty()) {
            qtWasabi::SkinView *sv = view->ensureSubwindow(screenshotContainerRef);
            if (!sv) { QCoreApplication::exit(5); return; }
            sv->resize(sv->layoutNativeSize());
            const QImage shot = sv->grab().toImage();
            if (shot.save(screenshotPath)) {
                qInfo() << "qtamp: wrote" << screenshotPath
                        << "(" << shot.width() << "x" << shot.height() << ")";
                QCoreApplication::exit(0);
            } else {
                QCoreApplication::exit(5);
            }
            return;
        }
        // Test hook: WASABIQT_TEST_SUBWIN_RESIZE="<container>:WxH" — build
        // the named subwindow and resize it BEFORE the main grab, to verify
        // a subwindow resize cannot reflow the player (per-root scoping of
        // the Maki onResize cascade).
        if (const QByteArray sr = qgetenv("WASABIQT_TEST_SUBWIN_RESIZE");
            !sr.isEmpty()) {
          const int colon = sr.indexOf(':');
          if (colon > 0) {
            if (qtWasabi::SkinView *sv = view->ensureSubwindow(
                    QString::fromLatin1(sr.left(colon)))) {
              const QByteArray spec = sr.mid(colon + 1);
              const auto pump = [](int ms) {
                QEventLoop el;
                QTimer::singleShot(ms, &el, &QEventLoop::quit);
                el.exec();
              };
              if (spec == "show") {
                // Mimic the live "open the PL window" flow (TOGGLE guid:pl):
                // SHOW the subwindow — the first show delivers an initial
                // resize/expose, which the bare ensureSubwindow (used by the
                // screenshot path) never does.
                sv->show();
                pump(1200);
              } else if (spec.startsWith("drag:")) {
                // Synthesize a REAL bottom-right edge drag through the
                // SkinView mouse handlers — the faithful replica of the
                // interactive resize (incl. the edge-press click dispatch
                // and the manual-resize fallback), unlike a bare resize().
                const QList<QByteArray> wh = spec.mid(5).split('x');
                if (wh.size() == 2) {
                  const QPointF p0(sv->width() - 3, sv->height() - 3);
                  const int dw = wh[0].trimmed().toInt() - sv->width();
                  const int dh = wh[1].trimmed().toInt() - sv->height();
                  auto send = [&](QEvent::Type t, const QPointF &lp) {
                    QMouseEvent ev(t, lp, sv->mapToGlobal(lp.toPoint()),
                                   Qt::LeftButton,
                                   t == QEvent::MouseButtonRelease
                                       ? Qt::NoButton : Qt::LeftButton,
                                   Qt::NoModifier);
                    QCoreApplication::sendEvent(sv, &ev);
                  };
                  send(QEvent::MouseButtonPress, p0);
                  pump(80);
                  const int kSteps = 6;
                  for (int i = 1; i <= kSteps; ++i) {
                    send(QEvent::MouseMove,
                         p0 + QPointF(dw * i / kSteps, dh * i / kSteps));
                    pump(120);
                  }
                  send(QEvent::MouseButtonRelease,
                       p0 + QPointF(dw, dh));
                  pump(200);
                }
              } else {
                // Comma-separated WxH sequence: successive programmatic
                // resizes with event pumping between steps.
                for (const QByteArray &step : spec.split(',')) {
                  const QList<QByteArray> wh = step.split('x');
                  if (wh.size() != 2) continue;
                  sv->resize(wh[0].trimmed().toInt(),
                             wh[1].trimmed().toInt());
                  pump(150);
                }
              }
            }
          }
        }
        // Test hook: WASABIQT_FORCE_RESIZE=WxH exercises the resize
        // handler offscreen (the live path is the OS toplevel resize).
        if (const QByteArray fr = qgetenv("WASABIQT_FORCE_RESIZE"); !fr.isEmpty()) {
          const QList<QByteArray> wh = fr.split('x');
          if (wh.size() == 2) {
            const int fw = wh[0].trimmed().toInt(), fh = wh[1].trimmed().toInt();
            if (fw > 0 && fh > 0) {
              qwin->resize(fw, fh);
              QCoreApplication::processEvents();
            }
          }
        }
        QImage shot = qwin->grabWindow();
        // WASABIQT_SHOT_ALPHA=1 — cut the grab to the skin's shaped window
        // region (the same region the compositor clips to live), leaving
        // everything outside fully transparent.  For marketing/press shots
        // of shaped skins on a transparent background.
        if (qEnvironmentVariableIntValue("WASABIQT_SHOT_ALPHA") == 1) {
          shot = shot.convertToFormat(QImage::Format_ARGB32);
          const QRegion reg = view->windowRegion();
          if (!reg.isEmpty()) {
            QImage cut(shot.size(), QImage::Format_ARGB32);
            cut.fill(Qt::transparent);
            QPainter cp(&cut);
            cp.setClipRegion(reg);
            cp.drawImage(0, 0, shot);
            cp.end();
            shot = cut;
          }
        }
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
        QStringList dirFiles;
        for (const QFileInfo &entry :
             dir.entryInfoList(audioExts, QDir::Files, QDir::Name))
          dirFiles << entry.absoluteFilePath();
        // Album-sequence order (track/disc tags), not filename.
        audiometa::sortByTrack(dirFiles);
        filesToPlay << dirFiles;
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

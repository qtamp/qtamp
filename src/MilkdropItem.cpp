// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#ifdef QTAMP_WITH_MILKDROP

#include "MilkdropItem.h"
#include "skinutils.h"

// projectM v4 — C API.  Renderer (libprojectM-4) + playlist
// module (libprojectM-4-playlist) are statically linked into the
// qtamp executable via the deps/projectm submodule build.
#include <projectM-4/projectM.h>
#include <projectM-4/audio.h>
#include <projectM-4/render_opengl.h>
#include <projectM-4/parameters.h>
#include <projectM-4/playlist.h>
#include <projectM-4/playlist_core.h>
#include <projectM-4/playlist_items.h>
#include <projectM-4/playlist_playback.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QTimer>
#include <QtDebug>
#include <QMutex>
#include <atomic>

namespace {

// Directories that contribute MilkDrop presets at runtime.  The v4
// playlist API scans recursively and de-dups for us; we just hand
// each dir to `projectm_playlist_add_path(recurse=true,
// allow_duplicates=false)`.  Order is significant — presets added
// later sit at the tail of the playlist.
const QStringList &presetDirs() {
    static const QStringList kDirs = {
        QDir::homePath() + QStringLiteral(
            "/.winamp/Plugins/Milkdrop/presets"),
        QStringLiteral("/usr/share/projectM/presets"),
        QStringLiteral("/usr/share/projectm/presets"),
    };
    return kDirs;
}

}  // namespace

// ── Private state ─────────────────────────────────────────────────
//
// One struct keeps GUI-thread state (analyzer pointer, item geometry,
// preset-command counters) AND render-thread state (own GL context,
// offscreen surface, FBO, projectM handle).  Access discipline:
//   • analyzer + counters: written GUI-thread, read render-thread.
//     Atomic where it matters; the counters are int and writes are
//     idempotent so we accept torn reads.
//   • context/surface/fbo/pm/playlist: render-thread only after the
//     one-shot init.
//   • m_lastTextureId: written render-thread, read scene-graph (the
//     same render-thread).  Same thread, no atomic needed.

class MilkdropItemPrivate {
public:
    MilkdropItem *q = nullptr;
    QPointer<QQuickWindow> window;

    // GUI-thread fields ----------------------------------------------
    AudioAnalyzer *analyzer = nullptr;
    std::atomic<int> prevCounter   {0};
    std::atomic<int> nextCounter   {0};
    std::atomic<int> shuffleEpoch  {0};
    std::atomic<bool> shuffleOn    {false};

    // Render-thread fields ------------------------------------------
    bool inited = false;
    QOpenGLContext   *pmContext = nullptr;
    QOffscreenSurface *pmSurface = nullptr;
    QOpenGLFramebufferObject *pmFbo = nullptr;
    projectm_handle           pm   = nullptr;
    projectm_playlist_handle  playlist = nullptr;
    QSize fboSize {0, 0};
    GLuint lastTextureId = 0;
    // Cached QSGTexture wrapper.  Recreated only when the FBO is
    // reallocated (size change) — wrapping the SAME GLuint frame
    // after frame would force RHI to re-import the native texture
    // every paint and was causing per-frame state churn that
    // surfaced as a one-frame flicker on hover (chrome QSGTexture
    // re-upload colliding with our wrapper recreate).
    QSGTexture *cachedTexture = nullptr;
    GLuint cachedTextureId = 0;
    QSize cachedTextureSize {0, 0};
    int applPrev = 0;
    int applNext = 0;
    int applShuffleEpoch = 0;

    // CPU-readback path (detached SkinView consumer). -----------------
    // cpuReadback + renderW/H written GUI-thread, read render-thread.
    // publishedFrame written render-thread (under frameMutex), read
    // GUI-thread (copyFrame) — the only field needing a real lock.
    std::atomic<bool> cpuReadback {false};
    std::atomic<int>  renderW {0};
    std::atomic<int>  renderH {0};
    mutable QMutex    frameMutex;
    QImage            publishedFrame;

    // Per-frame PCM scratch.
    static constexpr int kPCMChunk = 1024;
    float pcmL[kPCMChunk] = {0};
    float pcmR[kPCMChunk] = {0};
    float pcmInterleaved[kPCMChunk * 2] = {0};

    // Render entrypoint — invoked on the scene-graph render thread
    // via QQuickWindow::beforeFrameBegin (DirectConnection).  Makes
    // our dedicated context current on the offscreen surface, runs
    // one projectM frame into our FBO, glFinish to publish to the
    // shared resource view, restores.
    void renderFrame() {
        if (!window) return;
        // When driving a detached SkinView the item itself is hidden
        // (on-screen size 0), so the offscreen FBO follows an explicit
        // render size instead.  Docked path keeps following the item.
        const int rw = renderW.load(std::memory_order_relaxed);
        const int rh = renderH.load(std::memory_order_relaxed);
        const QSize want = (rw > 0 && rh > 0) ? QSize(rw, rh)
                                              : q->size().toSize();
        if (want.width() <= 0 || want.height() <= 0) return;

        if (!inited && !initLazy()) return;
        if (!pmContext->makeCurrent(pmSurface)) {
            qWarning() << "MilkdropItem: makeCurrent failed";
            return;
        }
        if (!pmFbo || pmFbo->size() != want) {
            delete pmFbo;
            QOpenGLFramebufferObjectFormat fmt;
            fmt.setAttachment(
                QOpenGLFramebufferObject::CombinedDepthStencil);
            pmFbo = new QOpenGLFramebufferObject(want, fmt);
            fboSize = want;
            projectm_set_window_size(pm,
                size_t(want.width()), size_t(want.height()));
        }
        pmFbo->bind();
        // Apply queued preset commands.
        const int wantPrev = prevCounter.load(std::memory_order_relaxed);
        while (applPrev < wantPrev) {
            projectm_playlist_play_previous(playlist, true);
            ++applPrev;
        }
        const int wantNext = nextCounter.load(std::memory_order_relaxed);
        while (applNext < wantNext) {
            projectm_playlist_play_next(playlist, true);
            ++applNext;
        }
        const int wantEpoch = shuffleEpoch.load(std::memory_order_relaxed);
        if (applShuffleEpoch < wantEpoch) {
            projectm_playlist_set_shuffle(playlist,
                shuffleOn.load(std::memory_order_relaxed));
            applShuffleEpoch = wantEpoch;
        }
        // Drain PCM.
        if (analyzer) {
            const int got = analyzer->drainRawPCM(pcmL, pcmR,
                                                   kPCMChunk);
            if (got > 0) {
                for (int i = 0; i < got; ++i) {
                    pcmInterleaved[2*i  ] = pcmL[i];
                    pcmInterleaved[2*i+1] = pcmR[i];
                }
                projectm_pcm_add_float(pm, pcmInterleaved,
                                       uint32_t(got),
                                       PROJECTM_STEREO);
            }
        }
        projectm_opengl_render_frame(pm);
        // Publish to the shared-context texture view.  Our patch in
        // ProjectM.cpp restored the bound DRAW_FRAMEBUFFER for us,
        // so the projectM composite lands in `pmFbo`.  glFinish
        // ensures the GPU is done before the scene-graph thread
        // samples the shared texture later in the same frame.
        pmContext->functions()->glFinish();
        lastTextureId = pmFbo->texture();
        // Detached path: read the just-rendered frame back to a QImage
        // for the consuming SkinView, which has no GL surface of its
        // own.  Done while the FBO is still bound and the context
        // current; the copy is cheap relative to the projectM frame.
        if (cpuReadback.load(std::memory_order_relaxed)) {
            QImage img = pmFbo->toImage(false);  // no premultiply flip
            if (!img.isNull()) {
                QMutexLocker lk(&frameMutex);
                publishedFrame = std::move(img);
            }
        }
        pmFbo->release();
        pmContext->doneCurrent();
        // Intentionally NO self-feeding update() call here.  An
        // external QTimer (`paintTick`) ticks the item at a fixed
        // 60 Hz cadence; feeding updates from beforeFrameBegin
        // creates a render/update positive feedback that competes
        // with hover-triggered updates and causes the chrome
        // QSGTexture re-upload to race against the next composite.
    }

    bool initLazy() {
        // First-time setup of the dedicated context, surface, and
        // projectM.  Runs on the render thread on first
        // beforeFrameBegin tick.
        QOpenGLContext *sceneCtx = QOpenGLContext::currentContext();
        if (!sceneCtx) {
            // Scene graph hasn't bound its context yet — try again
            // next frame.  Happens on the first tick before the
            // QQuickWindow's GL context is initialised.
            return false;
        }
        pmContext = new QOpenGLContext();
        pmContext->setShareContext(sceneCtx);
        pmContext->setFormat(sceneCtx->format());
        if (!pmContext->create()) {
            qWarning() << "MilkdropItem: dedicated context create failed";
            delete pmContext; pmContext = nullptr;
            return false;
        }
        pmSurface = new QOffscreenSurface();
        pmSurface->setFormat(sceneCtx->format());
        pmSurface->create();
        if (!pmSurface->isValid()) {
            qWarning() << "MilkdropItem: offscreen surface invalid";
            delete pmSurface; pmSurface = nullptr;
            delete pmContext; pmContext = nullptr;
            return false;
        }
        if (!pmContext->makeCurrent(pmSurface)) {
            qWarning() << "MilkdropItem: makeCurrent on init failed";
            return false;
        }
        pm = projectm_create();
        if (!pm) {
            qWarning() << "MilkdropItem: projectm_create failed";
            pmContext->doneCurrent();
            return false;
        }
        projectm_set_window_size(pm, 1, 1);
        projectm_set_fps(pm, 60);
        projectm_set_mesh_size(pm, 48, 36);
        projectm_set_preset_duration(pm, 30.0);
        // Visual-tuning defaults are kept simple — the soft-cut
        // duration and hard-cut enable that were tuned earlier as
        // workarounds for the chrome-blur are intentionally LEFT at
        // projectM defaults now.  The chrome-blur is fixed at the
        // architectural level; if the user wants tighter transitions
        // we can reintroduce those knobs as a preference, not as a
        // workaround.

        playlist = projectm_playlist_create(pm);
        if (!playlist) {
            qWarning() << "MilkdropItem: playlist_create failed";
        } else {
            unsigned total = 0;
            for (const QString &dir : presetDirs()) {
                if (!QFileInfo(dir).isDir()) continue;
                total += projectm_playlist_add_path(playlist,
                    dir.toLocal8Bit().constData(),
                    /*recurse_subdirs=*/true,
                    /*allow_duplicates=*/false);
            }
            // Pick a random initial preset so it doesn't always
            // start on the alphabetically-first one.
            if (projectm_playlist_size(playlist) > 0) {
                projectm_playlist_set_shuffle(playlist, true);
                projectm_playlist_play_next(playlist, true);
                projectm_playlist_set_shuffle(playlist, false);
            }
            qInfo().noquote()
                << "MilkdropItem: projectM v4 up (dedicated GL ctx),"
                << total << "presets indexed,"
                << projectm_playlist_size(playlist) << "in playlist.";
        }
        pmContext->doneCurrent();
        inited = true;
        return true;
    }

    void teardown() {
        if (!inited) return;
        if (!pmContext) return;
        if (pmContext->makeCurrent(pmSurface)) {
            if (playlist) projectm_playlist_destroy(playlist);
            if (pm)       projectm_destroy(pm);
            delete pmFbo;
            pmContext->doneCurrent();
        }
        playlist = nullptr;
        pm = nullptr;
        pmFbo = nullptr;
        delete pmSurface; pmSurface = nullptr;
        delete pmContext; pmContext = nullptr;
        // The cached QSGTexture wrapper points into a GLuint that
        // belonged to the just-deleted FBO, so it's dangling.  The
        // scene graph will delete it when the node it's attached to
        // gets recycled; we just drop the cached pointer so we don't
        // try to reuse it.
        cachedTexture = nullptr;
        cachedTextureId = 0;
        inited = false;
    }
};

// ── MilkdropItem ───────────────────────────────────────────────────

MilkdropItem::MilkdropItem(QQuickItem *parent)
    : QQuickItem(parent), d(new MilkdropItemPrivate) {
    d->q = this;
    setFlag(QQuickItem::ItemHasContents, true);
    // We don't claim mouse/hover — chrome below us still gets events.
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);

    // 60 Hz repaint tick.  This is the SOLE driver of MilkDrop's
    // animation cadence — drives QQuickItem::update() at a steady
    // 16 ms interval, which lets the scene graph schedule one paint
    // per tick.  Each paint emits beforeFrameBegin → our
    // renderFrame() runs projectM into the FBO → updatePaintNode
    // points the QSGSimpleTextureNode at the fresh texture.
    //
    // Crucially, the tick is INDEPENDENT of hover / drawer-mode /
    // any other update() the chrome triggers.  Multiple update()
    // calls in a single frame coalesce; the scene graph paints
    // once.  Without a steady tick the visualization would freeze
    // whenever the chrome isn't repainting (no hover, no spectrum
    // tick), since beforeFrameBegin only fires when the scene
    // graph itself decides to paint.
    auto *paintTick = new QTimer(this);
    paintTick->setInterval(16);                // ~60 fps
    QObject::connect(paintTick, &QTimer::timeout, this, [this]() {
        // While feeding a detached SkinView this item is hidden, so
        // update()ing it schedules no scene-graph paint and
        // beforeFrameBegin would never fire.  Force a window frame
        // instead so projectM keeps producing readback frames; the
        // hidden item adds nothing to the visible chrome.
        if (d->cpuReadback.load(std::memory_order_relaxed)) {
            if (d->window) d->window->update();
        } else {
            update();
        }
    });
    paintTick->start();
}

MilkdropItem::~MilkdropItem() {
    if (d->window) {
        QObject::disconnect(d->window, nullptr, this, nullptr);
    }
    d->teardown();
    delete d;
}

void MilkdropItem::setAnalyzer(AudioAnalyzer *a) {
    d->analyzer = a;
}

AudioAnalyzer *MilkdropItem::analyzer() const {
    return d->analyzer;
}

void MilkdropItem::selectPrev() {
    d->prevCounter.fetch_add(1, std::memory_order_relaxed);
    update();
}
void MilkdropItem::selectNext() {
    d->nextCounter.fetch_add(1, std::memory_order_relaxed);
    update();
}
void MilkdropItem::setShuffle(bool on) {
    if (d->shuffleOn.exchange(on, std::memory_order_relaxed) == on)
        return;
    d->shuffleEpoch.fetch_add(1, std::memory_order_relaxed);
    update();
}

void MilkdropItem::setCpuReadbackEnabled(bool on) {
    if (d->cpuReadback.exchange(on, std::memory_order_relaxed) == on)
        return;
    if (!on) {
        // Drop the stale frame so a re-enable can't briefly publish an
        // old image at the wrong size.
        QMutexLocker lk(&d->frameMutex);
        d->publishedFrame = QImage();
    }
}

void MilkdropItem::setRenderSize(const QSize &s) {
    d->renderW.store(s.width()  > 0 ? s.width()  : 0,
                     std::memory_order_relaxed);
    d->renderH.store(s.height() > 0 ? s.height() : 0,
                     std::memory_order_relaxed);
}

bool MilkdropItem::copyFrame(QImage &out) const {
    QMutexLocker lk(&d->frameMutex);
    if (d->publishedFrame.isNull()) return false;
    out = d->publishedFrame;   // implicit-shared, cheap; deep-copied on write
    return true;
}

void MilkdropItem::itemChange(ItemChange change,
                               const ItemChangeData &value) {
    if (change == ItemSceneChange) {
        // Disconnect from the old window (if any), connect to the
        // new.  The beforeFrameBegin signal is emitted on the scene-
        // graph render thread (DirectConnection runs our slot
        // there).  We use beforeFrameBegin (the earliest per-frame
        // hook) so projectM renders into its FBO BEFORE the scene
        // graph starts recording the chrome render pass.
        if (d->window) {
            QObject::disconnect(d->window, nullptr, this, nullptr);
        }
        d->window = value.window;
        if (d->window) {
            QObject::connect(d->window.data(),
                &QQuickWindow::beforeFrameBegin,
                this, [this]() { d->renderFrame(); },
                Qt::DirectConnection);
            QObject::connect(d->window.data(),
                &QQuickWindow::sceneGraphInvalidated,
                this, [this]() { d->teardown(); },
                Qt::DirectConnection);
        }
    }
    QQuickItem::itemChange(change, value);
}

QSGNode *MilkdropItem::updatePaintNode(QSGNode *oldNode,
                                        UpdatePaintNodeData *) {
    if (!d->window || d->lastTextureId == 0 ||
        d->fboSize.width() <= 0 || d->fboSize.height() <= 0) {
        delete oldNode;
        d->cachedTexture = nullptr;
        d->cachedTextureId = 0;
        return nullptr;
    }
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    // Cache the QSGTexture wrapper across frames.  Reuse it as long
    // as the underlying GLuint + size haven't changed; only rebuild
    // on FBO realloc (resize).  Recreating the wrapper every paint
    // was forcing RHI to re-import the native texture each composite
    // and caused per-frame state churn that surfaced as a one-frame
    // flicker on hover.
    if (d->cachedTexture == nullptr ||
        d->cachedTextureId != d->lastTextureId ||
        d->cachedTextureSize != d->fboSize) {
        // Note: QSGSimpleTextureNode owns the previous wrapper and
        // will delete it when setTexture() is called with the new
        // one, so we don't free `d->cachedTexture` ourselves.
        d->cachedTexture =
            QNativeInterface::QSGOpenGLTexture::fromNative(
                d->lastTextureId, d->window, d->fboSize,
                QQuickWindow::TextureHasAlphaChannel);
        d->cachedTextureId   = d->lastTextureId;
        d->cachedTextureSize = d->fboSize;
        node->setTexture(d->cachedTexture);
    }
    node->setRect(boundingRect());
    return node;
}

#endif  // QTAMP_WITH_MILKDROP

#pragma once
//
// MilkdropItem — QQuickItem that hosts a libprojectM 4 MilkDrop
// visualizer in a dedicated OpenGL context that SHARES texture
// resources with Qt's scene-graph context.
//
// Why a dedicated context?  Qt 6's scene graph runs through QRhi,
// which keeps an internal cache of bound pipeline state.  When raw
// GL calls inside a renderer change state behind RHI's back, RHI
// doesn't notice and keeps using stale assumptions — the visible
// symptom is the chrome going blurry for a frame whenever projectM
// does heavy GL work (drawer open, each preset switch).  Snapshot/
// restore of GL state is incomplete by construction (sampler
// objects, indexed scissor, per-texture-unit bindings on non-0
// units, etc.).  The architectural fix is to run projectM on its
// own QOpenGLContext: state mutations there CAN'T leak into the
// scene graph context because they're in a different context.
// Texture resources are shared via `setShareContext`, so the
// scene graph can sample our FBO's color texture directly.
//
// Threading: rendering happens on the scene-graph render thread
// via QQuickWindow::beforeFrameBegin (Qt::DirectConnection).  We
// make our context current on a QOffscreenSurface, render projectM,
// `glFinish` to publish, release.  updatePaintNode() wraps our
// FBO's GLuint texture in a QSGTexture via the canonical Qt 6
// QNativeInterface::QSGOpenGLTexture::fromNative bridge, and uses
// a QSGSimpleTextureNode to composite it.
//

#ifdef QTAMP_WITH_MILKDROP

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QSize>

QT_BEGIN_NAMESPACE
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOffscreenSurface;
class QSGTexture;
class QQuickWindow;
QT_END_NAMESPACE

class AudioAnalyzer;
class MilkdropItemPrivate;

class MilkdropItem : public QQuickItem {
    Q_OBJECT
public:
    explicit MilkdropItem(QQuickItem *parent = nullptr);
    ~MilkdropItem() override;

    void setAnalyzer(AudioAnalyzer *a);
    AudioAnalyzer *analyzer() const;

    // Cross-thread preset commands.  Each setter bumps a monotonic
    // counter or flips a flag; the render-thread side reads the
    // state and applies it via projectM's playlist API.  See the
    // canonical Wasabi semantics: Random is a sticky shuffle toggle
    // bound to a cfgattrib, Prev/Next walk the playlist.
    void selectPrev();
    void selectNext();
    void setShuffle(bool on);

    // CPU-frame readback path — used to render the visualizer into a
    // DETACHED SkinView (a plain QWidget software window that has no GL
    // scene graph to host this QQuickItem).  When enabled, renderFrame()
    // additionally reads the FBO back to a QImage that copyFrame()
    // publishes thread-safely; setRenderSize drives the offscreen FBO to
    // the detached slot's size independently of this item's on-screen
    // size (which is 0/hidden while detached).  The docked overlay path
    // is untouched — readback stays OFF and costs nothing then.
    void setCpuReadbackEnabled(bool on);
    void setRenderSize(const QSize &s);   // empty = follow on-screen size
    bool copyFrame(QImage &out) const;    // false if none published yet

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    MilkdropItemPrivate *d;
};

#endif  // QTAMP_WITH_MILKDROP

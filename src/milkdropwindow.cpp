#include "milkdropwindow.h"
#include <cstring>
#include <algorithm>
#include <string>
#include <QPainter>

MilkdropWindow::MilkdropWindow(QWidget *parent) : QOpenGLWidget(parent) {
    setWindowTitle("Milkdrop Visualization");
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 480);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);

    // Timer driving projectM render at ~33fps
    renderTimer = new QTimer(this);
    connect(renderTimer, &QTimer::timeout, this, QOverload<>::of(&MilkdropWindow::update));
}

MilkdropWindow::~MilkdropWindow() {
    makeCurrent();
#ifdef HAVE_PROJECTM
    if (pm) { delete pm; pm = nullptr; }
#endif
    doneCurrent();
}

bool MilkdropWindow::isAvailable() const {
#ifdef HAVE_PROJECTM
    return true;
#else
    return false;
#endif
}

void MilkdropWindow::feedPCMInt16(const qint16 *data, int frames, int channels) {
#ifdef HAVE_PROJECTM
    if (!pm) return;
    int n = qMin(frames, 512);
    pm->pcm()->addPCM16Data(data, n);
#else
    Q_UNUSED(data); Q_UNUSED(frames); Q_UNUSED(channels);
#endif
}

void MilkdropWindow::feedPCMFloat(const float *data, int frames, int channels) {
#ifdef HAVE_PROJECTM
    if (!pm) return;
    float mono[512];
    int n = qMin(frames, 512);
    for (int i = 0; i < n; i++)
        mono[i] = data[i * channels];
    pm->pcm()->addPCMfloat(mono, n);
#else
    Q_UNUSED(data); Q_UNUSED(frames); Q_UNUSED(channels);
#endif
}

void MilkdropWindow::nextPreset() {
#ifdef HAVE_PROJECTM
    if (pm) pm->selectNext(true);
#endif
}
void MilkdropWindow::prevPreset() {
#ifdef HAVE_PROJECTM
    if (pm) pm->selectPrevious(true);
#endif
}
void MilkdropWindow::randomPreset() {
#ifdef HAVE_PROJECTM
    if (pm) pm->selectRandom(true);
#endif
}

void MilkdropWindow::toggleLock() {
#ifdef HAVE_PROJECTM
    if (!pm) return;
    pm->setPresetLock(!pm->isPresetLocked());
#endif
}

void MilkdropWindow::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0, 0, 0, 1);

#ifdef HAVE_PROJECTM
    // Create projectM instance from settings struct
    projectM::Settings s;
    s.meshX = 32;
    s.meshY = 24;
    s.fps = 33;
    s.textureSize = 1024;
    s.windowWidth = width();
    s.windowHeight = height();
    s.presetURL = "/usr/share/projectM/presets";
    s.smoothPresetDuration = 5;
    s.presetDuration = 30;
    s.beatSensitivity = 1.0f;
    s.aspectCorrection = true;
    s.easterEgg = 1.0f;
    s.shuffleEnabled = true;
    s.softCutRatingsEnabled = false;

    // Find fonts
    std::string fontPath;
    for (auto &candidate : {std::string("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
                             std::string("/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf"),
                             std::string("/usr/share/fonts/truetype/freefont/FreeSans.ttf")}) {
        if (QFile::exists(QString::fromStdString(candidate))) { fontPath = candidate; break; }
    }
    s.titleFontURL = fontPath;
    s.menuFontURL = fontPath;

    try {
        pm = new projectM(s, projectM::FLAG_NONE);
        pm->projectM_resetGL(width(), height());
        pm->projectM_setTitle("");  // Clear the default "projectM" title overlay
        pm->selectRandom(true);
        qDebug() << "Milkdrop (projectM) initialized with" << pm->getPlaylistSize() << "presets";
    } catch (const std::exception &e) {
        qWarning() << "Failed to initialize projectM:" << e.what();
        pm = nullptr;
    } catch (...) {
        qWarning() << "Failed to initialize projectM (unknown error)";
        pm = nullptr;
    }
#endif

    renderTimer->start(1000 / 33);
}

void MilkdropWindow::resizeGL(int w, int h) {
#ifdef HAVE_PROJECTM
    if (pm) pm->projectM_resetGL(w, h);
#else
    Q_UNUSED(w); Q_UNUSED(h);
#endif
}

void MilkdropWindow::paintGL() {
#ifdef HAVE_PROJECTM
    if (pm) {
        pm->renderFrame();
    } else {
        glClear(GL_COLOR_BUFFER_BIT);
    }
#else
    glClear(GL_COLOR_BUFFER_BIT);
    // Draw "not available" message
    QPainter p(this);
    p.setPen(QColor(0, 255, 0));
    p.setFont(QFont("Sans", 14));
    p.drawText(rect(), Qt::AlignCenter, "MilkDrop requires libprojectM\nInstall: sudo apt install libprojectm-dev");
    p.end();
#endif
}

void MilkdropWindow::keyPressEvent(QKeyEvent *e) {
#ifdef HAVE_PROJECTM
    if (!pm) { QOpenGLWidget::keyPressEvent(e); return; }
    switch (e->key()) {
        case Qt::Key_Space: nextPreset(); break;
        case Qt::Key_Backspace: prevPreset(); break;
        case Qt::Key_L: toggleLock(); break;
        case Qt::Key_R: pm->setShuffleEnabled(!pm->isShuffleEnabled()); break;
        case Qt::Key_F:
        case Qt::Key_F11:
            toggleFullScreen();
            break;
        case Qt::Key_Escape:
            if (isFullScreen()) toggleFullScreen();
            else close();
            break;
        case Qt::Key_Q: close(); break;
        default: QOpenGLWidget::keyPressEvent(e); return;
    }
    e->accept();
#else
    switch (e->key()) {
        case Qt::Key_F:
        case Qt::Key_F11:
            toggleFullScreen();
            break;
        case Qt::Key_Escape:
            if (isFullScreen()) toggleFullScreen();
            else close();
            break;
        case Qt::Key_Q: close(); break;
        default: QOpenGLWidget::keyPressEvent(e); return;
    }
    e->accept();
#endif
}

void MilkdropWindow::mouseDoubleClickEvent(QMouseEvent *e) {
    toggleFullScreen();
}

void MilkdropWindow::showEvent(QShowEvent *e) {
    QOpenGLWidget::showEvent(e);
    setFocus();
}

void MilkdropWindow::closeEvent(QCloseEvent *e) {
    if (isFullScreen()) {
        showNormal();
        emit fullscreenChanged(false);
    }
    renderTimer->stop();
    e->accept();
}

void MilkdropWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
        emit fullscreenChanged(false);
    } else {
        showFullScreen();
        emit fullscreenChanged(true);
    }
    setFocus();
}

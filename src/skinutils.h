#ifndef SKINUTILS_H
#define SKINUTILS_H

#include <QString>
#include <QColor>
#include <QPoint>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>

// Extract a .wsz or .zip skin archive to a cache directory.
QString extractSkinArchive(const QString &archivePath);

// Skin Playlist Colors — parse PLEDIT.TXT (from Windows skins)
struct SkinPlaylistColors {
    QColor normal      = QColor(0, 255, 0);
    QColor current     = QColor(255, 255, 255);
    QColor normBg      = QColor(0, 0, 0);
    QColor selectBg    = QColor(0, 0, 198);
    QColor mbFg        = QColor(0, 255, 0);
    QColor mbBg        = QColor(0, 0, 0);
};

SkinPlaylistColors parsePleditTxt(const QString &skinPath);

// Global skin playlist colors (loaded when skin changes)
extern SkinPlaylistColors g_plColors;

// Simple FFT for spectrum analyzer (radix-2 DIT, 512-point)
void fft512(const float *input, float *magnitudes);

// Shared audio analyzer used by both the classic-skin path
// (WinampWindow::processAudioBuffer) and the modern-skin path
// (QtampHost → WasabiQt::Host → VisWidget).  Holds the latest
// spectrum / oscilloscope / VU / level values extracted from the
// most recent QAudioBuffer.  Feed it from the QAudioBufferOutput
// callback; consumers read the snapshot at paint time.
class QAudioBuffer;
class AudioAnalyzer {
public:
    static constexpr int kSpectrumBands = 19;
    static constexpr int kOscSamples    = 75;

    void feed(const QAudioBuffer &buffer);
    void reset();

    // Peak-falloff speed.  0=Slow, 1=Medium, 2=Fast, 3=Fastest.
    // Maps to the per-feed decay applied to m_peaks before the new
    // spectrum samples are folded in.
    void setPeakFalloff(int idx);

    const float *spectrum() const { return m_spectrum; }
    const float *peaks()    const { return m_peaks; }
    const float *osc()      const { return m_osc; }
    float vuLeft()  const { return m_vuL; }
    float vuRight() const { return m_vuR; }
    double level()  const { return m_level; }

private:
    float  m_spectrum[kSpectrumBands] = {0};
    float  m_peaks[kSpectrumBands]    = {0};
    float  m_osc[kOscSamples]         = {0};
    float  m_vuL = 0.0f;
    float  m_vuR = 0.0f;
    double m_level = 0.0;
    float  m_peakDecay = 0.02f;   // medium
};

// Winamp visualization colors (24 entries)
extern QColor visColors[24];

// Load viscolor.txt if present
void loadVisColors(const QString &skinPath);

// Shared text.bmp character lookup (5x6 per char)
QPoint getTextCharPos(QChar ch);

// Detect whether a skin directory is a modern (XML) skin
bool isModernSkinDir(const QString &path);

// Config file path helper
QString configPath();

#endif // SKINUTILS_H

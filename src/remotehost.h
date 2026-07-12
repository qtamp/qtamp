// RemoteHost — a PlayerHost backed by a networked qtamp backend instead
// of a local audio pipeline. Reads answer from a cached RemoteSnapshot
// (with the play position interpolated by PositionClock, so the posbar
// is smooth without position traffic); writes become fire-and-forget
// /cmd POSTs with optimistic local echo where flicker matters; pushed
// SSE events keep the cache fresh and drive the PlayerHost change
// signals, so the window's existing repaint machinery works unchanged.
//
// Speaks the control-channel protocol of pylon/PROTOCOL.md directly —
// against `qtamp --backend` on loopback or through any pass-through
// proxy (the qtamp-pylon, the ts4.party chain). See docs/OKF-remote.md.
#pragma once

#include <QImage>
#include <QUrl>

#include "playerhost.h"
#include "remotestate.h"
#include "remotetransport.h"

namespace qtamp {

class RemoteHost : public PlayerHost {
    Q_OBJECT
public:
    // `base` is the protocol root (…/state etc. appended). Takes
    // ownership of the transport.
    RemoteHost(const QUrl &base, RemoteTransport *transport,
               QObject *parent = nullptr);

    // ── qtWasabi::Host reads, answered from the cache ────────────────
    qint64 positionMs() const override;
    qint64 durationMs() const override { return m_snap.transport.durationMs; }
    int bitrate() const override { return m_snap.track.bitrate; }
    int sampleRate() const override { return m_snap.track.sampleRate; }
    int channelCount() const override { return m_snap.track.channels; }
    int volume() const override { return m_snap.transport.volume; }
    bool isPlaying() const override { return m_snap.transport.playing; }
    bool isPaused() const override { return m_snap.transport.paused; }
    QString songTitle() const override { return m_snap.track.title; }
    QString songFilename() const override { return m_snap.track.filename; }
    QString playItemMetaData(const QString &field) const override;
    QString playItemDisplayTitle() const override {
        return m_snap.track.displayTitle.isEmpty()
                   ? m_snap.track.title
                   : m_snap.track.displayTitle;
    }
    QString decoderName() const override { return m_snap.track.decoder; }
    QImage albumArt() const override { return m_art; }

    using Host::sliderPosition;
    using Host::setSliderPosition;
    double sliderPosition(const QString &action,
                          const QString &param) const override;
    void setSliderPosition(const QString &action, double v,
                           const QString &param) override;

    int playlistRowCount() const override { return m_snap.playlist.count(); }
    QString playlistRowText(int row) const override {
        return row >= 0 && row < m_snap.playlist.rows.size()
                   ? m_snap.playlist.rows[row].text
                   : QString();
    }
    qint64 playlistRowDurationMs(int row) const override {
        return row >= 0 && row < m_snap.playlist.rows.size()
                   ? m_snap.playlist.rows[row].durationMs
                   : 0;
    }
    int playlistCurrentRow() const override {
        return m_snap.playlist.currentIndex;
    }
    void playlistSetCurrentRow(int row) override;
    void playlistPlayRow(int row) override;
    // Menu verbs are native popups; remotely they are consumed (so the
    // click doesn't fall through to a drag) and dropped. The explicit
    // playlist ops exist in the protocol for a later web-side menu.
    bool pleditCommand(const QString &) override { return true; }

    // ── qtWasabi::Host writes → commands ─────────────────────────────
    void play() override;
    void pause() override;
    void stop() override;
    void next() override { sendCmd(QStringLiteral("next"), {}); }
    void prev() override { sendCmd(QStringLiteral("prev"), {}); }
    void seekMs(qint64 ms) override;
    void setVolume(int v) override;

    // ── PlayerHost surface ────────────────────────────────────────────
    void openPath(const QUrl &) override {}       // no local files remotely
    void enqueueAndPlay(const QUrl &, bool) override {}
    QString songPath() const override { return m_snap.track.filename; }
    QUrl currentSourceUrl() const override {
        return QUrl::fromLocalFile(m_snap.track.filename);
    }
    qtWasabi::HostCapabilities hostCapabilities() const override {
        return {/*localFiles=*/false, /*localAnalyzer=*/false};
    }

private:
    void resync();
    void onEvent(const QByteArray &event, const QByteArray &data);
    void sendCmd(const QString &op, const QJsonObject &args);
    void anchorFromSnapshot();
    void fetchArt();

    QUrl endpoint(const char *path) const;

    QUrl m_base;
    RemoteTransport *m_transport;
    RemoteSnapshot m_snap;
    PositionClock m_clock;
    QImage m_art;
    QString m_artForFile;  // which track the cached art belongs to
};

}  // namespace qtamp

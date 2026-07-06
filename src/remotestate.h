// RemoteSnapshot — the shared state document of the networked player,
// plus the pure logic around it: JSON parse/serialize (used by BOTH the
// RemoteHost cache and the --backend serializer, so client parse and
// server emit are the same tested code), revision-checked event
// application, and the interpolated position clock.
//
// The wire vocabulary is documented in pylon/PROTOCOL.md and
// docs/OKF-remote.md. Everything here is network-free and GUI-free by
// design: it unit-tests headlessly (tests/remotestate_test.cpp).
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace qtamp {

struct TransportState {
    bool   playing = false;
    bool   paused  = false;
    qint64 positionMs = 0;    // position at sampling time…
    qint64 positionAtMs = 0;  // …on the server's monotonic clock
    qint64 durationMs = 0;
    int    volume = 100;      // 0..100
    double pan = 0.5;         // 0..1 slider axis, 0.5 = centre

    bool operator==(const TransportState &o) const {
        return playing == o.playing && paused == o.paused &&
               positionMs == o.positionMs && positionAtMs == o.positionAtMs &&
               durationMs == o.durationMs && volume == o.volume &&
               qFuzzyCompare(pan + 1.0, o.pan + 1.0);
    }
};

struct RemoteTrack {
    QString title, artist, album, filename, displayTitle, decoder;
    int bitrate = 0, sampleRate = 0, channels = 0;

    bool operator==(const RemoteTrack &o) const {
        return title == o.title && artist == o.artist && album == o.album &&
               filename == o.filename && displayTitle == o.displayTitle &&
               decoder == o.decoder && bitrate == o.bitrate &&
               sampleRate == o.sampleRate && channels == o.channels;
    }
};

struct RemotePlaylistRow {
    QString text;
    qint64 durationMs = 0;

    bool operator==(const RemotePlaylistRow &o) const {
        return text == o.text && durationMs == o.durationMs;
    }
};

struct RemotePlaylist {
    quint64 revision = 0;  // playlist-local counter, bumps on row changes
    int currentIndex = -1;
    QVector<RemotePlaylistRow> rows;

    int count() const { return rows.size(); }
    bool operator==(const RemotePlaylist &o) const {
        return revision == o.revision && currentIndex == o.currentIndex &&
               rows == o.rows;
    }
};

struct RemoteEq {
    bool on = false, autoOn = false;
    int preamp = 31;                 // Winamp 0..63 slider scale
    QVector<int> bands = QVector<int>(10, 31);

    bool operator==(const RemoteEq &o) const {
        return on == o.on && autoOn == o.autoOn && preamp == o.preamp &&
               bands == o.bands;
    }
};

struct RemoteSnapshot {
    QString epoch;         // fresh per backend boot; change = full resync
    quint64 revision = 0;  // global monotonic change counter
    qint64 serverNowMs = 0;
    TransportState transport;
    RemoteTrack track;
    RemotePlaylist playlist;
    RemoteEq eq;

    bool operator==(const RemoteSnapshot &o) const {
        return epoch == o.epoch && revision == o.revision &&
               serverNowMs == o.serverNowMs && transport == o.transport &&
               track == o.track && playlist == o.playlist && eq == o.eq;
    }
};

// Parse a full snapshot document. Returns false on a structurally broken
// document (missing sections); missing scalar fields keep their defaults.
bool parseSnapshot(const QJsonObject &doc, RemoteSnapshot *out);

// The inverse, shared with the backend's serializer.
QJsonObject serializeSnapshot(const RemoteSnapshot &s);

// Apply one pushed event to the cache.
enum class ApplyResult {
    Applied,      // cache updated
    Stale,        // event revision <= cache revision: dropped
    NeedsResync,  // epoch changed or revision gap: caller must re-snapshot
    Ignored       // unknown event name / ping
};
ApplyResult applyEvent(const QByteArray &name, const QJsonObject &payload,
                       RemoteSnapshot *snap);

// PositionClock — client-side interpolation of the play position, so a
// posbar advances smoothly at paint time without the server pushing
// position ticks. Anchored at LOCAL receipt time: the interpolation
// error is then only the one-way delivery latency, and no clock-sync
// protocol is needed. While playing, positionAt() never runs backwards
// across re-anchors (small negative corrections are held until they are
// overtaken); corrections beyond the snap threshold — a seek, a stall —
// apply immediately.
class PositionClock {
public:
    static constexpr qint64 kSnapThresholdMs = 250;

    void anchor(qint64 positionMs, qint64 localReceiptMs, bool playing) {
        const qint64 predicted = positionAt(localReceiptMs);
        const bool hadAnchor = m_hasAnchor;
        m_hasAnchor = true;
        m_playing = playing;
        m_anchorLocalMs = localReceiptMs;
        if (!hadAnchor || qAbs(predicted - positionMs) > kSnapThresholdMs) {
            m_anchorPosMs = positionMs;   // seek/stall/first anchor: snap
            m_floorMs = positionMs;
        } else {
            m_anchorPosMs = positionMs;   // small drift: correct…
            m_floorMs = qMax(m_floorMs, predicted);  // …but never rewind
        }
    }

    qint64 positionAt(qint64 localNowMs) const {
        if (!m_hasAnchor) return 0;
        qint64 p = m_anchorPosMs;
        if (m_playing) p += localNowMs - m_anchorLocalMs;
        if (m_playing && p < m_floorMs) p = m_floorMs;  // monotonic guard
        return qMax<qint64>(0, p);
    }

    bool hasAnchor() const { return m_hasAnchor; }

private:
    bool m_hasAnchor = false;
    bool m_playing = false;
    qint64 m_anchorPosMs = 0;
    qint64 m_anchorLocalMs = 0;
    qint64 m_floorMs = 0;
};

}  // namespace qtamp

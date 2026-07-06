#include "remotehost.h"

#include <QDateTime>
#include <QJsonDocument>

namespace qtamp {

namespace {
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
}

RemoteHost::RemoteHost(const QUrl &base, RemoteTransport *transport,
                       QObject *parent)
    : PlayerHost(parent), m_base(base), m_transport(transport) {
    m_transport->setParent(this);
    connect(m_transport, &RemoteTransport::eventReceived, this,
            &RemoteHost::onEvent);
    // Reconnects re-snapshot: the stream may have dropped events while
    // down, and the first frame after connect is a full `state` anyway —
    // the explicit fetch covers transports without that guarantee.
    connect(m_transport, &RemoteTransport::streamStateChanged, this,
            [this](bool up) {
                if (up) resync();
            });
    m_transport->openEventStream(endpoint("events"));
}

QUrl RemoteHost::endpoint(const char *path) const {
    QUrl u = m_base;
    QString p = u.path();
    if (!p.endsWith(QLatin1Char('/'))) p += QLatin1Char('/');
    u.setPath(p + QLatin1String(path));
    return u;
}

void RemoteHost::resync() {
    m_transport->getJson(endpoint("state"), [this](bool ok, QJsonObject doc) {
        if (!ok) return;  // stream reconnect will retry
        RemoteSnapshot fresh;
        if (!parseSnapshot(doc, &fresh)) return;
        const bool trackChanged =
            fresh.track.filename != m_snap.track.filename;
        const bool playlistChangedNow =
            !(fresh.playlist == m_snap.playlist);
        m_snap = fresh;
        anchorFromSnapshot();
        emit playbackStateChanged();
        if (trackChanged) {
            emit sourceChanged();
            emit metaDataChanged();
            fetchArt();
        }
        if (playlistChangedNow) emit playlistChanged();
    });
}

void RemoteHost::anchorFromSnapshot() {
    m_clock.anchor(m_snap.transport.positionMs, nowMs(),
                   m_snap.transport.playing && !m_snap.transport.paused);
}

qint64 RemoteHost::positionMs() const {
    return m_clock.hasAnchor() ? m_clock.positionAt(nowMs())
                               : m_snap.transport.positionMs;
}

void RemoteHost::onEvent(const QByteArray &event, const QByteArray &data) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    const QString prevFile = m_snap.track.filename;
    const RemotePlaylist prevPlaylist = m_snap.playlist;
    const ApplyResult r = applyEvent(event, doc.object(), &m_snap);
    if (r == ApplyResult::NeedsResync) {
        resync();
        return;
    }
    if (r != ApplyResult::Applied) return;

    if (event == "state" || event == "transport") {
        anchorFromSnapshot();
        emit playbackStateChanged();
    }
    if (event == "state" || event == "track") {
        if (m_snap.track.filename != prevFile) {
            emit sourceChanged();
            fetchArt();
        }
        emit metaDataChanged();
    }
    if (event == "state" || event == "playlist") {
        if (!(m_snap.playlist == prevPlaylist)) emit playlistChanged();
    }
}

void RemoteHost::sendCmd(const QString &op, const QJsonObject &args) {
    QJsonObject cmd{{QStringLiteral("op"), op}};
    if (!args.isEmpty()) cmd.insert(QStringLiteral("args"), args);
    m_transport->postJson(endpoint("cmd"), cmd,
                          [this, op](bool ok, QJsonObject reply) {
        if (!ok || !reply.value(QLatin1String("ok")).toBool()) {
            // A rejected command (stale playlist revision, gated path)
            // means our cache lied: re-snapshot and repaint the truth.
            if (qEnvironmentVariableIntValue("QTAMP_TRACE_REMOTE") == 1) {
                fprintf(stderr, "qtamp: remote cmd %s rejected: %s\n",
                        op.toLocal8Bit().constData(),
                        QJsonDocument(reply)
                            .toJson(QJsonDocument::Compact)
                            .constData());
            }
            resync();
        }
    });
}

void RemoteHost::fetchArt() {
    const QString file = m_snap.track.filename;
    if (file == m_artForFile) return;
    m_artForFile = file;
    m_transport->getBytes(endpoint("art/current"),
                          [this, file](bool ok, QByteArray body) {
        if (m_snap.track.filename != file) return;  // raced a track change
        m_art = ok ? QImage::fromData(body) : QImage();
    });
}

QString RemoteHost::playItemMetaData(const QString &field) const {
    const QString f = field.toLower();
    if (f == QLatin1String("title")) return m_snap.track.title;
    if (f == QLatin1String("artist")) return m_snap.track.artist;
    if (f == QLatin1String("album")) return m_snap.track.album;
    if (f == QLatin1String("length")) {
        return m_snap.transport.durationMs > 0
                   ? QString::number(m_snap.transport.durationMs)
                   : QString();
    }
    return QString();
}

double RemoteHost::sliderPosition(const QString &action,
                                  const QString &param) const {
    if (action.compare(QLatin1String("EQ_BAND"), Qt::CaseInsensitive) == 0) {
        const int band = param.toInt();
        if (band >= 0 && band < m_snap.eq.bands.size())
            return double(m_snap.eq.bands[band]) / 63.0;
        return 31.0 / 63.0;
    }
    if (action.compare(QLatin1String("PAN"), Qt::CaseInsensitive) == 0)
        return m_snap.transport.pan;
    if (action.compare(QLatin1String("VOLUME"), Qt::CaseInsensitive) == 0)
        return qBound(0.0, m_snap.transport.volume / 100.0, 1.0);
    if (action.compare(QLatin1String("SEEK"), Qt::CaseInsensitive) == 0) {
        const qint64 dur = m_snap.transport.durationMs;
        return dur > 0 ? qBound(0.0, double(positionMs()) / dur, 1.0) : 0.0;
    }
    return Host::sliderPosition(action);
}

void RemoteHost::setSliderPosition(const QString &action, double v,
                                   const QString &param) {
    // Optimistic echo first (the server event confirms or corrects),
    // then the command.
    if (action.compare(QLatin1String("EQ_BAND"), Qt::CaseInsensitive) == 0) {
        const int band = param.toInt();
        const int value = qBound(0, qRound(v * 63.0), 63);
        if (band >= 0 && band < m_snap.eq.bands.size())
            m_snap.eq.bands[band] = value;
        sendCmd(QStringLiteral("setEqBand"),
                {{QStringLiteral("band"), band},
                 {QStringLiteral("value"), value}});
        return;
    }
    if (action.compare(QLatin1String("PAN"), Qt::CaseInsensitive) == 0) {
        m_snap.transport.pan = qBound(0.0, v, 1.0);
        sendCmd(QStringLiteral("setPan"),
                {{QStringLiteral("v"), m_snap.transport.pan}});
        return;
    }
    if (action.compare(QLatin1String("VOLUME"), Qt::CaseInsensitive) == 0) {
        setVolume(qRound(qBound(0.0, v, 1.0) * 100.0));
        return;
    }
    if (action.compare(QLatin1String("SEEK"), Qt::CaseInsensitive) == 0) {
        seekMs(qint64(qBound(0.0, v, 1.0) * m_snap.transport.durationMs));
        return;
    }
    Host::setSliderPosition(action, v);
}

void RemoteHost::play() {
    m_snap.transport.playing = true;
    m_snap.transport.paused = false;
    anchorFromSnapshot();
    emit playbackStateChanged();
    sendCmd(QStringLiteral("play"), {});
}

void RemoteHost::pause() {
    m_snap.transport.paused = true;
    anchorFromSnapshot();
    emit playbackStateChanged();
    sendCmd(QStringLiteral("pause"), {});
}

void RemoteHost::stop() {
    m_snap.transport.playing = false;
    m_snap.transport.paused = false;
    m_snap.transport.positionMs = 0;
    anchorFromSnapshot();
    emit playbackStateChanged();
    sendCmd(QStringLiteral("stop"), {});
}

void RemoteHost::seekMs(qint64 ms) {
    m_snap.transport.positionMs = qMax<qint64>(0, ms);
    anchorFromSnapshot();
    sendCmd(QStringLiteral("seek"),
            {{QStringLiteral("ms"), double(m_snap.transport.positionMs)}});
}

void RemoteHost::setVolume(int v) {
    m_snap.transport.volume = qBound(0, v, 100);
    sendCmd(QStringLiteral("setVolume"),
            {{QStringLiteral("v"), m_snap.transport.volume}});
}

void RemoteHost::playlistSetCurrentRow(int row) {
    // No optimistic echo: acting on a stale row mapping is worse than a
    // round-trip highlight delay. The revision guard rejects races.
    sendCmd(QStringLiteral("playlistSetCurrentRow"),
            {{QStringLiteral("row"), row},
             {QStringLiteral("expectPlaylistRevision"),
              double(m_snap.playlist.revision)}});
}

void RemoteHost::playlistPlayRow(int row) {
    sendCmd(QStringLiteral("playlistPlayRow"),
            {{QStringLiteral("row"), row},
             {QStringLiteral("expectPlaylistRevision"),
              double(m_snap.playlist.revision)}});
}

}  // namespace qtamp

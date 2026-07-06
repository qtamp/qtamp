#include "backendserver.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>

#include "playerhost.h"

namespace qtamp {

namespace {

struct ConnState {
    QByteArray buf;
};

constexpr int kMaxBody = 64 * 1024;
constexpr int kMaxHeader = 8 * 1024;

QByteArray statusText(int status) {
    switch (status) {
        case 200: return QByteArrayLiteral("OK");
        case 304: return QByteArrayLiteral("Not Modified");
        case 400: return QByteArrayLiteral("Bad Request");
        case 404: return QByteArrayLiteral("Not Found");
        case 405: return QByteArrayLiteral("Method Not Allowed");
        case 413: return QByteArrayLiteral("Payload Too Large");
        default:  return QByteArrayLiteral("Error");
    }
}

QByteArray sseFrame(const QByteArray &event, const QJsonObject &payload) {
    return "event: " + event + "\ndata: " +
           QJsonDocument(payload).toJson(QJsonDocument::Compact) + "\n\n";
}

}  // namespace

BackendServer::BackendServer(PlayerHost *host, Hooks hooks, QObject *parent)
    : QObject(parent),
      m_host(host),
      m_hooks(std::move(hooks)),
      m_epoch(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    m_clock.start();

    // Prompt pushes on the host's change notifications; the timer only
    // catches signal-less drift (QtampHost's own transport flags flip
    // without a Qt signal — the pump is not a QMediaPlayer).
    connect(host, &PlayerHost::sourceChanged, this,
            &BackendServer::pushChanges);
    connect(host, &PlayerHost::metaDataChanged, this,
            &BackendServer::pushChanges);
    connect(host, &PlayerHost::playbackStateChanged, this,
            &BackendServer::pushChanges);
    connect(host, &PlayerHost::playlistChanged, this, [this]() {
        m_playlistDirty = true;
        pushChanges();
    });
    auto *tick = new QTimer(this);
    tick->setInterval(250);
    connect(tick, &QTimer::timeout, this, &BackendServer::pushChanges);
    tick->start();

    // SSE keepalive: a clock beacon every 5 s that also defeats idle
    // timeouts along the public proxy chain.
    auto *ping = new QTimer(this);
    ping->setInterval(5000);
    connect(ping, &QTimer::timeout, this, [this]() {
        const QByteArray frame = sseFrame(
            QByteArrayLiteral("ping"),
            {{QStringLiteral("serverNowMs"), double(m_clock.elapsed())}});
        for (QTcpSocket *s : std::as_const(m_eventSinks)) s->write(frame);
    });
    ping->start();
}

bool BackendServer::listen(quint16 port) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this,
            &BackendServer::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, port)) return false;
    // Test hook and ops breadcrumb; port 0 resolves to the real one here.
    fprintf(stderr, "qtamp: backend listening on 127.0.0.1:%u\n",
            unsigned(m_server->serverPort()));
    return true;
}

quint16 BackendServer::port() const {
    return m_server ? m_server->serverPort() : 0;
}

RemoteSnapshot BackendServer::buildSnapshot() const {
    RemoteSnapshot s;
    s.epoch = m_epoch;
    s.revision = m_revision;
    s.serverNowMs = m_clock.elapsed();

    s.transport.playing = m_host->isPlaying();
    s.transport.paused = m_host->isPaused();
    s.transport.positionMs = m_host->positionMs();
    s.transport.positionAtMs = s.serverNowMs;
    s.transport.durationMs = m_host->durationMs();
    s.transport.volume = m_host->volume();
    s.transport.pan = m_host->sliderPosition(QStringLiteral("PAN"));

    s.track.title = m_host->songTitle();
    s.track.artist = m_host->playItemMetaData(QStringLiteral("artist"));
    s.track.album = m_host->playItemMetaData(QStringLiteral("album"));
    s.track.filename = m_host->songPath();
    s.track.displayTitle = m_host->playItemDisplayTitle();
    s.track.decoder = m_host->decoderName();
    s.track.bitrate = m_host->bitrate();
    s.track.sampleRate = m_host->sampleRate();
    s.track.channels = m_host->channelCount();

    s.playlist.revision = m_playlistRevision;
    s.playlist.currentIndex = m_host->playlistCurrentRow();
    const int count = m_host->playlistRowCount();
    s.playlist.rows.reserve(count);
    for (int i = 0; i < count; ++i) {
        s.playlist.rows.append({m_host->playlistRowText(i),
                                m_host->playlistRowDurationMs(i)});
    }

    s.eq.on = m_hooks.eqOn ? m_hooks.eqOn() : false;
    s.eq.autoOn = m_hooks.eqAuto ? m_hooks.eqAuto() : false;
    for (int b = 0; b < 10; ++b) {
        const double p = m_host->sliderPosition(QStringLiteral("EQ_BAND"),
                                                QString::number(b));
        s.eq.bands[b] = p < 0.0 ? 31 : qBound(0, qRound(p * 63.0), 63);
    }
    return s;
}

QJsonObject BackendServer::sectionEnvelope(const char *section,
                                           const QJsonObject &body) const {
    return {{QStringLiteral("epoch"), m_epoch},
            {QStringLiteral("revision"), double(m_revision)},
            {QStringLiteral("serverNowMs"), double(m_clock.elapsed())},
            {QString::fromLatin1(section), body}};
}

void BackendServer::pushChanges() {
    const RemoteSnapshot s = buildSnapshot();
    const QJsonObject doc = serializeSnapshot(s);

    // Per-section fingerprints; the transport one EXCLUDES position, so
    // ordinary playback never pushes — clients interpolate, and edges
    // (play/pause/seek/track change) show up as a changed fingerprint
    // because position jumps relative to the anchored sample only via
    // the sections below.
    QJsonObject tNoPos = doc.value(QLatin1String("transport")).toObject();
    tNoPos.remove(QStringLiteral("positionMs"));
    tNoPos.remove(QStringLiteral("positionAtMs"));
    const QByteArray fpT = QJsonDocument(tNoPos).toJson(QJsonDocument::Compact);
    const QByteArray fpTr = QJsonDocument(
        doc.value(QLatin1String("track")).toObject())
        .toJson(QJsonDocument::Compact);
    QJsonObject plDoc = doc.value(QLatin1String("playlist")).toObject();
    const QByteArray fpP =
        QJsonDocument(plDoc).toJson(QJsonDocument::Compact);
    const QByteArray fpE = QJsonDocument(
        doc.value(QLatin1String("eq")).toObject())
        .toJson(QJsonDocument::Compact);

    const bool tChanged = fpT != m_fpTransport;
    const bool trChanged = fpTr != m_fpTrack;
    const bool pChanged = m_playlistDirty || fpP != m_fpPlaylist;
    const bool eChanged = fpE != m_fpEq;
    if (!tChanged && !trChanged && !pChanged && !eChanged) return;

    if (pChanged) {
        ++m_playlistRevision;
        m_playlistDirty = false;
    }
    ++m_revision;

    // Rebuild with the bumped revisions so events and snapshots agree.
    const RemoteSnapshot fresh = buildSnapshot();
    const QJsonObject freshDoc = serializeSnapshot(fresh);
    if (tChanged) {
        broadcast(QByteArrayLiteral("transport"),
                  sectionEnvelope("transport",
                                  freshDoc.value(QLatin1String("transport"))
                                      .toObject()));
    }
    if (trChanged) {
        broadcast(QByteArrayLiteral("track"),
                  sectionEnvelope("track",
                                  freshDoc.value(QLatin1String("track"))
                                      .toObject()));
    }
    if (pChanged) {
        broadcast(QByteArrayLiteral("playlist"),
                  sectionEnvelope("playlist",
                                  freshDoc.value(QLatin1String("playlist"))
                                      .toObject()));
    }
    if (eChanged) {
        broadcast(QByteArrayLiteral("eq"),
                  sectionEnvelope("eq",
                                  freshDoc.value(QLatin1String("eq"))
                                      .toObject()));
    }
    m_fpTransport = fpT;
    m_fpTrack = fpTr;
    m_fpPlaylist = fpP;
    m_fpEq = fpE;
}

void BackendServer::broadcast(const QByteArray &event,
                              const QJsonObject &payload) {
    if (m_eventSinks.isEmpty()) return;
    const QByteArray frame = sseFrame(event, payload);
    for (QTcpSocket *s : std::as_const(m_eventSinks)) s->write(frame);
}

bool BackendServer::pathAllowed(const QString &path) const {
    if (m_hooks.musicRoot.isEmpty()) return false;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    const QString root = QFileInfo(m_hooks.musicRoot).canonicalFilePath();
    if (canonical.isEmpty() || root.isEmpty()) return false;
    return canonical == root ||
           canonical.startsWith(root + QLatin1Char('/'));
}

void BackendServer::onNewConnection() {
    while (QTcpSocket *sock = m_server->nextPendingConnection()) {
        auto *state = new ConnState();
        connect(sock, &QTcpSocket::disconnected, sock,
                &QTcpSocket::deleteLater);
        connect(sock, &QObject::destroyed, this, [this, state, sock]() {
            m_eventSinks.removeAll(sock);
            delete state;
        });
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, state]() {
            state->buf.append(sock->readAll());
            if (state->buf.size() > kMaxHeader + kMaxBody) {
                respond(sock, 413, QByteArrayLiteral("text/plain"),
                        QByteArrayLiteral("too large"));
                return;
            }
            const int headerEnd = state->buf.indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QList<QByteArray> lines =
                state->buf.left(headerEnd).split('\n');
            const QList<QByteArray> req = lines.value(0).trimmed().split(' ');
            if (req.size() < 2) {
                respond(sock, 400, QByteArrayLiteral("text/plain"),
                        QByteArrayLiteral("bad request"));
                return;
            }
            int contentLength = 0;
            QByteArray ifNoneMatch;
            for (int i = 1; i < lines.size(); ++i) {
                const QByteArray line = lines[i].trimmed();
                const int colon = line.indexOf(':');
                if (colon < 0) continue;
                const QByteArray key = line.left(colon).toLower();
                const QByteArray value = line.mid(colon + 1).trimmed();
                if (key == "content-length") contentLength = value.toInt();
                else if (key == "if-none-match") ifNoneMatch = value;
            }
            if (contentLength < 0 || contentLength > kMaxBody) {
                respond(sock, 413, QByteArrayLiteral("text/plain"),
                        QByteArrayLiteral("body too large"));
                return;
            }
            const int total = headerEnd + 4 + contentLength;
            if (state->buf.size() < total) return;
            const QByteArray body =
                state->buf.mid(headerEnd + 4, contentLength);
            handleRequest(sock, req[0], req[1], ifNoneMatch, body);
        });
    }
}

void BackendServer::handleRequest(QTcpSocket *sock, const QByteArray &method,
                                  const QByteArray &path,
                                  const QByteArray &ifNoneMatch,
                                  const QByteArray &body) {
    if (path == "/state" && method == "GET") {
        respondJson(sock, 200, serializeSnapshot(buildSnapshot()));
        return;
    }

    if (path == "/events" && method == "GET") {
        // Long-lived SSE sink: headers now, first frame = full state,
        // then pushed events until the peer hangs up.
        sock->write(
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-store, no-transform\r\n"
            "Connection: keep-alive\r\n\r\n");
        sock->write(sseFrame(QByteArrayLiteral("state"),
                             serializeSnapshot(buildSnapshot())));
        m_eventSinks.append(sock);
        return;
    }

    if (path == "/art/current" && method == "GET") {
        const QImage art = m_host->albumArt();
        if (art.isNull()) {
            respond(sock, 404, QByteArrayLiteral("text/plain"),
                    QByteArrayLiteral("no art"));
            return;
        }
        const QByteArray etag =
            '"' + QByteArray::number(qHash(m_host->songPath())) + '"';
        if (!ifNoneMatch.isEmpty() && ifNoneMatch == etag) {
            respond(sock, 304, QByteArray(), QByteArray(), etag);
            return;
        }
        QByteArray png;
        QBuffer out(&png);
        out.open(QIODevice::WriteOnly);
        art.save(&out, "PNG");
        respond(sock, 200, QByteArrayLiteral("image/png"), png, etag);
        return;
    }

    if (path == "/cmd" && method == "POST") {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            respondJson(sock, 400,
                        {{QStringLiteral("ok"), false},
                         {QStringLiteral("error"),
                          QStringLiteral("bad json")}});
            return;
        }
        bool ok = false;
        QJsonObject reply = handleCmd(doc.object(), &ok);
        respondJson(sock, ok ? 200 : 400, reply);
        return;
    }

    respond(sock, 404, QByteArrayLiteral("text/plain"),
            QByteArrayLiteral("not found"));
}

QJsonObject BackendServer::handleCmd(const QJsonObject &cmd, bool *ok) {
    const QString op = cmd.value(QLatin1String("op")).toString();
    const QJsonObject args = cmd.value(QLatin1String("args")).toObject();
    *ok = true;

    auto fail = [&](const char *why) {
        *ok = false;
        return QJsonObject{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"),
                            QString::fromLatin1(why)}};
    };
    auto checkPlaylistRevision = [&]() -> bool {
        const QJsonValue expect =
            args.value(QLatin1String("expectPlaylistRevision"));
        return expect.isUndefined() ||
               quint64(expect.toDouble()) == m_playlistRevision;
    };

    if (op == QLatin1String("play")) {
        // Cold start plays the playlist's current row — the same thing a
        // double-click does — so autoplay works before any UI touch.
        if (!m_host->isPaused() && !m_host->isPlaying() &&
            m_host->playlistRowCount() > 0) {
            const int row = qBound(0, m_host->playlistCurrentRow(),
                                   m_host->playlistRowCount() - 1);
            m_host->playlistPlayRow(row);
        } else {
            m_host->play();
        }
    } else if (op == QLatin1String("pause")) {
        m_host->pause();
    } else if (op == QLatin1String("stop")) {
        m_host->stop();
    } else if (op == QLatin1String("next")) {
        m_host->next();
    } else if (op == QLatin1String("prev")) {
        m_host->prev();
    } else if (op == QLatin1String("seek")) {
        m_host->seekMs(qint64(args.value(QLatin1String("ms")).toDouble()));
    } else if (op == QLatin1String("setVolume")) {
        m_host->setVolume(
            qBound(0, args.value(QLatin1String("v")).toInt(), 100));
    } else if (op == QLatin1String("setPan")) {
        m_host->setSliderPosition(
            QStringLiteral("PAN"),
            qBound(0.0, args.value(QLatin1String("v")).toDouble(0.5), 1.0));
    } else if (op == QLatin1String("setEqOn")) {
        if (!m_hooks.setEqOn) return fail("unsupported");
        m_hooks.setEqOn(args.value(QLatin1String("on")).toBool());
    } else if (op == QLatin1String("setEqAuto")) {
        if (!m_hooks.setEqAuto) return fail("unsupported");
        m_hooks.setEqAuto(args.value(QLatin1String("on")).toBool());
    } else if (op == QLatin1String("setEqBand")) {
        const int band = args.value(QLatin1String("band")).toInt(-1);
        if (band < 0 || band > 9) return fail("bad band");
        const int value =
            qBound(0, args.value(QLatin1String("value")).toInt(31), 63);
        m_host->setSliderPosition(QStringLiteral("EQ_BAND"), value / 63.0,
                                  QString::number(band));
    } else if (op == QLatin1String("setEqPreamp")) {
        // No preamp axis in the current DSP; accepted for forward
        // compatibility, ignored.
    } else if (op == QLatin1String("playlistPlayRow") ||
               op == QLatin1String("playlistSetCurrentRow")) {
        if (!checkPlaylistRevision())
            return fail("playlistRevision mismatch");
        const int row = args.value(QLatin1String("row")).toInt(-1);
        if (row < 0 || row >= m_host->playlistRowCount())
            return fail("bad row");
        if (op == QLatin1String("playlistPlayRow"))
            m_host->playlistPlayRow(row);
        else
            m_host->playlistSetCurrentRow(row);
    } else if (op == QLatin1String("playlistAddPaths")) {
        const QJsonArray paths = args.value(QLatin1String("paths")).toArray();
        if (paths.isEmpty()) return fail("no paths");
        for (const QJsonValue &v : paths) {
            const QString p = v.toString();
            if (!pathAllowed(p)) return fail("path outside music root");
        }
        for (const QJsonValue &v : paths)
            m_host->enqueueAndPlay(QUrl::fromLocalFile(v.toString()),
                                   /*enqueueOnly=*/true);
    } else if (op == QLatin1String("playlistRemoveRows")) {
        if (!m_hooks.playlistRemoveRows) return fail("unsupported");
        if (!checkPlaylistRevision())
            return fail("playlistRevision mismatch");
        QList<int> rows;
        for (const QJsonValue &v :
             args.value(QLatin1String("rows")).toArray())
            rows.append(v.toInt(-1));
        m_hooks.playlistRemoveRows(rows);
    } else if (op == QLatin1String("playlistClear")) {
        if (!m_hooks.playlistClear) return fail("unsupported");
        m_hooks.playlistClear();
    } else if (op == QLatin1String("open")) {
        const QUrl u(args.value(QLatin1String("url")).toString());
        if (!u.isLocalFile() || !pathAllowed(u.toLocalFile()))
            return fail("path outside music root");
        m_host->openPath(u);
    } else {
        return fail("unknown op");
    }

    // Apply-time push: the mutation's effect reaches subscribers before
    // the HTTP reply is even read.
    pushChanges();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("revision"), double(m_revision)}};
}

void BackendServer::respond(QTcpSocket *sock, int status,
                            const QByteArray &type, const QByteArray &body,
                            const QByteArray &etag) {
    QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + ' ' +
                   statusText(status) + "\r\n";
    if (!type.isEmpty()) r += "Content-Type: " + type + "\r\n";
    if (!etag.isEmpty()) r += "ETag: " + etag + "\r\n";
    r += "Cache-Control: no-store\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Connection: close\r\n\r\n";
    r += body;
    sock->write(r);
    sock->disconnectFromHost();
}

void BackendServer::respondJson(QTcpSocket *sock, int status,
                                const QJsonObject &o) {
    respond(sock, status, QByteArrayLiteral("application/json"),
            QJsonDocument(o).toJson(QJsonDocument::Compact));
}

}  // namespace qtamp

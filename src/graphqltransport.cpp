#include "graphqltransport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace qtamp {

// ── shared translation ──────────────────────────────────────────────

const char *GraphQLTransportBase::kPlayerQuery =
    "{player{epoch revision serverNowMs "
    "transport{playing paused positionMs positionAtMs durationMs volume pan} "
    "track{title artist album filename displayTitle decoder bitrate "
    "sampleRate channels} "
    "playlist{revision currentIndex rowCount rows{index text durationMs}} "
    "eq{on auto preamp bands}}}";

const char *GraphQLTransportBase::kPlayerEventsSub =
    "subscription{playerEvents{kind epoch revision serverNowMs "
    "transport{playing paused positionMs positionAtMs durationMs volume pan} "
    "track{title artist album filename displayTitle decoder bitrate "
    "sampleRate channels} "
    "eq{on auto preamp bands}}}";

namespace {
int eq63ToMaki(int v) { return qRound(v / 63.0 * 254.0) - 127; }
int eqMakiTo63(int v) { return qRound((v + 127) / 254.0 * 63.0); }

QJsonObject eqToChannel(const QJsonObject &eq) {
    QJsonArray bands63;
    for (const auto &b : eq.value(QStringLiteral("bands")).toArray())
        bands63.append(eqMakiTo63(b.toInt()));
    return {
        {QStringLiteral("on"), eq.value(QStringLiteral("on"))},
        {QStringLiteral("auto"), eq.value(QStringLiteral("auto"))},
        {QStringLiteral("preamp"),
         eqMakiTo63(eq.value(QStringLiteral("preamp")).toInt())},
        {QStringLiteral("bands"), bands63}};
}
}  // namespace

QJsonObject GraphQLTransportBase::requestForCommand(const QJsonObject &cmd) {
    const QString op = cmd.value(QStringLiteral("op")).toString();
    const QJsonObject args = cmd.value(QStringLiteral("args")).toObject();
    const auto plain = [](const char *m) {
        return QStringLiteral("mutation{%1{ok error revision}}")
            .arg(QLatin1String(m));
    };
    QString q;
    if (op == QLatin1String("play") || op == QLatin1String("pause") ||
        op == QLatin1String("stop") || op == QLatin1String("next") ||
        op == QLatin1String("prev") || op == QLatin1String("playlistClear")) {
        const QString field = op == QLatin1String("playlistClear")
                                  ? QStringLiteral("playlistClear")
                                  : op;
        q = plain(field.toUtf8().constData());
    } else if (op == QLatin1String("seek")) {
        q = QStringLiteral("mutation{seek(ms:%1){ok error revision}}")
                .arg(args.value(QStringLiteral("ms")).toDouble(), 0, 'f', 0);
    } else if (op == QLatin1String("setVolume")) {
        q = QStringLiteral("mutation{setVolume(v:%1){ok error revision}}")
                .arg(args.value(QStringLiteral("v")).toInt());
    } else if (op == QLatin1String("setPan")) {
        q = QStringLiteral("mutation{setPan(v:%1){ok error revision}}")
                .arg(args.value(QStringLiteral("v")).toDouble());
    } else if (op == QLatin1String("setEqOn") ||
               op == QLatin1String("setEqAuto")) {
        q = QStringLiteral("mutation{%1(on:%2){ok error revision}}")
                .arg(op,
                     args.value(QStringLiteral("on")).toBool()
                         ? QStringLiteral("true")
                         : QStringLiteral("false"));
    } else if (op == QLatin1String("setEqPreamp")) {
        q = QStringLiteral("mutation{setEqPreamp(value:%1){ok error revision}}")
                .arg(eq63ToMaki(args.value(QStringLiteral("v")).toInt()));
    } else if (op == QLatin1String("setEqBand")) {
        q = QStringLiteral(
                "mutation{setEqBand(band:%1,value:%2){ok error revision}}")
                .arg(args.value(QStringLiteral("band")).toInt())
                .arg(eq63ToMaki(args.value(QStringLiteral("v")).toInt()));
    } else if (op == QLatin1String("playlistPlayRow") ||
               op == QLatin1String("playlistSetCurrentRow")) {
        const QString field = op == QLatin1String("playlistPlayRow")
                                  ? QStringLiteral("playRow")
                                  : QStringLiteral("setCurrentRow");
        const auto exp = args.value(QStringLiteral("expectPlaylistRevision"));
        q = exp.isUndefined() || exp.isNull()
                ? QStringLiteral("mutation{%1(row:%2){ok error revision}}")
                      .arg(field)
                      .arg(args.value(QStringLiteral("row")).toInt())
                : QStringLiteral("mutation{%1(row:%2,expectPlaylistRevision:"
                                 "%3){ok error revision}}")
                      .arg(field)
                      .arg(args.value(QStringLiteral("row")).toInt())
                      .arg(exp.toDouble(), 0, 'f', 0);
    } else if (op == QLatin1String("playlistAddPaths")) {
        QStringList items;
        for (const auto &p : args.value(QStringLiteral("paths")).toArray())
            items << QStringLiteral("\"%1\"")
                         .arg(p.toString()
                                  .replace(QLatin1Char('\\'),
                                           QLatin1String("\\\\"))
                                  .replace(QLatin1Char('"'),
                                           QLatin1String("\\\"")));
        q = QStringLiteral(
                "mutation{playlistAdd(paths:[%1]){ok error revision}}")
                .arg(items.join(QLatin1Char(',')));
    } else if (op == QLatin1String("playlistRemoveRows")) {
        QStringList items;
        for (const auto &r : args.value(QStringLiteral("rows")).toArray())
            items << QString::number(r.toInt());
        q = QStringLiteral(
                "mutation{playlistRemove(rows:[%1]){ok error revision}}")
                .arg(items.join(QLatin1Char(',')));
    } else if (op == QLatin1String("open")) {
        q = QStringLiteral("mutation{open(url:\"%1\"){ok error revision}}")
                .arg(args.value(QStringLiteral("url")).toString());
    } else {
        return {};
    }
    return {{QStringLiteral("query"), q}};
}

QJsonObject GraphQLTransportBase::replyFromResult(const QJsonObject &gqlData,
                                                  QString *fieldOut) {
    // data has exactly one root field: the mutation's CommandResult.
    for (auto it = gqlData.begin(); it != gqlData.end(); ++it) {
        if (fieldOut) *fieldOut = it.key();
        const QJsonObject r = it.value().toObject();
        QJsonObject reply{
            {QStringLiteral("ok"), r.value(QStringLiteral("ok")).toBool()},
            {QStringLiteral("revision"),
             r.value(QStringLiteral("revision"))}};
        if (!r.value(QStringLiteral("error")).isNull())
            reply.insert(QStringLiteral("error"),
                         r.value(QStringLiteral("error")));
        return reply;
    }
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("empty result")}};
}

QJsonObject GraphQLTransportBase::channelDocFromPlayer(
    const QJsonObject &player) {
    QJsonObject doc;
    doc.insert(QStringLiteral("epoch"), player.value(QStringLiteral("epoch")));
    doc.insert(QStringLiteral("revision"),
               player.value(QStringLiteral("revision")));
    doc.insert(QStringLiteral("serverNowMs"),
               player.value(QStringLiteral("serverNowMs")));
    doc.insert(QStringLiteral("transport"),
               player.value(QStringLiteral("transport")));
    if (player.value(QStringLiteral("track")).isObject())
        doc.insert(QStringLiteral("track"),
                   player.value(QStringLiteral("track")));
    const QJsonObject pl = player.value(QStringLiteral("playlist")).toObject();
    QJsonArray rows;
    for (const auto &r : pl.value(QStringLiteral("rows")).toArray()) {
        const QJsonObject ro = r.toObject();
        rows.append(QJsonObject{
            {QStringLiteral("text"), ro.value(QStringLiteral("text"))},
            {QStringLiteral("durationMs"),
             ro.value(QStringLiteral("durationMs"))}});
    }
    doc.insert(QStringLiteral("playlist"),
               QJsonObject{{QStringLiteral("revision"),
                            pl.value(QStringLiteral("revision"))},
                           {QStringLiteral("count"),
                            pl.value(QStringLiteral("rowCount"))},
                           {QStringLiteral("currentIndex"),
                            pl.value(QStringLiteral("currentIndex"))},
                           {QStringLiteral("rows"), rows}});
    doc.insert(QStringLiteral("eq"),
               eqToChannel(player.value(QStringLiteral("eq")).toObject()));
    return doc;
}

bool GraphQLTransportBase::channelEventFromPlayerEvent(const QJsonObject &ev,
                                                       QByteArray *nameOut,
                                                       QJsonObject *docOut) {
    const QString kind = ev.value(QStringLiteral("kind")).toString();
    QJsonObject doc{{QStringLiteral("epoch"),
                     ev.value(QStringLiteral("epoch"))},
                    {QStringLiteral("revision"),
                     ev.value(QStringLiteral("revision"))},
                    {QStringLiteral("serverNowMs"),
                     ev.value(QStringLiteral("serverNowMs"))}};
    if (kind == QLatin1String("PING")) {
        *nameOut = QByteArrayLiteral("ping");
        *docOut = doc;
        return true;
    }
    if (kind == QLatin1String("TRANSPORT")) {
        doc.insert(QStringLiteral("transport"),
                   ev.value(QStringLiteral("transport")));
        *nameOut = QByteArrayLiteral("transport");
        *docOut = doc;
        return true;
    }
    if (kind == QLatin1String("TRACK")) {
        doc.insert(QStringLiteral("track"), ev.value(QStringLiteral("track")));
        *nameOut = QByteArrayLiteral("track");
        *docOut = doc;
        return true;
    }
    if (kind == QLatin1String("EQ")) {
        doc.insert(QStringLiteral("eq"),
                   eqToChannel(ev.value(QStringLiteral("eq")).toObject()));
        *nameOut = QByteArrayLiteral("eq");
        *docOut = doc;
        return true;
    }
    // STATE / PLAYLIST_META (rows travel separately): full re-snapshot.
    return false;
}

// ── HTTP flavor ─────────────────────────────────────────────────────

GraphQLHttpTransport::GraphQLHttpTransport(QObject *parent)
    : GraphQLTransportBase(parent),
      m_nam(new QNetworkAccessManager(this)) {
    m_sse.onEvent = [this](QByteArray event, QByteArray data) {
        handleSseEvent(event, data);
    };
}

GraphQLHttpTransport::~GraphQLHttpTransport() = default;

QUrl GraphQLHttpTransport::graphqlEndpoint(const QUrl &base) const {
    QUrl u = base;
    QString path = u.path();
    // RemoteHost appends channel paths (/state, /cmd, /events, /art/..);
    // strip them back to the service root, then target /graphql.
    for (const char *suffix : {"/state", "/cmd", "/events"}) {
        if (path.endsWith(QLatin1String(suffix))) {
            path.chop(int(qstrlen(suffix)));
            break;
        }
    }
    const int art = path.indexOf(QLatin1String("/art/"));
    if (art >= 0) path.truncate(art);
    u.setPath(path + QStringLiteral("/graphql"));
    u.setQuery(QString());
    return u;
}

void GraphQLHttpTransport::execute(
    const QUrl &base, const QJsonObject &gqlBody,
    std::function<void(bool, QJsonObject)> cb) {
    QNetworkRequest req(graphqlEndpoint(base));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    for (const auto &h : m_headers) req.setRawHeader(h.first, h.second);
    QNetworkReply *r = m_nam->post(
        req, QJsonDocument(gqlBody).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [r, cb]() {
        r->deleteLater();
        const QJsonObject root =
            QJsonDocument::fromJson(r->readAll()).object();
        const bool ok = r->error() == QNetworkReply::NoError &&
                        root.contains(QStringLiteral("data"));
        cb(ok, root.value(QStringLiteral("data")).toObject());
    });
}

void GraphQLHttpTransport::postJson(const QUrl &url, const QJsonObject &body,
                                    JsonCallback cb) {
    m_base = url;
    const QJsonObject gql = requestForCommand(body);
    if (gql.isEmpty()) {
        if (cb)
            cb(true, QJsonObject{{QStringLiteral("ok"), false},
                                 {QStringLiteral("error"),
                                  QStringLiteral("unsupported op")}});
        return;
    }
    execute(url, gql, [cb](bool ok, QJsonObject data) {
        if (!cb) return;
        if (!ok) {
            cb(false, {});
            return;
        }
        cb(true, replyFromResult(data));
    });
}

void GraphQLHttpTransport::getJson(const QUrl &url, JsonCallback cb) {
    m_base = url;
    execute(url, {{QStringLiteral("query"), QLatin1String(kPlayerQuery)}},
            [cb](bool ok, QJsonObject data) {
                if (!cb) return;
                if (!ok || !data.value(QStringLiteral("player")).isObject()) {
                    cb(false, {});
                    return;
                }
                cb(true, channelDocFromPlayer(
                             data.value(QStringLiteral("player")).toObject()));
            });
}

void GraphQLHttpTransport::getBytes(const QUrl &url, BytesCallback cb) {
    QNetworkRequest req(url);
    for (const auto &h : m_headers) req.setRawHeader(h.first, h.second);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [r, cb]() {
        r->deleteLater();
        if (cb) cb(r->error() == QNetworkReply::NoError, r->readAll());
    });
}

void GraphQLHttpTransport::openEventStream(const QUrl &url) {
    m_base = url;
    m_closing = false;
#ifdef Q_OS_WASM
    // Browser: GET-style graphql-sse subscribe URL via the native
    // EventSource glue (QNetworkReply buffers streams on wasm).
    {
        QUrl u = graphqlEndpoint(url);
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("query"),
                       QString::fromLatin1(kPlayerEventsSub));
        u.setQuery(q);
        wasmEsOpen(this, u);
        emit streamStateChanged(true);
        return;
    }
#endif
    if (m_stream) {
        m_stream->abort();
        m_stream = nullptr;
    }
    QNetworkRequest req(graphqlEndpoint(url));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setRawHeader("Accept", "text/event-stream");
    for (const auto &h : m_headers) req.setRawHeader(h.first, h.second);
    m_sse.reset();
    m_stream = m_nam->post(
        req, QJsonDocument(QJsonObject{{QStringLiteral("query"),
                                        QLatin1String(kPlayerEventsSub)}})
                 .toJson(QJsonDocument::Compact));
    emit streamStateChanged(true);
    connect(m_stream, &QNetworkReply::readyRead, this, [this]() {
        if (m_stream) {
            m_sse.feed(m_stream->readAll());
            m_backoffMs = 500;
        }
    });
    connect(m_stream, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_stream;
        m_stream = nullptr;
        if (r) r->deleteLater();
        emit streamStateChanged(false);
        if (!m_closing) scheduleReconnect();
    });
}

void GraphQLHttpTransport::handleSseEvent(const QByteArray &event,
                                          const QByteArray &data) {
    if (event != "next") return;  // graphql-sse: next/complete + pings
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QJsonObject ev = root.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("playerEvents"))
                               .toObject();
    if (ev.isEmpty()) return;
    QByteArray name;
    QJsonObject doc;
    if (channelEventFromPlayerEvent(ev, &name, &doc)) {
        emit eventReceived(name,
                           QJsonDocument(doc).toJson(QJsonDocument::Compact));
    } else {
        reemitSnapshot();
    }
}

void GraphQLHttpTransport::reemitSnapshot() {
    // STATE / playlist-shaped change: fetch the full player (with rows)
    // and re-emit as a channel "state" event — applyEvent full-replace.
    getJson(m_base, [this](bool ok, QJsonObject doc) {
        if (ok)
            emit eventReceived(
                QByteArrayLiteral("state"),
                QJsonDocument(doc).toJson(QJsonDocument::Compact));
    });
}

void GraphQLHttpTransport::scheduleReconnect() {
    const int delay = m_backoffMs;
    m_backoffMs = qMin(m_backoffMs * 2, 8000);
    QTimer::singleShot(delay, this, [this]() {
        if (!m_closing && !m_stream && m_base.isValid())
            openEventStream(m_base);
    });
}

void GraphQLHttpTransport::closeEventStream() {
    m_closing = true;
#ifdef Q_OS_WASM
    wasmEsClose(this);
#endif
    if (m_stream) {
        m_stream->abort();
        m_stream = nullptr;
    }
}

// ── unix-socket flavor ──────────────────────────────────────────────

GraphQLLocalTransport::GraphQLLocalTransport(const QString &socketPath,
                                             QObject *parent)
    : GraphQLTransportBase(parent), m_socketPath(socketPath) {
    m_sse.onEvent = [this](QByteArray event, QByteArray data) {
        handleSseEvent(event, data);
    };
}

GraphQLLocalTransport::~GraphQLLocalTransport() = default;

QByteArray GraphQLLocalTransport::buildHttpRequest(const QByteArray &path,
                                                   const QByteArray &body,
                                                   bool sse) {
    QByteArray req;
    req += "POST " + path + " HTTP/1.1\r\n";
    req += "Host: qtwasabi.local\r\n";
    req += "Content-Type: application/json\r\n";
    if (sse) req += "Accept: text/event-stream\r\n";
    req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;
    return req;
}

void GraphQLLocalTransport::request(
    const QByteArray &rawRequest,
    std::function<void(bool, int, QByteArray)> done) {
    // Per-request state on the heap, freed exactly once: completion,
    // disconnect and error can all fire for the same request, so
    // finish() latches.
    struct Req {
        QByteArray buf;
        QByteArray payload;
        bool headersDone = false;
        bool chunked = false;
        int status = 0;
        qint64 contentLength = -1;
        qint64 chunkRemaining = 0;
        bool finished = false;
    };
    auto *sock = new QLocalSocket(this);
    auto *rq = new Req;
    auto *buf = &rq->buf;
    auto *headersDone = &rq->headersDone;
    auto *chunked = &rq->chunked;
    auto *status = &rq->status;
    auto *contentLength = &rq->contentLength;
    auto *payload = &rq->payload;
    auto *chunkRemaining = &rq->chunkRemaining;

    const auto cleanup = [sock, rq]() {
        if (rq->finished) return;
        rq->finished = true;
        sock->disconnect();
        sock->deleteLater();
        // rq stays alive until the socket dies with it.
        QObject::connect(sock, &QObject::destroyed, [rq]() { delete rq; });
    };

    const auto dechunk = [chunkRemaining](QByteArray &in, QByteArray &out) {
        for (;;) {
            if (*chunkRemaining == 0) {
                const int idx = in.indexOf("\r\n");
                if (idx < 0) return;
                bool ok = false;
                const qint64 size = in.left(idx).toLongLong(&ok, 16);
                in.remove(0, idx + 2);
                if (!ok || size == 0) return;
                *chunkRemaining = size;
            }
            const qint64 take = qMin<qint64>(*chunkRemaining, in.size());
            out += in.left(take);
            in.remove(0, take);
            *chunkRemaining -= take;
            if (*chunkRemaining == 0) {
                if (in.size() < 2) return;
                in.remove(0, 2);
            } else {
                return;
            }
        }
    };

    connect(sock, &QLocalSocket::readyRead, this, [=]() {
        *buf += sock->readAll();
        if (!*headersDone) {
            const int idx = buf->indexOf("\r\n\r\n");
            if (idx < 0) return;
            const QList<QByteArray> lines = buf->left(idx).split('\n');
            if (!lines.isEmpty()) {
                const QList<QByteArray> st =
                    lines[0].simplified().split(' ');
                if (st.size() >= 2) *status = st[1].toInt();
            }
            for (const QByteArray &l : lines) {
                const int c = l.indexOf(':');
                if (c < 0) continue;
                const QByteArray key = l.left(c).trimmed().toLower();
                const QByteArray val = l.mid(c + 1).trimmed();
                if (key == "transfer-encoding")
                    *chunked = val.toLower().contains("chunked");
                else if (key == "content-length")
                    *contentLength = val.toLongLong();
            }
            buf->remove(0, idx + 4);
            *headersDone = true;
        }
        if (*chunked) {
            dechunk(*buf, *payload);
        } else {
            *payload += *buf;
            buf->clear();
        }
        if (*contentLength >= 0 && payload->size() >= *contentLength) {
            if (rq->finished) return;
            done(true, *status, *payload);
            cleanup();
        }
    });
    connect(sock, &QLocalSocket::disconnected, this, [=]() {
        if (rq->finished) return;
        if (*headersDone)
            done(true, *status, *payload);
        else
            done(false, 0, {});
        cleanup();
    });
    connect(sock, &QLocalSocket::errorOccurred, this,
            [=](QLocalSocket::LocalSocketError) {
                if (rq->finished) return;
                if (sock->state() != QLocalSocket::ConnectedState &&
                    !*headersDone) {
                    done(false, 0, {});
                    cleanup();
                }
            });
    connect(sock, &QLocalSocket::connected, this,
            [sock, rawRequest]() { sock->write(rawRequest); });
    sock->connectToServer(m_socketPath);
}

void GraphQLLocalTransport::execute(
    const QJsonObject &gqlBody, std::function<void(bool, QJsonObject)> cb) {
    const QByteArray raw = buildHttpRequest(
        QByteArrayLiteral("/graphql"),
        QJsonDocument(gqlBody).toJson(QJsonDocument::Compact), false);
    request(raw, [cb](bool ok, int status, QByteArray body) {
        if (!ok || status != 200) {
            cb(false, {});
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        cb(root.contains(QStringLiteral("data")),
           root.value(QStringLiteral("data")).toObject());
    });
}

void GraphQLLocalTransport::postJson(const QUrl &, const QJsonObject &body,
                                     JsonCallback cb) {
    const QJsonObject gql = requestForCommand(body);
    if (gql.isEmpty()) {
        if (cb)
            cb(true, QJsonObject{{QStringLiteral("ok"), false},
                                 {QStringLiteral("error"),
                                  QStringLiteral("unsupported op")}});
        return;
    }
    execute(gql, [cb](bool ok, QJsonObject data) {
        if (!cb) return;
        if (!ok) {
            cb(false, {});
            return;
        }
        cb(true, replyFromResult(data));
    });
}

void GraphQLLocalTransport::getJson(const QUrl &, JsonCallback cb) {
    execute({{QStringLiteral("query"), QLatin1String(kPlayerQuery)}},
            [cb](bool ok, QJsonObject data) {
                if (!cb) return;
                if (!ok || !data.value(QStringLiteral("player")).isObject()) {
                    cb(false, {});
                    return;
                }
                cb(true, channelDocFromPlayer(
                             data.value(QStringLiteral("player")).toObject()));
            });
}

void GraphQLLocalTransport::getBytes(const QUrl &url, BytesCallback cb) {
    // Art travels over the same socket: GET-equivalent via POST is not
    // valid here, so issue a minimal GET request.
    QByteArray req;
    req += "GET " + url.path().toUtf8() + " HTTP/1.1\r\n";
    req += "Host: qtwasabi.local\r\nConnection: close\r\n\r\n";
    request(req, [cb](bool ok, int status, QByteArray body) {
        if (cb) cb(ok && status == 200, body);
    });
}

void GraphQLLocalTransport::openEventStream(const QUrl &) {
    m_closing = false;
    if (m_streamSock) {
        m_streamSock->abort();
        m_streamSock->deleteLater();
        m_streamSock = nullptr;
    }
    m_streamBuf.clear();
    m_streamHeadersDone = false;
    m_streamChunked = false;
    m_chunkRemaining = 0;
    m_sse.reset();

    m_streamSock = new QLocalSocket(this);
    connect(m_streamSock, &QLocalSocket::connected, this, [this]() {
        m_streamSock->write(buildHttpRequest(
            QByteArrayLiteral("/graphql"),
            QJsonDocument(QJsonObject{{QStringLiteral("query"),
                                       QLatin1String(kPlayerEventsSub)}})
                .toJson(QJsonDocument::Compact),
            true));
        emit streamStateChanged(true);
    });
    connect(m_streamSock, &QLocalSocket::readyRead, this, [this]() {
        m_streamBuf += m_streamSock->readAll();
        if (!m_streamHeadersDone) {
            const int idx = m_streamBuf.indexOf("\r\n\r\n");
            if (idx < 0) return;
            const QByteArray head = m_streamBuf.left(idx).toLower();
            m_streamChunked = head.contains("transfer-encoding") &&
                              head.contains("chunked");
            m_streamBuf.remove(0, idx + 4);
            m_streamHeadersDone = true;
        }
        if (!m_streamChunked) {
            m_sse.feed(m_streamBuf);
            m_streamBuf.clear();
            m_backoffMs = 500;
            return;
        }
        QByteArray payload;
        for (;;) {
            if (m_chunkRemaining == 0) {
                const int idx = m_streamBuf.indexOf("\r\n");
                if (idx < 0) break;
                bool ok = false;
                const qint64 size = m_streamBuf.left(idx).toLongLong(&ok, 16);
                m_streamBuf.remove(0, idx + 2);
                if (!ok || size == 0) break;
                m_chunkRemaining = size;
            }
            const qint64 take =
                qMin<qint64>(m_chunkRemaining, m_streamBuf.size());
            payload += m_streamBuf.left(take);
            m_streamBuf.remove(0, take);
            m_chunkRemaining -= take;
            if (m_chunkRemaining == 0) {
                if (m_streamBuf.size() < 2) break;
                m_streamBuf.remove(0, 2);
            } else {
                break;
            }
        }
        if (!payload.isEmpty()) {
            m_sse.feed(payload);
            m_backoffMs = 500;
        }
    });
    connect(m_streamSock, &QLocalSocket::disconnected, this, [this]() {
        emit streamStateChanged(false);
        if (m_streamSock) {
            m_streamSock->deleteLater();
            m_streamSock = nullptr;
        }
        if (!m_closing) scheduleReconnect();
    });
    m_streamSock->connectToServer(m_socketPath);
}

void GraphQLLocalTransport::handleSseEvent(const QByteArray &event,
                                           const QByteArray &data) {
    if (event != "next") return;
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    const QJsonObject ev = root.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("playerEvents"))
                               .toObject();
    if (ev.isEmpty()) return;
    QByteArray name;
    QJsonObject doc;
    if (channelEventFromPlayerEvent(ev, &name, &doc)) {
        emit eventReceived(name,
                           QJsonDocument(doc).toJson(QJsonDocument::Compact));
    } else {
        reemitSnapshot();
    }
}

void GraphQLLocalTransport::reemitSnapshot() {
    getJson(QUrl(), [this](bool ok, QJsonObject doc) {
        if (ok)
            emit eventReceived(
                QByteArrayLiteral("state"),
                QJsonDocument(doc).toJson(QJsonDocument::Compact));
    });
}

void GraphQLLocalTransport::scheduleReconnect() {
    const int delay = m_backoffMs;
    m_backoffMs = qMin(m_backoffMs * 2, 8000);
    QTimer::singleShot(delay, this, [this]() {
        if (!m_closing && !m_streamSock) openEventStream(QUrl());
    });
}

void GraphQLLocalTransport::closeEventStream() {
    m_closing = true;
    if (m_streamSock) {
        m_streamSock->abort();
        m_streamSock->deleteLater();
        m_streamSock = nullptr;
    }
}

}  // namespace qtamp

// GraphQLTransport — RemoteTransport implementations that speak the
// canonical Wasabi 2 GraphQL API (api/schema.graphql in qtWasabi)
// instead of the legacy control channel.  RemoteHost stays UNTOUCHED:
// these transports translate its channel-shaped calls
//   getJson(state)  -> Query.player           (adapted to channel doc)
//   postJson({op})  -> the matching mutation  (CommandResult -> reply)
//   openEventStream -> subscription playerEvents (SSE), mapped back to
//                      channel events; playlist/state kinds trigger a
//                      full snapshot re-emit so rows stay authoritative
// so applyEvent/PositionClock semantics carry over verbatim.
//
// Two flavors:
//   GraphQLHttpTransport   — QNAM against graphql+http(s):// (TCP/edge)
//   GraphQLLocalTransport  — QLocalSocket + hand-rolled HTTP/1.1 with
//                            an incremental chunked decoder against
//                            graphql+unix:// (node streams are chunked)
#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QUrl>

#include "remotetransport.h"

namespace qtamp {

// Shared op->mutation translation + document adaptation.
class GraphQLTransportBase : public RemoteTransport {
    Q_OBJECT
public:
    using RemoteTransport::RemoteTransport;

protected:
    // Build the GraphQL request body for a channel command, or an empty
    // object when the op has no mapping (caller reports failure).
    static QJsonObject requestForCommand(const QJsonObject &cmd);
    // Extract the CommandResult of `field` into a channel-shaped reply.
    static QJsonObject replyFromResult(const QJsonObject &gqlData,
                                       QString *fieldOut = nullptr);
    // Adapt Query.player data into the channel snapshot document.
    static QJsonObject channelDocFromPlayer(const QJsonObject &player);
    // Map one playerEvents payload to (channelEvent, channelDoc);
    // returns false when the payload needs a full snapshot re-emit
    // (STATE / PLAYLIST_META kinds) instead of a section event.
    static bool channelEventFromPlayerEvent(const QJsonObject &ev,
                                            QByteArray *nameOut,
                                            QJsonObject *docOut);

    static const char *kPlayerQuery;       // full snapshot incl. rows
    static const char *kPlayerEventsSub;   // typed subscription
};

class GraphQLHttpTransport : public GraphQLTransportBase {
    Q_OBJECT
public:
    explicit GraphQLHttpTransport(QObject *parent = nullptr);
    ~GraphQLHttpTransport() override;

    void setExtraHeaders(const QList<QPair<QByteArray, QByteArray>> &h) {
        m_headers = h;
    }

    void postJson(const QUrl &url, const QJsonObject &body,
                  JsonCallback cb) override;
    void getJson(const QUrl &url, JsonCallback cb) override;
    void getBytes(const QUrl &url, BytesCallback cb) override;
    void openEventStream(const QUrl &url) override;
    void closeEventStream() override;

#ifdef Q_OS_WASM
    // Raw graphql-sse frames from the EventSource glue get translated
    // instead of emitted directly.
    void wasmDeliverEvent(const QByteArray &event,
                          const QByteArray &data) override {
        handleSseEvent(event, data);
    }
#endif

private:
    QUrl graphqlEndpoint(const QUrl &base) const;
    void execute(const QUrl &base, const QJsonObject &gqlBody,
                 std::function<void(bool, QJsonObject)> cb);
    void scheduleReconnect();
    void handleSseEvent(const QByteArray &event, const QByteArray &data);
    void reemitSnapshot();

    class QNetworkAccessManager *m_nam;
    QList<QPair<QByteArray, QByteArray>> m_headers;
    class QNetworkReply *m_stream = nullptr;
    SseReader m_sse;
    QUrl m_base;
    int m_backoffMs = 500;
    bool m_closing = false;
};

// graphql+unix:// — one short-lived QLocalSocket per unary request
// (Connection: close), one long-lived socket for the subscription
// stream (chunked-decoded, fed to SseReader).  Blueprint proven by
// spikes/v0/qlocal-client.
class GraphQLLocalTransport : public GraphQLTransportBase {
    Q_OBJECT
public:
    explicit GraphQLLocalTransport(const QString &socketPath,
                                   QObject *parent = nullptr);
    ~GraphQLLocalTransport() override;

    void postJson(const QUrl &url, const QJsonObject &body,
                  JsonCallback cb) override;
    void getJson(const QUrl &url, JsonCallback cb) override;
    void getBytes(const QUrl &url, BytesCallback cb) override;
    void openEventStream(const QUrl &url) override;
    void closeEventStream() override;

private:
    struct Pending;
    void execute(const QJsonObject &gqlBody,
                 std::function<void(bool, QJsonObject)> cb);
    void request(const QByteArray &rawRequest,
                 std::function<void(bool, int, QByteArray)> done);
    void scheduleReconnect();
    void handleSseEvent(const QByteArray &event, const QByteArray &data);
    void reemitSnapshot();
    static QByteArray buildHttpRequest(const QByteArray &path,
                                       const QByteArray &body, bool sse);

    QString m_socketPath;
    QLocalSocket *m_streamSock = nullptr;
    SseReader m_sse;
    QByteArray m_streamBuf;
    bool m_streamHeadersDone = false;
    bool m_streamChunked = false;
    qint64 m_chunkRemaining = 0;
    int m_backoffMs = 500;
    bool m_closing = false;
};

}  // namespace qtamp

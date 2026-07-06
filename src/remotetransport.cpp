#include "remotetransport.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "ssereader.h"

#ifdef Q_OS_WASM
#include <emscripten.h>

#include <QHash>

#include <cstdlib>

// Browser EventSource glue.  Qt-WASM's QNetworkReply buffers a GET until
// it finishes, so the streaming-SSE path silently never delivers — the
// browser's native EventSource is the reliable primitive (and it also
// owns reconnection).  The glue is self-contained: strings cross the
// boundary through our own KEEPALIVE alloc/free wrappers and manual
// UTF-8 on HEAPU8, so no Emscripten runtime-method exports are needed
// (EXPORTED_RUNTIME_METHODS is last-wins on the link line and Qt sets
// its own list).
namespace qtamp {
namespace {
QHash<int, HttpTransport *> &wasmStreams() {
    static QHash<int, HttpTransport *> reg;
    return reg;
}
}  // namespace
}  // namespace qtamp

extern "C" {
EMSCRIPTEN_KEEPALIVE void *qtamp_es_alloc(int n) { return std::malloc(n); }
EMSCRIPTEN_KEEPALIVE void qtamp_es_free(void *p) { std::free(p); }
EMSCRIPTEN_KEEPALIVE void qtamp_es_event(int id, const char *event,
                                         const char *data) {
    if (auto *t = qtamp::wasmStreams().value(id))
        t->wasmDeliverEvent(QByteArray(event), QByteArray(data));
}
EMSCRIPTEN_KEEPALIVE void qtamp_es_state(int id, int up) {
    if (auto *t = qtamp::wasmStreams().value(id)) t->wasmDeliverState(up != 0);
}
}

// clang-format off
EM_JS(void, qtamp_es_open, (int id, const char *urlPtr), {
    let end = urlPtr;
    while (HEAPU8[end]) end++;
    const url = new TextDecoder().decode(HEAPU8.subarray(urlPtr, end));
    if (!Module.qtampES) Module.qtampES = {};
    if (Module.qtampES[id]) Module.qtampES[id].close();
    const es = new EventSource(url);
    Module.qtampES[id] = es;
    const push = (name, payload) => {
        const enc = new TextEncoder();
        const ev = enc.encode(name);
        const data = enc.encode(payload || '');
        const ptr = _qtamp_es_alloc(ev.length + data.length + 2);
        HEAPU8.set(ev, ptr);
        HEAPU8[ptr + ev.length] = 0;
        const dPtr = ptr + ev.length + 1;
        HEAPU8.set(data, dPtr);
        HEAPU8[dPtr + data.length] = 0;
        _qtamp_es_event(id, ptr, dPtr);
        _qtamp_es_free(ptr);
    };
    // SSE named events bypass onmessage — subscribe to every event the
    // protocol defines (pylon/PROTOCOL.md).
    for (const n of ['state', 'transport', 'track', 'playlist', 'eq', 'ping'])
        es.addEventListener(n, (e) => push(n, e.data));
    es.onopen = () => _qtamp_es_state(id, 1);
    // EventSource reconnects on its own; just report the drop so the
    // host can resync when onopen fires again.
    es.onerror = () => _qtamp_es_state(id, 0);
});
EM_JS(void, qtamp_es_close, (int id), {
    if (Module.qtampES && Module.qtampES[id]) {
        Module.qtampES[id].close();
        delete Module.qtampES[id];
    }
});
// clang-format on
#endif  // Q_OS_WASM

namespace qtamp {

HttpTransport::HttpTransport(QObject *parent)
    : RemoteTransport(parent),
      m_nam(new QNetworkAccessManager(this)) {
    m_sse.onEvent = [this](QByteArray event, QByteArray data) {
        emit eventReceived(event, data);
    };
}

HttpTransport::~HttpTransport() {
#ifdef Q_OS_WASM
    if (m_wasmStreamId) {
        qtamp_es_close(m_wasmStreamId);
        wasmStreams().remove(m_wasmStreamId);
    }
#endif
}

QNetworkRequest HttpTransport::makeRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    for (const auto &h : m_headers) req.setRawHeader(h.first, h.second);
    return req;
}

void HttpTransport::postJson(const QUrl &url, const QJsonObject &body,
                             JsonCallback cb) {
    QNetworkReply *r = m_nam->post(
        makeRequest(url), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r, &QNetworkReply::finished, this, [r, cb]() {
        r->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
        if (cb) cb(r->error() == QNetworkReply::NoError, doc.object());
    });
}

void HttpTransport::getJson(const QUrl &url, JsonCallback cb) {
    QNetworkReply *r = m_nam->get(makeRequest(url));
    connect(r, &QNetworkReply::finished, this, [r, cb]() {
        r->deleteLater();
        const QJsonDocument doc = QJsonDocument::fromJson(r->readAll());
        if (cb) cb(r->error() == QNetworkReply::NoError, doc.object());
    });
}

void HttpTransport::getBytes(const QUrl &url, BytesCallback cb) {
    QNetworkReply *r = m_nam->get(makeRequest(url));
    connect(r, &QNetworkReply::finished, this, [r, cb]() {
        r->deleteLater();
        if (cb) cb(r->error() == QNetworkReply::NoError, r->readAll());
    });
}

void HttpTransport::openEventStream(const QUrl &url) {
#ifdef Q_OS_WASM
    m_streamUrl = url;
    m_closing = false;
    if (!m_wasmStreamId) {
        static int nextId = 0;
        m_wasmStreamId = ++nextId;
        wasmStreams().insert(m_wasmStreamId, this);
    }
    qtamp_es_open(m_wasmStreamId, url.toString().toUtf8().constData());
    return;
#endif
    m_streamUrl = url;
    m_closing = false;
    if (m_stream) {
        m_stream->abort();  // triggers finished -> reconnect suppressed
        m_stream = nullptr;
    }
    QNetworkRequest req = makeRequest(url);
    req.setRawHeader("Accept", "text/event-stream");
    if (!m_sse.lastEventId().isEmpty())
        req.setRawHeader("Last-Event-ID", m_sse.lastEventId());
    m_sse.reset();
    m_stream = m_nam->get(req);
    emit streamStateChanged(true);
    connect(m_stream, &QNetworkReply::readyRead, this, [this]() {
        if (m_stream) {
            m_sse.feed(m_stream->readAll());
            m_backoffMs = 500;  // healthy stream resets the backoff
        }
    });
    connect(m_stream, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_stream;
        m_stream = nullptr;
        if (r) r->deleteLater();
        emit streamStateChanged(false);
        // An SSE stream never finishes on purpose: any end is a drop.
        if (!m_closing) scheduleReconnect();
    });
}

void HttpTransport::scheduleReconnect() {
    const int delay = m_backoffMs;
    m_backoffMs = qMin(m_backoffMs * 2, 8000);
    QTimer::singleShot(delay, this, [this]() {
        if (!m_closing && !m_stream && !m_streamUrl.isEmpty())
            openEventStream(m_streamUrl);
    });
}

void HttpTransport::closeEventStream() {
    m_closing = true;
#ifdef Q_OS_WASM
    if (m_wasmStreamId) qtamp_es_close(m_wasmStreamId);
#endif
    if (m_stream) {
        m_stream->abort();
        m_stream = nullptr;
    }
}

}  // namespace qtamp

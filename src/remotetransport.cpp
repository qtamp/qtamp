#include "remotetransport.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include "ssereader.h"

namespace qtamp {

HttpTransport::HttpTransport(QObject *parent)
    : RemoteTransport(parent),
      m_nam(new QNetworkAccessManager(this)) {
    m_sse.onEvent = [this](QByteArray event, QByteArray data) {
        emit eventReceived(event, data);
    };
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
    if (m_stream) {
        m_stream->abort();
        m_stream = nullptr;
    }
}

}  // namespace qtamp

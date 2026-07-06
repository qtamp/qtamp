// SseReader — incremental Server-Sent-Events parser. Feed it transport
// chunks as they arrive (any split, including mid-line and mid-CRLF) and
// it emits complete events. Pure logic, no networking: the transports
// (native QNetworkReply streaming, and any test) own the bytes.
//
// Implements the subset of the SSE grammar the qtamp protocol uses:
// `event:`/`data:`/`id:` fields, multi-`data:` accumulation joined with
// newlines, comment lines (leading ':') ignored, blank-line dispatch,
// both LF and CRLF line endings. Unknown fields are ignored per spec.
#pragma once

#include <QByteArray>

#include <functional>

namespace qtamp {

class SseReader {
public:
    // Called once per complete event. `event` defaults to "message" when
    // the stream never named it, per the SSE spec.
    std::function<void(QByteArray event, QByteArray data)> onEvent;

    void feed(const QByteArray &chunk);

    // Last seen `id:` field, for Last-Event-ID resume semantics.
    QByteArray lastEventId() const { return m_lastEventId; }

    // Drop any half-accumulated event (stream reconnect).
    void reset() {
        m_buf.clear();
        m_event.clear();
        m_data.clear();
        m_hasData = false;
    }

private:
    void consumeLine(const QByteArray &line);
    void dispatch();

    QByteArray m_buf;         // undelivered partial line
    QByteArray m_event;       // current event name
    QByteArray m_data;        // accumulated data lines
    bool m_hasData = false;   // distinguishes "" data from no data
    QByteArray m_lastEventId;
};

}  // namespace qtamp

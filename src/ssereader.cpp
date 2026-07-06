#include "ssereader.h"

namespace qtamp {

void SseReader::feed(const QByteArray &chunk) {
    m_buf.append(chunk);
    // Extract complete lines; whatever trails without a newline stays
    // buffered for the next chunk. CRLF is normalized by stripping the
    // trailing '\r' after splitting on '\n' — this also handles a CRLF
    // pair split across two chunks (the '\r' arrives as the line tail).
    int start = 0;
    for (;;) {
        const int nl = m_buf.indexOf('\n', start);
        if (nl < 0) break;
        QByteArray line = m_buf.mid(start, nl - start);
        if (line.endsWith('\r')) line.chop(1);
        consumeLine(line);
        start = nl + 1;
    }
    if (start > 0) m_buf.remove(0, start);
}

void SseReader::consumeLine(const QByteArray &line) {
    if (line.isEmpty()) {  // blank line = dispatch boundary
        dispatch();
        return;
    }
    if (line.startsWith(':')) return;  // comment / keepalive

    QByteArray field, value;
    const int colon = line.indexOf(':');
    if (colon < 0) {
        field = line;  // field with empty value, per spec
    } else {
        field = line.left(colon);
        value = line.mid(colon + 1);
        if (value.startsWith(' ')) value.remove(0, 1);  // single optional SP
    }

    if (field == "event") {
        m_event = value;
    } else if (field == "data") {
        if (m_hasData) m_data.append('\n');
        m_data.append(value);
        m_hasData = true;
    } else if (field == "id") {
        // Per spec an id containing NUL is ignored.
        if (!value.contains('\0')) m_lastEventId = value;
    }
    // "retry" and unknown fields: ignored.
}

void SseReader::dispatch() {
    if (!m_hasData) {  // event name without data never fires, per spec
        m_event.clear();
        return;
    }
    const QByteArray name =
        m_event.isEmpty() ? QByteArrayLiteral("message") : m_event;
    const QByteArray data = m_data;
    m_event.clear();
    m_data.clear();
    m_hasData = false;
    if (onEvent) onEvent(name, data);
}

}  // namespace qtamp

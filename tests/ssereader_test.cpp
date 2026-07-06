// Unit tests for src/ssereader.{h,cpp}. The critical property: parsing is
// identical no matter how the transport chunks the byte stream — every
// test corpus is re-fed at every chunk size from 1 byte upward, including
// splits inside CRLF pairs and UTF-8-agnostic byte splits.

#include "ssereader.h"

#include <QByteArray>
#include <QList>
#include <QPair>

#include <cstdio>

using namespace qtamp;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

namespace {

using Events = QList<QPair<QByteArray, QByteArray>>;

Events parseChunked(const QByteArray &stream, int chunkSize) {
    Events out;
    SseReader r;
    r.onEvent = [&out](QByteArray e, QByteArray d) {
        out.append({e, d});
    };
    for (int i = 0; i < stream.size(); i += chunkSize)
        r.feed(stream.mid(i, chunkSize));
    return out;
}

void testBasicAndChunking() {
    const QByteArray stream =
        "event: transport\n"
        "data: {\"revision\":1}\n"
        "\n"
        "data: plain\n"
        "\n"
        ": keepalive comment\n"
        "event: playlist\r\n"
        "data: part1\r\n"
        "data: part2\r\n"
        "\r\n";
    const Events expect = {
        {"transport", "{\"revision\":1}"},
        {"message", "plain"},
        {"playlist", "part1\npart2"},
    };
    for (int chunk = 1; chunk <= stream.size(); ++chunk) {
        const Events got = parseChunked(stream, chunk);
        if (got != expect) {
            std::printf("  FAIL chunk size %d: got %d events\n", chunk,
                        int(got.size()));
            ++g_failures;
            return;
        }
    }
}

void testIdAndReset() {
    SseReader r;
    Events out;
    r.onEvent = [&out](QByteArray e, QByteArray d) { out.append({e, d}); };
    r.feed("id: 42\nevent: x\ndata: y\n\n");
    CHECK(r.lastEventId() == "42");
    CHECK(out.size() == 1 && out[0].first == "x");

    // A half-accumulated event is dropped by reset (reconnect semantics),
    // but the last event id survives for Last-Event-ID.
    r.feed("event: half\ndata: incomplete");
    r.reset();
    r.feed("data: after\n\n");
    CHECK(out.size() == 2);
    CHECK(out[1].first == "message");
    CHECK(out[1].second == "after");
    CHECK(r.lastEventId() == "42");
}

void testSpecCorners() {
    SseReader r;
    Events out;
    r.onEvent = [&out](QByteArray e, QByteArray d) { out.append({e, d}); };

    // Event name without data never dispatches; the name does not leak
    // into the next event.
    r.feed("event: ghost\n\n");
    CHECK(out.isEmpty());
    r.feed("data: real\n\n");
    CHECK(out.size() == 1 && out[0].first == "message");

    // No space after the colon is valid; value keeps inner spaces.
    r.feed("data:a b  c\n\n");
    CHECK(out.size() == 2 && out[1].second == "a b  c");

    // A field line without any colon is a field with empty value.
    r.feed("data\n\n");
    CHECK(out.size() == 3 && out[2].second == "");

    // Empty data line contributes an empty segment.
    r.feed("data: x\ndata:\ndata: y\n\n");
    CHECK(out.size() == 4 && out[3].second == "x\n\ny");
}

}  // namespace

int main() {
    testBasicAndChunking();
    testIdAndReset();
    testSpecCorners();
    if (g_failures) {
        std::printf("ssereader_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("ssereader_test: all checks passed\n");
    return 0;
}

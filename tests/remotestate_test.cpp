// Unit tests for src/remotestate.{h,cpp}: snapshot JSON round-trip,
// revision-checked event application, epoch resync, and the PositionClock
// interpolation policy. Framework-free, Qt Core only, no network, no GUI.

#include "remotestate.h"

#include <QJsonArray>
#include <QJsonObject>

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

RemoteSnapshot sampleSnapshot() {
    RemoteSnapshot s;
    s.epoch = QStringLiteral("boot-1");
    s.revision = 412;
    s.serverNowMs = 987654321;
    s.transport = {true, false, 63210, 987654321, 214000, 78, 0.5};
    s.track = {QStringLiteral("Title"),  QStringLiteral("Artist"),
               QStringLiteral("Album"),  QStringLiteral("/music/x.mp3"),
               QStringLiteral("Artist - Title"), QStringLiteral("mpeg"),
               320, 44100, 2};
    s.playlist.revision = 17;
    s.playlist.currentIndex = 1;
    s.playlist.rows = {{QStringLiteral("One"), 100000},
                       {QStringLiteral("Two"), 214000}};
    s.eq.on = true;
    s.eq.preamp = 40;
    s.eq.bands[3] = 55;
    return s;
}

void testRoundTrip() {
    const RemoteSnapshot s = sampleSnapshot();
    RemoteSnapshot back;
    CHECK(parseSnapshot(serializeSnapshot(s), &back));
    CHECK(back == s);
    CHECK(back.playlist.count() == 2);
    CHECK(back.eq.bands.size() == 10);
}

void testParseRejectsBroken() {
    RemoteSnapshot s;
    CHECK(!parseSnapshot(QJsonObject{}, &s));  // no sections at all
    CHECK(!parseSnapshot(
        QJsonObject{{QStringLiteral("transport"), QJsonObject{}}}, &s));
}

QJsonObject sectionEvent(const char *section, quint64 rev,
                         const QJsonObject &body,
                         const QString &epoch = QStringLiteral("boot-1")) {
    return {{QStringLiteral("epoch"), epoch},
            {QStringLiteral("revision"), double(rev)},
            {QStringLiteral("serverNowMs"), 1000.0},
            {QString::fromLatin1(section), body}};
}

void testEventApply() {
    RemoteSnapshot s = sampleSnapshot();

    // In-order transport event applies and bumps the revision.
    auto r = applyEvent("transport",
                        sectionEvent("transport", 413,
                                     {{QStringLiteral("playing"), false},
                                      {QStringLiteral("paused"), true}}),
                        &s);
    CHECK(r == ApplyResult::Applied);
    CHECK(s.revision == 413);
    CHECK(!s.transport.playing);
    CHECK(s.transport.paused);
    // Untouched fields keep their previous values (partial section doc).
    CHECK(s.transport.durationMs == 214000);

    // Stale (already-seen) revision is dropped without touching state.
    r = applyEvent("transport",
                   sectionEvent("transport", 413,
                                {{QStringLiteral("playing"), true}}),
                   &s);
    CHECK(r == ApplyResult::Stale);
    CHECK(!s.transport.playing);

    // A revision gap applies the fresh section but demands a resync.
    r = applyEvent("track",
                   sectionEvent("track", 420,
                                {{QStringLiteral("title"),
                                  QStringLiteral("New")}}),
                   &s);
    CHECK(r == ApplyResult::NeedsResync);
    CHECK(s.track.title == QStringLiteral("New"));
    CHECK(s.revision == 420);

    // Epoch change: backend rebooted, nothing applies, resync demanded.
    r = applyEvent("transport",
                   sectionEvent("transport", 2,
                                {{QStringLiteral("playing"), true}},
                                QStringLiteral("boot-2")),
                   &s);
    CHECK(r == ApplyResult::NeedsResync);
    CHECK(!s.transport.playing);

    // Playlist event replaces the rows wholesale.
    r = applyEvent(
        "playlist",
        sectionEvent("playlist", 421,
                     {{QStringLiteral("revision"), 18.0},
                      {QStringLiteral("currentIndex"), 0},
                      {QStringLiteral("rows"),
                       QJsonArray{QJsonObject{
                           {QStringLiteral("text"), QStringLiteral("Only")},
                           {QStringLiteral("durationMs"), 5000.0}}}}}),
        &s);
    CHECK(r == ApplyResult::Applied);
    CHECK(s.playlist.count() == 1);
    CHECK(s.playlist.revision == 18);
    CHECK(s.playlist.rows[0].text == QStringLiteral("Only"));

    // Full state event is always authoritative (the resync answer).
    RemoteSnapshot fresh = sampleSnapshot();
    fresh.epoch = QStringLiteral("boot-2");
    fresh.revision = 3;
    r = applyEvent("state", serializeSnapshot(fresh), &s);
    CHECK(r == ApplyResult::Applied);
    CHECK(s == fresh);

    // Ping only refreshes the server clock.
    r = applyEvent("ping",
                   {{QStringLiteral("serverNowMs"), 424242.0}}, &s);
    CHECK(r == ApplyResult::Ignored);
    CHECK(s.serverNowMs == 424242);
}

void testPositionClock() {
    PositionClock c;
    CHECK(!c.hasAnchor());
    CHECK(c.positionAt(1000) == 0);

    // First anchor snaps; position advances with local time while playing.
    c.anchor(10000, 50000, true);
    CHECK(c.positionAt(50000) == 10000);
    CHECK(c.positionAt(51000) == 11000);

    // Paused: frozen.
    c.anchor(12000, 52000, false);
    CHECK(c.positionAt(52000) == 12000);
    CHECK(c.positionAt(60000) == 12000);

    // Resume, then a small correction backwards must not rewind the bar:
    // the floor holds until real time overtakes it.
    c.anchor(12000, 60000, true);
    CHECK(c.positionAt(61000) == 13000);
    c.anchor(12900, 61000, true);  // 100ms behind prediction: slew, not jump
    CHECK(c.positionAt(61000) == 13000);   // held at the floor
    CHECK(c.positionAt(61200) >= 13000);   // still monotonic
    CHECK(c.positionAt(62200) == 14100);   // overtaken: back on the anchor

    // A correction beyond the snap threshold (a seek) applies immediately,
    // backwards included.
    c.anchor(5000, 62000, true);
    CHECK(c.positionAt(62000) == 5000);
    CHECK(c.positionAt(62500) == 5500);
}

}  // namespace

int main() {
    testRoundTrip();
    testParseRejectsBroken();
    testEventApply();
    testPositionClock();
    if (g_failures) {
        std::printf("remotestate_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("remotestate_test: all checks passed\n");
    return 0;
}

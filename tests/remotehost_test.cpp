// Unit tests for src/remotehost.{h,cpp}: RemoteHost driven by an
// InjectedTransport (no network). Assert that reads answer from the
// injected snapshot, that pushed events update the cache and fire the
// PlayerHost signals, and that writes emit the right command JSON with
// optimistic echo. Runs offscreen (QGuiApplication for the QObject/
// signal machinery).

#include "remotehost.h"
#include "remotetransport.h"
#include "remotestate.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>

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

QJsonObject sampleStateDoc() {
    RemoteSnapshot s;
    s.epoch = QStringLiteral("boot-1");
    s.revision = 10;
    s.transport = {true, false, 5000, 5000, 200000, 80, 0.5};
    s.track.title = QStringLiteral("Song");
    s.track.artist = QStringLiteral("Band");
    s.track.filename = QStringLiteral("/music/song.mp3");
    s.track.displayTitle = QStringLiteral("Band - Song");
    s.playlist.revision = 3;
    s.playlist.currentIndex = 0;
    s.playlist.rows = {{QStringLiteral("Band - Song"), 200000}};
    s.eq.bands[2] = 40;
    return serializeSnapshot(s);
}

QByteArray compact(const QJsonObject &o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void testReadsFromSnapshot() {
    auto *t = new InjectedTransport();
    t->stateReply = sampleStateDoc();
    RemoteHost host(QUrl(QStringLiteral("http://x/")), t);

    // The ctor opens the stream; InjectedTransport::openEventStream emits
    // streamStateChanged(true) → resync → getJson serves stateReply.
    CHECK(host.isPlaying());
    CHECK(!host.isPaused());
    CHECK(host.durationMs() == 200000);
    CHECK(host.volume() == 80);
    CHECK(host.songTitle() == QStringLiteral("Song"));
    CHECK(host.playItemMetaData(QStringLiteral("artist")) ==
          QStringLiteral("Band"));
    CHECK(host.playlistRowCount() == 1);
    CHECK(host.playlistRowText(0) == QStringLiteral("Band - Song"));
    CHECK(host.playlistCurrentRow() == 0);
    // EQ band 2 = 40 on the 0..63 scale → sliderPosition normalized.
    const double p = host.sliderPosition(QStringLiteral("EQ_BAND"),
                                         QStringLiteral("2"));
    CHECK(qAbs(p - 40.0 / 63.0) < 1e-9);
    // Position interpolates forward from the anchor while playing.
    CHECK(host.positionMs() >= 5000);
}

void testEventsUpdateAndSignal() {
    auto *t = new InjectedTransport();
    t->stateReply = sampleStateDoc();
    RemoteHost host(QUrl(QStringLiteral("http://x/")), t);

    int playbackCount = 0, playlistCount = 0;
    QObject::connect(&host, &PlayerHost::playbackStateChanged,
                     [&]() { ++playbackCount; });
    QObject::connect(&host, &PlayerHost::playlistChanged,
                     [&]() { ++playlistCount; });

    // A transport event (higher revision) pauses playback.
    t->inject("transport",
              compact({{QStringLiteral("epoch"), QStringLiteral("boot-1")},
                       {QStringLiteral("revision"), 11.0},
                       {QStringLiteral("transport"),
                        QJsonObject{{QStringLiteral("playing"), true},
                                    {QStringLiteral("paused"), true},
                                    {QStringLiteral("positionMs"), 6000.0}}}}));
    CHECK(host.isPaused());
    CHECK(playbackCount >= 1);

    // A playlist event replaces the rows and fires playlistChanged.
    t->inject("playlist",
              compact({{QStringLiteral("epoch"), QStringLiteral("boot-1")},
                       {QStringLiteral("revision"), 12.0},
                       {QStringLiteral("playlist"),
                        QJsonObject{{QStringLiteral("revision"), 4.0},
                                    {QStringLiteral("currentIndex"), 1},
                                    {QStringLiteral("rows"), QJsonArray{}}}}}));
    CHECK(host.playlistRowCount() == 0);
    CHECK(playlistCount >= 1);

    // An epoch change forces a resync (getJson re-fetched).
    const int before = t->stateFetches;
    t->inject("transport",
              compact({{QStringLiteral("epoch"), QStringLiteral("boot-2")},
                       {QStringLiteral("revision"), 2.0},
                       {QStringLiteral("transport"), QJsonObject{}}}));
    CHECK(t->stateFetches > before);
}

void testWritesEmitCommands() {
    auto *t = new InjectedTransport();
    t->stateReply = sampleStateDoc();
    RemoteHost host(QUrl(QStringLiteral("http://x/")), t);
    t->posted.clear();

    host.pause();
    CHECK(host.isPaused());  // optimistic echo, before any server event
    CHECK(!t->posted.isEmpty());
    CHECK(t->posted.last().value(QStringLiteral("op")).toString() ==
          QStringLiteral("pause"));

    host.setVolume(55);
    CHECK(host.volume() == 55);
    CHECK(t->posted.last().value(QStringLiteral("op")).toString() ==
          QStringLiteral("setVolume"));
    CHECK(t->posted.last()
              .value(QStringLiteral("args"))
              .toObject()
              .value(QStringLiteral("v"))
              .toInt() == 55);

    host.seekMs(30000);
    CHECK(t->posted.last().value(QStringLiteral("op")).toString() ==
          QStringLiteral("seek"));

    // A row op carries the playlist revision guard.
    host.playlistPlayRow(0);
    const QJsonObject cmd = t->posted.last();
    CHECK(cmd.value(QStringLiteral("op")).toString() ==
          QStringLiteral("playlistPlayRow"));
    CHECK(cmd.value(QStringLiteral("args"))
              .toObject()
              .value(QStringLiteral("expectPlaylistRevision"))
              .toInt() == 3);

    // A rejected command triggers a resync.
    const int before = t->stateFetches;
    t->postReply = {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"),
                     QStringLiteral("playlistRevision mismatch")}};
    host.playlistPlayRow(0);
    CHECK(t->stateFetches > before);
}

}  // namespace

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    testReadsFromSnapshot();
    testEventsUpdateAndSignal();
    testWritesEmitCommands();
    if (g_failures) {
        std::printf("remotehost_test: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("remotehost_test: all checks passed\n");
    return 0;
}

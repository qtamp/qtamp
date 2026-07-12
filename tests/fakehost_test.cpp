// FakeHost: deterministic scripted-host checks (Wasabi 2 V2 gate).
// Idle state matches the QtampHost calibration (the pixel suites prove
// the rendering side byte-identically); here: the ML panel sections
// the engine now pulls through Host::mlPanelChildren, and the
// deterministic interaction behavior the CLICK_AT gates rely on.
#include <QCoreApplication>

#include <cstdio>

#include "../src/fakehost.h"

static int failures = 0;
static void check(bool ok, const char *label) {
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok) ++failures;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    FakeHost host;

    // Idle calibration (must mirror QtampHost with nothing loaded).
    check(!host.isPlaying() && !host.isPaused(), "idle: stopped");
    check(host.volume() == 70, "idle: volume 70");
    check(host.positionMs() == 0 && host.durationMs() == 0,
          "idle: position/duration 0");
    check(host.songTitle().isEmpty(), "idle: no title");
    check(host.playlistRowCount() == 0 && host.playlistCurrentRow() == -1,
          "idle: empty playlist");
    check(qFuzzyCompare(
              host.sliderPosition(QStringLiteral("PAN"), QString()), 0.5),
          "idle: pan centred");

    // ML panel sections: canned, non-empty, stable.
    const auto pl = host.mlPanelChildren(QStringLiteral("playlists"));
    check(pl.size() == 2 && pl[0].label == QLatin1String("Fake Mixtape"),
          "panel: playlists canned");
    check(host.mlPanelChildren(QStringLiteral("bookmarks")).size() == 1,
          "panel: bookmarks canned");
    check(host.mlPanelChildren(QStringLiteral("history")).size() == 1,
          "panel: history canned");
    check(host.mlPanelChildren(QStringLiteral("devices")).size() == 1,
          "panel: devices canned");
    check(host.mlPanelChildren(QStringLiteral("nonsense")).isEmpty(),
          "panel: unknown namespace empty");

    // Engine default: a plain Host serves EMPTY sections (the panel
    // renders nothing rather than reading app data itself).
    struct PlainHost : qtWasabi::Host {
        qint64 positionMs() const override { return 0; }
        qint64 durationMs() const override { return 0; }
        bool isPlaying() const override { return false; }
        bool isPaused() const override { return false; }
        QString songTitle() const override { return {}; }
        void play() override {}
        void pause() override {}
        void stop() override {}
    } plain;
    check(plain.mlPanelChildren(QStringLiteral("playlists")).isEmpty(),
          "engine default: empty sections");

    // Deterministic interaction: enqueue + play row.
    host.enqueueAndPlay(QUrl::fromLocalFile(
        QStringLiteral("/fake/dir/fake-track.mp3")), false);
    check(host.isPlaying(), "interaction: playing after enqueueAndPlay");
    check(host.playlistRowCount() == 1 && host.playlistCurrentRow() == 0,
          "interaction: row added + current");
    check(host.songTitle() == QLatin1String("fake-track"),
          "interaction: title from row");
    check(host.durationMs() == 175000, "interaction: canned duration");
    host.seekMs(60000);
    check(host.positionMs() == 60000, "interaction: seek is exact");
    host.pause();
    check(host.isPaused(), "interaction: paused");
    host.stop();
    check(!host.isPlaying() && host.positionMs() == 0,
          "interaction: stop resets deterministically");

    printf(failures == 0 ? "PASS\n" : "FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

# Status: networked qtamp (WASM + GraphQL sync)

Living progress report for the split described in `docs/OKF-remote.md`. Updated
as milestones land.

## What works right now

**M0 — cleanup (done).** The discarded frame-streaming experiment
(`src/remotecontrol.*`, the CMake/main.cpp edits behind it) was reverted. The
build is pristine. Baselines are green: `tests/regression/run.sh` passes all six
skins (WinampModernPP, Winamp Modern, DeClassified, Bento, Big Bento,
QTAMP-Winamp2000SP4); ctest passes the qtWasabi engine suite (titlebar,
maki_loader, skin_xml, layer_paint, tree_paint, skinview, text_paint,
skin_runtime) plus wavreader. (`layout_test` and `visual_diff` are pre-existing
submodule build-helper issues unrelated to this work.)

**M1 — PlayerHost extraction (done, committed `afcc6ad`).** `src/playerhost.h`
introduces `PlayerHost : public QObject, public qtWasabi::Host` carrying the
concrete surface the window and `main()` used to call directly on `QtampHost`
(openPath/enqueue, currentSourceUrl, the EQ band store, `analyzerPtr`,
`showPreferencesFn`) plus the change signals `sourceChanged /
playbackStateChanged / metaDataChanged / playlistChanged`. `QtampHost` now
derives from `PlayerHost` and forwards its `QMediaPlayer` signals onto them;
`QtampPlayerWindow` and `main()` hold a `PlayerHost*` and no longer name the
concrete host. **Verified behaviour-neutral: the pixel-regression suite is
byte-identical to the M0 baseline**, so the refactor changed nothing that
renders — exactly the property a groundwork refactor needs.

This is the load-bearing step: the engine seam is now clean enough that a second
`Host` implementation (the networked `RemoteHost`) can back the same window
without any engine or window change.

**M2 — protocol core (done, committed `81adf30`).** `src/remotestate.{h,cpp}`
carries the `RemoteSnapshot` document (epoch/revision/transport/track/playlist/
eq), JSON parse+serialize shared between the backend serializer and the future
RemoteHost cache, revision-checked `applyEvent` (stale events dropped, revision
gaps and epoch changes demand a resync), and the `PositionClock` (position
interpolated from the last transport event, anchored at local receipt time, with
a snap threshold for seeks and a monotonic floor). `src/ssereader.{h,cpp}` is an
incremental, byte-split-safe SSE parser. `pylon/PROTOCOL.md` is the wire
contract. Unit tests `remotestate` and `ssereader` pass (round trips, event
ordering, epoch resync, interpolation policy, SSE corpus re-fed at every chunk
size).

**M3 — backend mode (done, committed `f86bb93`).** `qtamp --backend <port>`
runs headless (offscreen QPA, no skin, no window): a local `QtampHost` + a
hidden `PlaylistWindow` model + the audio pipeline, exposed over the loopback
control channel `src/backendserver.{h,cpp}` (a plain `QTcpServer` speaking the
PROTOCOL.md routes: `GET /state`, `GET /events` SSE, `POST /cmd`, `GET
/art/current`). Change push is fingerprint-based — transport pushes only on
edges, never on position (clients interpolate); the playlist got a `changed()`
signal (add/remove/clear/crop) that bumps the playlist revision. Commands are
path-gated to the music root. Verified by `tests/remote/backend_test.sh` (state
shape, SSE push on mutation, path-guard rejection, playlist count) — all green,
and the pixel-regression suite is still identical (the playlist-signal change is
behaviour-neutral).

## What is next (not built yet)

- **M4** `RemoteHost` + `--connect` + the qtamp-pylon; the first full local sync
  loop (backend + pylon + two native heads on the laptop, no server).
- **M5** `--container` root window.
- **M6** the `QTAMP_WASM_REMOTE` browser build.
- **M7** the TeamSpeak music-bot container on the server.
- **M8** public routing, the browser iframes in the bot's user-info panel, the
  MacBook native connect, and opening it to friends.

Full plan and milestone detail: the session plan file
(`~/.claude/plans/kannst-du-dich-noch-jolly-cake.md`).

## Who built what (model attribution)

This work spans two Claude models in one working session. The user asked for
this to be recorded.

- **Claude Opus 4.8** — did the exploration and the two-perspective design of the
  networked split (the `qtWasabi::Host` cut-line analysis, the protocol shapes,
  the build-mode matrix, the pylon/bot/integration design, the test matrix and
  milestone order). Opus also authored the earlier, later-discarded
  frame-streaming plan and its `RemoteControlServer` prototype (reverted in M0).
- **Claude Fable 5** — did the implementation from M0 onward: the M0 cleanup and
  baseline verification; the M1 `PlayerHost` extraction; the M2 protocol core
  (`remotestate`, `ssereader`, `PROTOCOL.md`) with its unit tests; the M3
  backend mode (`backendserver`, the `PlaylistWindow::changed()` signal, the
  `--backend` wiring in `main.cpp`) with its integration test; and this
  documentation. Milestones M4 onward are Fable's to implement.

(The pre-existing qtamp and qtWasabi codebase this builds on is the user's own
prior work, largely authored across earlier sessions; this attribution covers
only the networked-player split.)

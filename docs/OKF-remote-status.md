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

**M4 — RemoteHost + `--connect` + local sync loop (qtamp side done, committed
`6988468`).** `src/remotetransport.{h,cpp}` is the transport abstraction:
`HttpTransport` (QNetworkAccessManager POSTs/GETs + a streaming SSE GET feeding
SseReader, capped-backoff reconnect, CF-Access header support) and
`InjectedTransport` (the no-network test double). `src/remotehost.{h,cpp}` is a
`PlayerHost` whose reads answer from the cached snapshot (position interpolated
by PositionClock), whose writes become `/cmd` POSTs with optimistic echo, and
whose pushed SSE events update the cache and fire the PlayerHost change signals —
so the window's repaint machinery works unchanged. `main.cpp` grows `--connect
<url>` (RemoteHost-backed player), the host factory (local vs remote), and a
headless `--probe <field>` for tests. **Verified three ways, all offscreen, no
TS/pylon/browser**: `remotehost_test` (reads/events/writes against the injected
transport), and `tests/remote/sync_test.sh` — a real `qtamp --backend` plus a
real `qtamp --connect` head that **converges to every backend mutation** (play,
pause, playlist add). The pixel regression is still identical.

**M4b — qtamp-pylon (done).** `pylon/` is the GraphQL facade over the control
channel, built on the same vendored `@getcronit/pylon` 2.9.5 skeleton as the
ts6-client bridge pylon. `src/backendlink.ts` mirrors the backend snapshot over
HTTP+SSE (reconnect forever, `player: null` while down, epoch/revision resync
semantics matching the C++ `applyEvent`); `src/index.ts` exposes
`Query.player`, flat mutations (play/pause/stop/seek/volume/pan/eq/playlist ops,
each forwarding the command and returning refetched state), and the
`playerEvents`/`playlistEvents` subscriptions (JSON-string scalars, newest-wins
PushQueue, 5s keepalive). `pylon.config.ts` additionally passes the raw control
channel through (`/state`, `/events`, `/cmd`, `/art/current`) — so native
`--connect` heads reach the backend **through the pylon's port unchanged**, and
GraphQL stays the additive facade for browser/driver. Verified three ways:
5 BackendLink unit tests against a PROTOCOL.md mock backend, 4 e2e tests booting
the **built** `.pylon/index.js` over real HTTP (schema mirror, mutation
forwarding + rejection-as-GraphQL-error, subscription push, passthrough), and
`scripts/e2e-remote.sh` — the whole production chain minus TS/Cloudflare: a real
`qtamp --backend`, the built pylon in front, GraphQL mutations through the
pylon, a real `qtamp --connect` head converging through the passthrough, and a
GraphQL subscription carrying a pause issued on the backend directly. All green.

With M4+M4b the full local sync loop is proven end to end: any head (native or
GraphQL client) can mutate, every other head converges.

**M5 — `--container` root window (done).** `qtamp --container <ref>` renders
any skin container as the window's root instead of `"main"` — `--container pl`
is just the Playlist Editor. The ref resolves exactly like the skin's TOGGLE
action (container id, component GUID, or the `pl`/`ml`/`vid` aliases); the root
container also drives skin reload, and a non-main root disables the subwindow
machinery (one container per process — the shape each browser iframe needs).
Gated by `tests/regression/container_root_test.sh`: the container-as-root
render must match the proven `--screenshot-container` subwindow capture (exact
size, MAE < 3 across the two render stacks; measured 1.295). The six-skin
pixel regression stays byte-identical, and the live proof works end to end:
a `--connect --container pl` head against a running backend renders the synced
playlist row in its pledit-rooted window.

## What is next (not built yet)
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
  `--backend` wiring) with its integration test; the M4 `RemoteHost` +
  `remotetransport` + the `--connect`/`--probe` wiring, with the `remotehost`
  unit test and the `sync_test.sh` two-process convergence proof; the M4b
  qtamp-pylon (BackendLink, schema, passthrough plugin, mock backend, 9 vitest
  tests) and the `e2e-remote.sh` full-chain proof; the M5 `--container` root
  window with its screenshot gate; and this documentation. Milestones M6
  onward are Fable's to implement.

(The pre-existing qtamp and qtWasabi codebase this builds on is the user's own
prior work, largely authored across earlier sessions; this attribution covers
only the networked-player split.)

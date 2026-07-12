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
incremental, byte-split-safe SSE parser. `docs/PROTOCOL.md` is the wire
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

**M6 — `QTAMP_WASM_REMOTE` (code complete; server build pending).** The new
CMake option builds the networked browser head: same engine + bundled skin, no
local audio, `QTAMP_REMOTE_ONLY` boots straight into RemoteHost. The embedding
page configures it via query params (`?window=player|pledit`,
`?graphql=/api/music/graphql` — trailing `/graphql` stripped, relative paths
resolved against the page origin). Under wasm the SSE stream is the browser's
native `EventSource` (Qt-WASM's QNetworkReply buffers GETs to completion, so
the streaming path never delivers); the JS glue is self-contained via own
KEEPALIVE alloc wrappers and manual UTF-8 on HEAPU8. The demo track stays out
of the remote qrc. Shipped alongside: `wasm/remote/index.html` (native-size
container per window param), `scripts/build-wasm-remote.sh` (builder-container
build with a hard <25 MiB Pages gate), `wasm/test/mock-remote-server.mjs`
(one-origin dist + PROTOCOL.md mock, route-verified in node) and
`wasm/test/cdp-check-remote.mjs` (real-Chromium gate: /state fetch,
EventSource subscription, SSE push survival, revision-gap resync, pledit
variant at 436x164). Native build and the whole remote test family stay green.
**Built and verified:** `scripts/build-wasm-remote.sh` ran in the builder on
the build server — qtamp.wasm is 22 MiB after wasm-opt (under the 25 MiB
gate), the js/wasm link test passed in-build, and `cdp-check-remote.mjs`
PASSES against the real dist in a real Chromium (SwiftShader): /state fetch,
EventSource subscription, SSE push survival, revision-gap resync (events
delivered AND applied in the browser), the pledit head at exactly 436x164,
no fatal console errors. One fix fell out: Qt 6.8 renders into a shadow root,
so the test walks shadow DOMs to find the canvas.

**Open fidelity item (does not block the sync layer):** the wasm pledit head
draws its list area flat grey and does not render the synced playlist row,
while a native head with the SAME bundled skin against the SAME mock renders
the dark-blue list with the row — so the state arrives, the wasm-side pledit
paint is at fault (suspect: the pe_init-style gate documented in the
draw_pe gotchas). Needs the qtWasabi debug loop before the playlist iframe
ships to users; the player window renders correctly.

**M7 — bot container (code complete locally; server deploy pending).** The new
repo `~/git/ts4party-musicbot` carries the whole container: a two-stage
Dockerfile (qtamp compiled x86_64 from a staged pinned source tree; runtime =
the ts6-remote dependency set + Qt6/gstreamer decoders + node), `entry.sh`
supervising pulseaudio (null sinks `music_out`/`ts_out`), `qtamp --backend
18800` (PULSE_SINK=music_out), qtamp-pylon :8789, the ts6 bridge pylon :8788 +
pump :9223, the TS6 client (records `music_out.monitor` as its microphone,
`--disable-audio-input` binary neutralization) and the driver. The driver
(`driver/driver.mjs`, plain node) keeps the bot connected
(spawn/startConnection via the bridge pylon, waits for status 4,
setRoleAudioCapture with vad/agc/denoise off), mirrors `/music` into the
playlist (idempotent by path, epoch-aware adoption after backend restarts) and
autoplays when idle — 6 node:test cases against mocked pylons, all green.
`deploy/run.sh` guards 8788/8789/9223/18800 loopback-only via nft;
`deploy/smoke.sh` checks every port plus an audio-RMS gate on
`music_out.monitor`.

**M7 server image (built + smoke-verified in-image).** The
`ts4party-musicbot` image now builds on the Netcup host: the server layout is
staged (`/opt/ts4party-musicbot/{app,bridge-api,home,music,deploy}` — TS6 app
and bridge-api copied from the ts6-remote install, qtamp source staged at the
pinned main). This was the FIRST x86-64 Linux build of qtamp against the
fetch-wasabi.sh WCL mirror and surfaced a three-part fix chain, all landed in
qtWasabi (`m16/decouple-and-list-input`): the platform overlay now supplies
`linux-amd64/types.h` (the public mirror dispatches to it but does not ship
it), the two bfc string TUs compile from configure-time build-tree copies
with `va_list saveargs = args;` rewritten to `va_copy` (x86-64 SysV va_list
is an array type; aarch64/wasm ABIs accepted the assignment, which is why no
earlier target hit it), and libuuid joined the image deps. The runtime image
carries the full `cmake --install` prefix (qtamp links the SHARED qtwasabi,
resolved via the `$ORIGIN/../lib/qtamp` RPATH) and a pre-built qtamp-pylon
(no npm at boot). In-image smoke, all green: the backend serves `/state`,
the pylon answers `Query.player` and forwards mutations (setVolume echoes
42), the control-channel passthrough streams, and the driver against the
live stack picks up dropped files, playlistAdds them through the pylon
(rowCount 2) and autoplays row 0 with `playing:true`. The aarch64 laptop
build stays clean after the qtWasabi changes (ctest unchanged, six-skin
pixel regression byte-identical).

**Open:** creating the GitHub repo (sandbox-denied; user), the container
START via `deploy/run.sh` (user-gated by standing rule) with bot UID
recording and the real audio E2E, and TS file-transfer ingest (live-probe
follow-up — until then `/music` is the drop directory).

**M8 — public routing + iframes (local code done; deploy pending).** In
ts6-client (feat/bridge-live-tap, pushed): the pylon's `loopback-proxy` plugin
(`TS6_PROXY_ROUTES="music=http://127.0.0.1:8789,..."` → `/music/*` with the
prefix stripped, zero-copy responses so SSE streams through; 4 vitest cases,
full suite 34 green), the client-info player sections in `tsclient-shim.js`
(gated on the bot UID via localStorage `TS6_MUSIC_BOT_UID`, lazy iframes onto
`/player/?window=player|pledit&graphql=/api/music/graphql`), and
`build-docroot.sh` PLAYER_DIST support (bundles the wasm head under
`/player/` with a no-cache `_headers` rule — js+wasm must deploy as a pair).
**Open:** live-DOM verification of the shim selectors, the Pages deploy, the
CF Access service token for the MacBook native connect, and the friends
policy.

## What is next (user-gated)
- **M7 go-live** create the GitHub repo for ts4party-musicbot, user runs
  `deploy/run.sh` (nft guard + container start), record the bot UID, drop
  real mp3/flac into `/opt/ts4party-musicbot/music`, audio E2E in the
  channel (`deploy/smoke.sh` includes the RMS gate).
- **M8 deploy** redeploy ts4.party Pages with PLAYER_DIST + shim (set
  `TS6_MUSIC_BOT_UID` in localStorage to enable the sections), set
  TS6_PROXY_ROUTES on the ts6-remote container (container recreate), CF
  Access service token for the MacBook native connect, friends via Zitadel.
- **Fidelity** the pledit-wasm paint item above, before the playlist iframe
  ships.

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
  window with its screenshot gate; the M6 `QTAMP_WASM_REMOTE` build mode
  (EventSource glue, query-param config, remote host page, build script, mock
  server + cdp gate); the M7 ts4party-musicbot repo (container, entry.sh,
  driver + its 6 tests); the M8 local pieces in ts6-client (loopback-proxy
  plugin + tests, client-info player sections, PLAYER_DIST docroot support);
  the M6 server build + real-Chromium verification; the M7 image bring-up on
  the server including the WCL x86-64 fix chain in qtWasabi (linux-amd64
  overlay types, the va_copy string-TU rewrite, libuuid) and the in-image
  smoke of backend, pylon and driver; and this documentation. The remaining
  user-gated go-live steps are listed above.

(The pre-existing qtamp and qtWasabi codebase this builds on is the user's own
prior work, largely authored across earlier sessions; this attribution covers
only the networked-player split.)

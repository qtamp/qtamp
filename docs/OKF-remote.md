# OKF: networked qtamp (WASM frontend + GraphQL sync)

How one qtamp player runs on a server and shows up, live and synchronized, in
every viewer's browser and on native desktop clients — the real Winamp-Modern
skin rendered client-side, not a video of it.

This is the reference document for the split. It is the single source of truth
for the architecture, the wire protocol, and the build modes. The pylon's
`PROTOCOL.md` (once written, phase 7) restates the wire shapes for the server
side; this file owns the qtamp side and the overall picture.

## Why a split, not frame streaming

An earlier plan streamed rendered PNG frames from a headless qtamp to each
browser and forwarded clicks back. It was discarded: the frame path made every
viewer a passive screen, cost a server-side grab per frame, and could never
give a native desktop client (the user's MacBook) a real synced window.

The chosen design puts the **real qtWasabi engine** (skin + Maki VM) in each
viewer as a small WebAssembly build, and keeps the **audio + files + playlist**
on the server exactly once. They talk GraphQL: queries and mutations over HTTP,
push over SSE subscriptions. Because there is exactly one backend, every
head — browser iframes AND native `--connect` clients — shows the same state
and a click in any of them changes all of them.

The cut line is `qtWasabi::Host`. The engine (`deps/qtWasabi`) reaches the
outside world only through that abstract vtable, so a `RemoteHost` that answers
reads from a synced cache and turns writes into mutations slots in without
touching the engine.

## The picture

```
                 ┌──────────────────────── server (one process tree) ────────────────────────┐
 mp3/flac  ─────▶│  /music ──▶ qtamp --backend 18800  ──(PulseAudio music_out)──▶ TS mic      │
 (TS upload)     │              │  loopback HTTP+SSE control channel                            │
                 │              ▼                                                                │
                 │        qtamp-pylon :8789  ── GraphQL (Query/Mutation/Subscription) ──┐       │
                 └───────────────────────────────────────────────────────────────────── │ ──────┘
                                                                                          │
        browser iframe  ◀── WASM qtamp (RemoteHost, --connect /api/music/graphql) ◀──────┤
        MacBook native  ◀── native qtamp --connect https://ts4.party/api/music/graphql ◀─┘
```

Every head is a qtamp binary running the same engine and the same skin. The
only difference is which `Host` the factory in `main()` builds:

- `QtampHost` (local): the full audio pipeline — `QMediaPlayer` for metadata,
  `QAudioDecoder`→PCM, an always-on `QAudioSink`, the EQ DSP, the vis analyzer,
  and `PlaylistWindow` as the model. This is desktop qtamp as it always was,
  and it is what `--backend` runs headless on the server.
- `RemoteHost` (networked): answers every Host read from a cached
  `RemoteSnapshot` (with a client-side interpolated play position so the posbar
  is smooth without a 20 Hz network feed), turns every write into a GraphQL
  mutation, and applies pushed events to the cache. No audio, no files.

## The Host seam (`PlayerHost`)

`src/playerhost.h` — `class PlayerHost : public QObject, public qtWasabi::Host`.
The engine only needs the abstract `qtWasabi::Host` vtable, but the qtamp
*integration layer* (`QtampPlayerWindow`, `main()`) calls a handful of concrete
methods beyond it. `PlayerHost` gathers exactly that surface so the window and
`main()` depend on the base, never on a concrete subclass:

- playback entry points: `openPath`, `enqueueAndPlay`, `openFiles/FolderAndEnqueue`
- reads off the vtable: `songPath`, `currentSourceUrl`, `analyzerPtr` (null = no
  local vis, e.g. remote), the EQ band store `setEqBandValue/eqBandValue`
  (default routes through the `EQ_BAND` slider axis, so a remote host gets EQ
  for free), `reloadVisPrefs`, `bindWindow`, `isRemote`
- change signals: `sourceChanged / playbackStateChanged / metaDataChanged /
  playlistChanged` — these replace the direct `QMediaPlayer` signal connects in
  the window. `QtampHost` forwards its player's signals onto them; `RemoteHost`
  fires them when synced events arrive. Either way the window's existing
  repaint + `fireTitleChange` machinery runs unchanged.

`isRemote()` lets the window disable the local-only affordances (Open File/
Folder/URL, the library) when they cannot work.

## Wire protocol

One JSON vocabulary is used in both places — the loopback control channel
(`--backend`) and the GraphQL payloads — so the pylon mapping is a thin
envelope. Shapes:

**Snapshot (`RemoteSnapshot`)** — `{epoch, revision, serverNowMs, transport
{playing, paused, positionMs, positionAtMs, durationMs, volume, pan}, track
{title, artist, album, filename, displayTitle, decoder, bitrate, sampleRate,
channels}, playlist{revision, count, currentIndex, rows[{text, durationMs}]},
eq{on, auto, preamp, bands[10]}}`.

- `epoch` = a fresh id per backend boot. A changed epoch means "full resync,
  the revision counter restarted".
- `revision` = global monotonic counter, bumped on every change. Events out of
  order or with a gap > 1 set `needsResync`.
- `positionAtMs`/`serverNowMs` = the server clock at sampling time. The client
  anchors position at **local receipt time**, so interpolation error is only
  one-way latency and no clock-sync protocol is needed.
- eq bands are the Winamp 0..63 slider scale; `pan` is the 0..1 axis (0.5 =
  centre).

**Events (SSE)** — `state` (full snapshot, first frame after connect),
`transport`, `track`, `playlist` (full rows), `eq`, `ping` (every 5 s, also a
clock beacon and CDN idle-timeout defeat), and phase-2 `spectrum` (19 quantized
bytes ~10 Hz, subscriber-gated). Position is never pushed on a timer — clients
interpolate; only transport edges (play/pause/seek) push a `transport` event.

**Mutations** — 1:1 with Host writes: `play, pause, stop, next, prev, seek,
setVolume, setPan, setEqBand, setEqPreamp, setEqOn, setEqAuto, playlistPlayRow,
playlistSetCurrentRow` (the row ops carry `expectPlaylistRevision` so a click on
a stale row is rejected, not misapplied), plus explicit playlist ops that
replace the native right-click menus in a browser: `playlistAddPaths` (path-
allowlisted to the music root), `playlistRemoveRows`, `playlistClear`.

Writes are fire-and-forget with optimistic local echo where flicker matters
(seek re-anchors the position clock immediately; volume/pan/eq update the cache
at once; play/pause flip the flags — a wrong guess is corrected by the next
event within one latency).

## Build modes

- **desktop** (default): `QtampHost`, full audio, `--connect <url>` switches it
  to `RemoteHost` while keeping the same window/skin.
- **`--backend <port>`**: headless (`QT_QPA_PLATFORM=offscreen`, no skin, no
  window), the audio pipeline + playlist model + the loopback control channel.
  This is the server's player.
- **`QTAMP_WASM_REMOTE`** (CMake): the browser build — engine + skin +
  `RemoteHost`, no audio pipeline, no bundled demo track. Reads `?graphql=` and
  `?container=` from `location.search`. Kept single-threaded and under the 25
  MiB Cloudflare-Pages per-file limit.
- **`--container <ref>`**: render a chosen skin container (e.g. `Pledit`) as the
  root window, so the playlist gets its own WASM instance per iframe — sidesteps
  the unverified multi-toplevel-QWidget-under-WASM path.

## Testing

The split is built test-first; every layer is verifiable offscreen with no
TeamSpeak server and no network:

- unit (ctest): `remotestate_test` (snapshot round-trip, event apply, revision
  gaps, PositionClock interpolation), `ssereader_test` (adversarial chunking),
  `remotehost_test` (RemoteHost + an InjectedTransport — inject snapshot, assert
  every getter; inject events, assert signals + cache; call writes, assert the
  emitted mutation JSON).
- integration (scripts): `--backend` via curl; native `--connect` vs a
  zero-dep mock backend, with the existing `WASABIQT_CLICK_AT` knob proving a
  skin click reaches the mutation log; a full backend↔connect loopback pair.
- pylon (vitest): schema, push, path allowlist, plus a two-native-head
  convergence e2e.
- WASM: a `cdp-check-remote` variant driving the real browser build against the
  mock backend.
- the existing pixel-regression suite is the gate that the `PlayerHost` refactor
  changed nothing.

## Status

See `docs/OKF-remote-status.md` for what is built and working right now, and
which model did which part.

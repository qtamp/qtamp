# qtamp backend control-channel protocol

The contract between `qtamp --backend <port>` (the headless player that owns
audio, files and the playlist) and its consumers — primarily the qtamp-pylon,
which fans it out as GraphQL, and the test mocks, which implement this same
contract. The JSON shapes are shared verbatim with the GraphQL payloads; the
C++ parse/serialize lives in `src/remotestate.{h,cpp}` and is the single
tested implementation of the vocabulary (used by both the backend serializer
and the RemoteHost cache).

Transport: plain HTTP/1.1 on `127.0.0.1:<port>` only. The backend is a
loopback trust boundary; anything public fronts it (the pylon) and owns auth.

## Routes

```
GET  /state            → 200 application/json   full snapshot (below)
GET  /events           → 200 text/event-stream  SSE, first frame = `state`
POST /cmd              → 200 {"ok":true,"revision":N}
                        | 4xx {"ok":false,"error":"..."}
GET  /art/current      → 200 image/*  (album art; ETag = track revision)
                        | 404 when the current track has none
```

## The snapshot

```json
{
  "epoch": "0f3c…",              // fresh id per backend boot
  "revision": 412,               // global monotonic change counter
  "serverNowMs": 987654321,      // backend monotonic clock at sampling time
  "transport": { "playing": true, "paused": false,
                 "positionMs": 63210, "positionAtMs": 987654321,
                 "durationMs": 214000, "volume": 78, "pan": 0.5 },
  "track": { "title": "…", "artist": "…", "album": "…",
             "filename": "/music/x.mp3", "displayTitle": "Artist - Title",
             "decoder": "mpeg", "bitrate": 320, "sampleRate": 44100,
             "channels": 2 },
  "playlist": { "revision": 17, "count": 2, "currentIndex": 1,
                "rows": [ {"text": "…", "durationMs": 214000} ] },
  "eq": { "on": true, "auto": false, "preamp": 31,
          "bands": [31,31,28,31,31,31,31,31,31,31] }
}
```

- `epoch` changes = the backend restarted: consumers drop everything and
  re-snapshot (`applyEvent` answers `NeedsResync`).
- `revision` bumps on EVERY observable change. Events carry it; consumers drop
  stale revisions and re-snapshot on gaps (newest-wins queues may drop
  backlog, so a gap means some section may be stale).
- `positionAtMs`/`serverNowMs`: the backend's monotonic clock when the
  position was sampled. Clients anchor interpolation at LOCAL receipt time
  (error = one-way latency; no clock sync), see `PositionClock`.
- eq values are the Winamp 0..63 slider scale; `pan` is the 0..1 axis with
  0.5 = centre; `volume` is 0..100.

## Events (SSE)

```
event: state       data: <full snapshot>            (first frame, resyncs)
event: transport   data: {"epoch","revision","serverNowMs","transport":{…}}
event: track       data: {…,"track":{…}}
event: playlist    data: {…,"playlist":{…full rows…}}
event: eq          data: {…,"eq":{…}}
event: ping        data: {"serverNowMs":…}          (every 5 s, clock beacon)
event: spectrum    data: {"serverNowMs":…,"b64":"…"} (phase 2, 19 bytes,
                                                      only while subscribed)
```

Rules:
- position is NEVER pushed on a timer; only transport edges (play, pause,
  stop, seek, track change) emit `transport`. Clients interpolate between
  events.
- `playlist` resends the full row set on every change (bounded and race-free;
  delta framing is a documented future extension, not part of v1).
- every sectional event carries the global `revision` and the `epoch`.

## Commands (POST /cmd)

Body `{"op": "<name>", "args": {…}}`. Ops, 1:1 with the Host writes:

```
play | pause | stop | next | prev
seek              {"ms": 63210}
setVolume         {"v": 0..100}
setPan            {"v": 0..1}
setEqOn           {"on": true}         setEqAuto {"on": true}
setEqPreamp       {"value": 0..63}
setEqBand         {"band": 0..9, "value": 0..63}
playlistPlayRow   {"row": N, "expectPlaylistRevision": 17}
playlistSetCurrentRow {"row": N, "expectPlaylistRevision": 17}
playlistAddPaths  {"paths": ["/music/a.mp3", …]}
playlistRemoveRows{"rows": [2,5]}
playlistClear     {}
open              {"url": "file:///music/a.mp3"}
```

- `expectPlaylistRevision` (optional): the backend rejects the row op with
  `{"ok":false,"error":"playlistRevision mismatch"}` when the playlist has
  changed since the client rendered — a click on a stale row must not act on
  the wrong track. The client re-snapshots on rejection.
- `open`/`playlistAddPaths` are path-gated to the configured music root: this
  channel is loopback-only, but the pylon forwards viewer input into it.
- `play` on a cold player (nothing loaded, playlist non-empty) plays the
  playlist's current row — the same thing a double-click does — so a driver's
  autoplay works before any UI interaction happened.

## pledit menu verbs

The skin's playlist chrome buttons (`PE_Add`, `PE_Rem`, `PE_Sel`, `PE_Misc`,
`PE_List`) pop native menus in desktop qtamp. Remotely they are disabled in
v1: RemoteHost consumes the click (so it does not fall through to a window
drag) and does nothing. The explicit playlist ops above exist precisely so a
later web-side menu can drive the same functionality.

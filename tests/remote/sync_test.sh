#!/usr/bin/env bash
# End-to-end sync proof, fully local, no TS server / pylon / browser:
# one `qtamp --backend` (the audio+playlist owner) and a second
# `qtamp --connect` head (a real RemoteHost-backed player, offscreen).
# Mutate through the backend's control channel, then assert the
# connected head's view converges — i.e. the RemoteHost saw the SSE
# events and updated its cache. The head exposes its cache via a tiny
# --probe flag (added for exactly this: print a snapshot field and exit).
set -u

BUILD="${1:-$(cd "$(dirname "$0")/../../build" && pwd)}"
QTAMP="$BUILD/qtamp"
SKIN="$(cd "$(dirname "$0")/../.." && pwd)/wasm/assets/skin/QTAMP-WinampModernPP"
FIXTURE="$(cd "$(dirname "$0")/../.." && pwd)/wasm/assets/demo.wav"
export QTAMP_MUSIC_ROOT="$(dirname "$FIXTURE")"

[ -x "$QTAMP" ] || { echo "no qtamp binary at $QTAMP"; exit 1; }

fail=0
contains() { if echo "$1" | grep -q "$2"; then echo "  ok   $3"; else echo "  FAIL $3: [$2] not in [$1]"; fail=1; fi; }

tmp="$(mktemp -d)"
BPID=""
trap 'kill $BPID 2>/dev/null; rm -rf "$tmp"' EXIT

# 1) backend
QT_QPA_PLATFORM=offscreen "$QTAMP" --backend 0 >"$tmp/bout" 2>"$tmp/berr" &
BPID=$!
PORT=""
for _ in $(seq 1 100); do
    PORT="$(sed -n 's/.*listening on 127.0.0.1:\([0-9]*\).*/\1/p' "$tmp/berr" | head -1)"
    [ -n "$PORT" ] && break
    sleep 0.1
done
[ -n "$PORT" ] || { echo "backend never came up"; cat "$tmp/berr"; exit 1; }
BASE="http://127.0.0.1:$PORT"
echo "backend on $BASE"

# 2) mutate the backend: add the fixture + play
curl -s -X POST "$BASE/cmd" \
    -d "{\"op\":\"playlistAddPaths\",\"args\":{\"paths\":[\"$FIXTURE\"]}}" >/dev/null
curl -s -X POST "$BASE/cmd" -d '{"op":"play","args":{}}' >/dev/null
sleep 0.3

# 3) a connected head probes the backend and prints a snapshot field.
#    --probe <field> fetches /state through a RemoteHost and prints it.
PROBE_PLAYLIST="$(QT_QPA_PLATFORM=offscreen "$QTAMP" \
    --connect "$BASE" --probe playlistCount 2>"$tmp/herr")"
contains "$PROBE_PLAYLIST" "1" "connected head sees playlist count 1"

PROBE_PLAYING="$(QT_QPA_PLATFORM=offscreen "$QTAMP" \
    --connect "$BASE" --probe playing 2>>"$tmp/herr")"
contains "$PROBE_PLAYING" "true" "connected head sees playing=true"

# 4) change on the backend, re-probe: convergence.
curl -s -X POST "$BASE/cmd" -d '{"op":"pause","args":{}}' >/dev/null
sleep 0.2
PROBE_PAUSED="$(QT_QPA_PLATFORM=offscreen "$QTAMP" \
    --connect "$BASE" --probe paused 2>>"$tmp/herr")"
contains "$PROBE_PAUSED" "true" "connected head converges to paused=true"

if [ "$fail" = 0 ]; then echo "sync_test: all checks passed"; else echo "sync_test: FAILURES"; fi
exit $fail

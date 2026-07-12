#!/usr/bin/env bash
# Integration test for `qtamp --backend`: drives the loopback control
# channel (docs/PROTOCOL.md) with curl and asserts the state shape, the
# command surface, and that mutations push SSE events.
#
# Offscreen, no audio device required: the assertions are about state and
# eventing, not audible playback (position advance needs a real sink and
# is deliberately NOT asserted here).
set -u

BUILD="${1:-$(cd "$(dirname "$0")/../../build" && pwd)}"
QTAMP="$BUILD/qtamp"
SKIN="$(cd "$(dirname "$0")/../.." && pwd)/wasm/assets/skin/QTAMP-WinampModernPP"
FIXTURE="$(cd "$(dirname "$0")/../.." && pwd)/wasm/assets/demo.wav"

[ -x "$QTAMP" ] || { echo "no qtamp binary at $QTAMP"; exit 1; }

fail=0
check() { if [ "$1" != "$2" ]; then echo "  FAIL $3: expected [$2] got [$1]"; fail=1; else echo "  ok   $3"; fi; }
contains() { if echo "$1" | grep -q "$2"; then echo "  ok   $3"; else echo "  FAIL $3: [$2] not in output"; fail=1; fi; }

# Music root = the fixture's dir, so playlistAddPaths of the fixture is
# allowed by the path guard.
export QTAMP_MUSIC_ROOT="$(dirname "$FIXTURE")"

tmp="$(mktemp -d)"
trap 'kill "$PID" 2>/dev/null; rm -rf "$tmp"' EXIT

# Ephemeral port; the backend prints the real one to stderr.
QT_QPA_PLATFORM=offscreen "$QTAMP" --backend 0 >"$tmp/out" 2>"$tmp/err" &
PID=$!

PORT=""
for _ in $(seq 1 100); do
    PORT="$(sed -n 's/.*listening on 127.0.0.1:\([0-9]*\).*/\1/p' "$tmp/err" | head -1)"
    [ -n "$PORT" ] && break
    sleep 0.1
done
[ -n "$PORT" ] || { echo "backend never printed its port"; cat "$tmp/err"; exit 1; }
BASE="http://127.0.0.1:$PORT"
echo "backend on $BASE"

# --- /state shape ---
STATE="$(curl -s "$BASE/state")"
contains "$STATE" '"epoch"'    "state has epoch"
contains "$STATE" '"revision"' "state has revision"
contains "$STATE" '"transport"' "state has transport"
contains "$STATE" '"playlist"' "state has playlist"
contains "$STATE" '"eq"' "state has eq"

# --- SSE: open a stream, then mutate, assert an event arrives ---
curl -sN "$BASE/events" >"$tmp/sse" 2>/dev/null &
SSE=$!
sleep 0.4   # let the stream open + receive the initial `state` frame

# Add the fixture (allowed path), then play.
ADD="$(curl -s -X POST "$BASE/cmd" \
    -d "{\"op\":\"playlistAddPaths\",\"args\":{\"paths\":[\"$FIXTURE\"]}}")"
contains "$ADD" '"ok":true' "playlistAddPaths ok"

PLAY="$(curl -s -X POST "$BASE/cmd" -d '{"op":"play","args":{}}')"
contains "$PLAY" '"ok":true' "play ok"

# A path OUTSIDE the music root must be rejected.
BAD="$(curl -s -X POST "$BASE/cmd" \
    -d '{"op":"playlistAddPaths","args":{"paths":["/etc/passwd"]}}')"
contains "$BAD" '"ok":false' "path guard rejects /etc/passwd"

# An unknown op is a 400/ok:false.
UNK="$(curl -s -X POST "$BASE/cmd" -d '{"op":"frobnicate","args":{}}')"
contains "$UNK" '"ok":false' "unknown op rejected"

sleep 0.6   # let pushed events land
kill "$SSE" 2>/dev/null
contains "$(cat "$tmp/sse")" 'event: state'    "SSE delivered initial state"
contains "$(cat "$tmp/sse")" 'event: playlist' "SSE pushed a playlist event"

# Final state reflects the add.
STATE2="$(curl -s "$BASE/state")"
contains "$STATE2" '"count":1' "playlist count is 1 after add"

if [ "$fail" = 0 ]; then echo "backend_test: all checks passed"; else echo "backend_test: FAILURES"; fi
exit $fail

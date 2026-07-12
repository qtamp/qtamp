#!/usr/bin/env bash
# The full networked-player loop, entirely local: a real `qtamp
# --backend` (audio owner), the built qtamp-pylon in front of it, a
# GraphQL mutation through the pylon, and a real `qtamp --connect` head
# whose --probe answers over GraphQL (schema v2, the canonical pylon).
# Proves every hop of the production chain except TS/CF.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QTAMP="$ROOT/build/qtamp"
FIXTURE="$ROOT/wasm/assets/demo.wav"
PYLON_PORT="${PYLON_PORT:-$((18500 + RANDOM % 1000))}"
export QTAMP_MUSIC_ROOT="$(dirname "$FIXTURE")"

[ -x "$QTAMP" ] || { echo "no qtamp binary at $QTAMP (build first)"; exit 1; }
[ -f "$ROOT/deps/qtWasabi/api/pylon/.pylon/index.js" ] || {
    echo "pylon not built (cd deps/qtWasabi/api/pylon && npm run build)"; exit 1; }

fail=0
contains() { if echo "$1" | grep -q "$2"; then echo "  ok   $3"; else echo "  FAIL $3: [$2] not in [$1]"; fail=1; fi; }

tmp="$(mktemp -d)"
BPID=""; PPID_PYLON=""
trap 'kill $BPID $PPID_PYLON 2>/dev/null; rm -rf "$tmp"' EXIT

# 1) the backend
PSOCK="$tmp/player.sock"
QT_QPA_PLATFORM=offscreen "$QTAMP" --serve-player "$PSOCK" >"$tmp/bout" 2>"$tmp/berr" &
BPID=$!
for _ in $(seq 1 100); do [ -S "$PSOCK" ] && break; sleep 0.1; done
[ -S "$PSOCK" ] || { echo "player never came up"; exit 1; }
echo "player on unix:$PSOCK"

# 2) the pylon in front of it
( cd "$ROOT/deps/qtWasabi/api/pylon" && exec env PORT="$PYLON_PORT" \
    QTAMP_PLAYER_SOCKET="$PSOCK" \
    PYLON_DISABLE_TELEMETRY=true \
    node .pylon/index.js >"$tmp/pout" 2>&1 ) &
PPID_PYLON=$!
for _ in $(seq 1 100); do
    curl -sf "http://127.0.0.1:$PYLON_PORT/graphql?query=%7Bplayer%7Brevision%7D%7D" >/dev/null && break
    sleep 0.1
done
PBASE="http://127.0.0.1:$PYLON_PORT"
echo "pylon on :$PYLON_PORT"

# 3) mutate over GRAPHQL (the web/driver path)
ADD="$(curl -s "$PBASE/graphql" -H 'content-type: application/json' -d \
  "{\"query\":\"mutation(\$p:[String!]!){playlistAdd(paths:\$p){ok player{playlist{rowCount}}}}\",\"variables\":{\"p\":[\"$FIXTURE\"]}}")"
contains "$ADD" '"rowCount":1' "GraphQL playlistAdd through the pylon"

PLAY="$(curl -s "$PBASE/graphql" -H 'content-type: application/json' \
    -d '{"query":"mutation{play{ok player{transport{playing}}}}"}')"
contains "$PLAY" '"playing":true' "GraphQL play through the pylon"

# 4) a REAL qtamp head connects THROUGH the pylon passthrough
PROBE="$(QT_QPA_PLATFORM=offscreen "$QTAMP" --connect "graphql+$PBASE" --probe playing 2>/dev/null)"
contains "$PROBE" "true" "--connect head through the pylon sees playing=true"

PROBE2="$(QT_QPA_PLATFORM=offscreen "$QTAMP" --connect "graphql+$PBASE" --probe playlistCount 2>/dev/null)"
contains "$PROBE2" "1" "--connect head through the pylon sees the playlist"

# 5) subscription carries the pause pushed via the backend directly
curl -sN "$PBASE/graphql?query=subscription%7BplayerEvents%7Bkind%20transport%7Bpaused%7D%7D%7D" \
    -H 'accept: text/event-stream' >"$tmp/sub" 2>/dev/null &
SUBPID=$!
sleep 1.2
curl -s "$PBASE/graphql" -H 'content-type: application/json' -d '{"query":"mutation{pause{ok}}"}' >/dev/null
sleep 1.5
kill $SUBPID 2>/dev/null
contains "$(cat "$tmp/sub")" '"paused":true' "playerEvents subscription pushed the pause"

if [ "$fail" = 0 ]; then echo "e2e-remote: all checks passed"; else echo "e2e-remote: FAILURES"; fi
exit $fail

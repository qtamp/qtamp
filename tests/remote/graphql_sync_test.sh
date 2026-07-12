#!/usr/bin/env bash
# Wasabi 2 V1 gate: the head speaks GraphQL.  Full chain — real
# `qtamp --backend` (legacy channel, player-internal) → the canonical
# qtwasabi-pylon (schema v2, typed subscriptions) → real `--connect`
# probe heads over BOTH GraphQL transports:
#   graphql+http://127.0.0.1:<port>   (TCP)
#   graphql+unix://<socket path>      (QLocalSocket + chunked SSE)
# Asserts convergence after GraphQL mutations, exactly like the legacy
# sync_test.sh does for the control channel.
set -u

BUILD="${1:-$(cd "$(dirname "$0")/../../build" && pwd)}"
QTAMP="$BUILD/qtamp"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PYLON="$REPO/deps/qtWasabi/api/pylon"
export QTAMP_MUSIC_ROOT="$REPO/wasm/assets"

[ -x "$QTAMP" ] || { echo "no qtamp binary at $QTAMP"; exit 1; }
[ -f "$PYLON/.pylon/index.js" ] || {
    echo "canonical pylon not built (cd deps/qtWasabi/api/pylon && npm run build)"; exit 1; }

fail=0
contains() { if echo "$1" | grep -q "$2"; then echo "  ok   $3"; else echo "  FAIL $3: [$2] not in [$1]"; fail=1; fi; }

tmp="$(mktemp -d)"
EPORT=$((18000 + RANDOM % 2000))
SOCK="${XDG_RUNTIME_DIR:-/tmp}/qtwasabi/gqlsync-$$.sock"
BPID=""; PPID2=""
trap 'kill $BPID $PPID2 2>/dev/null; rm -rf "$tmp"; rm -f "$SOCK"' EXIT

# 1) the player (audio owner)
PSOCK="$tmp/player.sock"
QT_QPA_PLATFORM=offscreen "$QTAMP" --serve-player "$PSOCK" >"$tmp/bout" 2>"$tmp/berr" &
BPID=$!
for _ in $(seq 1 100); do [ -S "$PSOCK" ] && break; sleep 0.1; done
[ -S "$PSOCK" ] || { echo "player never came up"; exit 1; }
echo "player on unix:$PSOCK"

# 2) the canonical pylon (TCP + unix socket)
( cd "$PYLON" && PORT=$EPORT PYLON_SOCKET="$SOCK" \
    QTAMP_PLAYER_SOCKET="$PSOCK" \
    PYLON_DISABLE_TELEMETRY=true node .pylon/index.js >"$tmp/pout" 2>&1 ) &
PPID2=$!
for _ in $(seq 1 60); do
    curl -sf "http://127.0.0.1:$EPORT/graphql" -X POST -H 'content-type: application/json' \
      -d '{"query":"{apiInfo{schemaVersion}}"}' >/dev/null 2>&1 && break
    sleep 0.25
done
echo "pylon :$EPORT + $SOCK"

GQL() { curl -s "http://127.0.0.1:$EPORT/graphql" -X POST -H 'content-type: application/json' -d "$1"; }
PROBE() { QT_QPA_PLATFORM=offscreen "$QTAMP" --connect "$1" --probe "$2" 2>/dev/null; }

# 3) mutate over GraphQL, converge over BOTH transports
GQL "{\"query\":\"mutation{playlistAdd(paths:[\\\"$QTAMP_MUSIC_ROOT/demo.wav\\\"]){ok}}\"}" >/dev/null
GQL '{"query":"mutation{play{ok}}"}' >/dev/null

contains "$(PROBE "graphql+http://127.0.0.1:$EPORT" playing)"       "true" "graphql+http: playing converges"
contains "$(PROBE "graphql+http://127.0.0.1:$EPORT" playlistCount)" "1"    "graphql+http: playlist converges"
contains "$(PROBE "graphql+unix://$SOCK" playing)"                  "true" "graphql+unix: playing converges"
contains "$(PROBE "graphql+unix://$SOCK" playlistCount)"            "1"    "graphql+unix: playlist converges"

# 4) pause via GraphQL, both transports see it (event-driven update)
GQL '{"query":"mutation{pause{ok}}"}' >/dev/null
contains "$(PROBE "graphql+http://127.0.0.1:$EPORT" paused)" "true" "graphql+http: pause converges"
contains "$(PROBE "graphql+unix://$SOCK" paused)"            "true" "graphql+unix: pause converges"

# 5) rejected mutation surfaces as CommandResult, not transport failure
GUARD="$(GQL '{"query":"mutation{playRow(row:0,expectPlaylistRevision:999){ok error}}"}')"
contains "$GUARD" '"ok":false' "revision guard rejects"

if [ "$fail" = 0 ]; then echo "graphql_sync_test: all checks passed"; else echo "graphql_sync_test: FAILURES"; fi
exit $fail

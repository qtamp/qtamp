#!/usr/bin/env bash
# V6 — orchestrated default: qtamp spawns the player+pylon trio on
# per-instance unix sockets and connects the head over graphql+unix.
# This is the PERMANENT live-stack pixel gate (MAE < 3, kept forever)
# plus lifecycle checks: single-instance enqueue, pylon respawn, and
# trio teardown with the launcher.
set -u
BUILD="${1:-$(cd "$(dirname "$0")/../../build" && pwd)}"
QTAMP="$BUILD/qtamp"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PYLON="$REPO/deps/qtWasabi/api/pylon"
SKIN="$HOME/.winamp/skins/WinampModernPP"
BASE="$REPO/tests/regression/winampmodernpp.png"
[ -x "$QTAMP" ] || { echo "no qtamp at $QTAMP"; exit 2; }
[ -f "$PYLON/.pylon/index.js" ] || { echo "pylon not built"; exit 2; }
[ -d "$SKIN" ] || { echo "skin missing"; exit 77; }

fail=0
tmp="$(mktemp -d)"
RUN="$tmp/run"; mkdir -p "$RUN"
export QTWASABI_PYLON_DIR="$PYLON"
cleanup() {
  kill "${LIVEPID:-}" 2>/dev/null || true
  pkill -f "serve-player $RUN/qtwasabi" 2>/dev/null || true
  rm -rf "$tmp" || true
}
trap cleanup EXIT

# ── 1) live-stack pixel gate (MAE < 3, the permanent gate) ──────────
HOME="$tmp" XDG_RUNTIME_DIR="$RUN" QT_QPA_PLATFORM=offscreen \
  timeout 45 "$QTAMP" --orchestrated --modern-skin "$SKIN" \
  --screenshot "$tmp/live.png" >/dev/null 2>&1
if [ -f "$tmp/live.png" ]; then
  MAE=$(python3 - "$tmp/live.png" "$BASE" <<'PY'
import sys
from PIL import Image
import numpy as np
a=np.asarray(Image.open(sys.argv[1]).convert("RGB"),dtype=float)
b=np.asarray(Image.open(sys.argv[2]).convert("RGB"),dtype=float)
print("%.3f" % (np.abs(a-b).mean() if a.shape==b.shape else 999))
PY
)
  if python3 -c "import sys; sys.exit(0 if float('$MAE')<3 else 1)"; then
    echo "  OK     live-stack pixel gate (MAE=$MAE < 3)"
  else
    echo "  FAIL   live-stack pixel gate (MAE=$MAE)"; fail=1
  fi
else
  echo "  FAIL   live-stack render produced no screenshot"; fail=1
fi

# ── 2) single instance: a second launch enqueues on the running trio ─
# Keep a live orchestrated instance up (offscreen, no screenshot, so it
# stays in the event loop), seeded with one track.
SEED="$REPO/wasm/assets/demo.wav"
HOME="$tmp" XDG_RUNTIME_DIR="$RUN" QT_QPA_PLATFORM=offscreen \
  "$QTAMP" --orchestrated --modern-skin "$SKIN" "$SEED" \
  >"$tmp/live.log" 2>&1 &
LIVEPID=$!
GSOCK="$RUN/qtwasabi/qtamp-single.gsock"
for _ in $(seq 1 100); do [ -f "$GSOCK" ] && break; sleep 0.1; done
# Wait for the trio's graphql socket to answer.
GS=""
for _ in $(seq 1 100); do
  GS="$(cat "$GSOCK" 2>/dev/null)"
  [ -n "$GS" ] && curl -sf --unix-socket "$GS" http://local/graphql \
    -X POST -H 'content-type: application/json' \
    -d '{"query":"{player{playlist{rowCount}}}"}' >/dev/null 2>&1 && break
  sleep 0.1
done
# Wait for the async CLI seed to land (QTimer + posted command).
rc() { curl -s --unix-socket "$GS" http://local/graphql -X POST \
  -H 'content-type: application/json' \
  -d '{"query":"{player{playlist{rowCount}}}"}' \
  | grep -o '"rowCount":[0-9]*' | grep -o '[0-9]*' | head -1; }
count0=0
for _ in $(seq 1 60); do
  count0=$(rc); [ "${count0:-0}" -ge 1 ] 2>/dev/null && break; sleep 0.2
done
# Second launch with a file arg — should enqueue, not spawn.
HOME="$tmp" XDG_RUNTIME_DIR="$RUN" QT_QPA_PLATFORM=offscreen \
  timeout 15 "$QTAMP" "$SEED" >"$tmp/second.log" 2>&1
sleep 1
count1=0
for _ in $(seq 1 40); do
  count1=$(rc); [ "${count1:-0}" -gt "${count0:-0}" ] 2>/dev/null && break
  sleep 0.2
done
if grep -q 'enqueued' "$tmp/second.log" && \
   [ "${count1:-0}" -gt "${count0:-0}" ] 2>/dev/null; then
  echo "  OK     single-instance enqueue ($count0 -> $count1, no 2nd trio)"
else
  echo "  FAIL   single-instance (log: $(tail -1 "$tmp/second.log"); $count0 -> $count1)"
  fail=1
fi

# ── 3) pylon respawn: kill the child pylon, head recovers ───────────
PYPID=$(pgrep -f "index.js" -P "$LIVEPID" 2>/dev/null | head -1)
[ -z "$PYPID" ] && PYPID=$(pgrep -f "$RUN/qtwasabi/qtamp-gql" 2>/dev/null | head -1)
if [ -n "$PYPID" ]; then
  kill "$PYPID" 2>/dev/null
  # After respawn the fresh pylon rebinds the same socket; probe again.
  ok=0
  for _ in $(seq 1 100); do
    curl -sf --unix-socket "$GS" http://local/graphql -X POST \
      -H 'content-type: application/json' \
      -d '{"query":"{apiInfo{schemaVersion}}"}' >/dev/null 2>&1 \
      && { ok=1; break; }
    sleep 0.2
  done
  [ "$ok" = 1 ] && echo "  OK     pylon respawn (head recovers)" \
    || { echo "  FAIL   pylon respawn"; fail=1; }
else
  echo "  SKIP   pylon respawn (pylon pid not found)"
fi

# ── 4) teardown: killing the launcher reaps the trio ────────────────
kill "$LIVEPID" 2>/dev/null
sleep 1
LEFT=$(pgrep -f "$RUN/qtwasabi/qtamp-player" 2>/dev/null | wc -l)
[ "$LEFT" = 0 ] && echo "  OK     trio reaped with the launcher" \
  || { echo "  FAIL   $LEFT orphan(s) survived"; fail=1; }

# ── 5) startup-failure fallback: orchestration is ATTEMPTED (default
# interactive mode, no --screenshot which would skip the trio) and,
# when it fails, the launcher falls back to the embedded player and
# KEEPS RUNNING instead of dying.  A short timeout kills the still-live
# process (exit 124); a fatal death would exit 7 well before that.
FBH="$tmp/fbhome"; FBR="$tmp/fbrun"; mkdir -p "$FBH" "$FBR"
HOME="$FBH" XDG_RUNTIME_DIR="$FBR" QTWASABI_ORCH_FAIL=1 \
  QT_QPA_PLATFORM=offscreen timeout 4 "$QTAMP" --modern-skin "$SKIN" \
  "$SEED" >"$tmp/fb.log" 2>&1
fbrc=$?
if [ "$fbrc" = 124 ] && grep -q 'embedded' "$tmp/fb.log"; then
  echo "  OK     orch-start failure falls back to embedded (stays alive)"
else
  echo "  FAIL   fallback (rc=$fbrc, expected 124 + 'embedded' log)"
  grep -iE 'embedded|orchestr|fail' "$tmp/fb.log" | grep -iv vdpau | head -2
  fail=1
fi
# --orchestrated must NOT mask a broken stack: exit 7 (screenshot mode
# still forces the trio via the flag).
HOME="$FBH" XDG_RUNTIME_DIR="$FBR" QTWASABI_ORCH_FAIL=1 \
  QT_QPA_PLATFORM=offscreen timeout 30 "$QTAMP" --orchestrated \
  --modern-skin "$SKIN" --screenshot "$tmp/o.png" >/dev/null 2>&1
[ $? = 7 ] && echo "  OK     --orchestrated fails hard (exit 7)" \
  || { echo "  FAIL   --orchestrated did not exit 7"; fail=1; }

# ── 7) pylon boot crash-loop: the pylon dies instantly on every spawn
# (QTWASABI_NODE=/bin/false).  The launcher must NOT recurse/hang — it
# falls back to embedded and stays alive.
HOME="$FBH" XDG_RUNTIME_DIR="$FBR" QTWASABI_NODE=/bin/false \
  QT_QPA_PLATFORM=offscreen timeout 25 "$QTAMP" --modern-skin "$SKIN" \
  "$SEED" >"$tmp/bl.log" 2>&1
blrc=$?
if [ "$blrc" = 124 ] && grep -q 'embedded' "$tmp/bl.log"; then
  echo "  OK     pylon boot crash-loop falls back (no hang/recursion)"
else
  echo "  FAIL   boot crash-loop (rc=$blrc)"; fail=1
fi

[ $fail = 0 ] && echo "orchestrated_test: all checks passed" \
  || echo "orchestrated_test: FAILURES"
exit $fail

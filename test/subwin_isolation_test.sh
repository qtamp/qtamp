#!/usr/bin/env bash
# subwin_isolation_test.sh — offscreen regression test: resizing a
# SUBWINDOW (Playlist Editor) must not reflow the MAIN PLAYER.
#
# Encodes the "resizing the PL shifts the player's EQ drawer" corruption:
# async VM entries (Maki timers, animation-finished) re-entering the VM
# under whichever window happened to be active resolved byId lookups
# against the WRONG window's registry and wrote garbage geometry.  The
# fixes under test: per-root scoping of the onResize cascade
# (firePerObjectResize iterates the active root's widgets only) and
# owning-root bracketing of timer/animation VM entries (g_timerRoot).
#
# Mechanism: WASABIQT_TEST_SUBWIN_RESIZE="pledit:drag:WxH" synthesizes a
# real bottom-right edge drag through the SkinView mouse handlers (press,
# moves with event pumping so timers interleave, release), then the main
# window is grabbed.  Baseline = the same drag to the pledit's own
# default size (identical code path + timing, no actual size change).
#
# The two grabs are compared over the player's DRAWER REGION (y >= 140):
# below the LCD/vis area, so playback-time digits can't false-positive.
#
# Exit 0 = PASS (pixel-identical), 1 = FAIL.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/qtamp"
SKIN="$HOME/.winamp/skins/Winamp Modern"
MUSIC="$HOME/Music/Achtung Baby"
OUT=/tmp/subwin_isolation
mkdir -p "$OUT"

run() {  # $1 = drag spec, $2 = output png
  QT_QPA_PLATFORM=offscreen WASABIQT_COLORTHEME=Default \
  WASABIQT_TEST_SUBWIN_RESIZE="$1" \
  "$BIN" --modern-skin "$SKIN" "$MUSIC" --screenshot "$2" 2>/dev/null
}

echo "== baseline: no-op drag (pledit default size) =="
run "pledit:drag:436x164" "$OUT/base.png"
echo "== test: drag the pledit to 800x320 =="
run "pledit:drag:800x320" "$OUT/drag.png"

python3 - "$OUT/base.png" "$OUT/drag.png" <<'EOF'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGBA')
b = Image.open(sys.argv[2]).convert('RGBA')
if a.size != b.size:
    print(f"FAIL: size mismatch {a.size} vs {b.size}"); sys.exit(1)
diff = [(x, y) for y in range(140, a.size[1]) for x in range(a.size[0])
        if a.getpixel((x, y)) != b.getpixel((x, y))]
if diff:
    xs = [p[0] for p in diff]; ys = [p[1] for p in diff]
    print(f"FAIL: {len(diff)} drawer-region pixels differ "
          f"(bbox x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}) — "
          f"the PL resize leaked into the player")
    sys.exit(1)
print("PASS: player drawer pixel-identical across a PL edge-drag")
EOF

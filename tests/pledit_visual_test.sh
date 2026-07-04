#!/usr/bin/env bash
# pledit_visual_test.sh — objective pixel test for the Bento player's
# file-info | playlist divider (the seam the user kept circling).
#
# Encodes the three lessons from getting this wrong repeatedly. The
# divider must be ALL of:
#   (1) GREY, not a black gap          — the original "gray-black-gray" bug
#   (2) TEXTURED, not a flat fill      — real Winamp draws a 3-D bevel/groove
#   (3) on a player that stays DARK    — don't globally-grey the chrome
#
# Mechanism (deps/qtWasabi): Layout.cpp reserves the 8px Wasabi sizer gap
# between frame panes (resizeable frames only) and renders, IN that gap, a
# grey base fill + the divider bitmap groove; the panes' own bevels frame
# it — a textured 3-D divider, while the panes (and their buttons) stop
# before the gap so nothing overlaps it. LayerPainter `color=` fill backs it.
#
# Exit 0 = PASS, 1 = FAIL.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/qtamp"
SKIN="$HOME/.winamp/skins/Bento"
OUT=/tmp/pledit_test
mkdir -p "$OUT"
IM() { magick "$@"; }

TRACKS=(
  "/home/snekmin/Kordhell - MURDER IN MY MIND.mp3"
  "/home/snekmin/Die Prinzen - Alles nur geklaut.mp3"
  "/home/snekmin/The Smashing Pumpkins - Bullet With Butterfly Wings.flac"
)

# Divider band in an 800x600 Bento render: the textured grey divider sits
# at x~586..598, sampled over the upper player strip.
DX0=586 ; DX1=598 ; DY0=40 ; DY1=86

echo "== render qtamp 800x600 offscreen =="
QT_QPA_PLATFORM=offscreen "$BIN" --modern-skin "$SKIN" "${TRACKS[@]}" \
  --screenshot "$OUT/mine.png" 2>/dev/null

# Collect luma samples across the divider band.
lumas=()
for (( y=DY0; y<=DY1; y+=10 )); do
  for (( x=DX0; x<=DX1; x++ )); do
    v=$(IM "$OUT/mine.png" -format "%[fx:int(255*p{$x,$y}.intensity)]" info: 2>/dev/null)
    lumas+=("${v:-0}")
  done
done
read MEAN MIN MAX <<<"$(printf '%s\n' "${lumas[@]}" | awk '
  {s+=$1; n++; if(NR==1||$1<mn)mn=$1; if(NR==1||$1>mx)mx=$1}
  END{printf "%.1f %d %d", (n?s/n:0), mn, mx}')"
RANGE=$((MAX - MIN))

# Player display must stay DARK (regression guard for global-grey).
DISP=$(IM "$OUT/mine.png" -format "%[fx:int(255*p{300,40}.intensity)]" info: 2>/dev/null)

echo "divider band  mean=$MEAN (want 35..100: grey, not black gap)"
echo "              range=$RANGE (max=$MAX min=$MIN; want >30: textured, not flat)"
echo "player display luma=$DISP (want <=30: dark, not global-grey)"

IM "$OUT/mine.png" -crop 70x110+555+22 +repage -scale 420x660 "$OUT/seam_zoom.png" 2>/dev/null
echo "seam zoom: $OUT/seam_zoom.png"

echo
echo "== VERDICT =="
awk -v m="$MEAN" -v r="$RANGE" -v d="$DISP" 'BEGIN{
  grey    = (m+0 >= 35 && m+0 <= 100);
  textured= (r+0 > 30);
  dark    = (d+0 <= 30);
  if (!grey)     print "FAIL: divider not grey (mean="m") — black gap or wrong colour";
  else if (!textured) print "FAIL: divider is flat (luma range="r"<=30) — needs the 3-D bevel/groove";
  else if (!dark) print "FAIL: player not dark (display luma="d">30) — global-grey regression";
  else print "PASS: divider grey (mean="m") AND textured (range="r") AND player dark (luma="d")";
  exit (grey && textured && dark ? 0 : 1);
}'

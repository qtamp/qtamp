#!/usr/bin/env bash
# Menu-tree gate (Wasabi 2 V5d): Qt menus are invisible to the pixel
# suite, so the context menu + every WA5 popup are dumped headlessly
# (WASABIQT_DUMP_MENU short-circuits exec) and byte-compared against
# committed goldens.  Two fixtures pin the dynamic state: A = fresh
# config (empty recent/bookmarks, stopped FakeHost), B = two recent
# files + two bookmarks.  A third leg proves the id-dispatch chain the
# dump cannot see: WASABIQT_TEST_HEADMENU_PICK selects an action by id
# and the persisted effect is asserted.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
QTAMP="${1:-$REPO/build}/qtamp"
SKIN="$HOME/.winamp/skins/WinampModernPP"
[ -x "$QTAMP" ] || { echo "no qtamp at $QTAMP"; exit 2; }
[ -d "$SKIN" ] || { echo "skin missing: $SKIN"; exit 77; }

fail=0
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

capture() { # $1=outfile $2=fixture
  local H="$tmp/home-$2"
  mkdir -p "$H/.config/winamp"
  if [ "$2" = "B" ]; then
    printf 'Fixture One|/tmp/fix1.mp3\nFixture Two|https://fix.example/stream\n' \
      > "$H/.config/winamp/bookmarks.txt"
    cat > "$H/.config/winamp/winamp.conf" <<'EOC'
[RecentFiles]
1\path=/tmp/fixture-a.mp3
2\path=/tmp/fixture-b.ogg
size=2
EOC
  fi
  HOME="$H" QT_QPA_PLATFORM=offscreen WASABIQT_DUMP_MENU=1 \
    timeout 60 "$QTAMP" --fakehost --modern-skin "$SKIN" 2>&1 \
    | grep '\[menu-dump\]' > "$1"
}

for f in A B; do
  capture "$tmp/dump-$f.txt" "$f"
  if diff -u "$HERE/menus/golden-$f.txt" "$tmp/dump-$f.txt" > "$tmp/diff-$f.txt"; then
    echo "  OK     menu tree fixture $f"
  else
    echo "  FAIL   menu tree fixture $f:"; head -20 "$tmp/diff-$f.txt"; fail=1
  fi
done

# Dispatch leg: pick Oscilloscope by id through the real context menu;
# the persisted vis mode proves dispatchMenuAction ran.
H="$tmp/home-pick"; mkdir -p "$H/.config/winamp"
HOME="$H" QT_QPA_PLATFORM=offscreen \
  WASABIQT_TEST_HEADMENU_PICK=wa5:vis.2 WASABIQT_CLICK_AT="R200,60" \
  timeout 60 "$QTAMP" --fakehost --modern-skin "$SKIN" \
  --screenshot "$tmp/pick.png" >/dev/null 2>&1
if grep -q '^mode=2' "$H/.config/winamp/winamp.conf" 2>/dev/null; then
  echo "  OK     menu dispatch (pick wa5:vis.2 -> mode=2 persisted)"
else
  echo "  FAIL   menu dispatch"; cat "$H/.config/winamp/winamp.conf" 2>/dev/null; fail=1
fi

[ $fail = 0 ] && echo "menu_dump_test: all checks passed" || echo "menu_dump_test: FAILURES"
exit $fail

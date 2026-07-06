#!/usr/bin/env bash
#
# --container root-window gate: rendering a container as the window's
# ROOT (`--container pl`) must produce the same pixels as the proven
# subwindow capture path (`--screenshot-container pl`), which has its
# own baselines upstream.  Self-consistent — no committed baseline —
# so it survives skin tweaks while still catching a broken root load,
# wrong layout size, or missing chrome.  The two render stacks differ
# in antialiasing, so the gate is a small MAE bound, plus an exact
# size match.
#
# Usage: tests/regression/container_root_test.sh
# Set QTAMP=/abs/path to override the binary.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
QTAMP="${QTAMP:-$REPO_ROOT/build/qtamp}"
SKIN_DIR="${SKIN_DIR:-$HOME/.winamp/skins}"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

[ -x "$QTAMP" ] || { echo "no qtamp binary at $QTAMP"; exit 2; }

MAE_LIMIT="3.0"
SKINS=("WinampModernPP" "Winamp Modern")

fail=0
for skin in "${SKINS[@]}"; do
    if [[ ! -d "$SKIN_DIR/$skin" ]]; then
        printf "  skip   %-20s (not installed)\n" "$skin"
        continue
    fi
    slug=$(echo "$skin" | tr ' ' '_' | tr '[:upper:]' '[:lower:]')

    HOME="$TMPDIR" QT_QPA_PLATFORM=offscreen "$QTAMP" \
        --modern-skin "$SKIN_DIR/$skin" \
        --container pl \
        --screenshot "$TMPDIR/${slug}_root.png" \
        >"$TMPDIR/${slug}_root.log" 2>&1 || true
    HOME="$TMPDIR" QT_QPA_PLATFORM=offscreen "$QTAMP" \
        --modern-skin "$SKIN_DIR/$skin" \
        --screenshot-container pl \
        --screenshot "$TMPDIR/${slug}_sub.png" \
        >"$TMPDIR/${slug}_sub.log" 2>&1 || true

    if [[ ! -s "$TMPDIR/${slug}_root.png" || ! -s "$TMPDIR/${slug}_sub.png" ]]; then
        printf "  FAIL   %-20s (a capture is missing; see %s)\n" "$skin" "$TMPDIR"
        fail=1
        continue
    fi

    if out=$(python3 - "$TMPDIR/${slug}_root.png" "$TMPDIR/${slug}_sub.png" \
                     "$MAE_LIMIT" <<'EOF'
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert('RGBA'), dtype=float)
b = np.asarray(Image.open(sys.argv[2]).convert('RGBA'), dtype=float)
if a.shape != b.shape:
    print(f"size mismatch {a.shape} vs {b.shape}")
    sys.exit(1)
mae = float(np.abs(a - b).mean())
print(f"mae={mae:.3f}")
sys.exit(0 if mae < float(sys.argv[3]) else 1)
EOF
    ); then
        printf "  OK     %-20s (%s)\n" "$skin" "$out"
    else
        printf "  FAIL   %-20s (%s, limit %s)\n" "$skin" "$out" "$MAE_LIMIT"
        fail=1
    fi
done

exit "$fail"

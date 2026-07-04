#!/usr/bin/env bash
#
# Cross-skin regression screenshot test.
#
# Renders each of the five reference skins via the offscreen
# screenshot pipeline and pixel-diffs the output against the
# committed baseline PNGs under this directory.  Exits 0 if every
# screenshot matches its baseline, 1 if any diverge.
#
# Usage:
#   tests/regression/run.sh                # diff against baselines
#   tests/regression/run.sh --update       # refresh baselines (commit
#                                          # the new PNGs after eyeball
#                                          # verification)
#
# Set QTAMP=/abs/path/to/qtamp to use a non-default binary.  By
# default the script looks for the binary at build/qtamp relative
# to the qtamp repo root.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
QTAMP="${QTAMP:-$REPO_ROOT/build/qtamp}"
SKIN_DIR="${SKIN_DIR:-$HOME/.winamp/skins}"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

if [[ ! -x "$QTAMP" ]]; then
    echo "error: qtamp binary not found at $QTAMP" >&2
    echo "       (build first: cmake --build build --target qtamp)" >&2
    exit 2
fi

MODE="diff"
if [[ "${1:-}" == "--update" ]]; then
    MODE="update"
fi

SKINS=(
    "WinampModernPP"
    "Winamp Modern"
    "DeClassified"
    "Bento"
    "Big Bento"
    # The corpus cannot score this skin (its author screenshot predates
    # the shipped XML — see deps/qtWasabi/tests/corpus/manifest.tsv), so
    # its pixel gate lives here instead.
    "QTAMP-Winamp2000SP4"
)

fail=0
for skin in "${SKINS[@]}"; do
    slug=$(echo "$skin" | tr ' ' '_' | tr '[:upper:]' '[:lower:]')
    baseline="$SCRIPT_DIR/${slug}.png"
    rendered="$TMPDIR/${slug}.png"

    if [[ ! -d "$SKIN_DIR/$skin" ]]; then
        printf "  skip   %-20s (skin not installed at %s)\n" "$skin" "$SKIN_DIR/$skin"
        continue
    fi

    # Hermetic: the user's winamp.conf (saved color theme, vis mode)
    # must not leak into baselines — a stored synthetic theme once
    # masqueraded as an engine-wide color regression.  SKIN_DIR was
    # expanded above, so skins still come from the real location.
    HOME="$TMPDIR" QT_QPA_PLATFORM=offscreen "$QTAMP" \
        --modern-skin "$SKIN_DIR/$skin" \
        --screenshot "$rendered" >"$TMPDIR/${slug}.log" 2>&1 || true

    if [[ "$MODE" == "update" ]]; then
        cp "$rendered" "$baseline"
        printf "  update %-20s -> %s\n" "$skin" "$baseline"
        continue
    fi

    if [[ ! -f "$baseline" ]]; then
        printf "  NEW    %-20s (no baseline yet — rerun with --update)\n" "$skin"
        fail=1
        continue
    fi

    if cmp -s "$rendered" "$baseline"; then
        printf "  OK     %-20s\n" "$skin"
    else
        printf "  DIFF   %-20s (rendered=%s vs baseline=%s)\n" \
            "$skin" "$rendered" "$baseline"
        fail=1
    fi
done

if [[ "$MODE" == "diff" && "$fail" -ne 0 ]]; then
    echo
    echo "Regression failures above.  After verifying the change is intended,"
    echo "refresh the baselines: $0 --update"
fi

exit "$fail"

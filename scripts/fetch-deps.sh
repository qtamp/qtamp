#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# fetch-deps.sh — clone qtWasabi into deps/qtWasabi and trigger its
# own scripts/fetch-wasabi.sh so the Llama Group Winamp source
# archive lands inside deps/qtWasabi/wasabi-src/.  Both directories
# are gitignored; the contents never get committed back into qtamp.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_dir="$repo_root/deps"
qtwasabi_url="${QTWASABI_URL:-https://github.com/qtWasabi/qtWasabi.git}"
qtwasabi_ref="${QTWASABI_REF:-main}"

mkdir -p "$deps_dir"

if [ -d "$deps_dir/qtWasabi/.git" ]; then
    echo "==> deps/qtWasabi already cloned, pulling $qtwasabi_ref"
    git -C "$deps_dir/qtWasabi" fetch origin "$qtwasabi_ref"
    git -C "$deps_dir/qtWasabi" checkout "$qtwasabi_ref"
    git -C "$deps_dir/qtWasabi" pull --ff-only origin "$qtwasabi_ref"
else
    echo "==> Cloning qtWasabi ($qtwasabi_ref) into deps/qtWasabi"
    git clone --depth 1 --branch "$qtwasabi_ref" "$qtwasabi_url" "$deps_dir/qtWasabi"
fi

if [ -x "$deps_dir/qtWasabi/scripts/fetch-wasabi.sh" ]; then
    echo "==> Fetching Wasabi source archive (qtWasabi/scripts/fetch-wasabi.sh)"
    (cd "$deps_dir/qtWasabi" && ./scripts/fetch-wasabi.sh)
else
    echo "!! deps/qtWasabi/scripts/fetch-wasabi.sh missing or not executable"
    echo "!! qtWasabi will not be able to build until that script can run."
    exit 1
fi

echo "==> Done. qtamp can now configure with -DQTAMP_USE_QTWASABI=ON."

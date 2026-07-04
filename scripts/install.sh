#!/bin/sh
#
# qtamp universal installer: `curl https://qtamp.sh | sh`
#
# Detects the host OS and installs qtamp, the reference player for
# qtWasabi (the open-source reimplementation of the Wasabi/Maki Modern
# skin engine).  Everything is built from source:
#
#   macOS  → delegates to scripts/install-macos.sh (Homebrew toolchain,
#            .app bundle into /Applications)
#   Linux  → installs Qt6 + build tools via the native package manager,
#            clones qtamp + the qtWasabi engine, fetches the Wasabi
#            source tree from the public archive.org mirror (never
#            redistributed), builds, and installs to /usr/local
#
# The repositories:   https://github.com/qtamp/qtamp
#                     https://github.com/qtWasabi/qtWasabi
# Issues:             https://github.com/qtamp/qtamp/issues
#
# Flags:
#   --wasm        build the in-browser WebAssembly player instead of a
#                 native install. Needs Docker; runs the published
#                 ghcr.io/qtamp/qtamp-wasm-builder toolchain image on the
#                 same checkout and emits build-wasm/dist.
#                 Usage: curl https://qtamp.sh | sh -s -- --wasm
#
# Tunables (env):
#   QTAMP_REPO    git url     (default https://github.com/qtamp/qtamp.git)
#   QTAMP_REF     branch/tag  (default main)
#   QTAMP_PREFIX  install dir (default /usr/local; macOS /Applications)
#   QTAMP_WORK    build dir   (default ~/.cache/qtamp-build)
#   QTAMP_WASM_IMAGE  builder image (default ghcr.io/qtamp/qtamp-wasm-builder:latest)
#
set -eu

QTAMP_REPO="${QTAMP_REPO:-https://github.com/qtamp/qtamp.git}"
QTAMP_REF="${QTAMP_REF:-main}"
QTAMP_WORK="${QTAMP_WORK:-${HOME}/.cache/qtamp-build}"
QTAMP_WASM_IMAGE="${QTAMP_WASM_IMAGE:-ghcr.io/qtamp/qtamp-wasm-builder:latest}"

WASM=0
for arg in "$@"; do
    case "$arg" in
        --wasm) WASM=1 ;;
    esac
done

say()  { printf '\033[1;33m==>\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

OS="$(uname -s)"

# ── macOS: hand off to the dedicated .app installer ─────────────────
if [ "$OS" = "Darwin" ] && [ "$WASM" = "0" ]; then
    say "macOS detected, fetching the qtamp .app installer"
    exec /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/qtamp/qtamp/main/scripts/install-macos.sh)"
fi

if [ "$WASM" = "0" ]; then
    [ "$OS" = "Linux" ] || die "unsupported OS: $OS (Linux and macOS today, Windows planned)"
fi

# ── WebAssembly: same checkout, containerized toolchain ──────────────
if [ "$WASM" = "1" ]; then
    command -v docker >/dev/null 2>&1 || die "--wasm needs Docker (the toolchain runs in ghcr.io/qtamp/qtamp-wasm-builder)"
    say "Cloning qtamp into ${QTAMP_WORK}"
    mkdir -p "$QTAMP_WORK"; cd "$QTAMP_WORK"
    if [ -d qtamp/.git ]; then
        git -C qtamp fetch -q origin "$QTAMP_REF"
        git -C qtamp checkout -q "$QTAMP_REF"
        git -C qtamp pull -q --ff-only origin "$QTAMP_REF" || true
    else
        git clone -q --branch "$QTAMP_REF" "$QTAMP_REPO" qtamp
    fi
    cd qtamp
    git submodule update --init deps/qtWasabi
    say "Building the WebAssembly player in ${QTAMP_WASM_IMAGE}"
    docker pull "$QTAMP_WASM_IMAGE" || say "pull failed, using local image if present"
    docker run --rm -v "$(pwd):/src" "$QTAMP_WASM_IMAGE"
    say "Done. Deployable player: $(pwd)/build-wasm/dist"
    exit 0
fi

# ── Linux: dependencies via the native package manager ──────────────
say "Installing build dependencies (sudo required for this step)"
if command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y cmake gcc-c++ git curl p7zip p7zip-plugins ninja-build pkgconfig \
        qt6-qtbase-devel qt6-qtmultimedia-devel qt6-qtdeclarative-devel
elif command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update -qq
    sudo apt-get install -y cmake g++ git curl p7zip-full ninja-build pkg-config \
        qt6-base-dev qt6-multimedia-dev qt6-declarative-dev
elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm cmake gcc git curl p7zip ninja pkgconf \
        qt6-base qt6-multimedia qt6-declarative
elif command -v zypper >/dev/null 2>&1; then
    sudo zypper install -y cmake gcc-c++ git curl p7zip ninja \
        qt6-base-devel qt6-multimedia-devel qt6-declarative-devel
else
    die "no supported package manager found (dnf/apt/pacman/zypper). Install Qt6 (base, multimedia, declarative), cmake, ninja, git, p7zip manually, then re-run"
fi

# ── Sources ──────────────────────────────────────────────────────────
say "Cloning qtamp + qtWasabi into ${QTAMP_WORK}"
mkdir -p "$QTAMP_WORK"
cd "$QTAMP_WORK"
if [ -d qtamp/.git ]; then
    git -C qtamp fetch -q origin "$QTAMP_REF"
    git -C qtamp checkout -q "$QTAMP_REF"
    git -C qtamp pull -q --ff-only origin "$QTAMP_REF" || true
else
    git clone -q --branch "$QTAMP_REF" "$QTAMP_REPO" qtamp
fi
cd qtamp
git submodule update --init --recursive --quiet

# The Wasabi source tree (Winamp Collaborative License v1.0) is fetched
# by the USER from the public archive.org mirror at build time; it is
# never part of the repositories and never redistributed by qtamp.
say "Fetching the Wasabi source tree from archive.org (user-supplied, WCL v1.0)"
( cd deps/qtWasabi && ./scripts/fetch-wasabi.sh )

# ── Build ────────────────────────────────────────────────────────────
say "Configuring + building (this takes a few minutes)"
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQTAMP_USE_QTWASABI=ON \
    -DCMAKE_INSTALL_PREFIX="${QTAMP_PREFIX:-/usr/local}"
cmake --build build

say "Installing to ${QTAMP_PREFIX:-/usr/local} (sudo required)"
sudo cmake --install build

say "qtamp installed. Start it with:  qtamp"
say "Skins to try: https://github.com/qtamp (QTAMP-branded MIT showcase skins)"
say "Bugs / feedback: https://github.com/qtamp/qtamp/issues"

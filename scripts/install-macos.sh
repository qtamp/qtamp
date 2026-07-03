#!/bin/bash
#
# qtamp macOS installer — build qtamp + qtWasabi from source and install
# qtamp.app into /Applications.  Meant to be run straight off the web:
#
#     curl -fsSL https://raw.githubusercontent.com/kleberbaum/qtamp/main/scripts/install-macos.sh | bash
#
# It provisions the toolchain (Xcode CLT, Homebrew, cmake/ninja/qt), pulls
# qtamp + the qtWasabi engine, downloads the Wasabi source tree from the
# archive.org mirror, then builds, bundles a .app, and installs it.
#
# Tunables (env):
#   QTAMP_REPO          git url    (default kleberbaum/qtamp)
#   QTAMP_REF           branch/tag (default main)
#   WITH_MILKDROP       0|1        (default 0 — the projectM visualizer)
#   WITH_ML_PLUGINS     0|1        (default 0 — gen_ml/ml_* library plugins)
#   WASABIQT_ARCHIVE_URL url       (override the archive.org mirror)
#   PREFIX              install dir (default /Applications)
#
set -euo pipefail

QTAMP_REPO="${QTAMP_REPO:-https://github.com/kleberbaum/qtamp.git}"
QTAMP_REF="${QTAMP_REF:-main}"
WITH_MILKDROP="${WITH_MILKDROP:-0}"
PREFIX="${PREFIX:-/Applications}"
WORK="${WORK:-$HOME/.cache/qtamp-build}"
JOBS="$( (sysctl -n hw.ncpu 2>/dev/null) || echo 4)"

c_g=$'\033[1;32m'; c_c=$'\033[1;36m'; c_r=$'\033[1;31m'; c_0=$'\033[0m'
log()  { printf '%s==>%s %s\n' "$c_c" "$c_0" "$*"; }
ok()   { printf '%s  ok%s %s\n' "$c_g" "$c_0" "$*"; }
die()  { printf '%serror%s %s\n' "$c_r" "$c_0" "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || die "this installer is for macOS only."

# ── 1. Xcode Command Line Tools ──────────────────────────────────────
if ! /usr/bin/xcode-select -p >/dev/null 2>&1 \
   || [ ! -x /Library/Developer/CommandLineTools/usr/bin/clang ]; then
    log "Installing the Xcode Command Line Tools (you may be asked for your password)…"
    trigger=/tmp/.com.apple.dt.CommandLineTools.installondemand.in-progress
    touch "$trigger"
    label="$(softwareupdate -l 2>/dev/null \
             | grep -iE 'Label:.*Command Line Tools' \
             | sed -E 's/^.*Label: *//' | sort -V | tail -1)" || true
    if [ -n "$label" ]; then sudo softwareupdate -i "$label" --verbose || true; fi
    rm -f "$trigger"
    [ -x /Library/Developer/CommandLineTools/usr/bin/clang ] \
        || die "Command Line Tools install failed. Run 'xcode-select --install' and retry."
fi
ok "clang $(/usr/bin/clang --version | head -1 | awk '{print $NF}')"

# ── 2. Homebrew ──────────────────────────────────────────────────────
if ! command -v brew >/dev/null 2>&1; then
    for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
        [ -x "$b" ] && eval "$("$b" shellenv)" && break
    done
fi
if ! command -v brew >/dev/null 2>&1; then
    log "Installing Homebrew…"
    NONINTERACTIVE=1 /bin/bash -c \
      "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    for b in /opt/homebrew/bin/brew /usr/local/bin/brew; do
        [ -x "$b" ] && eval "$("$b" shellenv)" && break
    done
fi
command -v brew >/dev/null 2>&1 || die "Homebrew not on PATH after install."
ok "$(brew --version | head -1)"

# ── 3. Build dependencies ────────────────────────────────────────────
log "Installing build dependencies…"
brew_pkgs=(cmake ninja pkg-config qt p7zip)   # p7zip: extract the Wasabi archive
[ "$WITH_MILKDROP" = 1 ] && brew_pkgs+=(projectm)
brew install "${brew_pkgs[@]}"
QT_PREFIX="$(brew --prefix qt)"
ok "Qt at $QT_PREFIX"

# ── 4. Source ────────────────────────────────────────────────────────
log "Fetching qtamp source…"
mkdir -p "$WORK"; cd "$WORK"
rm -rf qtamp
git clone --depth 1 -b "$QTAMP_REF" "$QTAMP_REPO" qtamp
cd qtamp
# Only the engine submodule; projectM is optional and not needed unless
# MilkDrop is enabled.
git submodule update --init --depth 1 deps/qtWasabi
[ "$WITH_MILKDROP" = 1 ] && git submodule update --init --depth 1 deps/projectm || true
ok "source in $WORK/qtamp"

# ── 5. Wasabi source (Winamp tree, from the archive.org mirror) ──────
# qtWasabi does not redistribute the Wasabi source (the Winamp
# Collaborative License forbids that); fetch-wasabi.sh downloads the
# community archive.org mirror and extracts it into wasabi-src/Src.  The
# engine's CMake overlays the handful of macOS/port headers the mirror
# doesn't ship (see wasabi-port/platform-overlay) at configure time.
# Override the mirror with WASABIQT_ARCHIVE_URL if archive.org is slow.
log "Fetching the Wasabi source tree (archive.org; several hundred MB)…"
bash deps/qtWasabi/scripts/fetch-wasabi.sh
export WASABI_SRC_DIR="$PWD/deps/qtWasabi/wasabi-src/Src"
[ -f "$WASABI_SRC_DIR/Wasabi/api/script/vcpu.cpp" ] \
    || die "Wasabi source fetch incomplete (vcpu.cpp missing)."
ok "Wasabi source in wasabi-src"

# ── 6. Configure + build ─────────────────────────────────────────────
milk=OFF; [ "$WITH_MILKDROP" = 1 ] && milk=ON
# The gen_ml / ml_* media-library plugins need the proprietary
# Plugins/Library tree fetched too and are not verified on macOS yet; the
# core player + Modern-skin engine builds and runs without them.  Opt in
# with WITH_ML_PLUGINS=1.
plugins=OFF; [ "${WITH_ML_PLUGINS:-0}" = 1 ] && plugins=ON
log "Configuring…"
cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DQTAMP_USE_QTWASABI=ON \
    -DQTAMP_MILKDROP=$milk \
    -DQTWASABI_BUILD_PLUGINS=$plugins \
    -DQTWASABI_BUILD_TESTS=OFF \
    -DQTWASABI_BUILD_EXAMPLES=OFF \
    -DWASABI_SRC_DIR="$WASABI_SRC_DIR" \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX"
log "Building (-j$JOBS)…"
cmake --build build -j"$JOBS"
BIN="build/qtamp"
[ -x "$BIN" ] || die "build produced no qtamp binary."
ok "built $BIN"

# ── 7. Bundle qtamp.app ──────────────────────────────────────────────
log "Bundling qtamp.app…"
APP="$WORK/qtamp.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/qtamp"

# Engine dylib into Frameworks/, referenced via @rpath.  Copy the real
# versioned file, recreate the SONAME symlinks the exe links against, and
# add an rpath so @rpath/libqtwasabi.0.dylib resolves inside the bundle.
DYLIB="$(/usr/bin/find build -name 'libqtwasabi.*.dylib' ! -type l | head -1)"
if [ -n "${DYLIB:-}" ]; then
    base="$(basename "$DYLIB")"                     # e.g. libqtwasabi.0.0.1.dylib
    cp "$DYLIB" "$APP/Contents/Frameworks/$base"
    ( cd "$APP/Contents/Frameworks"
      ln -sf "$base" "libqtwasabi.0.dylib"
      ln -sf "$base" "libqtwasabi.dylib" )
    install_name_tool -add_rpath "@executable_path/../Frameworks" \
        "$APP/Contents/MacOS/qtamp" 2>/dev/null || true
fi

# Info.plist
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>qtamp</string>
  <key>CFBundleDisplayName</key><string>qtamp</string>
  <key>CFBundleExecutable</key><string>qtamp</string>
  <key>CFBundleIdentifier</key><string>at.snek.qtamp</string>
  <key>CFBundleVersion</key><string>0.0.1</string>
  <key>CFBundleShortVersionString</key><string>0.0.1</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>qtamp</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST

# Icon (mascot -> .icns) if the packaged PNG is present
ICONPNG="packaging/linux/icons/qtamp-256.png"
if [ -f "$ICONPNG" ] && command -v iconutil >/dev/null 2>&1; then
    iconset="$WORK/qtamp.iconset"; rm -rf "$iconset"; mkdir -p "$iconset"
    for s in 16 32 64 128 256 512; do
        sips -z $s $s "$ICONPNG" --out "$iconset/icon_${s}x${s}.png" >/dev/null 2>&1 || true
        d=$((s*2)); sips -z $d $d "$ICONPNG" --out "$iconset/icon_${s}x${s}@2x.png" >/dev/null 2>&1 || true
    done
    iconutil -c icns "$iconset" -o "$APP/Contents/Resources/qtamp.icns" 2>/dev/null || true
fi

# Bundle Qt frameworks, QML modules, and rewrite load paths.  qtamp builds
# its QML from an inline C++ string (import QtQuick / QtQuick.Window), so
# macdeployqt has no .qml on disk to scan — point it at a scratch file that
# names those imports, else it ships no QML modules and the app opens no
# window (looks like "nothing happens" when launched from Finder).
qmldir="$WORK/qtamp-qml"; rm -rf "$qmldir"; mkdir -p "$qmldir"
printf 'import QtQuick\nimport QtQuick.Window\nWindow { }\n' > "$qmldir/main.qml"
log "Running macdeployqt…"
"$QT_PREFIX/bin/macdeployqt" "$APP" -qmldir="$qmldir" -no-strip > "$WORK/macdeployqt.log" 2>&1 \
    || "$QT_PREFIX/bin/macdeployqt" "$APP" -qmldir="$qmldir" >> "$WORK/macdeployqt.log" 2>&1 || true
# Verify the bundle is actually self-contained — a partial macdeployqt would
# otherwise install a non-launching app under a success banner.
for fw in QtCore QtGui QtQuick; do
    [ -e "$APP/Contents/Frameworks/$fw.framework/Versions/A/$fw" ] \
        || die "macdeployqt did not bundle $fw.framework (see $WORK/macdeployqt.log)."
done
[ -e "$APP/Contents/PlugIns/platforms/libqcocoa.dylib" ] \
    || die "macdeployqt did not bundle the cocoa platform plugin (see $WORK/macdeployqt.log)."
[ -d "$APP/Contents/Resources/qml/QtQuick" ] \
    || die "macdeployqt did not bundle the QtQuick QML module — the app would open no window."
# macdeployqt re-signs ad-hoc; make sure the whole tree carries a valid
# signature after our install_name_tool edits so Gatekeeper lets it launch.
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
ok "bundled $APP"

# ── 8. Install ───────────────────────────────────────────────────────
log "Installing to $PREFIX…"
rm -rf "$PREFIX/qtamp.app" 2>/dev/null || sudo rm -rf "$PREFIX/qtamp.app"
if ! cp -R "$APP" "$PREFIX/" 2>/dev/null; then
    sudo cp -R "$APP" "$PREFIX/"
fi
# clear quarantine so it launches without Gatekeeper nagging
xattr -dr com.apple.quarantine "$PREFIX/qtamp.app" 2>/dev/null || true

ok "qtamp installed at $PREFIX/qtamp.app"
printf '\n%sqtamp is installed.%s  Launch it from Launchpad/Spotlight, or:\n' "$c_g" "$c_0"
printf '   open "%s/qtamp.app"\n\n' "$PREFIX"

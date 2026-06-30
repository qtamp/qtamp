---
type: Workflow
id: workflow/build-and-test
title: Build + offscreen-test workflow
description: How to configure and build qtamp with qtWasabi, and the fast offscreen render-to-PNG verification loop (with the three reference skins and the byte-identical-refactor check).
tags: [workflow, build, cmake, test, offscreen, screenshot]
related:
  - workflow/env-knobs.md
  - conventions/cardinal-rules.md
  - repos/relationship.md
  - components/surface-skinview-skinquickitem.md
---

# Build + offscreen-test workflow

## Build

qtamp depends on [qtWasabi](../repos/qtwasabi.md), which needs a user-supplied
WCL-licensed Winamp source release on disk to compile the vendored Maki VM +
BFC subset.

```sh
# 1) Clone with the qtWasabi submodule populated.
git clone --recurse-submodules https://github.com/kleberbaum/qtamp
cd qtamp
# (or: git submodule update --init --recursive)

# 2) Fetch the WCL-licensed Winamp source into deps/qtWasabi/wasabi-src/
#    (qtWasabi handles the download; the dir is gitignored on both ends).
(cd deps/qtWasabi && ./scripts/fetch-wasabi.sh)

# 3) Configure + build, opting qtWasabi in.
cmake -B build -G Ninja \
    -DQTAMP_USE_QTWASABI=ON \
    -DWASABI_SRC_DIR="$(pwd)/deps/qtWasabi/wasabi-src/Src"
cmake --build build

# 4) Run. --modern-skin hands the chrome to qtWasabi.
./build/qtamp --modern-skin ~/.winamp/skins/WinampModernPP
```

### Key CMake options (qtamp)

| Option | Default | Effect |
|---|---|---|
| `QTAMP_USE_QTWASABI` | OFF | Enable the modern-skin path via qtWasabi (defines `WINAMP_HAVE_WASABIQT`). |
| `QTAMP_MILKDROP` | OFF | Build the vendored projectM v4 static lib + `MilkdropItem` (see [the visualizer component](../components/milkdrop-visualizer.md)). |
| `WASABI_SRC_DIR` | — | Path to the extracted Winamp source (`.../wasabi-src/Src`). |
| `QTWASABI_DIR` | `deps/qtWasabi/` | Engine source location (or `$QTWASABI_DIR`). |

Build dirs by convention: `build/` (default), `build-milkdrop/` (when
`QTAMP_MILKDROP=ON`).

## The fast verification loop: offscreen render → PNG

Render a skin to a PNG with no display, then diff it:

```sh
QT_QPA_PLATFORM=offscreen ./build/qtamp \
    --modern-skin "$HOME/.winamp/skins/Bento" \
    --screenshot /tmp/out.png
```

Screenshot a **sub-window** (EQ, playlist, a detached holder) instead of the
main window:

```sh
QT_QPA_PLATFORM=offscreen build-milkdrop/qtamp \
    --modern-skin <skin> <media> \
    --screenshot-container "guid:{F0816D7B-…}" \
    --screenshot out.png
```

### The regression gate (do this before sending a change)

Confirm the **three reference skins** still render and that a non-visual
change leaves them **byte-identical**:

```
Bento, Big Bento, Winamp Modern  →  0 pixel diff for refactors, 0 Maki gurus
```

A behavioural fix should change *only* what it intends to. If a "general" fix
moves pixels on a skin it wasn't about, it is probably skin-specific in
disguise — see [the cardinal rules](../conventions/cardinal-rules.md).

> **Offscreen has a blind spot.** `startSystemMove` (Wayland window drag)
> *fails* offscreen (no compositor), so the base input path runs and can mask
> a live drag-vs-click bug. Input-routing fixes that touch
> `QtampPlayerWindow`'s own mouse overrides must be verified on a real
> Wayland session, not just offscreen. See
> [the surface component](../components/surface-skinview-skinquickitem.md).

## CLI flags (qtamp, handled in `src/main.cpp`)

| Flag | Arg | Purpose |
|---|---|---|
| `--modern-skin` | `<path>` | Load a Modern skin (path or unpacked dir). |
| `--classic-skin` | `<path>` | Load the classic-skin path instead. |
| `--screenshot` | `<path>` | Render offscreen, save PNG, exit. |
| `--screenshot-container` | `<container-id>` | Screenshot a sub-window/holder instead of the main window. |
| `--play-file` | `<path>` | Play a file at startup. |
| positional | `<file\|folder>` | Audio files/folders (folders expand in album order). |

See [env knobs](env-knobs.md) for the `WASABIQT_*` test variables that drive
clicks, hovers, tab selection, resizes, and theme switches in the screenshot
harness.

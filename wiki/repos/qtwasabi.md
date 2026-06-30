---
type: Repository
id: repos/qtwasabi
title: qtWasabi — Winamp Modern skin rendering engine
description: An open-source, Qt-native rendering engine that runs Winamp Modern (.wal/Wasabi) skins by executing the skins' vendored Maki VM bytecode and painting via QPainter, decoupled from any player by one Host interface.
resource: https://github.com/kleberbaum/qtWasabi
tags: [qtwasabi, engine, wasabi, maki, qt6, submodule]
related:
  - repos/qtamp.md
  - repos/relationship.md
  - architecture/hybrid-design.md
  - architecture/pipeline.md
  - components/skinruntime-maki-bridge.md
  - conventions/cardinal-rules.md
---

# qtWasabi

**Location in this repo:** `deps/qtWasabi` (a git submodule).
**License:** MIT (the repo); the user-supplied Wasabi source it builds against
is Winamp Collaborative License v1.0.

qtWasabi is an open-source, Qt-native **rendering engine** for Winamp's
Wasabi skins — the open continuation of the **Wasabi 2** decoupling Nullsoft
started and never finished. It runs the open-sourced **Maki bytecode VM
unmodified** and renders Modern `.wal` skins on *any* themeable player, across
Linux/macOS/Windows, Apple Silicon native.

It is **a rendering engine, not a player.** Everything player-shaped —
playback, the mixer, metadata, playlist, library — it asks for through one
interface, [`qtWasabi::Host`](../components/host-interface.md), which the
embedder implements. That single seam is what makes the engine standalone.

## The hybrid approach (why it's built this way)

Re-implementing the Maki VM from scratch is a multi-year bug-hunt (thousands
of shipped skins exercise tiny VM quirks; the only spec is the running code).
Porting the entire open-sourced Wasabi C++ to Linux is also impractical (the
2024 release ships `#error port me` markers across the BFC platform layer and
an abandoned X11 port). qtWasabi splits the difference — see
[the hybrid-design object](../architecture/hybrid-design.md):

| Component | Source |
|---|---|
| **Maki bytecode VM** | open-sourced `/Src/Wasabi/api/script/`, **vendored unmodified** |
| **Minimal BFC subset** (memblock, critsec, foreach, ptrlist, nsguid, thread, stack, node, wasabi_std) | open-sourced, POSIX-clean ~3000 LOC |
| **Wasabi widget classes** (Group, Layer, Button, Slider, Text, …) | **qtWasabi's own Qt6 implementation** |
| **Skin XML parser, sendparams, gammaset, font loading** | **qtWasabi's own** |
| **Window/canvas/event integration** | **qtWasabi's `qt6/`** (QWidget/QPainter-native) |

The unported BFC **platform** layer (`std_file`, `std_keyboard`, `std_wnd`,
the X11 backend) is **not used** — Qt provides file I/O (`QFile`), keyboard
(`QKeyEvent`), window (`QtWindowAdapter`), plugins (`QLibrary`).

## Repo layout (engine side)

```
public/qtWasabi/   embedder-facing C++ API (Host, Skin, SkinView, Version)
src/               engine: skin loader, host adapter, widget tree, sendparams,
                   gammaset, fontloader, widgets/, painters, ml/, pledit/
qt6/               Qt6 rendering + event adapter (QtCanvasAdapter, QtWindowAdapter, …)
wasabi-port/       vendored Maki VM + BFC subset + the bridge/bindings that drive it
wasabi-compat/     Win32 / Winamp-API shim so original ml_* plugins run on Linux
cmake/             FindWasabiSrc + package config
tests/             pixel-regression + Maki-opcode-coverage harness
scripts/           fetch-wasabi.sh (downloads the open-source release into ./wasabi-src/)
packaging/         RPM spec, macOS .dmg builder, installer.sh
```

> The README also names `script-bridge/` for the ScriptObject shims; in the
> live tree the Maki bridge lives under `wasabi-port/` (see
> [the Maki bridge component](../components/skinruntime-maki-bridge.md) for
> the verified file names).

## Status

Working engine. **Renders end to end today: Bento, Big Bento, Winamp
Modern** — skin XML → groupdef/sendparams expansion → widget tree → Maki
`onScriptLoaded`/`onResize` dispatch → `QPainter`. Window resize re-flows the
chrome through the real Maki `onResize` path (re-run to a fixpoint, not
hard-coded), and skins hot-reload.

The reference embedder is [qtamp](qtamp.md).

---
type: Architecture
id: architecture/hybrid-design
title: The hybrid design — vendored Maki VM + fresh Qt widget engine
description: Why qtWasabi vendors the Maki VM unmodified, re-implements the widgets in Qt, and uses only the POSIX-clean BFC foundation while ignoring the unported BFC platform layer.
tags: [architecture, hybrid, maki, bfc, vendored]
related:
  - architecture/pipeline.md
  - components/skinruntime-maki-bridge.md
  - conventions/cardinal-rules.md
  - glossary/terms.md
---

# The hybrid design

qtWasabi splits the difference between two impractical extremes:

- **Re-implement the Maki VM from scratch** → a multi-year bug-hunt.
  Thousands of shipped skins exercise tiny VM quirks and the only spec is the
  running code; a clean-room VM would drift and break obscure scripts.
- **Port the whole open-sourced Wasabi C++ to Linux** → impractical. The 2024
  Llama Group release ships `#error port me` markers across the BFC platform
  layer (file I/O, keyboard, window, canvas) and an abandoned X11 port.

So qtWasabi keeps the irreducible, portable core of the original and rebuilds
everything else on Qt.

| Component | Source | Why |
|---|---|---|
| **Maki bytecode VM** | open-sourced `/Src/Wasabi/api/script/`, **vendored unmodified** | bit-perfect compatibility with every shipped Modern skin |
| **Minimal BFC subset** (`memblock`, `critsec`, `foreach`, `ptrlist`, `nsguid`, `thread`, `stack`, `node`, `wasabi_std`) | open-sourced, POSIX-clean ~3000 LOC | the foundation the VM depends on |
| **Wasabi widget classes** (Group, Layer, Button, Slider, Text, Animation, Timer, Container, …) | **qtWasabi's own Qt6 implementation** | matches documented Wasabi behaviour but paints through `QPainter` — HiDPI/Wayland/Apple-Silicon all work |
| **Skin XML parser, sendparams, gammaset, font loading** | **qtWasabi's own** | Qt-native, no platform port needed |
| **Window/canvas/event integration** | **qtWasabi's `qt6/`** | replaces the 2015-era stub adapter |

## BFC: foundation vs platform

BFC = **B**eex **F**oundation **C**lasses, Nullsoft's homegrown C++ stdlib
(written ~2000–2002 for portability across MSVC 6 / GCC 2.x / CodeWarrior /
Borland). Every Wasabi class transitively pulls BFC. Two layers matter:

1. **Foundation** (containers, GUIDs, dispatch, node-tree, threading,
   strings, math) — POSIX-clean ~3000 LOC. The Maki VM and the
   script-binding registry depend on this. **Used.**
2. **Platform** (`std_file`, `std_keyboard`, `std_wnd`, `loadlib`, the X11
   backend) — the unfinished part. **Not used.** Qt provides all of it:
   file I/O via `QFile`, keyboard via `QKeyEvent`, window via
   `QtWindowAdapter`, plugins via `QLibrary`. A compile shim tells BFC's
   transitively-included `std_file.h` etc. that the symbols are declared, but
   the VM never calls them, so the linker never needs an implementation.

## The cost: GUID-dispatch shims

The price of keeping the real VM is that Wasabi's *script bindings* — the C++
classes the VM calls into for `setVisible`, `getAutoWidth`, `setXmlParam`,
etc. — must bridge the VM's `ScriptObject` interface to qtWasabi's Qt-native
widgets.

Every Wasabi class derives from `Dispatchable` and answers
`dependent_getInterface(const GUID *classid)` — GUID-keyed, COM-ish
polymorphism without being COM. When Maki does `obj.setVisible(0)`, the VM
asks the C++ object "are you a `GuiObject`?" via `dependent_getInterface`,
then calls the registered method on that interface.

qtWasabi's bridge (`wasabi-port/`) is exactly this: a thin
`Dispatchable`-derived `WidgetScriptObject` per Qt-native widget that answers
the GUID query with itself, so the VM can call its bindings against our
widget. ~10 LOC per binding. See
[the Maki bridge component](../components/skinruntime-maki-bridge.md).

## What this buys

- Maki scripts run on the **actual shipped VM** — any quirk a skin depends on
  works automatically; nothing to chase down per skin.
- Widget rendering is **Qt-native** — HiDPI, Wayland, Apple Silicon, no
  platform-port quagmire.
- qtWasabi itself is freshly-authored Qt6 code, redistributable, and small
  enough to embed in any Qt media player.

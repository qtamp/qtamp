---
type: Component
id: components/milkdrop-visualizer
title: MilkDrop / projectM visualizer (qtamp side)
description: qtamp's optional projectM v4 GL visualizer overlay, including the CPU-readback path that lets a single GL item render into detached software SkinView windows.
resource: src/MilkdropItem.h
tags: [component, qtamp, milkdrop, projectm, visualizer, vis, detached]
related:
  - repos/qtamp.md
  - components/surface-skinview-skinquickitem.md
  - components/widget-tree.md
  - components/painters-and-registries.md
  - glossary/terms.md
---

# MilkDrop / projectM visualizer

**Headers:** `/src/MilkdropItem.{h,cpp}` (qtamp).
**Build:** opt-in via `-DQTAMP_MILKDROP=ON` (vendored projectM v4 static lib,
built into `build-milkdrop/`; compile-guarded by `QTAMP_WITH_MILKDROP`).

This is a **qtamp-side** concern (the embedder), not part of the qtWasabi
engine — it produces the AVS/visualizer pixels the engine's vis windowholder
displays.

`MilkdropItem` is a `QQuickItem` hosting libprojectM v4 in a dedicated GL
context sharing textures with the scene graph. There is **one** `MilkdropItem`
living in the *main* `QQuickWindow`'s scene graph.

## The detached-window readback architecture

A detached vis window is a software `SkinView` (a `QWidget`, **no GL**), so
the single GL `MilkdropItem` cannot render directly into it. The solution
(readback):

- `MilkdropItem` gained `setCpuReadbackEnabled(bool)` + `setRenderSize(QSize)`
  + `copyFrame(QImage&)`; with readback on it does `fbo->toImage()` after
  `glFinish()` into a mutex-guarded frame, and forces `window()->update()`
  (the item is hidden while detached, so updating it alone schedules nothing).
- The engine's `WindowHolderRegistry` exposes
  `registerHolderFrameProvider(fn(guidKey,size)->QImage)` +
  `holderFrameFor`; `WindowHolder::paint`'s AVS/video branch blits the
  provider frame when non-null. The provider returns a frame **only if its
  size matches the request** — a size-guard that stops a second AVS slot
  (e.g. Bento's small chrome vis) from blitting a wrong-size copy.
- `syncMilkdropOverlay()` (main.cpp, ~20fps) scans the main tree and every
  visible detached `SkinView`'s tree for the best-alive AVS slot; a detached
  slot wins → hide the GL overlay and feed it via readback, otherwise the GL
  overlay paints directly as before.

## The bare-GUID classifier (why detached worked at all)

Winamp Modern's detached windows reference their component via a **bare GUID
(no `guid:` prefix)** — `vis-normal.xml` uses
`<component param="{0000000A-…}">`, detached Video `{F0816D7B-…}`. The single
`bareGuidLower()` normaliser (see [the widget tree](widget-tree.md)) is what
lets the detached holder be classified as an AVS / album-art slot at all —
the [cardinal rule](../conventions/cardinal-rules.md) in action.

## Relevant env knobs

- `WASABIQT_TRACE_MILKDROP` (qtamp, `main.cpp`) — trace projectM preset
  changes; the engine-side `WASABIQT_TRACE_MILKDROP` prints
  `[milkdrop] overlay -> owner=main|detached` per tick.

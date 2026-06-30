---
type: Architecture
id: architecture/pipeline
title: The render pipeline — parse → expand → widget-tree → Maki VM → painters → surface
description: The single-direction flow that turns a .wal skin into pixels, the file each stage lives in, and the Maki VM seam in the middle.
tags: [architecture, pipeline, render, maki]
related:
  - components/skinxml.md
  - components/layout.md
  - components/widget-tree.md
  - components/skinruntime-maki-bridge.md
  - components/painters-and-registries.md
  - components/surface-skinview-skinquickitem.md
  - conventions/cardinal-rules.md
---

# The render pipeline

A skin goes through **one direction**, parse → pixels:

```
skin.xml ─▶ SkinXml ─▶ Layout ─▶ Widget tree ─▶ SkinRuntime ─▶ Painters ─▶ Qt surface
           (parse)   (expand)  (resolve rects) (Maki VM)     (QPainter)  (SkinView / SkinQuickItem)
```

| Stage | Lives in (engine header / impl) | Does |
|---|---|---|
| **Parse** | `public/qtWasabi/SkinXml.h` · `src/SkinXml.cpp` | Read `skin.xml` + its includes into a `Document`: containers, layouts, groupdefs, `<script>` refs, bitmaps, colors, fonts, gammasets. |
| **Expand** | `public/qtWasabi/Layout.h` · `src/Layout.cpp` | `expandLayout(doc, container, layout)` inlines every `<groupdef>`, applies `<sendparams>` overrides per group instance, and emits a tree of `Widget` (`ResolvedWidget`) nodes. Also models `Wasabi:Frame` pane splits, the `sysregion` window mask, and chrome cut-outs. |
| **Widget tree** | `public/qtWasabi/Widget.h` · `src/widgets/*` | Polymorphic widget classes (`LayerWidget`, `ButtonWidget`, `SliderWidget`, `TextWidget`, `ContainerWidget`, `WindowHolderWidget`, …). Each resolves its rect from attrs (including relative `relatw`/negative-`w` coords) at paint time and alpha-hit-tests for input. |
| **Maki VM** | `public/qtWasabi/SkinRuntime.h` · `src/SkinRuntime.cpp` + `wasabi-port/` | Loads each `<script>` into the VM, binds a `WidgetScriptObject` to every id'd widget, and dispatches the skin's handlers — `onScriptLoaded`, `onResize`, `onAction`, `onTimer`, `setXmlParam`, `findObject`, … **The scripts drive the layout; the engine just runs them.** |
| **Paint** | `TreePainter` · `LayerPainter` · `TextPainter` (in `public/qtWasabi/` + `src/`) | Walk the tree and paint each widget through `QPainter`. The `BitmapRegistry` / `FontRegistry` / `ColorRegistry` / `GammasetRegistry` resolve skin resources. |
| **Surface** | `SkinView` (`QWidget`) / `SkinQuickItem` (`QQuickItem`) · `qt6/` | The Qt integration — hosts the painted tree, routes mouse + resize events back into the engine, and applies the alpha window mask. |

## The Maki VM seam (the heart of it)

The middle of the pipeline is where this engine is unusual: **the layout is
driven by the skins' own Maki scripts, not by qtWasabi.** After expansion,
`SkinRuntime` loads each `<script>` into the **vendored Maki bytecode VM**
and dispatches the skin's handlers. When a script mutates geometry inside an
`onResize` handler, the engine **re-runs the onResize cascade to a
fixpoint** — exactly the reflow the scripts expect — rather than hard-coding
any single skin's layout.

The VM is **vendored unmodified** and **bridged, never edited**. See:

- [the Maki bridge / SkinRuntime component](../components/skinruntime-maki-bridge.md)
  for `maki-bridge.cpp` (the *only* TU that includes the VM headers),
  `maki-bindings.cpp` (the `wq_*` binding bodies), and `SkinRuntimeBridge.cpp`
  (the `extern "C"` surface back into the Qt widget tree),
- [the cardinal rules](../conventions/cardinal-rules.md) for *why* it must
  stay that way.

## Live behaviour

- **Resize** re-flows the chrome through the real Maki `onResize` path.
- **Skins hot-reload** (qtamp watches the skin dir; see
  [build & test](../workflow/build-and-test.md), `WASABIQT_HOT_RELOAD`).
- **Renders end to end today:** Bento, Big Bento, Winamp Modern.

---
type: Section Index
id: components/index
title: Components & seams
description: The concrete code seams of the engine and the embedder — verified against the source tree — with header paths.
tags: [components, seams]
related:
  - components/skinxml.md
  - components/layout.md
  - components/widget-tree.md
  - components/skinruntime-maki-bridge.md
  - components/host-interface.md
  - components/painters-and-registries.md
  - components/surface-skinview-skinquickitem.md
  - components/wasabi-compat.md
  - components/milkdrop-visualizer.md
---

# Components & seams

Each object below names the real header/impl files (paths relative to
`deps/qtWasabi/` for the engine, `/` for qtamp).

| Object | Stage / role |
|---|---|
| [SkinXml](skinxml.md) | Parse `skin.xml` → `Document`. |
| [Layout](layout.md) | Expand groupdefs/sendparams → `Widget` tree; sysregion + frames. |
| [Widget tree](widget-tree.md) | The polymorphic widget classes and rect/hit-test model. |
| [SkinRuntime + Maki bridge](skinruntime-maki-bridge.md) | Run the vendored Maki VM; the bridge/bindings. |
| [Host interface](host-interface.md) | The ~51-virtual embedder seam. |
| [Painters & registries](painters-and-registries.md) | TreePainter/LayerPainter/TextPainter + resource registries. |
| [Surface: SkinView vs SkinQuickItem](surface-skinview-skinquickitem.md) | The two Qt render backends. |
| [wasabi-compat](wasabi-compat.md) | Win32/Winamp-API shim for original `ml_*` plugins. |
| [MilkDrop visualizer](milkdrop-visualizer.md) | qtamp-side projectM overlay + detached-window readback. |

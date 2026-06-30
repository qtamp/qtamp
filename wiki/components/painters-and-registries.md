---
type: Component
id: components/painters-and-registries
title: Painters & resource registries
description: TreePainter/LayerPainter/TextPainter walk the widget tree and paint through QPainter; the Bitmap/Font/Color/Gammaset registries resolve skin resources.
resource: deps/qtWasabi/public/qtWasabi/TreePainter.h
tags: [component, paint, qpainter, registry, gammaset, pipeline]
related:
  - architecture/pipeline.md
  - components/widget-tree.md
  - components/surface-skinview-skinquickitem.md
  - glossary/terms.md
---

# Painters & resource registries

**Pipeline stage:** Paint (fifth stage). Everything paints through
`QPainter`.

## Painters

| Header (`public/qtWasabi/`) | Class | Does |
|---|---|---|
| `TreePainter.h` | `TreePainter` | Recursively walks the resolved [widget tree](widget-tree.md) and paints each node, delegating to the layer/text painters; consults the registries below. |
| `LayerPainter.h` | `LayerPainter` | Bitmap blit renderer for `<layer>` — handles `relatw`/`relath` relative sizing and `activeAlpha`. |
| `TextPainter.h` | `TextPainter` | Bitmap-font text renderer for `<text>` — alignment, `display` keys (metadata fields), forced-uppercase, the lfHeight→pixelSize ratio. |

## Registries (`public/qtWasabi/`, impl in `src/`)

| Class | Resolves |
|---|---|
| `BitmapRegistry` | `.png`/`.jpg` from the skin archive, indexed by bitmap id. |
| `FontRegistry` | Wasabi `.fnt` bitmap-font files → glyph → region in atlas. |
| `ColorRegistry` | colour-scheme definitions used for themed chrome. |
| `GammasetRegistry` | **Color Theme** palette transforms — the active *gammaset* tints the rendered output (the Modern "Color Themes" feature). |
| `WindowHolderRegistry` | a factory for `<windowholder>` content (playlist editor, media library, vis, video). qtamp registers a frame provider here for detached projectM readback. |

> **Gammaset** is the Modern-skin colour-theming mechanism: a named palette
> transform applied over the painted output (see the
> [glossary](../glossary/terms.md)). qtamp exposes it via
> `WASABIQT_COLORTHEME` / `WASABIQT_GAMMASET` / `WASABIQT_LIVE_THEME`.

## Relevant env knobs

- `WASABIQT_TRACE_LAYER` (`LayerPainter.cpp`), `WASABIQT_TRACE_META`
  (`TextPainter.cpp`, metadata field resolution), `WASABIQT_FONT_RATIO`
  (`TextPainter.cpp`/`Text.cpp`, default 6/7), `WASABIQT_TRACE_BITMAP`
  (`BitmapRegistry.cpp`), `WASABIQT_SYN_BODY` (`GammasetRegistry.cpp`,
  synthetic gammaset `r,g,b,gray,boost` for testing).

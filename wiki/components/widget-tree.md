---
type: Component
id: components/widget-tree
title: Widget tree — the polymorphic widget classes
description: The Qt-native re-implementations of Wasabi's widget classes; each resolves its rect from attrs at paint time and alpha-hit-tests for input.
resource: deps/qtWasabi/public/qtWasabi/Widget.h
tags: [component, widgets, hit-test, pipeline]
related:
  - architecture/pipeline.md
  - architecture/hybrid-design.md
  - components/layout.md
  - components/painters-and-registries.md
  - conventions/cardinal-rules.md
  - glossary/terms.md
---

# Widget tree

**Base header:** `deps/qtWasabi/public/qtWasabi/Widget.h`
**Subclasses:** `deps/qtWasabi/src/widgets/*` (~37 classes)
**Pipeline stage:** Widget tree (third stage).

These are qtWasabi's **own Qt6 re-implementations** of Wasabi's widget
classes (the VM and BFC foundation are vendored; the widgets are not — see
[the hybrid design](../architecture/hybrid-design.md)). Each widget:

- **resolves its rect from attrs at paint time**, including relative
  `relatw`/`relath` and negative-`w` coordinate forms, and
- **alpha-hit-tests for input** — `Widget::hitTest` samples the painted alpha
  buffer and rejects clicks where the pixel is effectively transparent
  (`alphaBuf` ≤ 16). A widget can override `isSolidHitRegion()` (default
  false) to make its whole bbox clickable like a real HWND — `WindowHolderWidget`
  does this for the playlist/library so clicks in the transparent gap between
  rows still land.

## The widget classes (header → class)

| Header (`src/widgets/`) | Class | Role |
|---|---|---|
| `Widget.h` (base) | `Widget` | polymorphic base; rect, hit-test, attrs |
| `Layer.h` | `LayerWidget` | bitmap layer (`relatw`/`relath`/`activeAlpha`) |
| `Button.h` | `ButtonWidget` (+ toggle / nstates) | clickable buttons |
| `Slider.h` | `SliderWidget` | draggable sliders (owns drag lifecycle) |
| `Text.h` | `TextWidget` (+ song ticker) | bitmap-font text |
| `Container.h` | `ContainerWidget` | nested group layout |
| `WindowHolder.h` | `WindowHolderWidget` | embedded panel host (playlist, media library, vis, video) |
| `Vis.h` / `EqVis.h` | `VisWidget` / `EqVisWidget` | spectrum/oscilloscope visualizers |
| `AlbumArt.h` | `AlbumArtWidget` | album cover display |
| `Rect.h` | `RectWidget` | coloured rectangles |
| `Menu.h` / `PopupMenu.h` / `Popup.h` | menu widgets | menus/popups |
| `ScrollBar.h` `CheckBox.h` `RadioGroup.h` `Edit.h` `Browser.h` `Grid.h` `TreeList.h` `MultiColumnList.h` `GuiList.h` `PlaylistPro.h` `Status.h` `LayoutStatus.h` `TabSheet.h` `Splitter.h` `DropDownList.h` `SectionFrame.h` `ComponentBucket.h` `ColorThemesList.h` `Images.h` `ProgressGrid.h` `AnimatedLayer.h` `GroupXFade.h` `HideObject.h` | the rest | the long tail of Wasabi widget tags |

## The windowholder seam (read this if touching detached windows)

`WindowHolderWidget` hosts a *component* by GUID. Two spellings exist and
both must be handled by **general** code, never per-GUID:

- **docked** holders use `hold="guid:avs"` (prefixed),
- **detached** windows use a **bare GUID with no `guid:` prefix**, e.g.
  `<component param="{0000000A-…}">`.

The fix for "detached vis/video paints black" was a single `bareGuidLower()`
normaliser (strip optional `guid:`, lowercase) used by *all four*
`is*Hold`/`isAvsGuidRef` classifiers — the canonical example of the
[cardinal rule](../conventions/cardinal-rules.md). Related: a holder also
participates in mouse routing via `capturesMouse()` / `onLeftButtonDown()`
(reads `heldGuid(attrs)`, not raw `attrs.value("hold")`).

## Relevant env knobs

- `WASABIQT_TRACE_HOLDER` (`WindowHolder.cpp`), `WASABIQT_TRACE_CONTAINER`
  (`Container.cpp`), `WASABIQT_TRACE_SLIDER` (`Slider.cpp`),
  `WASABIQT_TRACE_ALBUMART` (`AlbumArt.cpp`), `WASABIQT_TRACE_GRID`
  (`Grid.cpp`), `WASABIQT_TRACE_UNKNOWN_TAGS` (`Widget.cpp`, logs an
  unrecognised XML tag once), `WASABIQT_TICK_SPEED` (`Text.cpp`, ticker
  speed), `WASABIQT_FONT_RATIO` (`Text.cpp`, lfHeight→pixelSize ratio,
  default 6/7).

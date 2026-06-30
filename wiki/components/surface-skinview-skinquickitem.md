---
type: Component
id: components/surface-skinview-skinquickitem
title: Surface — SkinView vs SkinQuickItem
description: The two Qt render backends that host the painted tree and route input back into the engine — a QWidget (SkinView) and a QQuickItem (SkinQuickItem); qtamp uses the Quick path.
resource: deps/qtWasabi/public/qtWasabi/SkinView.h
tags: [component, surface, skinview, skinquickitem, qt6, input]
related:
  - architecture/pipeline.md
  - components/painters-and-registries.md
  - components/host-interface.md
  - repos/qtamp.md
---

# Surface: SkinView vs SkinQuickItem

**Pipeline stage:** Surface (final stage) — the Qt integration that hosts the
painted tree, routes mouse + resize events back into the engine, and applies
the alpha window mask. Two interchangeable backends mirror the same API:

| Backend | Header | Qt base | Use |
|---|---|---|---|
| **SkinView** | `deps/qtWasabi/public/qtWasabi/SkinView.h` | `QWidget` | software `QPainter` widget. Used for **detached** sub-windows (no GL). |
| **SkinQuickItem** | `deps/qtWasabi/public/qtWasabi/SkinQuickItem.h` | `QQuickItem` | Qt Quick / scene-graph renderer. **The primary path** — qtamp's main window uses it. |

The Qt-level adapters live in `deps/qtWasabi/qt6/`: `QtCanvasAdapter`
(QImage/QPainter backend), `QtTimerAdapter` (QTimer for Maki timeouts),
`QtWindowAdapter` (QWindow / Wayland / X11), `Win32Shim` (Windows stubs).

## How qtamp uses it

qtamp's main player window is `QtampPlayerWindow` (`/src/main.cpp`), a
**`SkinQuickItem` subclass** parented into a frameless, transparent
`QQuickWindow` (so the skin paints its own titlebar and rounded corners; an
explicit ARGB surface format is set for Wayland). It overrides
`mousePressEvent` / `mouseMoveEvent` / `mouseReleaseEvent` / `wheelEvent` for
hit-testing and action dispatch.

> **Important input-routing gotcha** (hard-won): the *live* press path is the
> override in `QtampPlayerWindow::mousePressEvent`, **not**
> `SkinQuickItem::mousePressEvent`. The override's capture-claim loop must
> claim list holders (`w->capturesMouse()`), otherwise on Wayland it falls
> through to `startSystemMove()` and drags the window instead of selecting a
> playlist row. **Offscreen `startSystemMove` fails (no compositor)**, so the
> base path runs and the bug is masked — input-routing fixes must be verified
> against `QtampPlayerWindow`'s own overrides, and a Wayland-drag fix cannot
> be proven offscreen. Diagnose with `WASABIQT_TRACE_PLCLICK` (qtamp) /
> `WASABIQT_TRACE_MAKI`.

## Relevant env knobs (`SkinQuickItem.cpp` / `SkinView.cpp`)

- `WASABIQT_TRACE_RESIZE`, `WASABIQT_TRACE_HOVER`, `WASABIQT_TRACE_MASK`,
  `WASABIQT_INPUT_REGION` (enable per-widget input region),
  `WASABIQT_LEGACY_CUTOUTS`, `WASABIQT_NO_REGION_CLIP` (`SkinView.cpp`,
  disable computed window-region clipping).

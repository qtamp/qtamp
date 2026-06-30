---
type: Component
id: components/wasabi-compat
title: wasabi-compat — Win32 / Winamp-API shim for ml_* plugins
description: A Win32 + Winamp-API shim that lets the original media-library ml_* plugins compile and run on Linux against the engine, so the real media-library window renders themed by the skin with no re-implementation.
resource: deps/qtWasabi/wasabi-compat/
tags: [component, wasabi-compat, media-library, ml, win32-shim]
related:
  - architecture/pipeline.md
  - components/widget-tree.md
  - glossary/terms.md
---

# wasabi-compat

**Dir:** `deps/qtWasabi/wasabi-compat/`
**Related media-library code:** `deps/qtWasabi/src/ml/` (host widget, list/tree
views, header window), `deps/qtWasabi/src/pledit/` (in-player playlist
editor).

The original media-library is a set of `ml_*` plugins from the `gen_ml`
lineage. Rather than re-implement them, `wasabi-compat/` provides a **Win32 +
Winamp-API shim** (window messages, GDI raster, the service manager, `wa_dlg`
theming) so those plugins compile and run on Linux against the engine. This
is how the real media-library window renders — themed by the skin — with no
re-implementation.

It contains `compat-includes/` (e.g. `ml_nowplaying`, `ml_gen` stubs),
`services/` (service dispatch), `win32/` (`SendMessage`, window APIs), and
`patches/`.

The in-player playlist (`pledit/`) is a related host renderer
(`PleditHostRenderer`, `PleditHostShim`) that implements select / scroll /
scrollbar-drag / wheel for the playlist holder — **generically across
skins**, including skins with no scrollbar bitmaps (flat-bar fallback). See
[the widget tree](widget-tree.md) for the `WindowHolderWidget` hit-region and
mouse-capture details.

## Relevant env knobs

- `WASABIQT_ML_ICONS_DIR` (media-library icon path), `WASABIQT_TRACE_WADLG`
  (`MlHostWidget.cpp`, dialog palette priming).
- `WASABIQT_NO_PLEDIT` / `WASABIQT_TRACE_PLEDIT` / `WASABIQT_TRACE_PLCOLOR`
  (`PleditHostRenderer.cpp`); `WASABIQT_PE_ROWH` / `WASABIQT_PE_TOP` /
  `WASABIQT_PE_RESERVE` (`PleditHostShim.cpp`, row-height / top-margin /
  reserved-rows tuning).

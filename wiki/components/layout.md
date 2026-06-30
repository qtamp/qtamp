---
type: Component
id: components/layout
title: Layout — groupdef/sendparams expansion → widget tree
description: Expands the parsed Document into a tree of resolved Widget nodes, inlining groupdefs, applying per-instance sendparams, and modelling Wasabi:Frame splits, the sysregion window mask, and chrome cut-outs.
resource: deps/qtWasabi/public/qtWasabi/Layout.h
tags: [component, expand, layout, sendparams, sysregion, pipeline]
related:
  - architecture/pipeline.md
  - components/skinxml.md
  - components/widget-tree.md
  - components/skinruntime-maki-bridge.md
  - glossary/terms.md
---

# Layout

**Header:** `deps/qtWasabi/public/qtWasabi/Layout.h`
**Impl:** `deps/qtWasabi/src/Layout.cpp`
**Pipeline stage:** Expand (second stage).

`Layout::expandLayout(doc, container, layout)` turns a parsed
[`Document`](skinxml.md) into a tree of resolved `Widget` nodes
(`Layout::ResolvedWidget`). It:

- **inlines every `<groupdef>`** (a group template) wherever it is
  instantiated,
- **applies `<sendparams>` overrides** per group instance (the mechanism by
  which one groupdef is reused with different bitmaps/coords/ids),
- models **`Wasabi:Frame`** pane splits (resizable dividers; a frame's live
  divider position is queryable from Maki),
- computes the **`sysregion`** window mask and chrome **cut-outs** (the
  non-rectangular window shape and transparent holes), and
- provides `hitTest(tree, pos, actionOnly)` used by the embedder's mouse
  routing.

`Layout` also hosts a set of engine-level helpers invoked at load time:
`runKnownScripts` (well-known chrome mutations — drawer y, titlebar streaks),
`wireSteppers` (stepper+display cfgattrib wiring). These are *general*
helpers, not per-skin code.

## Relevant env knobs (all in `Layout.cpp`)

- `WASABIQT_SIZER` — dump geometry calculation (value used as a filter).
- `WASABIQT_TRACE_FRAME` — trace frame/group expansion.
- `WASABIQT_NO_FRAMEDIV` / `WASABIQT_DIVOFF` — disable / offset frame bevel
  dividers (default bevel offset 7).
- `WASABIQT_NO_SCRIPTHIDE` — disable visibility inheritance from Maki.
- `WASABIQT_DEBUG_SYSREGION` / `WASABIQT_DEBUG_SYSREGION_DUMP` /
  `WASABIQT_TRACE_CUTOUTS` / `WASABIQT_TRACE_CUTOUT_PAIRS` — trace/dump the
  window-region mask computation.

> Coordinates are **group-relative**, not window-relative. A widget's
> `x=`/`y=` are local to its group; the group itself is placed by its
> parent. Clicking the literal attr coord on a nested widget hits the wrong
> pixel unless you add the group offset. This is a common source of confusion
> when writing offscreen click tests — see
> [env knobs](../workflow/env-knobs.md) and the
> [glossary](../glossary/terms.md).

Output feeds the [Widget tree](widget-tree.md) and then
[SkinRuntime](skinruntime-maki-bridge.md), which re-runs the `onResize`
cascade to a fixpoint.

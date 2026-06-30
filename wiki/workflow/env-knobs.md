---
type: Reference
id: workflow/env-knobs
title: WASABIQT_* environment knobs & test hooks
description: The full inventory of WASABIQT_* environment variables across qtamp and qtWasabi — tracing, control, tuning, and the offscreen screenshot test hooks (fire-click, click-at, force-tab, etc.).
tags: [workflow, env, testing, debug, wasabiqt]
related:
  - workflow/build-and-test.md
  - components/skinruntime-maki-bridge.md
  - components/surface-skinview-skinquickitem.md
  - components/layout.md
---

# WASABIQT_* environment knobs

All engine debug/control is gated behind `WASABIQT_*` environment variables
(no per-skin code, no debug builds needed). Set them in the environment of
the `qtamp` process. Knobs are split below by where they live.

> **Coordinate gotcha for the click/hover hooks:** the `x,y` you pass are
> **window coordinates**, not group-local widget `x=`/`y=` attrs. A nested
> widget's attr coord is local to its group; the group is offset by its
> parent. Use the real window pixel. See [the Layout component](../components/layout.md).

## qtamp test hooks (offscreen screenshot harness, `src/main.cpp`)

These fire in `--screenshot` mode to exercise interaction before the PNG.

| Knob | Effect |
|---|---|
| `WASABIQT_CLICK_AT="x,y;x,y;…"` | Synthesize full Qt clicks at window coords — drives the **live** `mousePressEvent` hit path. |
| `WASABIQT_CLICK_DELAY` | Override the synth-click delay (default 600ms; needed e.g. to click a drawer row only after a slide finishes). |
| `WASABIQT_FIRE_CLICK="id\|x,y\|…"` | Dispatch `onLeftClick` on a widget **id** (via `findById`, **bypassing** hit-test) or a real click at `x,y` — the fast path. |
| `WASABIQT_FIRE_HOVER` / `WASABIQT_HOVER_AT="x,y;…"` | Synthesize hover at a widget id / at coords. |
| `WASABIQT_FIRE_PRESS_HOLD` | Synthesize a press with no release (id or x,y). |
| `WASABIQT_FORCE_TAB` / `WASABIQT_FORCE_ACTIVE_TAB` | Pre-select an Options/Color-Themes tab (the latter writes `__tab:active` cfgattrib, engine-level, no Maki). |
| `WASABIQT_FORCE_ATTR="id:key=val;…"` | Force widget attrs before the screenshot. |
| `WASABIQT_DRAWER_CLOSED` | Close the drawer before the screenshot. |
| `WASABIQT_SWITCH_TO` / `WASABIQT_TEST_SWITCH_SKIN` | Switch skin(s) at runtime to test `reloadSkin` (comma-separated, ~700ms apart). |
| `WASABIQT_TEST_SUBWIN_RESIZE` / `WASABIQT_FORCE_RESIZE="WxH"` | Build+resize a subwindow / force the main window size. |
| `WASABIQT_PLAY_FILE` | Play a file at startup (alt to positional args). |
| `WASABIQT_SELFTEST_CHROME` | Run the menu-bar ring + dialog re-tint self-test, then quit. |
| `WASABIQT_COLORTHEME` / `WASABIQT_GAMMASET` / `WASABIQT_LIVE_THEME` | Override the color theme / force a gammaset at startup / switch gammaset after load. |
| `WASABIQT_NO_ANIM` | Snap to final state (skip animations). |
| `WASABIQT_NO_STATIC_SCRIPTS` | Skip `Layout::runKnownScripts` (Maki only, no chrome mutation). |
| `WASABIQT_NO_RUNTIME` | Skip Maki dispatch entirely (static fallback only). |
| `WASABIQT_NO_FIRE_RESIZE` | Skip `SkinRuntime::dispatchInitialResize` (offscreen). |
| `WASABIQT_HOT_RELOAD` | Watch the skin dir; reload on a 250ms debounce. |
| `WASABIQT_TRACE_MAKI` / `WASABIQT_TRACE_META` / `WASABIQT_TRACE_MILKDROP` / `WASABIQT_TRACE_LAYOUTROOT` / `WASABIQT_TRACE_HOVER` / `WASABIQT_TRACE_PLCLICK` | qtamp-side tracing (action/slider dispatch, metadata, projectM presets, the resolved widget tree, hover, playlist-click diagnosis). |

## Engine tracing (qtWasabi, stderr)

`WASABIQT_TRACE_MAKI` (+ `_DEBUG`), `WASABIQT_TRACE_META`,
`WASABIQT_TRACE_LAYER`, `WASABIQT_TRACE_BITMAP`, `WASABIQT_TRACE_RESIZE`,
`WASABIQT_TRACE_HOVER`, `WASABIQT_TRACE_MASK`, `WASABIQT_TRACE_CONTAINER`,
`WASABIQT_TRACE_HOLDER`, `WASABIQT_TRACE_GRID`, `WASABIQT_TRACE_VOL`,
`WASABIQT_TRACE_MENULAYER`, `WASABIQT_TRACE_UNKNOWN_TAGS`,
`WASABIQT_TRACE_SLIDER`, `WASABIQT_TRACE_ALBUMART`, `WASABIQT_TRACE_FRAME`,
`WASABIQT_TRACE_BITMAP`, `WASABIQT_TRACE_CUTOUTS` /
`WASABIQT_TRACE_CUTOUT_PAIRS`, `WASABIQT_DEBUG_SYSREGION` (+ `_DUMP`),
`WASABIQT_TRACE_ADDSCRIPT`, `WASABIQT_TRACE_HYDRATE`, `WASABIQT_TRACE_ATTRIB`,
`WASABIQT_TRACE_SCRIPTS` / `_SCRIPTSCOPE` / `_SETTLE` / `_XUI`,
`WASABIQT_TRACE_GEOSET`, `WASABIQT_TRACE_TIMER`, `WASABIQT_TRACE_PLENLARGE`,
`WASABIQT_TRACE_PLEDIT` / `_PLCOLOR`, `WASABIQT_TRACE_WADLG`,
`WASABIQT_TRACE_UNKNOWN_DLF`.

## Engine control / behaviour toggles (qtWasabi)

| Knob | File | Effect |
|---|---|---|
| `WASABIQT_NO_SCRIPTHIDE` | `Layout.cpp` | Disable Maki visibility inheritance. |
| `WASABIQT_NO_FRAMEDIV` / `WASABIQT_DIVOFF` | `Layout.cpp` | Disable / offset frame bevel dividers (default 7). |
| `WASABIQT_NO_REGION_CLIP` | `SkinView.cpp` | Disable computed window-region clipping. |
| `WASABIQT_INPUT_REGION` | `SkinQuickItem.cpp` | Enable per-widget input region (off by default for perf). |
| `WASABIQT_LEGACY_CUTOUTS` | `SkinQuickItem.cpp` | Re-enable old per-widget cutout calc. |
| `WASABIQT_NO_PER_OBJECT_RESIZE` | `SkinRuntimeBridge.cpp` | Disable per-widget `onResize` dispatch. |
| `WASABIQT_NO_PLEDIT` | `PleditHostRenderer.cpp` | Disable playlist-editor rendering (diagnostic). |
| `WASABIQT_FATAL_ASSERTS` | `wasabi-port-stubs.cpp` | Abort on a VM ASSERT instead of logging. |
| `WASABIQT_PUSH_ORDER` | `maki-bridge.cpp` | Script variable push order (`revdecl` = reverse). |

## Engine tuning (qtWasabi)

| Knob | File | Effect |
|---|---|---|
| `WASABIQT_FONT_RATIO` | `TextPainter.cpp` / `Text.cpp` | lfHeight→pixelSize ratio (default 6/7). |
| `WASABIQT_TICK_SPEED` | `Text.cpp` | Scrolling-ticker speed. |
| `WASABIQT_PE_ROWH` / `WASABIQT_PE_TOP` / `WASABIQT_PE_RESERVE` | `PleditHostShim.cpp` | Playlist row-height / top-margin / reserved-rows. |
| `WASABIQT_SYN_BODY` | `GammasetRegistry.cpp` | Synthetic gammaset for testing (`r,g,b,gray,boost`). |
| `WASABIQT_ML_ICONS_DIR` | `MlHostWidget.cpp` | Media-library icon search path. |
| `WASABIQT_SIZER` | `Layout.cpp` | Dump geometry calc (value = filter). |

## Test-harness only (qtWasabi `tests/`)

`WASABIQT_REGEN_GOLDENS` (regenerate reference images instead of comparing),
`WASABIQT_DUMP_CODEBLOCKS` (dump compiled script codeblocks),
`WASABIQT_DISPATCH_ONLOAD` (fire `onScriptLoaded` during tests).

> This inventory is exhaustive as of the current tree (~56 engine knobs plus
> the qtamp test hooks). New knobs are added as new diagnostics are needed;
> grep `WASABIQT_` to confirm the live set.

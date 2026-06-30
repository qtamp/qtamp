---
type: Glossary
id: glossary/terms
title: Domain glossary
description: Definitions of the Wasabi/Winamp-Modern domain terms used throughout the codebase and this wiki.
tags: [glossary, terms, wasabi, maki, wal]
related:
  - architecture/pipeline.md
  - architecture/hybrid-design.md
  - components/widget-tree.md
  - components/skinruntime-maki-bridge.md
  - components/painters-and-registries.md
---

# Domain glossary

## Wasabi
Nullsoft's UI framework and skin engine, the technology behind Winamp's
**Modern** skins (Winamp 3 / Winamp 5). It comprises a widget framework, an
XML skin format, the **Maki** scripting VM, and the **BFC** foundation
library. qtWasabi is the open continuation of the **Wasabi 2** decoupling
(prying the engine loose from the player), rendered through Qt.

## Wasabi 2 / Replicant
Nullsoft's in-house, never-finished attempt to refactor Wasabi into a
standalone, service-oriented, portable core (`Src/replicant/`). qtWasabi
shares its goal and finishes it in the open by replacing the rendering layer
with Qt.

## Maki
The Wasabi **scripting language** and its **bytecode VM**. Skins ship
compiled `.maki` bytecode that the VM executes to drive layout and behaviour
(`onScriptLoaded`, `onResize`, `onAction`, `onTimer`, `setXmlParam`,
`findObject`, …). qtWasabi **vendors the VM unmodified** because thousands of
shipped skins are the only real spec for it; it is **bridged, never edited**.
A **Maki guru** is the VM's runtime-error report — CI expects zero gurus. See
[the Maki bridge](../components/skinruntime-maki-bridge.md).

## `.wal`
**W**inamp **A**dvanced **L**ibrary skin — a Modern skin packaged as a
renamed **zip archive** whose entry point is `skin.xml`. qtWasabi loads a
`.wal` archive or an already-unpacked directory. (Distinct from the *classic*
`.wsz` skin format, which is a future milestone, not current.)

## skin.xml
The root of a Modern skin: declares containers, layouts, **groupdef**s,
`<script>` refs, and resources (bitmaps, colors, fonts, gammasets). Parsed by
[SkinXml](../components/skinxml.md).

## groupdef / sendparams
A **groupdef** is a reusable group template. **sendparams** are per-instance
overrides (bitmaps, coords, ids) applied when a groupdef is instantiated, so
one template renders many concrete widgets. [Layout](../components/layout.md)
inlines groupdefs and applies sendparams.

## windowholder
A Wasabi widget (`<windowholder>` / a holder slot, `WindowHolderWidget`) that
**hosts a component** — the in-player playlist editor, the media-library
window, the visualizer (AVS), or video — inside the skin's chrome. A holder
references its component by **GUID**. Key gotcha: **docked** holders use a
`guid:`-**prefixed** spelling (`hold="guid:avs"`), while **detached** windows
use a **bare GUID** (`<component param="{0000000A-…}">`). General code
(`bareGuidLower()`) must handle both — never per-GUID. See
[the widget tree](../components/widget-tree.md).

## GUID component
A skinnable sub-UI identified by a globally-unique id, e.g. the visualizer
(`{0000000A-…}`), video (`{F0816D7B-…}`), the playlist editor, the media
library. Wasabi resolves a component by its GUID via BFC's GUID-keyed
dispatch (`dependent_getInterface`) — COM-ish polymorphism without COM. See
[the hybrid design](../architecture/hybrid-design.md).

## gammaset
The Modern-skin **Color Themes** mechanism: a named **palette transform**
(roughly a colourisation/gamma adjustment) applied over the painted output to
re-tint a skin. Managed by `GammasetRegistry`; exposed in qtamp via
`WASABIQT_COLORTHEME` / `WASABIQT_GAMMASET` / `WASABIQT_LIVE_THEME`. See
[painters & registries](../components/painters-and-registries.md).

## MCV
**M**aki **C**ompiled **V**iew / Maki-Compiled-View — the compiled-script
view a Maki-driven group presents; in practice the script-controlled,
interactive skin region (e.g. Bento's MCV interactivity). The widget tree +
the Maki runtime together realise the MCV; the layout is whatever the skin's
own scripts produce, re-settled to a fixpoint by the engine.

## BFC
**B**eex **F**oundation **C**lasses — Nullsoft's homegrown C++ stdlib
(~2000–2002). Two layers: a POSIX-clean **foundation** (containers, GUIDs,
dispatch, node-tree, threading, strings, math — *used*, ~3000 LOC) and an
unported **platform** layer (`std_file`, `std_keyboard`, `std_wnd`, the X11
backend — *not used*; Qt provides all of it). See
[the hybrid design](../architecture/hybrid-design.md).

## AVS
**A**dvanced **V**isualization **S**tudio — Winamp's visualizer. In qtWasabi
the AVS slot is a windowholder; in qtamp the pixels come from
[the MilkDrop / projectM visualizer](../components/milkdrop-visualizer.md).

## Wasabi:Frame
A Wasabi resizable pane split (a draggable divider between two panes). The
engine models the frame's live divider position; Maki can query/set it
(`getPosition` returns the live position). Handled in
[Layout](../components/layout.md).

## sysregion
The non-rectangular **window mask** a skin defines (its silhouette + any
transparent cut-outs). Computed during expansion and applied at the
[surface](../components/surface-skinview-skinquickitem.md); traceable via
`WASABIQT_DEBUG_SYSREGION`.

## cfgattrib
A configuration attribute channel the engine uses to wire UI state without
per-skin code (e.g. `__action:EQ_TOGGLE`, `__tab:active`). Steppers/displays
are wired through cfgattribs by `Layout::wireSteppers`.

## Host
The ~51-virtual `qtWasabi::Host` interface the embedder implements — the one
seam between engine and player. See
[the Host interface](../components/host-interface.md).

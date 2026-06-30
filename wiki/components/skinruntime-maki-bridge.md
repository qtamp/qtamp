---
type: Component
id: components/skinruntime-maki-bridge
title: SkinRuntime + the wasabi-port Maki bridge
description: Runs the skins' own scripts on the vendored Maki VM and bridges the VM's ScriptObject interface to qtWasabi's Qt-native widgets — the VM bridged, never edited.
resource: deps/qtWasabi/public/qtWasabi/SkinRuntime.h
tags: [component, maki, vm, bridge, vendored, runtime, pipeline]
related:
  - architecture/pipeline.md
  - architecture/hybrid-design.md
  - components/widget-tree.md
  - conventions/cardinal-rules.md
  - glossary/terms.md
---

# SkinRuntime + the Maki bridge

**Runtime header:** `deps/qtWasabi/public/qtWasabi/SkinRuntime.h`
**Runtime impl:** `deps/qtWasabi/src/SkinRuntime.cpp`
**Bridge dir:** `deps/qtWasabi/wasabi-port/`
**Pipeline stage:** Maki VM (fourth stage — the heart).

`SkinRuntime` loads each compiled `<script>` (`.maki` bytecode) into the
**vendored Maki bytecode VM**, binds a `WidgetScriptObject` to every id'd
widget, and dispatches the skin's handlers — `onScriptLoaded`, `onResize`,
`onAction`, `onTimer`, `setXmlParam`, `findObject`, … **The scripts drive the
layout; the engine just runs them.** When a script mutates geometry in an
`onResize` handler, the runtime re-runs the cascade **to a fixpoint**
(`dispatchInitialResize`), the reflow the scripts expect.

## The bridge files (`wasabi-port/`)

The VM is **vendored unmodified** and **bridged, never edited** — see
[the cardinal rules](../conventions/cardinal-rules.md). The bridge is
deliberately structured so the VM's macro-heavy headers touch exactly one
translation unit:

| File | Role |
|---|---|
| **`maki-bridge.cpp`** (`maki-bridge.h`) | **The SOLE translation unit that includes the VM headers** (`api/script/vcpu.h`). Forwards VM entry points (script load, null-object hydration, VM state) through a clean Qt-compatible surface so the VM's types and macro pollution never leak into the Qt side. |
| **`maki-bindings.cpp`** | The `wq_*` binding bodies the VM calls when a script does `setXmlParam`, `getAutoWidth`, `findObject`, `new Timer`, `screen_avail_w`, `named_window`, etc. These hold Maki/Wasabi semantics (e.g. a `Timer`'s `setDelay` only re-arms when already started; `getPosition` on a frame returns its live divider position). |
| **`widget-script-object.cpp`** | `WidgetScriptObject` — the per-widget `Dispatchable`-derived adapter that answers the VM's GUID-keyed `dependent_getInterface` query with itself, then routes `setAttr`/`getAttr`/method dispatch to the Qt widget. |
| **`SkinRuntimeBridge.cpp`** (in `src/`) | A small `extern "C"` surface that lets the bindings reach the Qt widget tree (look up a widget by id, mutate an attr, fire an event, set geometry) **without including any Qt header**. |
| `wasabi-port-stubs.cpp` / `wasabi-port-link-stubs.cpp` | VM init + the ASSERT handler; fallback stubs for unresolved Wasabi API calls (the unported BFC platform symbols). |
| `wasabi-port-shim.h` / `wasabi-port-cleanmacros.h` | Macro hygiene so VM headers don't poison the rest of the build. |

**Binding naming:** all exposed script APIs use the `wq_` prefix.

## The rule (do not skip)

The fix for *any* wrong skin behaviour lands in `maki-bindings.cpp` or the
[widget engine](widget-tree.md) — **never** inside the VM, and **never** as
per-skin glue. See [the cardinal rules](../conventions/cardinal-rules.md).

## Relevant env knobs

- `WASABIQT_TRACE_MAKI` / `WASABIQT_TRACE_MAKI_DEBUG` — Maki execution trace
  (verbose / extreme). Spans `SkinRuntime.cpp`, `maki-bridge.cpp`,
  `maki-bindings.cpp`, `SkinRuntimeBridge.cpp`, the surfaces, etc.
- `WASABIQT_TRACE_ADDSCRIPT` / `WASABIQT_TRACE_HYDRATE` / `WASABIQT_PUSH_ORDER`
  (`maki-bridge.cpp`) — script load, null-object hydration, variable push
  order.
- `WASABIQT_TRACE_ATTRIB` (`maki-bindings.cpp`, `widget-script-object.cpp`) —
  widget attribute get/set.
- `WASABIQT_TRACE_SCRIPTS` / `WASABIQT_TRACE_SCRIPTSCOPE` /
  `WASABIQT_TRACE_SETTLE` / `WASABIQT_TRACE_XUI` (`SkinRuntime.cpp`).
- `WASABIQT_TRACE_GEOSET` / `WASABIQT_TRACE_TIMER` /
  `WASABIQT_NO_PER_OBJECT_RESIZE` (`SkinRuntimeBridge.cpp`).
- `WASABIQT_FATAL_ASSERTS` — abort on a VM ASSERT instead of logging (a
  *Maki guru* is the VM's runtime-error report; CI expects zero gurus).

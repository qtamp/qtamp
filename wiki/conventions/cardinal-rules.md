---
type: Rule
id: conventions/cardinal-rules
title: Cardinal rules — fix the engine, never the skin
description: The non-negotiable invariants of qtWasabi — engine-level-not-per-skin fixes, vendored Maki VM bridged-never-edited, and byte-identical refactors.
tags: [rules, cardinal, engine, maki, invariant]
related:
  - architecture/pipeline.md
  - architecture/hybrid-design.md
  - components/skinruntime-maki-bridge.md
  - components/widget-tree.md
  - workflow/build-and-test.md
---

# Cardinal rules

These are the invariants that define qtWasabi. Breaking them defeats the
project's reason to exist. If a change you are about to make conflicts with
one of these, stop and rethink — the real fix is almost always one level
down.

## 1. Fix the engine, never the skin

**There is no per-skin code. No `if (id == "someWidget")`. No "Bento needs
this". No special-casing a layout.**

The whole point of the engine is that *any* `.wal` skin — including skins
that do not exist yet — renders correctly from the **same code path**,
because the Maki VM and the documented Wasabi widget behaviour are the spec.
Thousands of shipped skins exercise the engine; they are the test corpus.

When a skin renders wrong, the bug is in the engine:

- a Maki binding returning the wrong value or type,
- a widget mis-resolving its rect,
- an event that doesn't fire,
- a normaliser that only matched one spelling of an attribute.

…and the fix belongs **there**, so every other skin benefits. If you find
yourself reaching for a skin name or a widget id, that's the tell: the real
fix is general.

### What "general" looks like in practice

- **Layout settling.** When a script mutates geometry inside an `onResize`
  handler, the engine re-runs the `onResize` cascade *to a fixpoint* — the
  reflow the scripts expect — instead of hard-coding any one skin's result.
- **Normalising attribute spellings.** Winamp Modern's *detached* windows
  host their component via a **bare GUID with no `guid:` prefix** (e.g.
  `<component param="{0000000A-…}">`), while *docked* holders use
  `hold="guid:avs"` (prefixed). The fix for "detached vis/video paints
  black" was a single `bareGuidLower()` normaliser (strip optional `guid:`,
  lowercase) used by *all four* `is*Hold` classifiers — not a check for any
  particular GUID. One general fix lit up both detached vis and detached
  video. See [the windowholder glossary term](../glossary/terms.md).
- **Generic input routing.** Playlist select/scroll was made to work across
  *all* skins by introducing engine-level concepts (`capturesMouse()`,
  `isSolidHitRegion()`, a wheel accumulator) — never by recognising a
  specific playlist.

## 2. The vendored Maki VM is bridged, never edited

The Maki bytecode VM is **vendored unmodified** (in `wasabi-port/`). It is
the one part qtWasabi deliberately does not re-implement, because thousands
of shipped skins are the only real spec for it; a re-implementation would
drift and break obscure scripts.

**Do not patch the VM.** Behaviour gaps get fixed in the bindings
(`maki-bindings.cpp`, the `wq_*` method bodies) or in the widget engine —
never inside the VM. The VM's headers are included by **exactly one
translation unit** (`maki-bridge.cpp`) so the VM's types never leak into the
Qt side. See [the Maki bridge component](../components/skinruntime-maki-bridge.md).

## 3. A behavioural fix should change only what it intends to

Before sending a change, confirm the three reference skins still render and
that a **non-visual change leaves them byte-identical**:

```
Bento, Big Bento, Winamp Modern  →  0 pixel diff for refactors, 0 Maki gurus
```

If a "general" fix moves pixels on a skin it wasn't about, it is probably
skin-specific in disguise. (A *Maki guru* is the VM's runtime-error report —
zero gurus means no script blew up.) See
[the build & test workflow](../workflow/build-and-test.md) for the offscreen
diff loop.

## 4. No platform-port layer, no player in the engine

- The open-sourced BFC **platform** pieces (`std_file`, `std_wnd`, the X11
  backend) are **not used** — Qt provides file I/O, keyboard, window,
  plugins.
- Playback, decoding, and the music library live **behind `Host`**, in the
  embedder ([qtamp](../repos/qtamp.md)), never in the engine.

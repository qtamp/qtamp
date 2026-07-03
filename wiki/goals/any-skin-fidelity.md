---
type: Goal
id: goals/any-skin-fidelity
title: qtamp's role in the one goal — reference embedder, never a second engine
description: >
  The embedder-side view of qtWasabi's governing goal (every skin ever built
  or yet to be built for original Winamp, rendered by the original Maki VM).
  Names the skin-imitating code still living in qtamp that must migrate into
  the engine's generic mechanisms and be deleted here.
tags: [goal, qtamp, embedder, fidelity, crutches]
timestamp: 2026-07-03T16:30:00+02:00
related:
  - ../index.md
  - ../conventions/cardinal-rules.md
  - ../repos/relationship.md
---

# qtamp's role in the one goal

The goal belongs to the engine and is stated in full in the qtWasabi OKF
bundle: `deps/qtWasabi/okf/goals/any-skin-fidelity.md`.

> qtWasabi has to work for every skin that is already built for the original
> Winamp, and for every skin that will be built for it in the future,
> potentially thousands of Maki binaries, by rendering them correctly through
> the original Maki VM.

qtamp is **not that important** in this picture. It is the reference
implementation: it proves the engine achieves the goal by embedding it
behind the single `qtWasabi::Host` interface and supplying real playback,
playlist, and media-library data. Nothing more.

## What follows for this repo

1. **qtamp never compensates for the engine.** When a skin misbehaves, the
   fix goes into qtWasabi so every embedder benefits. A workaround here is
   debt in the wrong repository.
2. **qtamp carries no skin knowledge.** Widget ids, GUID literals, drawer
   geometry, tab mappings, and skin names in `src/main.cpp` all violate
   the goal.

## The embedder crutches to delete

Registered centrally in `deps/qtWasabi/okf/fidelity/crutch-register.md`;
the embedder-owned entries, all in `src/main.cpp`:

- `setDrawerOpen` (drawer ids + y=17/133 + compactH=144) and
  `switchDrawerTab` (+ the id-substring tab detection): reimplementations of
  `configtabs.m`. Deleted the day the engine's unified state store and event
  surface let the skin's own script drive the drawer.
- `applyDrawerModeFixup`: papers over the engine's WindowHolder
  script-object lookup gap.
- The literal `HeadAMP` branch with its hand-measured tint rectangle.
- The 750 ms resize-callback block (tuned to Bento's `maximize.m`).
- The qtamp-private ColorThemes drawer list with fixed pixel metrics.
- `toggleShade`'s invented 30 px strip instead of the skin's real
  `<layout id="shade">`.

Each is scheduled against the engine workstream that obsoletes it in
`deps/qtWasabi/okf/roadmap/index.md`. None gets extended in the meantime.

## What qtamp keeps

The Host implementation (audio pipeline, playlist model, the DuckDB+Parquet
media-library index), window/session plumbing, packaging, and the offscreen
verification harness the engine's corpus gate runs on. That is the whole
job of a reference embedder.

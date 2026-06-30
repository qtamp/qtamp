---
type: Bundle Index
id: index
title: qtamp / qtWasabi knowledge bundle
description: Landing page for the OKF knowledge base covering qtamp (player + reference embedder) and qtWasabi (Winamp Modern skin rendering engine).
tags: [qtamp, qtwasabi, okf, index]
related:
  - README.md
  - architecture/index.md
  - conventions/cardinal-rules.md
---

# qtamp / qtWasabi knowledge bundle

This is the root of an [Open Knowledge Format](README.md) bundle describing
two related projects:

- **[qtamp](repos/qtamp.md)** — a Qt6-native music player and the *reference
  embedder* for qtWasabi.
- **[qtWasabi](repos/qtwasabi.md)** — an open-source rendering engine that
  runs Winamp **Modern** (`.wal` / Wasabi) skins by executing the skins' own
  vendored Maki VM bytecode and painting through `QPainter`, decoupled from
  any player by a single `qtWasabi::Host` interface.

qtamp embeds qtWasabi (the engine lives at `deps/qtWasabi` as a git
submodule). See [the relationship object](repos/relationship.md).

## The one thing to internalize first

> **Fixes are engine-level and general — never per-skin `if (id == "...")`
> glue. Thousands of shipped skins are the spec.**

Read [`conventions/cardinal-rules.md`](conventions/cardinal-rules.md) before
touching code.

## Sections

- [Architecture](architecture/index.md) — the render pipeline, the hybrid
  design, and the embedding boundary.
- [Repos](repos/index.md) — the two projects and how they relate.
- [Components](components/index.md) — the concrete code seams.
- [Conventions](conventions/index.md) — the cardinal rules, commit
  authorship, comment style.
- [Workflow](workflow/index.md) — build + offscreen test harness + env knobs.
- [Glossary](glossary/index.md) — domain terms.

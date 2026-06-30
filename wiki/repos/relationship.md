---
type: Relationship
id: repos/relationship
title: How qtamp consumes qtWasabi
description: qtamp embeds qtWasabi (a git submodule at deps/qtWasabi) by implementing the qtWasabi::Host interface; the engine never talks to the player directly.
tags: [relationship, submodule, host, embedding]
related:
  - repos/qtamp.md
  - repos/qtwasabi.md
  - architecture/embedding-boundary.md
  - components/host-interface.md
---

# qtamp ↔ qtWasabi

## Submodule

[qtWasabi](qtwasabi.md) lives at **`deps/qtWasabi`** as a **git submodule** of
[qtamp](qtamp.md). Clone with submodules populated:

```sh
git clone --recurse-submodules https://github.com/kleberbaum/qtamp
# or, after a plain clone:
git submodule update --init --recursive
```

Bump the engine with:

```sh
git submodule update --remote deps/qtWasabi
git add deps/qtWasabi
git commit -m "chore(deps): bump qtWasabi"
```

qtWasabi in turn needs a user-supplied WCL-licensed 2024 Llama Group Winamp
source release on local disk (`deps/qtWasabi/wasabi-src/`, fetched by
`scripts/fetch-wasabi.sh`, gitignored on both ends) to compile the vendored
Maki VM + minimal BFC subset. See
[build & test](../workflow/build-and-test.md).

## The dependency direction

```
qtamp (player)  ── depends on ──▶  qtWasabi (engine)  ── needs at build time ──▶  user-supplied Wasabi source (WCL)
   │                                                                                       (Maki VM + BFC subset)
   └── implements qtWasabi::Host  ◀── engine asks the player for everything player-shaped ──┘
```

- **qtamp depends on qtWasabi**, never the reverse. The engine knows nothing
  about qtamp.
- The **only** coupling is the [`Host` interface](../components/host-interface.md):
  qtamp's `QtampHost` (in `src/main.cpp`) implements ~51 virtuals bridging
  `QMediaPlayer` / playlist / library / EQ state to the engine. The engine
  reaches the outside world *exclusively* through that seam — see
  [the embedding boundary](../architecture/embedding-boundary.md).
- qtamp builds the engine in only when `-DQTAMP_USE_QTWASABI=ON` (which
  defines `WINAMP_HAVE_WASABIQT`); without it, qtamp falls back to its own
  classic-skin code path (`WinampWindow`).

## Why this shape

This is the Wasabi 2 decoupling, finished in the open: the engine is a small
embeddable piece any themeable Qt player can adopt by implementing `Host`.
qtamp exists to *prove* that — it is the reference embedder, not the only
possible one.

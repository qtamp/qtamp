---
type: Rule
id: conventions/commit-authorship
title: Commit & authorship conventions
description: Commits are authored solely as Florian Kleber, use Commitizen-style messages, and must NOT carry a Co-Authored-By trailer.
tags: [conventions, git, commit, authorship, commitizen]
related:
  - conventions/cardinal-rules.md
  - conventions/comments.md
  - repos/relationship.md
---

# Commit & authorship conventions

## Author

All commits are authored **solely as Florian Kleber**:

```
Florian Kleber <kleber@snek.at>
```

(GitHub handle `kleberbaum`; do not invent a different first name from the
handle.)

## No AI co-author trailer

**Do NOT add a `Co-Authored-By: Claude …` (or any AI) trailer** to commits.
Commits must read as authored by Florian Kleber only — no co-author lines.

## Message format — Commitizen / Conventional Commits

Use **Commitizen**-style (Conventional Commits) messages:

```
type(scope): short imperative summary

optional body explaining the why
```

Common types: `feat`, `fix`, `refactor`, `chore`, `test`, `docs`, `perf`.
Example from this repo's documented workflow for bumping the engine submodule:

```
chore(deps): bump qtWasabi
```

## Submodule bumps

When the engine changes, bump the submodule pointer explicitly and commit it
on the qtamp side:

```sh
git submodule update --remote deps/qtWasabi
git add deps/qtWasabi
git commit -m "chore(deps): bump qtWasabi"
```

See [the relationship object](../repos/relationship.md).

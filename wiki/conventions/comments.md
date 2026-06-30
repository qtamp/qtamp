---
type: Rule
id: conventions/comments
title: Code comment conventions
description: Comments describe qtWasabi's own behaviour, reference the Maki VM / Winamp API / Wasabi conventions as the spec, never cite the original Winamp source tree by path, and never leave milestone scaffolding.
tags: [conventions, comments, style]
related:
  - conventions/cardinal-rules.md
  - conventions/commit-authorship.md
---

# Code comment conventions

qtWasabi is its own engine. In code comments:

- **Don't** cite the original Winamp source tree by path
  (`Src/Wasabi/...`, `winamp-linux/...`, `file.cpp:line`). Describe
  qtWasabi's own behaviour.
- **Do** reference *the Maki VM*, *the Winamp API*, and Wasabi conventions
  (`Wasabi:Frame`, the XML attrs) — those name the **spec** the engine
  implements. Saying the Maki VM was *open-sourced* is fine; it was.
- **Don't** leave development scaffolding — phase/milestone tags, task
  numbers, "this session", "future work". Explain the *why* of the code as
  it stands.

Comments explain **why, not what.** Keep them complete and accurate to the
code.

> Corollary for this wiki and for any docs you author: the same applies —
> name the spec (Maki VM, Wasabi attrs), not the vendored source paths, and
> don't leave milestone/task scaffolding in prose meant to outlive the
> session.

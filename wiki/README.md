# qtamp / qtWasabi — Open Knowledge Format (OKF) wiki

This folder is a machine-readable knowledge base for the **qtamp** and
**qtWasabi** projects, authored in Google's **Open Knowledge Format (OKF)**
so that AI coding agents (and humans) can onboard and develop in this repo
without re-deriving the architecture from scratch every time.

> If you are an AI agent landing in this repo: **start here, then read
> [`/architecture/pipeline.md`](architecture/pipeline.md) and
> [`/conventions/cardinal-rules.md`](conventions/cardinal-rules.md) before
> writing any code.** The cardinal rules are load-bearing — violating them
> (e.g. adding per-skin glue) is the single most common way to "fix" a skin
> while breaking the engine's whole reason to exist.

## What is OKF?

The **Open Knowledge Format (OKF)** is an open specification, published by
Google Cloud (v0.1, 12 June 2026), that formalizes the "LLM-wiki" pattern
into a portable, vendor-neutral format for representing the metadata,
context, and curated knowledge that AI agents need. It is deliberately
minimal:

- **It's just markdown files with YAML frontmatter.** Readable by humans in
  any editor, renderable on GitHub, parseable by agents with no bespoke SDK.
- **It's just files.** Shippable as a tarball, hostable in any git repo,
  diffable in version control alongside the code it describes.
- **It's a format, not a platform.** No runtime, no database, no model
  provider lock-in.

Source: ["How the Open Knowledge Format can improve data sharing", Google Cloud Blog](https://cloud.google.com/blog/products/data-analytics/how-the-open-knowledge-format-can-improve-data-sharing)
· Spec: [`GoogleCloudPlatform/knowledge-catalog/okf/SPEC.md`](https://github.com/GoogleCloudPlatform/knowledge-catalog/blob/main/okf/SPEC.md)

## Why this folder exists

qtWasabi has one cardinal invariant — **fixes are engine-level and general,
never per-skin** — and a hybrid architecture (vendored Maki VM + fresh Qt
widget engine) whose seams are easy to misunderstand. An agent that doesn't
know this will "fix" a skin by special-casing it and silently break the
project's core promise. This wiki captures the architecture, the seams, the
rules, and the offscreen test workflow as **version-controlled knowledge
objects** that travel with the code.

## The OKF convention adopted here

This wiki follows **OKF v0.1 faithfully**: every knowledge object is a
markdown file with a YAML frontmatter block. The spec requires exactly one
frontmatter field — `type` — and recommends `title`, `description`,
`resource`, `tags`, `timestamp`; producers may add their own keys.

Concretely, every concept file in this bundle uses this frontmatter shape:

```yaml
---
type: <concept kind>          # REQUIRED by OKF (e.g. "Architecture", "Component", "Rule", "Glossary Term")
id: <bundle-relative concept id, no .md>   # producer key: stable identifier (mirrors the OKF "path-as-id")
title: <human-readable title>
description: <one-sentence summary>
tags: [<tag>, ...]
related:                       # producer key: explicit typed relationships
  - <bundle-relative path to a related concept>
---
```

Notes on our choices:

- **OKF's canonical concept id is "the file path with `.md` removed"** (e.g.
  `components/host.md` → `components/host`). We also write that id into an
  explicit `id:` frontmatter key so an agent that has only the file *contents*
  (not its path) still has a stable identifier to cite and cross-reference.
- **Relationships.** In OKF, a markdown link from concept A to concept B
  *asserts a relationship* (untyped). We surface relationships two ways, so
  they are both human-navigable and trivially machine-extractable:
  - **`related:` frontmatter** — a YAML list of the most important
    neighbours, written as **bundle-root-relative paths** (rooted at this
    `wiki/` directory, e.g. `components/host-interface.md`). This is OKF's
    recommended "absolute" link form (path from the bundle root); a tool can
    resolve every `related:` entry against the bundle root with no per-file
    context. Each `related:` path equals some other object's `id:` + `.md`.
  - **Inline prose links** — ordinary **file-relative** markdown links (e.g.
    `../components/host-interface.md`) so the pages also render and navigate
    correctly on GitHub.
- **`index.md`** files are reserved by OKF as directory landing pages; we use
  one per subfolder. **`log.md`** is reserved by OKF for a chronological
  change history; we do not ship one yet (this is a fresh bundle).

This is real OKF v0.1, not an invented schema — the only additions are the
two producer keys `id` and `related`, which the spec explicitly permits
("…other producer-defined key/value pairs").

## How an agent should consume this bundle

1. Read this `README.md`.
2. Read [`/architecture/pipeline.md`](architecture/pipeline.md) for the
   parse → expand → widget-tree → Maki VM → painters → surface flow.
3. Read [`/conventions/cardinal-rules.md`](conventions/cardinal-rules.md)
   **before editing anything** — especially the "fix the engine, never the
   skin" rule and the commit/authorship conventions.
4. Use the `related:` frontmatter (and inline links) to walk to whatever
   component you're about to touch — e.g. the
   [`/components/`](components/index.md) objects for the Host interface, the
   Maki bridge, the widget tree, the painters.
5. Use [`/workflow/build-and-test.md`](workflow/build-and-test.md) for the
   offscreen-render verification loop and the `WASABIQT_*` env knobs.
6. When in doubt about a domain term (Wasabi, Maki, `.wal`, windowholder,
   GUID component, gammaset, MCV), check
   [`/glossary/`](glossary/index.md).

## Bundle map

```
wiki/
├── README.md                          ← you are here
├── index.md                           ← bundle landing page
├── architecture/
│   ├── index.md
│   ├── pipeline.md                    ← the parse→…→surface render pipeline
│   ├── hybrid-design.md               ← vendored Maki VM + fresh Qt engine
│   └── embedding-boundary.md          ← Host / Skin seam
├── repos/
│   ├── index.md
│   ├── qtamp.md                       ← the player + reference embedder
│   ├── qtwasabi.md                    ← the rendering engine
│   └── relationship.md                ← submodule + dependency relationship
├── components/
│   ├── index.md
│   ├── skinxml.md
│   ├── layout.md
│   ├── widget-tree.md
│   ├── skinruntime-maki-bridge.md
│   ├── host-interface.md
│   ├── painters-and-registries.md
│   ├── surface-skinview-skinquickitem.md
│   ├── wasabi-compat.md
│   └── milkdrop-visualizer.md
├── conventions/
│   ├── index.md
│   ├── cardinal-rules.md              ← engine-level-not-per-skin; VM bridged-not-edited
│   ├── commit-authorship.md           ← Florian Kleber, Commitizen, no Co-Authored-By
│   └── comments.md
├── workflow/
│   ├── index.md
│   ├── build-and-test.md              ← build + offscreen screenshot harness
│   └── env-knobs.md                   ← WASABIQT_* env vars + CLI flags
└── glossary/
    ├── index.md
    └── terms.md                       ← Wasabi, Maki, .wal, windowholder, GUID, gammaset, MCV
```

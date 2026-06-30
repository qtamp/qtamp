---
type: Component
id: components/skinxml
title: SkinXml — the skin.xml parser
description: Parses skin.xml and its includes into a Document of containers, layouts, groupdefs, script refs, bitmaps, colors, fonts, and gammasets.
resource: deps/qtWasabi/public/qtWasabi/SkinXml.h
tags: [component, parse, xml, pipeline]
related:
  - architecture/pipeline.md
  - components/layout.md
  - glossary/terms.md
---

# SkinXml

**Header:** `deps/qtWasabi/public/qtWasabi/SkinXml.h`
**Impl:** `deps/qtWasabi/src/SkinXml.cpp`
**Pipeline stage:** Parse (first stage).

`SkinXml::parse(...)` reads `skin.xml` plus its `<include>`s into a
`Document` struct holding:

- **containers** and their **layouts** (e.g. `normal`, `shade`),
- **groupdefs** (reusable group templates that `Layout` later inlines),
- `<script>` refs (the compiled `.maki` Maki bytecode the runtime will load),
- **bitmaps**, **colors**, **fonts**, and **gammasets** (resource
  declarations the registries resolve).

It is qtWasabi's *own* parser (Qt-native), not a port of the Wasabi C++ XML
loader — see [the hybrid design](../architecture/hybrid-design.md).

The `Document` it produces is the input to the next stage,
[Layout](layout.md), which expands it into a [Widget tree](widget-tree.md).

> Domain note: a Modern skin is a `.wal` archive (a renamed zip) whose entry
> point is `skin.xml`. See the [glossary](../glossary/terms.md) for `.wal`,
> groupdef, sendparams, gammaset.

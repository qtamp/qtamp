---
type: Section Index
id: architecture/index
title: Architecture
description: How a .wal skin becomes pixels, why the engine is a hybrid of a vendored VM and a fresh Qt widget engine, and where the embedding seam is.
tags: [architecture]
related:
  - architecture/pipeline.md
  - architecture/hybrid-design.md
  - architecture/embedding-boundary.md
---

# Architecture

| Object | What it covers |
|---|---|
| [pipeline](pipeline.md) | The one-direction render pipeline: parse → expand → widget-tree → Maki VM → painters → surface. |
| [hybrid-design](hybrid-design.md) | Why qtWasabi vendors the Maki VM unmodified but re-implements the widgets in Qt; the BFC foundation-vs-platform split. |
| [embedding-boundary](embedding-boundary.md) | The `Host` / `Skin` seam that makes the engine standalone. |

---
type: Architecture
id: architecture/embedding-boundary
title: The embedding boundary — Host / Skin
description: The single seam (the qtWasabi::Host interface + the qtWasabi::Skin entry point) through which the engine reaches the outside world, making it standalone and embeddable.
tags: [architecture, host, skin, embedding, seam]
related:
  - components/host-interface.md
  - components/surface-skinview-skinquickitem.md
  - repos/qtamp.md
  - repos/relationship.md
---

# The embedding boundary

qtWasabi is a rendering engine, not a player. Everything player-shaped —
playback, the mixer, metadata, the playlist, the library — it asks for
through **one interface, `qtWasabi::Host`**, which the embedder implements.
That single seam is what makes the engine standalone: it continues the
Wasabi 2 decoupling by *never* talking to a player directly.

Two headers are all an embedder touches:

```cpp
class MyPlayer : public qtWasabi::Host { /* ~50 virtuals: playback, mixer,
                                            metadata, playlist, library, … */ };

MyPlayer host;
qtWasabi::Skin skin(&host);
skin.load("/path/to/Bento");        // a .wal archive or an unpacked dir
window->setCentralWidget(skin.widget());
```

- **`Host.h`** (`public/qtWasabi/Host.h`) — the player bridge. ~51 virtual
  methods grouped as: audio state (position/duration/bitrate/volume/
  play-state), track metadata (`songTitle`, `playItemMetaData`), transport
  (`play`/`pause`/`next`/`seekMs`/`setVolume`), window control
  (`close`/`minimize`/`toggleShade`), sliders (`sliderPosition` /
  `setSliderPosition`, with and without a param), visualization
  (`spectrumData`, `oscData`, `vuLeft`/`vuRight`, `peaksVisible`), album art,
  the playlist row model (`playlistRowCount`, `playlistRowText`,
  `playlistPlayRow`, `pleditCommand`), and the media-library row model
  (`libraryRowCount`, `libraryRowLabel`, `libraryRowHasChildren`). Most
  methods have safe defaults, so a minimal player overrides only a handful.
  See [the Host interface component](../components/host-interface.md).
- **`Skin.h`** (`public/qtWasabi/Skin.h`) — load a skin, get a `QWidget`.
  That's the whole entry point.

Because the engine only ever reaches the outside world through `Host`, **any
Qt player** — Audacious, WACUP, something new — gets classic `.wal` skin
support by implementing that interface. The reference embedder is
[qtamp](../repos/qtamp.md), whose implementation is the `QtampHost` class
(`src/main.cpp`), bridging `QMediaPlayer` state to the abstract interface.
See [the relationship object](../repos/relationship.md).

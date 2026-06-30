---
type: Component
id: components/host-interface
title: qtWasabi::Host — the embedder interface
description: The ~51-virtual abstract interface through which the engine asks the embedder for everything player-shaped; the single seam that makes the engine standalone.
resource: deps/qtWasabi/public/qtWasabi/Host.h
tags: [component, host, embedding, seam, api]
related:
  - architecture/embedding-boundary.md
  - components/surface-skinview-skinquickitem.md
  - repos/relationship.md
  - repos/qtamp.md
---

# qtWasabi::Host

**Header:** `deps/qtWasabi/public/qtWasabi/Host.h`
**Reference implementation:** `QtampHost` in `/src/main.cpp` (qtamp).

`Host` is the abstract base the embedder subclasses. It declares **~51
virtual methods** — most with safe defaults, so a minimal player overrides
only a handful. The engine reaches the outside world **exclusively** through
this interface (see [the embedding boundary](../architecture/embedding-boundary.md)).

## Method groups

| Group | Methods (representative) |
|---|---|
| **Audio state** | `positionMs`, `durationMs`, `bitrate`, `sampleRate`, `channelCount`, `volume`, `isPlaying`, `isPaused` |
| **Track metadata** | `songTitle`, `songFilename`, `playItemMetaData`, `playItemDisplayTitle`, `decoderName` |
| **Transport** | `play`, `pause`, `stop`, `next`, `prev`, `seekMs`, `setVolume` |
| **File picker** | `pickFile` |
| **Window control** | `close`, `minimize`, `maximize`, `toggleShade`, `showSystemMenu` |
| **Sliders** | `sliderPosition` / `setSliderPosition` (with and without a `param`) |
| **Visualization** | `audioLevel`, `spectrumData`, `peakData`, `oscData`, `vuLeft`, `vuRight`, `peaksVisible` |
| **Album art** | `albumArt` |
| **Playlist** | `playlistRowCount`, `playlistRowText`, `playlistRowDurationMs`, `playlistCurrentRow`, `playlistSetCurrentRow`, `playlistPlayRow`, `pleditCommand` |
| **Media library** | `libraryRowCount`, `libraryRowLabel`, `libraryRowPath`, `libraryRowHasChildren` |

## How qtamp implements it

`QtampHost` (`/src/main.cpp`) bridges `QMediaPlayer` state to this interface
and owns the audio pipeline:

- `QMediaPlayer` → `QAudioBufferOutput` → `onAudioBuffer()` → 10-band EQ DSP
  (`eq10`) + balance → `AudioAnalyzer` (spectrum/osc/VU) → an always-on
  `QAudioSink`. The analyzer output feeds the visualization methods above.
- EQ enable tracks the `__action:EQ_TOGGLE` cfgattrib; when off, all bands
  are 0 dB (bit-identical passthrough).
- The playlist/library row methods read qtamp's `PlaylistWindow` /
  media-library models.

> Any Qt player can do the same — that is qtWasabi's whole pitch. See
> [the relationship object](../repos/relationship.md).

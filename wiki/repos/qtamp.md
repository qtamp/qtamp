---
type: Repository
id: repos/qtamp
title: qtamp — Qt6 music player & reference embedder
description: A Qt6-native music player that exists to prove qtWasabi works end-to-end by embedding it as the reference Host implementation.
resource: https://github.com/kleberbaum/qtamp
tags: [qtamp, player, embedder, qt6, repo-root]
related:
  - repos/qtwasabi.md
  - repos/relationship.md
  - components/host-interface.md
  - components/milkdrop-visualizer.md
  - workflow/build-and-test.md
---

# qtamp

**Repo root:** `/home/snekmin/git/qtamp` (this repository).
**License:** MIT.

qtamp is a **Qt6-native music player** and the **reference embedder** for
[qtWasabi](qtwasabi.md). Its reason to exist is to prove qtWasabi works end
to end: load a `.wal`, render it correctly, run its Maki scripts, and play
music through it — on Linux, macOS, and Windows, on x86_64 and aarch64
(notably **Asahi Linux on Apple Silicon**, a first-class target with no Wine,
no x86 emulation).

It sits "somewhere between a thin reference shell and a daily-driver
player": enough player to live in (playlist, SQLite library, gapless
playback, ReplayGain, MIDI, a Qt-native plugin host) but the engineering
budget stays on getting qtWasabi right.

## What qtamp is / is not

- **Targets _Modern_ skins** (`.wal` / `skin.xml` / Maki). Classic `.wsz`
  skins are a *future qtWasabi* milestone, not a qtamp one.
- **Not a Winamp clone in the legal sense** — no Winamp source code in
  qtamp's repo or git history. It links against qtWasabi.
- **Not a Win32 plugin host** — legacy `in_*.dll` / `out_*.dll` / `gen_*.dll`
  do not load. qtamp's plugin protocol is fresh, Qt-native, cross-platform.
- **UI is QtWidgets, not QML** for the chrome — same reasoning as qtWasabi:
  skins are pixel-exact bitmap compositions and `QPainter` over `QWidget`
  maps 1:1.

## Architecture (player side)

```
qtamp
├── UI shell (QMainWindow + QtWidgets fallback chrome)
│   └── qtWasabi::Skin (the active loaded skin)  ── implements ──▶ qtWasabi::Host
├── Library (SQLite) │ Playlist (M3U/XSPF) │ Plugin host (in_/out_/dsp_/vis_/gen_)
└── qtamp::AudioEngine (abstract) └── QtMultimediaEngine (default, FFmpeg-backed)
        ├── in_ffmpeg (mp3/flac/ogg/wav/…)
        └── in_midi (FluidSynth + SoundFont; MIDI behaves like any other PCM input)
```

- **Audio backend is abstracted.** `qtamp::AudioEngine` is a ~10-method
  interface; QtMultimedia (FFmpeg under the hood since Qt 6.5) is the
  default. Swapping in libVLC/miniaudio later is a contained change.
- **MIDI is an input plugin, not a special case.** `in_midi` renders MIDI
  through FluidSynth + a user SoundFont into PCM, so MIDI tracks behave
  exactly like MP3s (gapless, EQ, scrubbing).
- The **MilkDrop / projectM visualizer** is a qtamp-side concern — see
  [the MilkDrop visualizer component](../components/milkdrop-visualizer.md).

## Repo layout (player side)

```
src/
  app/         QApplication entry, settings, main window
  audio/       AudioEngine interface + QtMultimedia backend
  library/     SQLite-backed music library
  playlist/    playlist model + M3U/XSPF I/O
  plugin/      plugin host + Qt interfaces
  midi-input/  RtMidi wrapper, ControlSource adapter
  skin-host/   qtWasabi::Host implementation (~40 methods bridging the
               engine to qtamp's audio/playlist/library)
plugins/       in_ffmpeg, in_midi, out_qt, vis_simple (reference plugins)
cmake/         find-modules for qtWasabi, FluidSynth, RtMidi
deps/qtWasabi/ the engine, as a git submodule
```

> Note: the live source tree may use additional/renamed directories (e.g. the
> player window lives in `src/main.cpp` as `QtampPlayerWindow`). The block
> above is the README's intended layout; see
> [the components index](../components/index.md) for the verified-from-code
> seams.

## Build (player side)

qtamp depends on qtWasabi, which in turn needs a user-supplied WCL-licensed
2024 Llama Group Winamp source release on local disk to compile the vendored
Maki VM + minimal BFC subset. See
[`workflow/build-and-test.md`](../workflow/build-and-test.md).

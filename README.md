<p align="center">
  <img src="https://github.com/qtWasabi/qtWasabi/raw/main/docs/mascot.png" alt="qtamp mascot" width="360">
</p>

<h3 align="center">qtamp</h3>

<p align="center">"Qt" as in the framework. "qt" as in cute. qtamp is a Qt-native amp, and we hope it's a cute one.</p>

A Qt6-native music player and the **reference embedder** for
[qtWasabi](https://github.com/qtWasabi/qtWasabi) — the open-source
continuation of Wasabi, Winamp's Modern skin engine. qtamp exists
to prove that qtWasabi works end-to-end: load a `.wal`, render it
correctly, run its Maki scripts, play music through it, on Linux,
macOS, and Windows, on x86_64 and aarch64.

qtamp is **somewhere between** a thin reference shell and a
daily-driver player. Enough player to actually live in — playlist,
library, gapless playback, ReplayGain, MIDI, basic plugin host —
but the engineering budget stays on getting qtWasabi right. If you
want a feature-complete Winamp successor today, use
[WACUP](https://getwacup.com/) or
[Audacious](https://audacious-media-player.org/). If you want
classic Modern skins running on Apple Silicon and Asahi Linux,
that's us.

### What qtamp gives you

- **Modern Winamp skins (`.wal`) on Linux/macOS/Windows.** The
  whole point. Drop in WinampModernPP, Bento, Big Bento, your
  own — they render through qtWasabi against `QPainter`, on
  whatever pixel grid your OS hands you.
- **Apple Silicon native + Asahi Linux first-class.** No Wine, no
  x86 emulation, no nostalgia VM. aarch64 throughout the stack.
- **HiDPI and Wayland that actually work.** Because Qt does the
  heavy lifting and we don't fight it.
- **MIDI playback** via FluidSynth + user-supplied SoundFont. The
  way Winamp did it with BASSMIDI, but native on every platform
  qtamp targets. First time classic skinning + MIDI work
  together on Apple Silicon.
- **Embeddable engine.** qtamp itself is one consumer of qtWasabi.
  Anyone else can embed `WasabiQt::Skin` in their own Qt-based
  player — that's qtWasabi's whole pitch, and qtamp is the
  reference proving it works.

### What qtamp is not

- **Not a Winamp clone in the legal sense.** No Winamp source code
  in the qtamp repo. The skin engine (qtWasabi) is a separate
  project with its own licensing posture; qtamp links against it.
- **Not a replacement for full-fat audio players.** No internet
  radio directories, no podcast manager, no music store
  integrations, no fancy library views. You point it at your
  music folder and it plays your music.
- **Not Classic-skin-compatible (yet).** qtamp targets _Modern_
  skins (`.wal` / `skin.xml` / Maki). Classic `.wsz` skin support
  is a future qtWasabi milestone, not a qtamp one.
- **Not a Win32 plugin host.** Legacy `in_*.dll` / `out_*.dll` /
  `gen_*.dll` Winamp plugins do not load. qtamp's plugin
  protocol is fresh, Qt-native, cross-platform. See below.
- **Not [Qmmp](https://qmmp.ylsoftware.com/) and not
  [QAmp](https://qamp.sourceforge.net/).** Both are good projects
  with similar names; qtamp is unrelated, written from scratch,
  and exists specifically as the reference embedder for qtWasabi.

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  qtamp                                                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  UI shell (QMainWindow + QtWidgets fallback chrome)  │   │
│  │  ┌────────────────────────────────────────────────┐  │   │
│  │  │  WasabiQt::Skin   (the active loaded skin)     │  │   │
│  │  │       │                                        │  │   │
│  │  │       └── implements WasabiQt::Host  ←─────────┼──┼── │
│  │  └────────────────────────────────────────────────┘  │   │
│  │  ┌──────────────┬───────────────┬─────────────────┐  │   │
│  │  │ Library      │ Playlist      │ Plugin host     │  │   │
│  │  │ (SQLite)     │ (M3U / XSPF)  │ (in_, out_, dsp_,│  │   │
│  │  │              │               │  vis_, gen_)    │  │   │
│  │  └──────────────┴───────────────┴─────────────────┘  │   │
│  │  ┌────────────────────────────────────────────────┐  │   │
│  │  │  qtamp::AudioEngine   (abstract)               │  │   │
│  │  │  └── QtMultimediaEngine  (default)             │  │   │
│  │  │      └── Qt's FFmpeg-backed media stack        │  │   │
│  │  │                                                │  │   │
│  │  │  Inputs producing PCM into AudioEngine:        │  │   │
│  │  │  ├── in_ffmpeg   (.mp3, .flac, .ogg, .wav, ...)│  │   │
│  │  │  └── in_midi     (.mid, .rmi, .kar, .mus)      │  │   │
│  │  │       └── FluidSynth + SoundFont               │  │   │
│  │  └────────────────────────────────────────────────┘  │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

**Audio backend is abstracted.** Default is QtMultimedia (uses
FFmpeg under the hood since Qt 6.5, broad format coverage, no
extra dependency). The `qtamp::AudioEngine` interface is
ten-ish virtual methods; swapping in libVLC or a custom
miniaudio+FFmpeg backend later is a contained change.

**MIDI is an input plugin, not a special case.** `in_midi` reads
MIDI files, pumps events into FluidSynth, pulls rendered PCM
back out, and presents that PCM to `AudioEngine` like any other
input. So MIDI tracks behave exactly like MP3s in the playlist —
same gapless logic, same volume, same EQ, same scrubbing. Users
pick a default SoundFont (`.sf2` / `.sf3`) in settings — the
same model as Winamp's BASSMIDI plugin. A small bundled fallback
SoundFont ships so qtamp plays MIDI out of the box.

**UI is QtWidgets, not QML.** Same reasoning as qtWasabi: skins
are pixel-exact bitmap compositions, `QPainter` over `QWidget`
maps to that 1:1, and the chrome around the skinned area
(menus, file dialogs, settings) is mundane widget territory.

### Plugin support

qtamp speaks a Qt-native plugin protocol modelled on Winamp's
classic plugin types but defined as Qt interfaces (no Win32 SDK,
no `HWND`, no `WM_*`). Five flavours:

| Type    | Purpose                          | Qt interface          |
|---------|----------------------------------|-----------------------|
| `in_*`  | Input / decoder                  | `qtamp::InputPlugin`  |
| `out_*` | Output / sink                    | `qtamp::OutputPlugin` |
| `dsp_*` | Real-time DSP                    | `qtamp::DspPlugin`    |
| `vis_*` | Visualisation                    | `qtamp::VisPlugin`    |
| `gen_*` | General / extends UI             | `qtamp::GenPlugin`    |

Reference plugins ship in-tree:

- `in_ffmpeg` — handles MP3, FLAC, AAC, OGG, Opus, WAV, …
- `in_midi` — handles MIDI files via FluidSynth
- `out_qt` — output via `QAudioSink`
- `vis_simple` — reference vis plugin

A compatibility shim for legacy Win32 Winamp plugins is
**not** a current goal.

### MIDI input (controller support)

Separate from MIDI _playback_, qtamp also accepts MIDI _input_
from connected controllers — bind your MIDI keyboard's transport
buttons to play/pause, knobs to volume/EQ, etc. Implemented via
[RtMidi](https://github.com/thestk/rtmidi), routed through a
`qtamp::ControlSource` interface alongside global keyboard
shortcuts. ALSA Sequencer on Linux, CoreMIDI on macOS, Windows
MME on Windows.

Post-1.0 milestone — useful for anyone with a controller, but
not gating shipping.

### Build / install

qtamp depends on **qtWasabi** for Modern-skin rendering.  qtWasabi
lives at `deps/qtWasabi` as a git submodule, and qtWasabi in turn
needs the WCL-licensed 2024 Llama Group Winamp source release on
the local disk to compile (the Maki VM and a small portable BFC
subset are linked in from there at build time, never redistributed).

#### Quickest path

```sh
# Fedora / RHEL / Debian / Arch / openSUSE / macOS:
curl -fsSL https://qtamp.org | sh
```

#### From source

```sh
# 1) Clone with the qtWasabi submodule populated.
git clone --recurse-submodules https://github.com/kleberbaum/qtamp
cd qtamp

# 1a) (or, if you cloned without --recurse-submodules)
#     git submodule update --init --recursive

# 2) Fetch the WCL-licensed Llama Group Winamp source archive into
#    deps/qtWasabi/wasabi-src/.  qtWasabi handles the download
#    itself; the directory is gitignored on both ends.
(cd deps/qtWasabi && ./scripts/fetch-wasabi.sh)

# 3) Configure and build, opting qtWasabi in.
cmake -B build -G Ninja \
    -DQTAMP_USE_QTWASABI=ON \
    -DWASABI_SRC_DIR="$(pwd)/deps/qtWasabi/wasabi-src/Src"
cmake --build build

# 4) Run.  --modern-skin <path> hands the chrome over to qtWasabi;
#    without it, qtamp falls back to the classic-skin code path.
./build/qtamp --modern-skin ~/.winamp/skins/WinampModernPP
```

#### Updating qtWasabi

```sh
git submodule update --remote deps/qtWasabi
git add deps/qtWasabi
git commit -m "chore(deps): bump qtWasabi"
```

Per-distro instructions, packaging recipes (RPM + macOS .dmg +
Flatpak), and troubleshooting in [`BUILD.md`](BUILD.md).

### Repo layout

```
src/
  app/                — QApplication entry, settings, main window
  audio/              — AudioEngine interface + QtMultimedia backend
  library/            — SQLite-backed music library
  playlist/           — playlist model + M3U/XSPF I/O
  plugin/             — plugin host + Qt interfaces
  midi-input/         — RtMidi wrapper, ControlSource adapter
  skin-host/          — WasabiQt::Host implementation (~40 methods
                        bridging the skin engine to qtamp's audio,
                        playlist, library)
plugins/
  in_ffmpeg/          — reference input plugin (FFmpeg)
  in_midi/            — reference MIDI input (FluidSynth + SoundFont)
  out_qt/             — reference output plugin (QAudioSink)
  vis_simple/         — reference vis plugin
soundfonts/
  README.md           — fallback SoundFont info + recommendations
cmake/                — find-modules for qtWasabi, FluidSynth, RtMidi
tests/                — unit + UI tests
packaging/            — RPM spec, .dmg builder, Flatpak manifest
docs/                 — design notes, mascot, screenshots
```

### Targets

- **Linux** — Asahi (aarch64), Fedora, Arch, Debian, openSUSE.
  Wayland-first, X11 supported via Qt.
- **macOS** — Apple Silicon native (M-series), Intel as a courtesy.
  Ships as a signed `.app` in a `.dmg`.
- **Windows** — x86_64 and arm64. Ships as a portable zip and an
  MSI installer.

### Status

Bootstrapping. qtamp's milestones are gated on qtWasabi's
milestones — we can't render skins until qtWasabi can render
skins. Tracking, in dependency order:

1. **Skeleton player** — QApplication, main window, file open,
   QtMultimedia plays an MP3. No skin yet.
2. **Library + playlist** — SQLite library scan, M3U import/export,
   playlist model wired to AudioEngine.
3. **MIDI input plugin** — `in_midi` + FluidSynth, bundled
   fallback SoundFont, `.mid` files play in the playlist
   indistinguishably from MP3s.
4. **WasabiQt::Host implementation** — implement the ~40-method
   host interface so qtWasabi can ask qtamp for the current
   track, play state, volume, etc.
5. **First skinned window** — qtWasabi milestone 6 lands
   (WACUP titlebar pixel-regression passes). qtamp loads
   WinampModernPP and shows the player frame.
6. **Plugin host** — five plugin interfaces stable, reference
   plugins ship in-tree, plugins can be loaded from
   `~/.config/qtamp/plugins/`.
7. **1.0** — daily-drivable. Gapless playback, ReplayGain, global
   shortcuts, MPRIS on Linux, Now Playing on macOS, SMTC on
   Windows.
8. **Post-1.0** — MIDI controller input (RtMidi), more vis plugins,
   Classic skin support (waiting on qtWasabi).

### License

qtamp: **MIT**, see [`COPYING`](COPYING).

qtamp links against qtWasabi (MIT) which loads user-supplied
Wasabi source under the Winamp Collaborative License v1.0 at
build time. FluidSynth is LGPL-2.1+ (linked dynamically).
RtMidi is MIT. Same posture as qtWasabi — no Winamp-licensed
code in qtamp's repo or git history.

### Mascot

qtamp shares qtWasabi's mascot — a friendly face cheering both
projects along. Not a logo, not a trademark.

### Inspirations

- **Winamp** — the original. qtamp is not a clone; it is a love
  letter to a UI idea Nullsoft got right in 2002 and that hasn't
  been improved on since.
- **WACUP** — for showing that the Modern skin format still has
  legs in 2026, and for being the canonical reference renderer
  qtWasabi's pixel tests measure against.
- **Audacious** — for proving a Qt-based, cross-platform Winamp
  successor is possible. Audacious goes wider on features;
  qtamp goes deeper on skin fidelity.
- **Qmmp** — for being there first as a Qt-based Winamp-style
  player, and for showing the niche has staying power.
- **BASSMIDI** — for showing how good MIDI playback in a media
  player can sound when you give it a real synth and a real
  SoundFont. qtamp does the same thing with FluidSynth.
- **cmus, mpd, deadbeef** — for the Linux audio-player tradition
  of doing one thing well.

### Related

- [qtWasabi](https://github.com/qtWasabi/qtWasabi) — the skin engine
  this player exists to demonstrate.
- [winamp-linux](https://github.com/lord3nd3r/winamp-linux) — earlier
  prototype, superseded by qtWasabi + qtamp.
- [FluidSynth](https://www.fluidsynth.org/) — software synthesizer
  powering MIDI playback.

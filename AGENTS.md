# AGENTS.md — working in qtamp

This file orients AI coding agents (and humans who think like them) working in
this repository. Read it before you touch anything. For machine-readable
project knowledge, see the [`wiki/`](wiki/) folder (Google **Open Knowledge
Format** — start at [`wiki/README.md`](wiki/README.md)).

## What this is

**qtamp** is a Qt6-native music player and the **reference embedder** for
**qtWasabi**, an open-source rendering engine that runs Winamp *Modern* skins
(`.wal` / `skin.xml` / Maki) by executing each skin's own vendored Maki VM
bytecode and painting through `QPainter`. The engine is decoupled from the
player by a single interface — `qtWasabi::Host` — which the embedder
implements. Target platforms: Linux/macOS/Windows on x86_64 **and** aarch64
(Apple Silicon / Asahi Linux is first-class).

This repo is two git repos:

| Path | Repo | What |
|---|---|---|
| `/` | **qtamp** | The player/embedder: `src/main.cpp` (`QtampPlayerWindow` + `QtampHost`), `MilkdropItem`, dialogs, playlist, audio metadata. |
| `deps/qtWasabi/` | **qtWasabi** (submodule) | The skin engine. Most engine work happens here. |
| `deps/projectm/` | projectM (submodule) | Vendored MilkDrop visualizer, with a local FBO-restore patch. |

## The one rule: fix the engine, never the skin

There is **no per-skin code**. No `if (id == "someWidget")`, no "Bento needs
this", no special-casing a layout or a skin name. Thousands of shipped skins —
including ones that don't exist yet — are the only real spec, so when a skin
renders or behaves wrong the bug is in the **engine** (a Maki binding returning
the wrong value/type, a widget mis-resolving its rect, an event that doesn't
fire) and the fix belongs there, where every skin benefits. If you reach for a
skin name or a widget id, stop — the real fix is one level down.

The Maki bytecode VM (`deps/qtWasabi/wasabi-port/`) is **vendored unmodified**.
Don't patch it — bridge it. Behaviour gaps get fixed in `maki-bindings.cpp` /
the widget engine, not in the VM.

Every fix must address the **underlying problem, not the symptom**. Verify
against the reference Winamp source at `~/git/winamp-linux/Src` when the
intended Wasabi/Maki behaviour is unclear.

## Build & verify

```bash
# qtamp with the MilkDrop visualizer (Asahi/aarch64 example)
cmake -S . -B build-milkdrop -DQTAMP_MILKDROP=ON
cmake --build build-milkdrop -j
./build-milkdrop/qtamp --modern-skin "$HOME/.winamp/skins/Winamp Modern" ~/Music/<album>
```

The **fast verification loop is offscreen** — render a skin (or a detached
container) to a PNG and inspect/diff it, no compositor needed:

```bash
QT_QPA_PLATFORM=offscreen ./build-milkdrop/qtamp --modern-skin <skin> [media] \
    --screenshot /tmp/out.png
# A specific container window (e.g. the detached Video):
... --screenshot-container "guid:{F0816D7B-FFFC-4343-80F2-E8199AA15CC3}" --screenshot /tmp/v.png
```

Key `WASABIQT_*` test knobs (see [`wiki/workflow/env-knobs.md`](wiki/workflow/env-knobs.md)
for the full list):

- `WASABIQT_FIRE_CLICK=<id>` — dispatch a widget's `onLeftClick` by id (bypasses hit-test).
- `WASABIQT_CLICK_AT="x,y[;x,y]"` — synth a **real** click through the live hit-test path (coords are **window** coords; widget `x`/`y` attrs are group-relative!).
- `WASABIQT_TRACE_MAKI=1` — trace Maki dispatch. **Never leave it on for a live run** (writes ~GBs/day).
- `WASABIQT_FORCE_ACTIVE_TAB`, `WASABIQT_FORCE_RESIZE`, `WASABIQT_FIRE_HOVER` — tab/resize/hover harness hooks.

**Verify, don't assume.** Offscreen can't reproduce Wayland-only behaviour
(e.g. `startSystemMove` always succeeds live but fails offscreen). When a fix
targets live behaviour, confirm it live — don't trust a green offscreen run.

After a feature lands, **remove dead/diagnostic code** that didn't pan out. No
leftover trace `fprintf`s, no commented-out experiments.

## Comment style

Match the surrounding code's density and idiom. Comments explain *why*, not
*what*. Do **not** cite Winamp-source paths or dev-phase/task scaffolding in
code comments.

## Commits

- **Conventional Commits / Commitizen**, multiline: a `type(scope): subject`
  header, a blank line, then a body explaining the why and the non-obvious how.
  Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `chore`.
  Mark breaking changes with `!` and a `BREAKING CHANGE:` footer.
- **Author every commit as `Florian Kleber <kleber@snek.at>`.** Do **not** add
  a `Co-Authored-By: Claude` trailer or any AI attribution.
- **Submodule workflow:** make engine changes in `deps/qtWasabi`, commit them
  there first (on its dev branch), then bump the submodule pointer from the
  qtamp repo in a `build(deps): bump qtWasabi …` commit. Same for `deps/projectm`.
- Commit or push only when the user asks. Don't commit `build*/` (gitignored)
  or `.claude/` (gitignored).

## Learn more

- [`deps/qtWasabi/ARCHITECTURE.md`](deps/qtWasabi/ARCHITECTURE.md) — the
  parse → expand → widget-tree → Maki VM → painters → surface pipeline.
- [`deps/qtWasabi/CONTRIBUTING.md`](deps/qtWasabi/CONTRIBUTING.md) — engine conventions.
- [`wiki/`](wiki/) — the OKF knowledge base: components, seams, glossary,
  conventions, and the env-knob/test inventory, structured for agent consumption.

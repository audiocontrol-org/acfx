# acfx

A cross-platform audio DSP platform: one platform-independent core, many thin
target adapters. Effects are written once and run as a desktop standalone
workbench, a DAW plugin (VST3 / AU / CLAP), and microcontroller firmware
(Daisy, Teensy) — from the *same* source, with one parameter declaration driving
every adapter.

This repository's first vertical slice proves that spine end-to-end with a single
effect: a State-Variable Filter (SVF). See
[`specs/svf-vertical-slice/`](specs/svf-vertical-slice/) for the spec, plan, and
the runnable validation guide ([`quickstart.md`](specs/svf-vertical-slice/quickstart.md)).

## Layout

```
core/        platform-independent spine — NO JUCE/libDaisy/Teensy headers
  dsp/         Effect concept, ProcessContext, AudioBlock, parameter model
  primitives/  thin wrappers over DaisySP (the SVF wrapper)
  effects/svf/ the SVF effect + its constexpr parameter table
host/          desktop-only ProcessorNode boundary (<= 1 virtual call / block)
adapters/      workbench (JUCE app), plugin (JUCE VST3/AU/CLAP), daisy, teensy
tests/         host-side doctest suite (core correctness, no hardware)
cmake/         CPM + pinned dependencies + ARM toolchain files
```

Dependencies point only inward (`adapters/* -> core/*`; `core/*` depends on
nothing platform-specific). The same `core/effects/svf` source compiles into every
target with no per-target `#ifdef` forks.

## Build & run

Builds use CMake presets. Each preset fetches only the dependencies it needs
(CPM-pinned in [`cmake/dependencies.cmake`](cmake/dependencies.cmake)).

### Everyday tasks & releases (`just`)

The common workflow is driven by [`just`](https://just.systems) recipes in the
[`justfile`](justfile) — a command runner **on top of** CMake (CMake stays the
C++ build system; `just` orchestrates). Install once: `brew install just`. List
everything with `just --list`.

```bash
just fw spike_breathing_canyon                       # build a nucleo firmware image
just flash spike_breathing_canyon                    # build + flash + report SRAM usage
just plugin acfx_plugin_breathing_canyon             # build VST3 + AU + Standalone
just reinstall acfx_plugin_breathing_canyon "acfx Breathing Canyon"  # dev-sign + install AU/VST3 locally
```

Releases are Developer-ID **signed + Apple notarized + stapled**, universal, and
**immutable** (a new build is always a new tag). Run these from a **real
terminal** — notarization needs the login-session keychain, so they will not work
from a sandboxed/headless context:

```bash
# one plugin -> one release
just release acfx_plugin_breathing_canyon "acfx Breathing Canyon" breathing-canyon-2026-08-27 --clean
# both experimental plugins (AU+VST3+Standalone each) -> one release
just release-experimental acfx-plugins-2026-08-27 notes.md
```

The release recipes delegate to [`scripts/release-plugin.sh`](scripts/release-plugin.sh)
(single plugin) and [`scripts/release-plugins.sh`](scripts/release-plugins.sh)
(bundle), which encode the codesign/notarytool detail; see the
[**Signing & notarization runbook**](adapters/plugin/SIGNING-AND-NOTARIZATION.md).
Machine specifics (toolchain path, signing identity, archs, min-OS) are `justfile`
variables you can override on the command line, e.g.
`just arm_toolchain=/path/to/bin fw <effect>`.

The CMake presets below are the lower-level commands the recipes wrap — use them
directly when you need something the recipes don't cover.

### Host tests (no hardware) — quickstart Scenario A

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Runs parameter scaling/skew checks, the per-mode SVF frequency-response check, the
high-resonance stability guard, and the no-heap-allocation-in-`process()`
invariant.

### Desktop workbench (sketch-and-hear) — Scenario B

```bash
cmake --preset desktop
cmake --build --preset desktop --target acfx_workbench
```

Launch the built app: auto-generated controls for cutoff / resonance / mode, a
dry/processed A/B toggle, and in-UI audio configuration:

- **Audio Settings…** opens a window (JUCE's device selector) to choose the audio
  **input/output device**, sample rate, buffer size, and which **MIDI inputs** drive
  the parameter CCs.
- The **source bar** switches between **Live** input and a **file** you pick with
  **Load file…** (looped through the filter) — no environment variable required.
- All of these selections — device, rate/buffer, source (including the chosen file),
  and MIDI inputs — are **remembered across launches**. Device/source changes apply
  with the audio engine stopped, so switching never glitches or stalls the stream.
- Failures (a device that won't open, an unreadable/missing file, unreadable saved
  settings) are surfaced and leave the workbench in a safe, usable state — never
  silent silence or placeholder audio.

### Desktop plugin (VST3 / AU / CLAP) — Scenario C

```bash
cmake --preset desktop
# Build the format wrappers (the acfx_plugin aggregate is shared code only):
cmake --build --preset desktop --target acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP
```

To ship a build others can install (Developer ID signed + Apple notarized, universal,
loads with no Gatekeeper prompts or OS-version errors), follow the runbook:
[**Signing & notarization**](adapters/plugin/SIGNING-AND-NOTARIZATION.md).

The workbench's built-in player is reached from the UI (the **Load file…** button);
`ACFX_WORKBENCH_FILE=/path/to/audio.wav` remains only as a **first-run convenience**
that seeds the source when nothing has been saved yet — a saved selection always takes
precedence.

### Hardware cross-compile — Scenario D

Requires an ARM embedded toolchain **with the C++ standard library** (the stock
`arm-none-eabi-gcc` may be C-only and cannot build the C++ core):

```bash
cmake --preset daisy  && cmake --build --preset daisy
cmake --preset teensy && cmake --build --preset teensy
```

Requires an ARM embedded toolchain **with the C++ standard library** (ARM's
gcc-arm-embedded or the vendor toolchain). Flashing and listening on a physical
board is a separate checkpoint when hardware is in hand.

## Quality gates

Quality gates are **explicit, visible steps — never git hooks** (this repo uses
zero hooks). Run them on purpose:

```bash
./scripts/check-portability.sh    # file-size budget, core platform-independence,
                                  # no-JUCE-in-MCU, one-source-many-targets
```

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs the host tests,
the portability gates, and the desktop build on every change.

## Standards

See [`CLAUDE.md`](CLAUDE.md) and the full project constitution at
[`.specify/memory/constitution.md`](.specify/memory/constitution.md).

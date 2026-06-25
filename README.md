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
built-in player or live input, a bound MIDI CC, and a dry/processed A/B toggle.

### Desktop plugin (VST3 / AU / CLAP) — Scenario C

```bash
cmake --preset desktop
cmake --build --preset desktop --target acfx_plugin
```

### Hardware cross-compile — Scenario D

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

---
title: acfx — Cross-Platform Audio Effects Platform
status: approved-design
date: 2026-06-25
---

# acfx — Cross-Platform Audio Effects Platform

## Purpose

`acfx` is a long-lived platform for building audio DSP that the author uses
personally and will eventually release as software packages and as custom-built
hardware. The first body of work is **audio effects**; the architecture must also
accommodate **synthesizers, drum machines, and samplers** without rework.

Each piece of DSP must run, from a single shared core, on four first-class
targets:

- **DAW plugin** (VST3 / AU / CLAP)
- **Desktop standalone** (the development workbench)
- **Daisy** (libDaisy / STM32)
- **Teensy** (Teensy Audio Library)

A secondary but central goal is **fast experimentation**: a tight
edit → compile → hear loop on the desktop. The chosen loop is **rebuild &
relaunch** (a few seconds, audio restarts) — explicitly *not* hot-reload, which
is deferred and only kept architecturally possible.

This document takes inspiration from `oletizi/ol_dsp` but modernizes the tooling
and corrects the design choices (notably virtual-call overhead in the audio path)
identified while reviewing it.

## Goals

1. One platform-independent DSP core, four thin target adapters.
2. A single parameter declaration per effect that drives auto-GUI, MIDI control,
   plugin automation, and hardware knobs/ADC.
3. Zero heap allocation and zero virtual-call overhead in the audio hot path.
4. A fast desktop sketch-and-hear loop with auto-generated controls and MIDI.
5. Reproducible, preset-driven cross-compilation to all four targets.

## Non-Goals (this milestone)

- Hot-reload of DSP without stopping audio (deferred; kept possible).
- Synths / drum machines / samplers (the core must not preclude them, but they
  are separate later cycles).
- A large library of effects (the first milestone proves the spine with one).
- A hand-designed/branded plugin GUI (auto-generated controls only for now).

## Architecture (Approach A — "Modern hybrid")

Layered monorepo; dependencies point **only inward**. Targets depend on the core;
the core depends on nothing platform-specific.

```
acfx/
├── CMakeLists.txt              # top-level orchestration
├── CMakePresets.json           # named configs: desktop, daisy, teensy, test
├── cmake/                      # toolchain files, helper functions, CPM.cmake
├── core/                       # platform-independent heart (no platform headers)
│   ├── dsp/                    #   Effect concept, ProcessContext, AudioBlock, params
│   ├── primitives/             #   thin wrappers over DaisySP (filter, osc, env, delay)
│   └── effects/                #   the actual effects (one folder each)
├── adapters/
│   ├── workbench/              #   desktop sketch-and-hear app (JUCE standalone)
│   ├── plugin/                 #   JUCE plugin → VST3 / AU / CLAP
│   ├── daisy/                  #   libDaisy integration
│   └── teensy/                 #   Teensy Audio Library integration
├── tests/                      # host-side unit tests (core only)
├── external/                   # pinned deps fetched by CPM (DaisySP, JUCE, doctest)
└── docs/superpowers/specs/     # design docs
```

**The load-bearing discipline:** `core/` compiles with no knowledge of JUCE,
libDaisy, or Teensy. Every target is a thin shell that includes `core/` and feeds
it audio and parameters. This is what makes "write once, run on four targets"
actually hold — and it avoids the desktop-side hardware stubs (`daisy_dummy.h`)
that `ol_dsp` relied on. MCU-specific code lives only in the MCU adapters and is
never compiled on the desktop, so there are no stub/fallback shims.

### The two core abstractions (the spine)

**1. The `Effect` concept (compile-time contract, zero overhead).** An effect is
any type satisfying a C++20 concept — no base class, no vtable in the audio path:

```cpp
template <typename T>
concept Effect = requires(T fx, ProcessContext ctx, AudioBlock io) {
    { fx.prepare(ctx) };                     // sample rate, max block size, channels
    { fx.process(io) };                      // in-place block processing — NO heap alloc
    { fx.reset() };                          // clear internal state
    { T::parameters() };                     // constexpr parameter descriptors
    { fx.setParameter(ParamId{0}, 0.0f) };   // normalized 0..1 value in
};
```

Effects are plain structs/classes satisfying this contract. Channel count is
handled via templates / a fixed-size span abstraction (`AudioBlock`), never via
runtime allocation.

**2. The Parameter model (one declaration, every consumer).** Each effect
declares its parameters once as `constexpr` data — id, name, unit, min/max,
default, and a scaling/skew curve (e.g. logarithmic for frequency). That single
declaration is consumed by:

- the **plugin adapter** → registers host automation parameters + builds the GUI
- the **workbench** → draws sliders and binds MIDI CCs
- the **hardware adapter** → maps ADC pins / encoders to parameters

**The boundary adapter.** At the host edge, a small `ProcessorNode` *virtual*
interface wraps any `Effect` so JUCE / standalone can hold it polymorphically
(`std::unique_ptr<ProcessorNode>`). The cost is exactly **one virtual call per
audio block** (not per sample) — negligible — while the entire DSP hot path
inside stays templated and inlined. Zero overhead where it matters (MCU / inner
loop), polymorphism only where it is free (once per block, desktop-side).

**Extensibility to generators.** A synth/drum/sampler is a type satisfying a
sibling `Generator` concept (produces audio from MIDI rather than transforming
audio in), reusing the same Parameter model and `ProcessorNode` boundary. The
effects-first design does not preclude it.

## Target Adapters

Each adapter is a thin shell; the same `core/effects/<Effect>` compiles into all
four.

- **Workbench (desktop standalone)** — a JUCE standalone app: pulls audio from the
  input device or a built-in loop/file player, holds the effect via its
  `ProcessorNode`, auto-renders the parameter GUI, and binds MIDI CCs. This is the
  sketch-and-hear surface; rebuild-&-relaunch returns here.
- **Plugin** — a JUCE `AudioProcessor` wrapping the *same* `ProcessorNode`. Host
  automation parameters are generated from the parameter descriptors. Exports
  **VST3 + AU + CLAP** (CLAP via the `clap-juce-extensions` add-on, since JUCE 8
  does not ship CLAP natively).
- **Daisy** — libDaisy audio callback calls `effect.process(block)`; the hardware
  adapter maps ADC pins / encoders to the parameter model. Built with the
  `arm-none-eabi-gcc` toolchain.
- **Teensy** — a Teensy Audio Library `AudioStream` node wrapping the effect;
  parameters driven by MIDI or analog inputs. Built with the Teensy ARM toolchain.

The plugin and workbench share nearly all their code (both JUCE, both
`ProcessorNode`). Daisy and Teensy share the hardware-control mapping concept but
differ in audio-callback glue.

## Build System & Cross-Compilation

- **CMake + `CMakePresets.json`** with named presets: `desktop`, `daisy`,
  `teensy`, `test`. One command per target
  (`cmake --preset daisy && cmake --build --preset daisy`).
- **CPM.cmake** for pinned, reproducible dependencies (JUCE, DaisySP, the test
  framework) — exact versions locked, no system-installed surprises.
- **Per-target toolchain files** in `cmake/` for the two ARM cross-compiles. MCU
  targets build `core/` + their own adapter only; JUCE is never pulled into a
  Daisy/Teensy build.
- **C++ standard:** `core/` is written to compile under **both C++17 and C++20**.
  The `Effect` / `Generator` concepts are C++20 contract-checks enabled wherever
  the toolchain supports them (desktop, tests, and Daisy's arm-gcc). If Teensy's
  compiler is limited to C++17, the same effect types still compile there as
  plain duck-typed templates — the named contract-check is lost on that one
  target, never the code. Teensy's exact support is verified during planning.

## Testing

- **`doctest`** (lightweight, fast-compiling, header-only) for host-side unit
  tests.
- The **core is fully testable host-side** with no hardware: parameter
  scaling/skew curves, effect correctness (impulse and frequency-response checks
  against known-good values), and stability guards (no NaN / denormal blowups).
- A **"no heap allocation in `process()`"** test — the invariant that makes the
  core safe on MCU and on the real-time audio thread.
- DSP regression via small golden-output comparisons where it makes sense.

## First Vertical Slice (the proving milestone)

The first milestone drives **one real effect end-to-end** so the whole spine
(core → parameter model → four adapters → build presets) is proven and every
effect after it is "just more of the same."

**The effect: a State-Variable Filter (SVF).** It exercises every part of the
parameter spine with low DSP risk:

- **Cutoff** — a logarithmic/skewed frequency parameter (proves scaling curves)
- **Resonance** — a linear parameter
- **Mode** — a discrete enum: low / high / band pass (proves discrete parameters)
- It has internal **state** (proves `prepare`/`reset`), is cheap on MCU, and is
  genuinely useful.

**Milestone deliverables:**

1. The `core/` abstractions: `Effect` concept, `ProcessContext`, `AudioBlock`,
   the Parameter model, the `ProcessorNode` boundary adapter.
2. The SVF effect in `core/effects/`, host-side unit-tested (frequency response +
   the no-allocation check).
3. **Desktop, fully working:** the workbench standalone *and* the plugin —
   auto-GUI, MIDI knobs, real sketch-and-hear and A-B.
4. **Hardware cross-compile proven:** the same SVF *builds and links* for Daisy
   and Teensy via their presets, with audio-callback + parameter-mapping glue in
   place.
5. CMake presets for all four + `test`, CPM dependency pinning, and CI for the
   desktop/test builds.

**Hardware verification boundary:** milestone 1 proves the two ARM targets
compile and link (the cross-platform claim is real). Flashing and listening on a
physical Daisy/Teensy is a checkpoint run when the board is in hand — possibly in
this milestone, possibly an immediate follow.

## Key Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Effect contract | C++20 concepts + templated effects; one virtual call per block at the host boundary | Zero overhead on MCU / hot path; polymorphism only where free |
| Desktop + plugin framework | JUCE 8 | Fastest path to the workbench: standalone + VST3/AU/CLAP + auto-GUI + MIDI |
| CLAP | `clap-juce-extensions` add-on | Keep JUCE, still ship CLAP |
| DSP primitives | Reuse DaisySP (wrapped) | MCU-proven; don't reinvent the math |
| Edit-to-hear loop | Rebuild & relaunch | Simple, reliable everywhere; hot-reload deferred |
| Build | CMake presets + CPM pinning + ARM toolchain files | Reproducible, one-command per target |
| Tests | doctest, host-side | Lightweight, fast, hardware-free |
| First effect | State-Variable Filter | Exercises continuous + skewed + discrete params + state |

## Constraints & Conventions

- No heap allocation in any `process()` / audio-callback path.
- No fallback or stub implementations outside test code; the layered design
  removes the need for desktop-side hardware stubs entirely.
- Source files kept within ~300–500 lines; split when they grow past that.
- Never bypass pre-commit / pre-push hooks.

## Open Items (resolved during planning)

- Exact Teensy compiler C++-standard support (C++17 vs C++20 for that target).
- Pinned versions of JUCE, DaisySP, libDaisy, Teensy core, and doctest.
- Whether the SVF wraps DaisySP's `Svf` or is a clean in-house implementation
  (the in-house Cytomic/Simper SVF is a candidate for full ownership).
- The audio source for the workbench: live input vs built-in loop player vs both.

## Future Cycles (out of scope here, captured for the roadmap)

- Additional effects (delay, reverb, saturation, …), each a new `core/effects/`
  type reusing the spine.
- Generators: synth voice, drum machine, sampler via the `Generator` concept.
- On-hardware audio verification harness for Daisy and Teensy.
- Optional hot-reload of the DSP core on desktop.
- Branded/custom plugin GUI beyond auto-generated controls.

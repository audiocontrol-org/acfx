> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Modulated Delay — feedback-filtered delay with movement, warble, and tape wow & flutter

**Branch**: `main` | **Date**: 2026-06-28 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/modulated-delay/spec.md`

## Summary

Build the second real effect on the acfx cross-platform spine: a delay whose
feedback path is shaped by a state-variable filter, with independent low-frequency
modulation of delay time and filter cutoff/resonance, plus an input-stage
wow & flutter that runs on its own delay line. The technical approach reuses the
proven spine from the SVF vertical slice unchanged — a header-centric C++ core
satisfying the compile-time `Effect` concept (zero virtual calls, zero heap
allocation in `process()`), one `constexpr` `ParameterDescriptor` table as the
single source of parameter truth, and lock-free cross-thread parameter publication
consumed at the top of `process()`. Two new platform-independent DSP building
blocks are introduced in `core/primitives/`: an allocation-free **interpolated
(fractional) delay line** and a small **LFO** with selectable shape. The
feedback filter reuses the existing `SvfPrimitive` (DaisySP `Svf`) verbatim. The
core is tested host-side with doctest: delay timing and in-range reads, feedback
shaping, modulation movement, wow/flutter behavior, and the no-heap-allocation
invariant. No adapter, build-system, or dependency changes are required — the new
effect compiles into the existing workbench/plugin/Daisy/Teensy targets exactly as
the SVF does.

## Technical Context

**Language/Version**: C++ — core compiles under **both C++17 and C++20**; the
`Effect` contract is a C++20 concept where supported (desktop, tests, Daisy/arm-gcc)
and degrades to plain duck-typed templates on C++17 toolchains (Teensy). Identical
effect source either way (matches the SVF slice).

**Primary Dependencies**: no new third-party dependencies. Reuses DaisySP
(already CPM-pinned) via the existing `SvfPrimitive` wrapper; reuses the existing
`core/dsp` contracts (`Effect`, `AudioBlock`, `ProcessContext`, `ParameterDescriptor`).
The interpolated delay line and the LFO are new **first-party** primitives written
against the standard library only (no platform headers). doctest for host-side tests.

**Storage**: N/A (no persistence; presets/state are out of scope, as in the SVF slice).

**Testing**: doctest, host-side, hardware-free — fractional-delay read accuracy and
in-range bounds, feedback-filter progressive shaping, per-destination LFO movement
(rate/depth/shape), wow & flutter pitch-instability behavior, sample-rate
independence, and a no-heap-allocation-in-`process()` invariant (reuse the existing
allocation sentinel).

**Target Platform**: the same four targets from one core — desktop standalone
(workbench), desktop plugin (VST3 / AU / CLAP), Daisy (libDaisy / STM32), Teensy
(Teensy Audio Library). This feature adds **no** adapter code; the new effect is
selected/loaded by the same mechanism as the SVF.

**Project Type**: cross-platform C++ audio-DSP monorepo — platform-independent core
library + existing thin target adapters (template web/mobile options removed below).

**Performance Goals**: real-time audio safe on MCU and desktop — zero heap allocation
and zero virtual calls in `process()`; bounded per-sample work (fractional read +
one SVF sample + per-destination LFO tick). Memory: the main delay buffer is sized
for **2.0 s** per channel at the prepared sample rate (~384 KB/ch at 48 kHz,
~768 KB/ch at 96 kHz, float); the wow & flutter delay line is small (tens of ms).

**Constraints**: no heap allocation / locks / unbounded work in any
`process()`/audio-callback path (Constitution VI); all buffers preallocated in
`prepare()`; no fallbacks/mock/stubs outside tests, no desktop-side hardware stubs
(Constitution IV/V); strict typing, no unchecked casts; source files ~300–500 lines
(Constitution VII) — the delay line, LFO, wow & flutter stage, and effect wiring are
separate small units; MCU builds must not pull in JUCE.

**Scale/Scope**: one new effect with one constexpr parameter table; two new
first-party primitives (interpolated delay line, LFO); reuse of `SvfPrimitive`. No
new adapters, presets, or dependencies. The 2 s buffer and per-channel state must
remain within the embedded targets' RAM budget (verified during implementation on
the MCU toolchains).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I. Commit & push early/often | Plan + each primitive/effect committed in small increments, pushed promptly; WIP commits expected | ✅ planned |
| II. No git hooks | No hooks introduced; CMake/doctest are explicit, visible gates | ✅ by design |
| III. Descriptive names | Spec dir `modulated-delay`, effect dir `core/effects/modulated-delay`, primitives `delay-line`/`lfo`; no numeric prefixes | ✅ |
| IV. Platform-independent core, thin adapters | New primitives + effect include no JUCE/libDaisy/Teensy headers; deps point inward; DaisySP reuse stays in `primitives/`; no desktop-side hardware stubs | ✅ central constraint |
| V. No fallbacks/mock outside tests | Out-of-range delay request is bounded (defined behavior), not a silent fallback; unimplemented paths raise descriptive errors; no mock data in core | ✅ |
| VI. Real-time safety | `process()` is non-allocating, lock-free, branch-bounded; all delay buffers preallocated in `prepare()`; cross-thread edits via lock-free pending atomics consumed at block top | ✅ enforced by no-alloc test |
| VII. Strict typing & small modules | No unchecked casts; effect split into delay-line / lfo / wow-flutter / effect units, each ≤~500 lines | ✅ |
| VIII. Test the core host-side | doctest suite: fractional-read accuracy + bounds, feedback shaping, modulation movement, wow/flutter, sample-rate independence, no-alloc invariant | ✅ Phase-1 contracts + tests |

**Result**: PASS — no violations to justify. Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/modulated-delay/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output — design decisions resolved
├── data-model.md        # Phase 1 output — core entities/state as data
├── quickstart.md        # Phase 1 output — runnable validation per user story
├── contracts/           # Phase 1 output — the new core interface contracts
│   ├── delay-line.md
│   ├── lfo.md
│   └── modulated-delay-effect.md
├── checklists/
│   └── requirements.md  # spec-quality checklist (passing)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

Dependencies point only inward: `adapters/* → core/*`; `core/*` depends on nothing
platform-specific (DaisySP is pure-DSP math, so `core/primitives` may wrap it).
This feature adds files under `core/` and `tests/` only — **no adapter changes**.

```text
core/                                # platform-independent — NO JUCE/libDaisy/Teensy
├── dsp/                             #   (unchanged) Effect concept, AudioBlock, ParamId, parameter
├── primitives/
│   ├── svf-primitive.h              #   (existing) reused verbatim for the feedback filter
│   ├── delay-line.h                 #   NEW — allocation-free interpolated (fractional) delay line
│   └── lfo.h                        #   NEW — selectable-shape low-frequency oscillator
└── effects/
    └── modulated-delay/             #   NEW — the effect + its constexpr parameter table
        ├── modulated-delay-effect.h #     top-level effect (Effect contract, param table, wiring)
        └── wow-flutter.h            #     input-stage wow & flutter (own delay line + 2 LFOs)

tests/
└── core/
    ├── delay-line-test.cpp          #   NEW — fractional read accuracy, bounds, no wrap-out-of-range
    ├── lfo-test.cpp                 #   NEW — rate/shape/depth, sample-rate independence
    └── modulated-delay-test.cpp     #   NEW — feedback shaping, modulation movement, wow/flutter, no-alloc
```

**Structure Decision**: cross-platform C++ monorepo (template web/mobile options
removed). The inward-only dependency rule is preserved; the only shared, reused code
is `core/`. The new effect's single source (`core/effects/modulated-delay/`)
compiles unmodified into every target, exactly as the SVF effect does. No new build
presets or dependencies are introduced.

## Complexity Tracking

> No Constitution Check violations — this section intentionally left empty.

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — Research** (`research.md`): resolve the design decisions — fractional
  interpolation method (linear vs allpass vs cubic) and its click/zipper tradeoffs;
  feedback-loop topology and stability bound (filter-in-loop ordering, clamping);
  LFO shape generation incl. the "random" source; modulation application granularity
  (per-sample vs per-block) to avoid zipper; delay-time smoothing on stepped control
  edits; wow & flutter modeling (wow vs flutter rate/depth ranges); the time-unit
  question for the parameter table (`ParamUnit` has no ms/seconds today); and the
  per-channel memory budget for the 2 s buffer on the embedded targets.
- **Phase 1 — Design & Contracts** (`data-model.md`, `contracts/`, `quickstart.md`):
  the core entities/state as data, the two new primitive contracts (delay line, LFO)
  plus the effect contract and its parameter table, and a runnable validation guide
  per user story.
- **Phase 2 — Tasks** (`tasks.md`): produced by `/speckit-tasks`, not here.

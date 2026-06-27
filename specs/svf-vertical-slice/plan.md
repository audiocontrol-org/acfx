> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: SVF Vertical Slice — proving the acfx cross-platform spine

**Branch**: `platform-foundation` | **Date**: 2026-06-25 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/svf-vertical-slice/spec.md`

## Summary

Prove the acfx platform spine end-to-end by taking one real effect — a
State-Variable Filter — from a single platform-independent core implementation to
four running/linking targets (desktop workbench, DAW plugin, Daisy, Teensy) with a
single parameter declaration driving every adapter. The technical approach is
Approach A from the approved design: a header-centric C++ core with a compile-time
`Effect` concept (zero virtual calls in the hot path), a `constexpr` parameter
model, and a one-virtual-call-per-block `ProcessorNode` boundary used only by the
desktop hosts. The SVF wraps DaisySP's proven `Svf` primitive. Build is CMake +
presets + CPM-pinned dependencies; the core is tested host-side with doctest,
including a frequency-response check and a no-heap-allocation invariant.

## Technical Context

**Language/Version**: C++ — core compiles under **both C++17 and C++20**; the
`Effect`/`Generator` contracts are C++20 concepts where supported (desktop, tests,
Daisy/arm-gcc) and degrade to plain duck-typed templates on toolchains limited to
C++17 (assumed for Teensy; needs-verification during implementation).

**Primary Dependencies** (all CPM-pinned to known-good release tags; exact tag
recorded in the CPM declaration when first fetched — no fabricated versions here):
JUCE 8 (workbench standalone + plugin), `clap-juce-extensions` (CLAP export),
DaisySP (wrapped DSP primitives, incl. `Svf`), libDaisy (Daisy adapter), Teensy
core + Teensy Audio Library (Teensy adapter), doctest (host-side tests), CPM.cmake.

**Storage**: N/A (no persistence in this milestone; presets/state are out of scope).

**Testing**: doctest, host-side, hardware-free — parameter scaling/skew, SVF
impulse/frequency-response against known-good references, NaN/denormal stability,
and a no-heap-allocation-in-`process()` invariant test.

**Target Platform**: four targets from one core — desktop standalone (dev
workbench), desktop plugin (VST3 / AU / CLAP), Daisy (libDaisy / STM32, arm-none-
eabi-gcc), Teensy (Teensy Audio Library, Teensy ARM toolchain).

**Project Type**: cross-platform C++ audio-DSP monorepo — platform-independent core
library + four thin target adapters (not web/mobile; the template's web/mobile
options are removed below).

**Performance Goals**: real-time audio safe on MCU and on the desktop audio thread —
zero heap allocation and zero virtual calls in `process()`; at most **one virtual
call per audio block** at the desktop host boundary; rebuild-and-relaunch edit→hear
loop on the order of a few seconds.

**Constraints**: no heap allocation / locks / unbounded work in any
`process()`/audio-callback path (Constitution VI); no fallbacks/mock/stubs outside
tests, no desktop-side hardware stubs (Constitution IV/V); strict typing, no
unchecked casts; source files ~300–500 lines (Constitution VII); MCU builds must
not pull in JUCE.

**Scale/Scope**: one effect (SVF), three parameters (cutoff/resonance/mode), four
adapters, four+test CMake presets. The milestone proves the spine; breadth (more
effects, generators) is explicitly deferred.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I. Commit & push early/often | Plan is committed/pushed in small increments; WIP commits expected | ✅ planned |
| II. No git hooks | No hooks introduced; CMake/CI/doctest are explicit, visible gates | ✅ by design |
| III. Descriptive names | Spec dir `svf-vertical-slice`, effect dir `core/effects/svf`, no numeric prefixes | ✅ |
| IV. Platform-independent core, thin adapters | `core/` includes no JUCE/libDaisy/Teensy headers; deps point inward; no desktop-side hardware stubs | ✅ central design constraint |
| V. No fallbacks/mock outside tests | Missing audio source / unimplemented path raises a descriptive error; DaisySP is a real dependency, not a stub | ✅ |
| VI. Real-time safety | `Effect::process` is non-allocating, lock-free, branch-bounded; polymorphism confined to ≤1 virtual call/block at `ProcessorNode` | ✅ enforced by FR-014 alloc test |
| VII. Strict typing & small modules | No `any`-equivalent escapes; `core/` split by concern (dsp/, primitives/, effects/); files ≤~500 lines | ✅ |
| VIII. Test the core host-side | doctest suite: scaling, freq-response, stability, no-alloc invariant | ✅ Phase-1 contracts + tests |

**Result**: PASS — no violations to justify. Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/svf-vertical-slice/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output — open-item decisions resolved
├── data-model.md        # Phase 1 output — core entities/contracts as data
├── quickstart.md        # Phase 1 output — runnable validation per user story
├── contracts/           # Phase 1 output — the core C++ interface contracts
│   ├── effect-concept.md
│   ├── parameter-model.md
│   └── processor-node.md
├── checklists/
│   └── requirements.md  # spec-quality checklist (already passing)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

The layout follows the approved design (Approach A). Dependencies point only
inward: `adapters/* → core/*`; `core/*` depends on nothing platform-specific
(DaisySP is a pure-DSP, platform-independent math library, so `core/primitives`
may wrap it without violating Principle IV).

```text
core/                          # platform-independent — NO JUCE/libDaisy/Teensy headers
├── dsp/                       #   Effect concept, ProcessContext, AudioBlock, ParamId
├── primitives/                #   thin wrappers over DaisySP (svf wrapper here)
└── effects/
    └── svf/                   #   the SVF effect + its constexpr parameter declaration

adapters/
├── workbench/                 #   JUCE standalone: audio in/player, auto-GUI, MIDI, A/B
├── plugin/                    #   JUCE AudioProcessor → VST3 / AU / CLAP
├── daisy/                     #   libDaisy audio callback + ADC/encoder → params
└── teensy/                    #   Teensy AudioStream node + analog/MIDI → params

host/                          # desktop-only boundary shared by workbench + plugin
└── processor-node/            #   ProcessorNode virtual wrapper (≤1 vcall/block)

tests/                         # host-side, hardware-free (doctest)
├── core/                      #   scaling, svf freq-response, stability, no-alloc invariant
└── support/                   #   test helpers (allocation sentinel, reference vectors)

cmake/                         # toolchain files (daisy, teensy), helpers, CPM.cmake
external/                      # CPM-fetched pinned deps (JUCE, DaisySP, libDaisy, doctest, …)

CMakeLists.txt                 # top-level orchestration
CMakePresets.json              # presets: desktop, daisy, teensy, test
```

**Structure Decision**: cross-platform C++ monorepo (the template's web/mobile
options are not applicable and are removed). The inward-only dependency rule is the
load-bearing invariant: `core/` and `host/processor-node/` are the only code shared
across desktop adapters; MCU adapters share the parameter-mapping concept but not
the audio-callback glue. The single source of an effect (`core/effects/svf/`)
compiles unmodified into every target.

## Complexity Tracking

> No Constitution Check violations — this section intentionally left empty.

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — Research** (`research.md`): resolve the four deferred open-items
  (SVF wraps DaisySP; workbench audio = live + built-in player; Teensy C++17 with
  concept-degradation; CPM pins to known-good tags), plus best-practice notes for
  the ProcessorNode boundary, the no-allocation test technique, and CLAP-via-JUCE.
- **Phase 1 — Design & Contracts** (`data-model.md`, `contracts/`, `quickstart.md`):
  the core entities as data, the three C++ interface contracts (Effect concept,
  parameter model, ProcessorNode), and a runnable validation guide per user story.
- **Phase 2 — Tasks** (`tasks.md`): produced by `/speckit-tasks`, not here.

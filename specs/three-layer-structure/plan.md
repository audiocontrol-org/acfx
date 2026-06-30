> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Three-Layer DSP Core Structure

**Branch**: `three-layer-structure` | **Date**: 2026-06-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/three-layer-structure/spec.md`; approved design record `docs/superpowers/specs/2026-06-29-three-layer-structure-design.md`.

## Summary

Establish the `labs/ → primitives/ → effects/` three-layer organization of the DSP core
(Constitution Principle IX) on the existing `core/dsp/` substrate. Add a `core/labs/`
layer whose units are C-hybrid (a portable RT-safe **kernel** + a host-only **harness**);
reorganize `core/primitives/` from flat headers into a taxonomy and migrate the three
existing primitives into it; prove the structure by carrying the SVF concept end-to-end
(lab → graduated primitive → effect); and extend `scripts/check-portability.sh` to
mechanically guard lab-harness isolation and dependency direction. No DSP behavior
changes — this is a structural move plus one worked example and a gate extension.

## Technical Context

**Language/Version**: C++17 for the portable core (`acfx_core` is an INTERFACE/header-only target, `cxx_std_17`); C++20 for the host test binary (`acfx_core_tests`). Lab harnesses are host-only, C++17/20.

**Primary Dependencies**: CMake (presets: `test`, `desktop`, `daisy`, `teensy`) + CPM-pinned deps (`cmake/dependencies.cmake`); DaisySP (pure-DSP math, wrapped by primitives); doctest (host tests); JUCE (desktop adapters only, never core). No new dependency is introduced by this feature.

**Storage**: N/A — source-tree structure only.

**Testing**: doctest host suite via `ctest --preset test` (the existing `acfx_core_tests` binary, sources listed explicitly in `tests/CMakeLists.txt`); the `scripts/check-portability.sh` gate; a new host-only SVF lab harness target.

**Target Platform**: Desktop (host tests + workbench + plugin) and ARM MCUs (Daisy, Teensy) cross-compiled from one source. Lab harnesses are host-only and never enter an MCU target.

**Project Type**: Cross-platform audio DSP library + thin per-target adapters (single repo, layered `core/`).

**Performance Goals**: Real-time-safe audio path — no heap allocation, locks, or unbounded work in any `process()` (Constitution VI). Lab kernels held to the same bar; the existing no-allocation sentinel test continues to pass.

**Constraints**: Header-only portable core; includes rooted at `core/` (e.g. `#include "primitives/filters/svf-primitive.h"`); source files within ~300–500 lines (Principle VII); no platform headers in `core/` (Principle IV); zero git hooks (Principle II); descriptive names, no numeric prefixes, no empty `.gitkeep` category dirs (Principle III).

**Scale/Scope**: One new layer dir; 3 primitives migrated into 3 categories; 1 worked-example lab (SVF); ~5 consumer files re-pointed; 1 gate script extended; 4 design docs (research/data-model/contracts/quickstart). No new effects or DSP concepts.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I. Commit & push early/often | Atomic commits per user story, pushed promptly | PASS — plan sequences US1/US2/US3 as separate commits |
| II. No git hooks | Enforcement is the on-purpose `check-portability.sh`, never a hook | PASS — FR-016/FR-018 extend the existing CI gate |
| III. Descriptive names, no numeric prefixes | Spec dir `three-layer-structure`; categories named for what they are; no empty dirs | PASS — taxonomy doc holds uninhabited categories |
| IV. Platform-independent core, thin adapters | `core/labs/**` kernels stay platform-free; harness isolation enforced | PASS — gate check 2 extended over `core/labs`; harness host-only |
| V. No fallbacks/mock data | Migration is a move, not a re-implementation; no stubs | PASS |
| VI. Real-time safety | Lab kernel held to no-alloc/no-lock bar; no-allocation sentinel test unchanged | PASS — kernel == primitive bar (FR-004) |
| VII. Strict typing, small modules | All touched/created files ≤ 500 lines | PASS — gate check 1 extended over `core/labs` |
| VIII. Test the core host-side | Existing host tests pass unchanged; harness reuses measurement intent | PASS — SC-001 |
| IX. Progressive layered architecture | This feature *is* the realization of Principle IX | PASS — the gap it closes |
| X. Measurable engineering | SVF harness emits per-mode frequency-response + stability evidence | PASS — FR-013 |
| XI. One concept at a time | Structural only; introduces no new DSP concept | PASS — FR-020 |

No violations. Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/three-layer-structure/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output (structural entities + dependency rules)
├── quickstart.md        # Phase 1 output (validation guide)
├── contracts/           # Phase 1 output (layering + lab-folder contracts)
│   ├── layering-rules.md
│   └── lab-folder.md
├── checklists/
│   └── requirements.md  # /speckit-specify output (passing)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── dsp/                              # UNCHANGED substrate — Effect, ProcessContext, AudioBlock, params
├── labs/                             # NEW layer
│   └── state-variable-filter/        # worked-example lab (SVF)
│       ├── README.md                 # theory + walkthrough + graduation target
│       └── harness/                  # host-only: per-mode freq-response + stability evidence
│           └── svf-harness.cpp
├── primitives/                       # FLAT → taxonomy
│   ├── README.md                     # NEW taxonomy doc (full intended taxonomy)
│   ├── filters/
│   │   └── svf-primitive.h           # moved from core/primitives/svf-primitive.h (graduated)
│   ├── delays/
│   │   └── delay-line.h              # moved from core/primitives/delay-line.h
│   └── modulation/
│       └── lfo.h                     # moved from core/primitives/lfo.h
└── effects/                          # UNCHANGED location; includes re-pointed
    ├── svf/                          # -> primitives/filters/svf-primitive.h
    └── modulated-delay/              # -> primitives/{delays,modulation}/...

scripts/check-portability.sh          # EXTENDED: lab-harness isolation + dependency direction; learns new layout
tests/CMakeLists.txt                  # re-pointed include paths resolve unchanged (root is core/)
CMakeLists.txt / cmake/*.cmake        # NEW host-only lab-harness target, excluded from daisy/teensy presets
```

**Structure Decision**: Single-repo layered core. The three layers are subdirectories of
`core/` (`labs/`, `primitives/`, `effects/`) over the `core/dsp/` substrate. Includes stay
rooted at the existing `acfx_core` INTERFACE include dir (`core/`), so migration changes
consumer include *paths* (`primitives/svf-primitive.h` → `primitives/filters/svf-primitive.h`)
but not the include *root* or the header-only build model. The SVF lab harness is a new
host-only CMake target built under the `test`/`desktop` presets and absent from `daisy`/`teensy`.

## Complexity Tracking

> No Constitution Check violations — no entries.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none) | — | — |

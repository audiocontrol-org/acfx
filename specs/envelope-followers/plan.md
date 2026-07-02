> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Envelope Followers — Dynamics Level-Detector Primitive

**Branch**: `envelope-followers` | **Date**: 2026-07-02 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/envelope-followers/spec.md`

**Design record**: `docs/superpowers/specs/2026-07-01-envelope-followers-design.md` (operator-approved)

## Summary

Deliver the first primitive of `phase-dynamic-systems` and the first inhabitant of the
`core/primitives/dynamics/` category: an `EnvelopeFollower` level detector that converts an
audio signal into a control-rate amplitude envelope. It is enum-selectable across three
detection modes (peak, RMS, peak-hold), two ballistics topologies (branching, decoupled —
each smooth-capable), and two detection domains (linear base contract, decibel peer), with
attack/release specified in seconds to 1 − 1/e and a one-pole coefficient computed only in the
setters. RMS uses a one-pole leaky integrator with an independent `setRmsWindow`; the dB domain
clamps to a −120 dBFS floor; peak-hold is applied at the detector stage (topology-independent).
The concept is authored as the `core/labs/envelope-follower/` laboratory (ballistics theory
README + RT-safe kernel + host-only harness) and then graduated by relocating its kernel into
`core/primitives/dynamics/` — the second concept to walk the Theory→Lab→Primitive pattern
greenfield, and the first in a new category (created with its inhabitant in one atomic commit).
Validation reuses the shipped measurement stimulus/response infrastructure (attack/release time
accuracy, sine-envelope accuracy, RMS ripple, peak-hold dwell, RT-safety invariants). Enforcement
extends `scripts/check-portability.sh`. Scope is the **detector only** — the gain computer, VCA,
sidechain EQ, and lookahead belong to `design:feature/compressors`.

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy build at
C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways)

**Primary Dependencies**: standard library `<cmath>` for `exp`/`sqrt`/`log10`/`fabs`; the shipped
measurement infrastructure (stimulus/response tooling, allocation sentinel, analytic-bound
assertion helpers) for validation; doctest for the host test suite; CMake + CPM for build. No new
third-party dependency.

**Storage**: N/A (DSP primitive; all detector/smoother state is a fixed set of scalars — no buffer,
per the one-pole RMS decision FR-009)

**Testing**: doctest host-side unit/measurement suites under `tests/core/`, plus the host-only lab
harness under `core/labs/envelope-follower/harness/`; `scripts/check-portability.sh` as the
explicit portability/layering gate (CI, never a hook)

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench + DAW plugins
+ Daisy + Teensy from one source; harness/tests are host-only

**Project Type**: DSP library primitive (single C++ core with thin adapters)

**Performance Goals**: real-time-safe per-sample `process()` — no heap allocation, no locks, bounded
work (Constitution VI); coefficients recomputed only in the `set*` methods and cached; the decibel
conversion is opt-in so the linear path stays transcendental-free for MCU targets

**Constraints**: no platform headers in `core/` (IV); no heap/locks on the audio path (VI); strict
typing, no `any`/unchecked casts, files ~300–500 lines (VII); timing/level assertions use analytic
truths + named tolerances, never fabricated exact figures (V, X); every coefficient bounded to
`[0, 1)` and no NaN/Inf on any input (FR-018)

**Scale/Scope**: one primitive (the full mode/topology/domain catalog — first graduated cut is the
whole catalog per the 2026-07-02 clarification), one lab (README + kernel + host-only harness), one
portability-gate extension, and the `core/primitives/README.md` prospectus→inhabited edit for
`dynamics/`. One design-record open question (low-sample-rate coefficient accuracy) is a planning
characterization detail, not a scope item.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec/clarify. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/envelope-followers`, `core/labs/envelope-follower/`, `core/primitives/dynamics/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Kernel/primitive use only `<cmath>` + `core/dsp/`; no JUCE/libDaisy/Teensy headers; harness is host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | Degenerate setter inputs are guarded to a defined bounded result, not silently faked (FR-018); the −120 dBFS floor is an explicit defined value, not a hidden fallback (FR-012). |
| VI. Real-time safety | PASS | `process()` allocation-free, lock-free, bounded; coefficients cached in setters; no buffer (one-pole RMS) (FR-013/016); asserted by the allocation sentinel (SC-007). |
| VII. Strict typing & small modules | PASS | No `any`/casts; the primitive header (and any ballistics helper split) stays within ~300–500 lines (FR-022). |
| VIII. Test core host-side | PASS | doctest suites + lab harness validate timing/level accuracy, stability (no NaN/Inf), and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Lab → primitive graduation (second greenfield walk, new `dynamics/` category); lab persists as living documentation (FR-019). |
| X. Measurable engineering | PASS | Attack/release times, sine-envelope levels, RMS ripple, peak-hold dwell, and RT-safety invariants are the acceptance evidence (SC-001..010). |
| XI. One concept at a time | PASS | Level detector + its lab only; gain computer, VCA, sidechain EQ, and lookahead are explicitly out of scope (FR-023, Assumptions). |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/envelope-followers/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (ballistics topologies, RMS, dB, time-constant math)
├── data-model.md        # Phase 1 output (entities: EnvelopeFollower, enums, envelope signal, lab)
├── quickstart.md        # Phase 1 output (how to build/run the suite + harness)
├── contracts/           # Phase 1 output (the primitive's public C++ API contract)
│   └── envelope-follower-api.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── dsp/                                  # shared substrate (unchanged)
├── labs/
│   ├── state-variable-filter/            # existing
│   ├── waveshaping/                      # existing (first greenfield lab)
│   └── envelope-follower/                # NEW — lab (persists post-graduation)
│       ├── README.md                     # ballistics theory + walkthrough; names graduation target
│       └── harness/                      # host-only: attack/release + RMS/hold measurement evidence
│           └── envelope-follower-harness.cpp   # (kernel header GRADUATED out; see below)
└── primitives/
    ├── filters/  delays/  modulation/    # existing categories
    ├── nonlinear/  oversampling/  analysis/    # existing categories
    └── dynamics/                          # NEW category — graduation target (first inhabitant)
        └── envelope-follower.h            # EnvelopeFollower: modes × topologies × domains, RT-safe
                                           #   (a ballistics/detector helper header may be split out
                                           #    if the ~300–500-line budget requires — /speckit-tasks)

core/primitives/README.md                  # EDIT: move dynamics/ from prospectus to inhabited (FR-020)

tests/core/
├── (existing measurement-*-test.cpp, svf-test.cpp, ...)
├── envelope-follower-test.cpp             # interface, peak mode, reset, edge cases (US1)
├── envelope-follower-ballistics-test.cpp  # attack/release time accuracy; branching vs decoupled; smooth (US1/US4)
├── envelope-follower-rms-test.cpp         # RMS accuracy (A/√2) + ripple bound (US2)
├── envelope-follower-hold-test.cpp        # peak-hold dwell + restart-on-higher-peak (US3)
├── envelope-follower-db-test.cpp          # dB domain level-independence + −120 dBFS floor (US5)
└── no-allocation-test.cpp                 # EXTEND: cover EnvelopeFollower process() across all configs (SC-007)

scripts/check-portability.sh               # EXTEND: cover core/labs/envelope-follower/** and
                                           #   core/primitives/dynamics/** (FR-021)
```

**Structure Decision**: Single C++ core with the established three-layer taxonomy. The kernel is a
single `EnvelopeFollower` primitive header authored in the lab and **graduated** (`git mv`'d) into
`core/primitives/dynamics/`, refined in place — never re-derived. The `dynamics/` category directory
and its first inhabitant are created in one atomic commit that also moves `dynamics/` from a
prospectus family to an inhabited category in `core/primitives/README.md` (FR-020, inhabit-before-
creating). If the combined modes × topologies × domains logic exceeds the ~300–500-line module
budget (VII), a detector/ballistics helper header is split out under the same category — a
`/speckit-tasks` decision. `core/labs/envelope-follower/` persists as the living lab (README theory +
host-only harness driving the graduated primitive).

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none)    | —          | —                                    |

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Waveshapers — Nonlinear Memoryless Primitive

**Branch**: `phase-nonlinear-dsp` | **Date**: 2026-06-30 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/waveshapers/spec.md`

**Design record**: `docs/superpowers/specs/2026-06-30-waveshapers-design.md` (operator-approved)

## Summary

Deliver the first nonlinear primitive of `phase-nonlinear-dsp`: a catalog of pure,
memoryless waveshaping transfer functions plus a runtime shape-selectable, stateful
`Waveshaper` wrapper (drive → bias → shape → DC-block → gain-comp), with two
selectable evaluation backends (closed-form reference + LUT) and an opt-in
`ADAAWaveshaper` anti-aliased variant layered strictly around the memoryless
contract. The concept is authored as the `core/labs/waveshaping/` laboratory
(theory README + RT-safe kernel + host-only harness) and then graduated by
relocating its kernel into `core/primitives/nonlinear/` — the first concept to walk
the Theory→Lab→Primitive pattern greenfield. Validation reuses the shipped Goertzel
THD/harmonic measurement infrastructure (per-shape harmonic signatures, naive-vs-ADAA
aliasing comparison, real-time-safety invariants). Enforcement extends
`scripts/check-portability.sh`.

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy
build at C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways)

**Primary Dependencies**: standard library `<cmath>` for closed-form transcendentals;
the shipped measurement infrastructure (Goertzel/THD analyzer, sine stimulus,
allocation sentinel, analytic-bound assertion helpers) for validation; doctest for the
host test suite; CMake + CPM for build. No new third-party dependency.

**Storage**: N/A (DSP primitive; any LUT is an in-memory fixed-size table built at `init`)

**Testing**: doctest host-side unit/measurement suites under `tests/core/`, plus the
host-only lab harness under `core/labs/waveshaping/harness/`; `scripts/check-portability.sh`
as the explicit portability/layering gate (CI, never a hook)

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench +
DAW plugins + Daisy + Teensy from one source; harness/tests are host-only

**Project Type**: DSP library primitive (single C++ core with thin adapters)

**Performance Goals**: real-time-safe per-sample `process()` — no heap allocation, no
locks, bounded work (Constitution VI); LUT backend gives uniform per-sample cost on MCU
targets; closed-form is the exact reference

**Constraints**: no platform headers in `core/` (IV); no heap/locks on the audio path
(VI); strict typing, no `any`/unchecked casts, files ~300–500 lines (VII); harmonic
assertions use analytic truths + named tolerances, never fabricated exact figures (V, X)

**Scale/Scope**: one primitive family (the full transfer-function catalog of FR-003),
one wrapper, one ADAA variant, one LUT support unit, one lab (README + kernel + harness),
one portability-gate extension; the first implemented catalog cut is a `/speckit-tasks`
sequencing decision (Open Questions), not a scope cut

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/waveshapers`, `core/labs/waveshaping/`, `core/primitives/nonlinear/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Kernel/primitive use only `<cmath>` + `core/dsp/`; no JUCE/libDaisy/Teensy headers; harness is host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | A shape lacking an analytic antiderivative is documented naive-only and the ADAA variant errors rather than silently mis-shaping (FR-014); LUT out-of-domain has a defined bounded policy, not a silent fallback. |
| VI. Real-time safety | PASS | `process()` allocation-free, lock-free, bounded; any LUT built in `init()` (FR-011, FR-020); asserted by the allocation sentinel. |
| VII. Strict typing & small modules | PASS | No `any`/casts; catalog, wrapper, ADAA, LUT split into separate units within ~300–500 lines (FR-023). |
| VIII. Test core host-side | PASS | doctest suites + lab harness validate correctness, stability (no NaN/denormal), and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Lab → primitive graduation, first greenfield walk; lab persists as living documentation (FR-019). |
| X. Measurable engineering | PASS | Per-shape harmonic (THD) signatures, aliasing comparison, and RT-safety invariants are the acceptance evidence (FR-016/017, SC-001..005). |
| XI. One concept at a time | PASS | Memoryless nonlinearity + its lab only; oversampling, circuit-solved diode, harmonic-analysis tooling, and the saturation effect are explicitly out of scope (FR-015, Assumptions). |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/waveshapers/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output (entities: shape, wrapper, backend, ADAA, lab)
├── quickstart.md        # Phase 1 output (how to build/run the suite + harness)
├── contracts/           # Phase 1 output (the primitive's public C++ API contract)
│   └── waveshaper-api.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── dsp/                                 # shared substrate (unchanged)
├── labs/
│   ├── state-variable-filter/           # existing (the retroactive precedent)
│   └── waveshaping/                      # NEW — first greenfield lab (persists post-graduation)
│       ├── README.md                     # theory + walkthrough; names graduation target
│       └── harness/                       # host-only: harmonic evidence, aliasing comparison
│           └── waveshaping-harness.cpp    #   (kernel headers GRADUATED out; see below)
└── primitives/
    ├── filters/  delays/  modulation/    # existing categories
    └── nonlinear/                         # NEW category — graduation target (kernel now lives here)
        ├── waveshaper-shapes.h            # pure shapes + antiderivatives + hasAntiderivative()
        ├── waveshaper.h                   # stateful Waveshaper wrapper (drive→bias→shape→DC→gainComp)
        ├── waveshaper-lut.h               # fixed-size LUT support unit
        └── adaa-waveshaper.h              # first-order ADAA variant (strictly layered)

tests/core/
├── (existing measurement-*-test.cpp, lfo-test.cpp, ...)
├── waveshaper-shapes-test.cpp            # pure transfer-function analytic correctness (US2)
├── waveshaper-antiderivatives-test.cpp   # antiderivative ↔ shape pairing (F'=shape) for ADAA (US4)
├── waveshaper-test.cpp                    # wrapper signal chain, DC-block, gain-comp (US1)
├── waveshaper-lut-test.cpp                # LUT-vs-closed-form interpolation bound (US3)
├── waveshaper-adaa-test.cpp               # naive-vs-ADAA aliasing reduction (US4)
└── waveshaper-harmonics-test.cpp          # per-shape harmonic signatures via measurement infra (US1/US2)

scripts/check-portability.sh               # EXTEND: cover core/labs/waveshaping/** and
                                           #   core/primitives/nonlinear/** (FR-022)
```

**Structure Decision**: Single C++ core with the established three-layer taxonomy.
The kernel is split into **four** headers — `waveshaper-shapes.h` (pure shapes +
antiderivatives + `hasAntiderivative()`), `waveshaper.h` (the stateful `Waveshaper`
wrapper), `waveshaper-lut.h` (LUT support), and `adaa-waveshaper.h` (the
`ADAAWaveshaper` variant) — to honor the ~300–500-line module budget (VII, FR-023).
The kernel has been **graduated**: those four headers now live under
`core/primitives/nonlinear/` (`git mv`'d from the lab, refined in place, not
re-derived), with `#include` paths in the tests and harness updated accordingly.
`core/labs/waveshaping/` persists as the living lab — README theory plus the
host-only harness — now driving the graduated primitive rather than holding the
kernel itself.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none)    | —          | —                                    |

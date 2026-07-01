> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Saturation — Composed Production Effect

**Branch**: `saturation` | **Date**: 2026-06-30 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/saturation/spec.md`

**Design record**: `docs/superpowers/specs/2026-06-30-saturation-design.md` (operator-approved)

## Summary

Deliver the first Production Effect (stage 4 of the four-stage graduation model) of
`phase-nonlinear-dsp`: a voiced saturation effect that **composes shipped primitives**
— the `Waveshaper` (nonlinearity + optional ADAA) between two `SvfPrimitive`
pre/post-emphasis filters, with a user tone tilt, a dry/wet parallel blend, and a
manual `output` makeup trim (shipped; a separate open question is an additional
*automatic* effect-level makeup law atop the waveshaper's own gain-comp — research.md).
Four voicings (Soft Clip, Tape, Console, Tube Preamp) each fix a
shape + pre/post-emphasis curve; `bias` stays a user control (design Decision 5).
Anti-aliasing ships on the `Waveshaper`'s `naive`/`adaa` modes via a `quality`
control, with an `oversampled` tier reserved as a **documented but unwired seam** — no
dependency on the not-yet-built `oversampling` sibling. The concept is authored as the
`core/labs/saturation/` laboratory (theory README + RT-safe `SaturationCore` kernel +
host-only harness) and then **graduated** by relocating its kernel into
`core/effects/saturation/`, where the `Effect`-contract wrapper (`SaturationEffect` —
constexpr `ParameterDescriptor` table + lock-free atomic parameter handoff) is added —
the first concept to walk Theory→Lab→**Effect** (prior graduations targeted a
primitive). Validation reuses the shipped Goertzel THD/harmonic measurement
infrastructure (per-voicing harmonic signatures, drive→THD monotonicity, gain-comp
band, mix dry/wet balance, naive-vs-ADAA aliasing comparison, RT-safety invariants).
Enforcement extends `scripts/check-portability.sh`.

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy
build at C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways)

**Primary Dependencies**: the shipped `Waveshaper` primitive
(`core/primitives/nonlinear/`) and `SvfPrimitive` filter (`core/primitives/filters/`);
the shared parameter substrate (`core/dsp/` — `ParameterDescriptor`, `ParamId`,
`AudioBlock`, `ProcessContext`); the shipped measurement infrastructure (Goertzel/THD
analyzer, sine stimulus, allocation sentinel, analytic-bound assertion helpers) for
validation; doctest for the host suite; CMake + CPM for build. No new third-party
dependency; no new DSP primitive.

**Storage**: N/A (DSP effect; any per-voicing filter coefficients are computed at
`prepare()` into fixed-size per-channel state)

**Testing**: doctest host-side unit/measurement suites under `tests/core/`, plus the
host-only lab harness under `core/labs/saturation/harness/`;
`scripts/check-portability.sh` as the explicit portability/layering gate (CI, never a
hook)

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench +
DAW plugins + Daisy + Teensy from one source; harness/tests are host-only

**Project Type**: DSP library production effect (single C++ core with thin adapters)

**Performance Goals**: real-time-safe per-block/per-sample `process()` — no heap
allocation, no locks, bounded work (Constitution VI); all filter-coefficient / LUT work
at `prepare()`; cross-thread parameter edits consumed lock-free at the top of a block

**Constraints**: no platform headers in `core/` (IV); no heap/locks on the audio path
(VI); strict typing, no `any`/unchecked casts, files ~300–500 lines (VII); harmonic
assertions use analytic truths + named tolerances, never fabricated exact figures (V,
X); composes shipped primitives only — invents no new nonlinearity/filter (IX, XI)

**Scale/Scope**: one effect (the composition kernel + the `Effect`-contract wrapper),
one voicing table (four voicings of FR-005), one lab (README + kernel + harness), one
portability-gate extension; per-voicing numeric tuning and the oversampled-tier wiring
are `/speckit-tasks`/planning sequencing decisions (Open Questions), not scope cuts

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/saturation`, `core/labs/saturation/`, `core/effects/saturation/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Kernel/effect use only `core/dsp/` + the shipped primitives; no JUCE/libDaisy/Teensy headers; harness is host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | The reserved `oversampled` quality tier is a **defined, bounded, documented** behavior (not a silent partial/aliased path) until the sibling lands (FR-015); no mock data — validation uses real measurement. |
| VI. Real-time safety | PASS | `process()` allocation-free, lock-free, bounded; filter coefficients built in `prepare()`; cross-thread param handoff is lock-free atomics consumed on the audio thread (FR-010, FR-020); asserted by the allocation sentinel. |
| VII. Strict typing & small modules | PASS | No `any`/casts; composition kernel, effect-contract wrapper, and voicing table split into separate units within ~300–500 lines (FR-023). |
| VIII. Test core host-side | PASS | doctest suites + lab harness validate per-voicing harmonics, drive→THD monotonicity, mix balance, aliasing reduction, and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Lab → **effect** graduation (first stage-4 walk); composes shipped primitives, invents none; lab persists as living documentation (FR-019). |
| X. Measurable engineering | PASS | Per-voicing harmonic signatures, drive→THD monotonicity, gain-comp band, mix balance, aliasing comparison, RT-safety invariants are the acceptance evidence (FR-016/017/018, SC-001..007). |
| XI. One concept at a time | PASS | Static-character composed saturation only; program-dependent dynamics, oversampling machinery, multi-stage topologies, and deeper harmonic tooling are explicitly out of scope (FR-015, FR-024, Assumptions). |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/saturation/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output (entities: effect, voicing, kernel, wrapper, quality, lab)
├── quickstart.md        # Phase 1 output (how to build/run the suite + harness)
├── contracts/           # Phase 1 output (the effect's public C++ API contract)
│   └── saturation-api.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── dsp/                                 # shared substrate (unchanged): ParameterDescriptor,
│                                        #   ParamId, AudioBlock, ProcessContext, parameter
├── primitives/
│   ├── nonlinear/                       # existing (SHIPPED) — Waveshaper, ADAAWaveshaper (composed)
│   └── filters/                         # existing (SHIPPED) — SvfPrimitive (composed for emphasis + tone)
├── labs/
│   ├── state-variable-filter/           # existing
│   ├── waveshaping/                     # existing
│   └── saturation/                      # NEW — first lab→effect concept (persists post-graduation)
│       ├── README.md                     # theory: gain-staging, per-voicing emphasis, voicing, naive-vs-ADAA
│       └── harness/                       # host-only: per-voicing harmonic evidence, aliasing comparison
│           └── saturation-harness.cpp     #   (kernel header GRADUATED out; see below)
└── effects/
    ├── svf/  modulated-delay/           # existing Production Effects (the SvfEffect contract precedent)
    └── saturation/                       # NEW — graduation target (kernel now lives here)
        ├── saturation-core.h             # RT-safe composition kernel (graduated from the lab)
        ├── saturation-voicings.h         # voicing table: shape + pre/post-emphasis curves per voicing
        ├── saturation-effect.h           # Effect-contract wrapper (ParameterDescriptor table + atomics)
        └── README.md                     # composition rationale (which primitives it uses, and why)

tests/core/
├── (existing measurement-*-test.cpp, waveshaper-*-test.cpp, svf-*-test.cpp, ...)
├── saturation-core-test.cpp             # composition kernel signal chain, mix blend, DC-free (US1/US3)
├── saturation-voicings-test.cpp         # per-voicing shape+emphasis distinctness (US2)
├── saturation-effect-test.cpp           # Effect contract: param table, cross-thread handoff, prepare/reset (US3)
├── saturation-harmonics-test.cpp        # per-voicing harmonic signatures + drive→THD monotonicity (US1/US2)
└── saturation-aliasing-test.cpp         # naive-vs-ADAA aliasing reduction via quality control (US4)

scripts/check-portability.sh             # EXTEND: cover core/labs/saturation/** and
                                         #   core/effects/saturation/** (FR-022)
```

**Structure Decision**: Single C++ core with the established three-layer taxonomy,
now reaching the **effects** (Production-Effect) layer. The composition kernel is split
to honor the ~300–500-line module budget (VII, FR-023): `saturation-core.h` (the RT-safe
per-channel composition — pre-emphasis → `Waveshaper` → post-de-emphasis → tone → mix →
output), `saturation-voicings.h` (the four-voicing table mapping each voicing to a
`Waveshaper` shape + `SvfPrimitive` pre/post-emphasis coefficients), and
`saturation-effect.h` (the `Effect`-contract wrapper adding the constexpr
`ParameterDescriptor` table and the lock-free atomic cross-thread parameter handoff,
mirroring `core/effects/svf/svf-effect.h`). The kernel is **graduated**:
`saturation-core.h` + `saturation-voicings.h` are authored in `core/labs/saturation/`,
then `git mv`'d into `core/effects/saturation/` (refined in place, not re-derived), with
the effect wrapper added at the effects layer and `#include` paths in tests/harness
updated. `core/labs/saturation/` persists as the living lab — README theory + host-only
harness — now driving the graduated effect. The effect **composes shipped primitives
only**; no new nonlinearity or filter is introduced.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none)    | —          | —                                    |

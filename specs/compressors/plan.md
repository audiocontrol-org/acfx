> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Compressors — Gain Computer + Compressor Effect

**Branch**: `compressors` | **Date**: 2026-07-02 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/compressors/spec.md`

**Design record**: `docs/superpowers/specs/2026-07-02-compressors-design.md` (operator-approved)

## Summary

Deliver the first dynamics *processor* of `phase-dynamic-systems`: a compressor that composes the
shipped `EnvelopeFollower` level detector with a **new stateless `GainComputer` primitive** (the
static curve) and a per-sample gain application, wrapped as a host-facing `CompressorEffect`. The
feature walks the full three-layer vertical in one item — a `core/labs/compressor/` lab (gain-computer
and topology theory README + RT-safe kernel + host-only harness), the `GainComputer` graduated into
`core/primitives/dynamics/gain-computer.h` (the category's **second inhabitant**), and
`CompressorCore` + `CompressorEffect` in `core/effects/compressor/` mirroring the shipped
`SaturationEffect`/`SvfEffect` idiom exactly.

The full captured catalog is the first cut (2026-07-02 clarification): `GainMode {compress, limit,
expand, gate}` with a single unified quadratic C¹ knee; `Detection {feedForward, feedBack}` (feedback
tapping the post-makeup, pre-mix signal); `BallisticsSite {level, gain}`; a `peak`/`rms` detector;
a sidechain highpass (composed `SvfPrimitive`); an external key input; lookahead (composed
`DelayLine`, latency reported); manual + closed-form auto-makeup (`−computeGainDb(0 dBFS)`, off for
expand/gate); dry/wet mix; output trim; and `perChannel`/`linked` stereo detection. `GainComputer` is
pure branch-only arithmetic (no runtime state, no transcendentals); the only per-sample transcendental
cost stays inside the composed `EnvelopeFollower`. Validation reuses the shipped measurement
stimulus/response infrastructure (static-curve accuracy, knee continuity, attack/release time,
feedback convergence, lookahead latency, stereo-link stability, auto-makeup unity, RT-safety).
Enforcement extends `scripts/check-portability.sh`. Program-dependent ("auto") ballistics, multiband,
and dynamic EQ are explicitly out of scope (sibling/future items).

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy build at
C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways)

**Primary Dependencies**: standard library `<cmath>` (`pow`/`log10`/`exp`/`fabs`/`fmax`) and
`<atomic>`/`<array>`/`<cstring>` for the effect wrapper's lock-free parameter handoff; the shipped
primitives `EnvelopeFollower` (`core/primitives/dynamics/envelope-follower.h`), `SvfPrimitive`
(`core/primitives/filters/svf-primitive.h`), and `DelayLine` (`core/primitives/delays/delay-line.h`);
`core/dsp/` (Effect concept, ParameterDescriptor, AudioBlock, ProcessContext); the shipped
measurement infrastructure for validation; doctest; CMake + CPM. No new third-party dependency.

**Storage**: N/A (DSP feature). `GainComputer` is stateless scalars; `CompressorCore` holds a fixed
set of scalars plus the composed primitives' state; the only buffer is the lookahead `DelayLine`,
sized in `prepare()` from the max lookahead and never in `process()`.

**Testing**: doctest host-side unit/measurement suites under `tests/core/`, plus the host-only lab
harness under `core/labs/compressor/harness/`; `scripts/check-portability.sh` as the explicit
portability/layering gate (CI, never a hook).

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench + DAW plugins +
Daisy + Teensy from one source; harness/tests are host-only.

**Project Type**: DSP library feature (a graduated primitive + a composed effect) in a single C++
core with thin adapters.

**Performance Goals**: real-time-safe per-sample `process()` — no heap allocation, no locks, bounded
work (Constitution VI); all coefficients recomputed only in the `set*`/`prepare` paths and cached;
`GainComputer::computeGainDb` is branch-only arithmetic; per-sample transcendental cost is confined to
the composed `EnvelopeFollower`.

**Constraints**: no platform headers in `core/` (IV); no heap/locks on the audio path (VI); strict
typing, no `any`/unchecked casts, files ~300–500 lines with the wrapper split if the ~17-parameter
table exceeds budget (VII, FR-028); level/timing assertions use analytic truths + named tolerances,
never fabricated figures (V, X); no NaN/Inf on any finite input; parameter edits handed off lock-free
and consumed at the top of `process()` (FR-020).

**Scale/Scope**: one new primitive (`GainComputer`), one composition kernel (`CompressorCore`), one
host wrapper (`CompressorEffect`, possibly split per FR-028), one lab (README + kernel + host-only
harness), the new `core/effects/compressor/` module, a portability-gate extension, and the
`core/primitives/README.md` prospectus→inhabited edit for the gain computer. Four parameterization/
structure open questions (wrapper file split, ratio skew, stereo-link max-vs-sum, per-topology
detector defaults) are `/speckit-plan`/`/speckit-tasks` decisions, not scope items.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec/clarify. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/compressors`, `core/labs/compressor/`, `core/primitives/dynamics/gain-computer.h`, `core/effects/compressor/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Primitive/core/effect use only `<cmath>`/`<atomic>` + `core/dsp/` + shipped primitives; no JUCE/libDaisy/Teensy headers; harness host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | Degenerate setter inputs guarded to defined bounded results (FR-024); the `range` floor, `−120 dBFS` detection floor, and auto-makeup-off-for-expand/gate are explicit defined rules, not hidden fallbacks. |
| VI. Real-time safety | PASS | `process()` allocation-free, lock-free, bounded; coefficients cached in setters/`prepare`; lookahead buffer sized in `prepare()`; asserted by the allocation sentinel (SC-012). |
| VII. Strict typing & small modules | PASS | No `any`/casts; the wrapper is split into cooperating files if the ~17-param table exceeds the ~300–500-line budget (FR-028). |
| VIII. Test core host-side | PASS | doctest suites + lab harness validate the static curve, timing, feedback convergence, latency, stereo linking, and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Lab → primitive graduation for `GainComputer` (second inhabitant of `dynamics/`); the compressor lab persists as living documentation; the effect composes shipped primitives (FR-008/025). |
| X. Measurable engineering | PASS | Static-curve level maps, C¹ knee continuity, attack/release times, feedback fixed-point, lookahead sample latency, stereo-link stability, and auto-makeup unity are the acceptance evidence (SC-001..015). |
| XI. One concept at a time | PASS | The detector-fed static dynamics processor only; program-dependent ballistics, multiband, and dynamic EQ are explicitly out of scope (FR-029). |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/compressors/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (gain-computer math, knee, topology, ballistics site, auto-makeup)
├── data-model.md        # Phase 1 output (GainComputer, CompressorCore, CompressorEffect, enums, params)
├── quickstart.md        # Phase 1 output (build/run the suite + harness + portability gate)
├── contracts/           # Phase 1 output (public C++ API contracts)
│   ├── gain-computer-api.md      # the graduated primitive's contract
│   └── compressor-effect-api.md  # the host-facing effect + core contract
├── checklists/
│   └── requirements.md  # spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── dsp/                                  # shared substrate (unchanged): effect.h, parameter.h, ...
├── labs/
│   ├── envelope-follower/                # existing (shipped)
│   ├── saturation/                       # existing
│   └── compressor/                       # NEW — lab (persists post-graduation)
│       ├── README.md                     # gain-computer + topology theory; names graduation target
│       └── harness/                      # host-only: static-curve + step + latency measurement evidence
│           └── compressor-harness.cpp    # (GainComputer kernel header GRADUATED out; see below)
├── primitives/
│   └── dynamics/                          # existing category (envelope-follower.h shipped)
│       ├── envelope-follower.h           # existing — COMPOSED, unchanged
│       └── gain-computer.h               # NEW — GainComputer: compress/limit/expand/gate, unified knee, stateless
└── effects/
    ├── saturation/  svf/  modulated-delay/    # existing effects
    └── compressor/                       # NEW effect module
        ├── compressor-core.h             # CompressorCore: composes EF + GainComputer + SVF + DelayLine
        ├── compressor-effect.h           # CompressorEffect: Effect contract, param table, atomic handoff
        └── compressor-parameters.h       # (OPTIONAL split per FR-028: descriptor table + denormalize)

core/primitives/README.md                  # EDIT: move "gain computers" from prospectus to inhabited (FR-026)

tests/core/
├── (existing measurement-*, envelope-follower-*, saturation-* suites)
├── gain-computer-test.cpp                # static-curve accuracy + knee C¹ continuity, all modes (US2/US4, SC-001/003/005)
├── compressor-test.cpp                   # end-to-end compress/limit, attack/release timing (US1/US3, SC-001/002)
├── compressor-topology-test.cpp          # feedforward vs feedback convergence; ballistics site (US5/US6, SC-004/002)
├── compressor-sidechain-test.cpp         # sidechain HPF + external key (US8/US9, SC-006/007)
├── compressor-lookahead-test.cpp         # lookahead latency == N samples; first-sample limiting (US10, SC-008)
├── compressor-makeup-link-test.cpp       # makeup/auto-makeup/mix + stereo linking (US11/US12, SC-009/010)
├── compressor-effect-test.cpp            # Effect concept, param handoff, static_assert table (US13, SC-011)
└── no-allocation-test.cpp                # EXTEND: cover CompressorEffect process() across all configs (SC-012)

scripts/check-portability.sh               # EXTEND: cover core/labs/compressor/**, gain-computer.h,
                                           #   and core/effects/compressor/** (FR-027)
```

**Structure Decision**: Single C++ core with the established three-layer taxonomy. The `GainComputer`
kernel is authored in the compressor lab and **graduated** (`git mv`'d) into
`core/primitives/dynamics/gain-computer.h`, refined in place — never re-derived; the graduation lands
in one atomic commit that also moves "gain computers" from prospectus to inhabited in
`core/primitives/README.md` (FR-026). `CompressorCore` + `CompressorEffect` live in the new
`core/effects/compressor/` module mirroring `core/effects/saturation/` exactly. If the ~17-parameter
descriptor table pushes `compressor-effect.h` past the ~300–500-line budget (VII/FR-028), the table +
denormalize logic split into `compressor-parameters.h` (a `/speckit-tasks` decision). The compressor
lab persists as the living lab (README theory + host-only harness driving the graduated primitive and
the effect).

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none)    | —          | —                                    |

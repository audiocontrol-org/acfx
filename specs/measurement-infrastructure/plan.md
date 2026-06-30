> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III.)

# Implementation Plan: Measurement Infrastructure

**Branch**: `measurement-infrastructure` | **Date**: 2026-06-29 | **Spec**: [spec.md](./spec.md)

**Input**: `specs/measurement-infrastructure/spec.md` (from the approved, externally-reviewed
design `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md`).

## Summary

Build a reusable, host-side, effect-agnostic measurement harness — the concrete enabler of
Constitution Principle X. A **Stimulus → Effect → Analyzer → Metric** pipeline of separated,
single-purpose components (generators, analyzers, metrics) with clean interfaces and minimal
first implementations. It generalizes the existing `measureMagnitude`/allocation-sentinel
patterns so any `Effect` (or per-sample callable) is validated by objective metrics:
frequency/impulse/phase response, THD (Goertzel, no general FFT — deferred to Phase 8),
latency, relative execution time (desktop-relative proxy), allocation, and numerical
stability (incl. silence/DC/denormal/idle checks). CI gates on doctest assertions vs analytic
bounds; an opt-in CSV report is available. Host-side test/support only — no runtime cost, no
new effects, no new dependency.

## Technical Context

**Language/Version**: C++ — host-side test/support layer; C++20 under the `test` preset
(doctest). No code added to the platform-independent audio path; if any helper lands in
`core/`, it must also be C++17-clean (Teensy), but the harness is expected to live in
`tests/support/`.

**Primary Dependencies**: none new. doctest (host tests, already pinned); reuses
`tests/support/allocation-sentinel` and `tests/support/svf-reference`. No FFT dependency
(Goertzel only; general FFT deferred to Phase 8).

**Storage**: optional CSV report file (opt-in); otherwise none.

**Testing**: doctest host-side. The harness IS test tooling; it is exercised by measuring the
existing SVF and modulated-delay effects (and trivial known processors) and asserting metrics
against analytic references within named tolerances.

**Target Platform**: host (the doctest `test` preset); the harness never ships in an adapter
or audio path.

**Project Type**: cross-platform C++ audio-DSP monorepo — this feature adds host-side
test/support tooling only.

**Performance Goals**: N/A at runtime (host-side offline analysis). The harness itself should
keep measurement runs fast enough for CI (the existing `measureMagnitude` settle/measure
lengths are the reference scale).

**Constraints**: host-side only, no `process()`/audio-path code (Principles VI/VIII);
platform-independent, no platform headers in `core/`; strict typing, no unchecked casts, files
~300–500 lines (Principle VII); no false precision (analytic truths + named tolerances); no
new third-party dependency; no general FFT (Phase 8); effect-agnostic via the `Effect` contract
and per-sample callables.

**Scale/Scope**: a small set of stimulus generators (impulse/step/sine/sweep/noise first;
multi-tone/MLS forward-looking), analyzers (impulse/Goertzel/correlation first; FFT
forward-looking), and metrics (magnitude/phase/latency/THD/relative-exec-time/allocation/
stability), plus an opt-in CSV reporter. Validated against SVF + modulated-delay.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I. Commit & push early/often | Each component committed in small increments, pushed | ✅ planned |
| II. No git hooks | None; CMake/doctest are the explicit gates | ✅ by design |
| III. Descriptive names | Spec dir `measurement-infrastructure`; `tests/support/measurement/` units named for purpose | ✅ |
| IV. Platform-independent core | Harness lives in `tests/support/`; no platform headers; `core/` untouched (no audio-path code) | ✅ |
| V. No fallbacks/mock outside tests | This IS test tooling; analytic-truth assertions, not fabricated data; missing capability errors loud | ✅ |
| VI. Real-time safety | No `process()`/audio-path code added; measurement is offline | ✅ N/A to hot path |
| VII. Strict typing & small modules | No unchecked casts; split generators/analyzers/metrics/reporter into small units ≤~500 lines | ✅ |
| VIII. Test the core host-side | This feature directly advances host-side testability — it is the measurement harness | ✅ central |
| IX. Progressive layered architecture | Analyzer/metric outputs kept lab-reusable (forward-looking, no lab code here) | ✅ designed-in |
| X. Measurable Engineering | This IS the enabler — the objective metric suite | ✅ the point |
| XI. One concept at a time | Single major idea (measurement harness), applied to existing effects | ✅ |

**Result**: PASS — no violations. Complexity Tracking empty. (Anti-over-abstraction risk from
the review is mitigated by minimal-first implementations behind clean interfaces.)

## Project Structure

### Documentation (this feature)

```text
specs/measurement-infrastructure/
├── plan.md · research.md · data-model.md · quickstart.md
├── contracts/
│   ├── stimulus.md      # stimulus-generator interface
│   ├── analyzer.md      # analyzer interface + the first-cut analyzers
│   └── metrics.md       # metric outputs + the CSV report contract
├── checklists/requirements.md
└── tasks.md             # /speckit-tasks output
```

### Source (host-side test/support — no core/ audio code)

```text
tests/support/
├── allocation-sentinel.{h,cpp}     # (existing) reused for the allocation metric
├── svf-reference.h                 # (existing) measureMagnitude seed; generalized
└── measurement/                    # NEW — the harness
    ├── stimulus.h                  #   ImpulseGenerator/StepGenerator/SineGenerator/SweepGenerator/NoiseGenerator
    ├── analyzers.h                 #   ImpulseAnalyzer/GoertzelAnalyzer/CorrelationAnalyzer
    ├── metrics.h                   #   magnitude/phase/latency/THD/relative-exec-time/stability + capture helper
    └── report.h                    #   opt-in CSV reporter

tests/core/
└── measurement-test.cpp            # NEW — exercises the harness on SVF + modulated-delay + known processors
```

**Structure Decision**: the harness is host-side test/support (`tests/support/measurement/`),
not `core/` — it must impose no runtime cost and never ship in an adapter. It composes the
existing sentinel + magnitude helpers. The single new test TU drives it against real effects.

## Complexity Tracking

> No Constitution Check violations — intentionally empty.

## Phases (artifacts generated here)

- **Phase 0 — Research** (`research.md`): record the settled decisions (Goertzel vs FFT;
  assertions + CSV; host-side; reuse existing helpers) + the measurement techniques (steady-state
  magnitude, Goertzel bin, impulse/correlation latency, denormal-prone stimulus construction,
  relative-exec-time timing method) and per-metric analytic-reference approach.
- **Phase 1 — Design & Contracts** (`data-model.md`, `contracts/*`, `quickstart.md`): the
  stimulus/analyzer/metric entities, the three interface contracts, and the verification guide.
- **Phase 2 — Tasks** (`tasks.md`): `/speckit-tasks`.

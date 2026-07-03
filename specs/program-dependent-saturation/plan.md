> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Program-Dependent Saturation — Envelope-Modulated Nonlinearity + Dynamics-Modulator Primitive

**Branch**: `program-dependent-saturation` | **Date**: 2026-07-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/program-dependent-saturation/spec.md`

**Design record**: `docs/superpowers/specs/2026-07-02-program-dependent-saturation-design.md` (operator-approved, gate 7/7)

## Summary

Deliver the first program-dependent (dynamic) saturator of `phase-dynamic-systems`: an effect that
composes the shipped, **unchanged** `SaturationCore` nonlinearity with the shipped `EnvelopeFollower`
level detector and a **new stateless `DynamicsModulator` primitive** (the envelope→signed-offset
mapper), so the saturation character tracks the signal's dynamics. Where the compressor turned an
envelope into gain reduction, this turns an envelope into signed **saturation-parameter offsets**
(drive/bias/tone/mix). The feature walks the full three-layer vertical in one item — a
`core/labs/program-dependent-saturation/` lab (envelope-modulates-nonlinearity theory README +
RT-safe kernel + host-only harness), the `DynamicsModulator` graduated into
`core/primitives/dynamics/dynamics-modulator.h` (the category's **third inhabitant**), and
`ProgramDependentSaturationCore` + `ProgramDependentSaturationEffect` in
`core/effects/program-dependent-saturation/` mirroring the shipped `SaturationEffect`/`SvfEffect`
idiom exactly.

The full captured catalog is the first cut (2026-07-03 clarification): a four-target modulation matrix
(`drive`, `bias`, `tone`, `mix`), each with a signed depth and a `ModCurve {linear, logarithmic,
exponential}`; `Detection {feedForward, feedBack}` (feedback tapping the **final output sample `y`**,
which keeps `SaturationCore` composed unchanged); the shipped `EnvelopeFollower` detector catalog
(peak/rms/peakHold, branching/decoupled, attack/release); named dynamic-character presets
(`none`/`opto`/`variMu`/`tapeComp`) as documented matrix configurations; a sidechain highpass
(composed `SvfPrimitive`); an external key input; and `perChannel`/`linked` stereo detection. The
`DynamicsModulator` is pure bounded arithmetic (no runtime state, no per-sample transcendental beyond
the composed `EnvelopeFollower`). The modulator consumes a **0..1 normalized-dB-window envelope**
(default −60..0 dBFS) scaled by depth into each target's native span; **drive/bias/mix modulate
per-sample** (cheap scalar setters) while **tone modulates per-block** (avoiding a per-sample SVF
coefficient recompute). The load-bearing contract is **zero-depth orthogonality**: all depths 0 ⇒
byte-for-byte the static `SaturationEffect`. Validation reuses the shipped measurement stimulus/response
and harmonic-analysis infrastructure. Enforcement extends `scripts/check-portability.sh`. The physical
tape magnetic model (hysteresis/wow-flutter), multiband, and dynamic EQ are explicitly out of scope
(sibling/future items).

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy build at
C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways).

**Primary Dependencies**: standard library `<cmath>` (`pow`/`log10`/`exp`/`fabs`) and
`<atomic>`/`<array>`/`<cstring>` for the effect wrapper's lock-free parameter handoff; the shipped
units composed **unchanged** — `SaturationCore` (`core/effects/saturation/saturation-core.h`),
`EnvelopeFollower` (`core/primitives/dynamics/envelope-follower.h`), `SvfPrimitive`
(`core/primitives/filters/svf-primitive.h`); `core/dsp/` (Effect concept, ParameterDescriptor,
AudioBlock, ProcessContext); the shipped measurement + harmonic-analysis infrastructure for validation;
doctest; CMake + CPM. No new third-party dependency.

**Storage**: N/A (DSP feature). `DynamicsModulator` is stateless scalars;
`ProgramDependentSaturationCore` holds a fixed set of scalars plus the composed units' state (one
shared `EnvelopeFollower`, one `SvfPrimitive`, one `SaturationCore`, four `DynamicsModulator`
instances, and a one-sample `prevOutput` feedback tap). No dynamic buffers.

**Testing**: doctest host-side unit/measurement suites under `tests/core/`, plus the host-only lab
harness under `core/labs/program-dependent-saturation/harness/`; `scripts/check-portability.sh` as the
explicit portability/layering gate (CI, never a hook).

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench + DAW plugins +
Daisy + Teensy from one source; harness/tests are host-only.

**Project Type**: DSP library feature (a graduated primitive + a composed effect) in a single C++ core
with thin adapters.

**Performance Goals**: real-time-safe per-sample `process()` — no heap allocation, no locks, bounded
work (Constitution VI); all coefficients recomputed only in `set*`/`prepare` and cached;
`DynamicsModulator::modulate` is bounded arithmetic; the only per-sample transcendental cost is confined
to the composed `EnvelopeFollower`. **One shared detector** feeds four stateless modulators (MCU-viable,
FR-020); **tone** modulation is applied per-block to avoid a per-sample SVF coefficient recompute
(FR-010a).

**Constraints**: no platform headers in `core/` (IV); no heap/locks on the audio path (VI); strict
typing, no `any`/unchecked casts, files ~300–500 lines with the wrapper split if the large parameter
table exceeds budget (VII, FR-025); harmonic/timing assertions use analytic truths + named tolerances,
never fabricated figures (V, X); no NaN/Inf on any finite input; every modulated parameter clamped into
`SaturationCore`'s valid range (FR-010); parameter edits handed off lock-free and consumed at the top of
`process()` (FR-017).

**Scale/Scope**: one new primitive (`DynamicsModulator`), one composition kernel
(`ProgramDependentSaturationCore`), one host wrapper (`ProgramDependentSaturationEffect`, possibly split
per FR-025), one lab (README + kernel + host-only harness), the new
`core/effects/program-dependent-saturation/` module, a portability-gate extension, and the
`core/primitives/README.md` prospectus→inhabited edit for the modulation mapper. Seven parameterization/
structure open questions (response-curve law, preset definitions, reference-window endpoints, per-block
tone smoothing, stereo-link detail, static-parameter application seam, wrapper file split) are
`/speckit-plan`/`/speckit-tasks` decisions, not scope items.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec/clarify. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/program-dependent-saturation`, `core/labs/program-dependent-saturation/`, `core/primitives/dynamics/dynamics-modulator.h`, `core/effects/program-dependent-saturation/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Primitive/core/effect use only `<cmath>`/`<atomic>` + `core/dsp/` + shipped primitives; no JUCE/libDaisy/Teensy headers; harness host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | Degenerate setter inputs guarded to defined bounded results (FR-021); the −60..0 dBFS normalization window, the modulated-parameter clamp (FR-010), and the −120 dBFS detection floor are explicit defined rules, not hidden fallbacks. |
| VI. Real-time safety | PASS | `process()` allocation-free, lock-free, bounded; coefficients cached in setters/`prepare`; tone modulation per-block to avoid per-sample SVF recompute; asserted by the allocation sentinel (SC-013). |
| VII. Strict typing & small modules | PASS | No `any`/casts; the wrapper is split into cooperating files if the large parameter table exceeds the ~300–500-line budget (FR-025). |
| VIII. Test core host-side | PASS | doctest suites + lab harness validate orthogonality, modulation-offset, THD-vs-level, step timing, feedback convergence, presets, and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Lab → primitive graduation for `DynamicsModulator` (third inhabitant of `dynamics/`); the lab persists as living documentation; the effect composes shipped units unchanged (FR-004/022/026). |
| X. Measurable engineering | PASS | Zero-depth orthogonality, modulation-offset accuracy, signed-direction THD-vs-level, attack/release times, feedback fixed-point, and preset equivalence are the acceptance evidence (SC-001..016). |
| XI. One concept at a time | PASS | The envelope-modulates-nonlinearity mechanism only; the physical tape model, multiband, and dynamic EQ are explicitly out of scope (FR-026); no new nonlinearity kernel is added. |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/program-dependent-saturation/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (envelope→offset mapping, normalization, curves, feedback tap, update rate)
├── data-model.md        # Phase 1 output (DynamicsModulator, ProgramDependentSaturationCore, effect, enums, params)
├── quickstart.md        # Phase 1 output (build/run the suite + harness + portability gate)
├── contracts/           # Phase 1 output (public C++ API contracts)
│   ├── dynamics-modulator-api.md                    # the graduated primitive's contract
│   └── program-dependent-saturation-effect-api.md   # the host-facing effect + core contract
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
│   ├── compressor/                       # existing (shipped)
│   └── program-dependent-saturation/     # NEW — lab (persists post-graduation)
│       ├── README.md                     # envelope-modulates-nonlinearity theory; names graduation target
│       └── harness/                      # host-only: orthogonality + THD-vs-level + step measurement evidence
│           └── program-dependent-saturation-harness.cpp   # (DynamicsModulator kernel header GRADUATED out)
├── primitives/
│   └── dynamics/                          # existing category (envelope-follower.h, gain-computer.h shipped)
│       ├── envelope-follower.h           # existing — COMPOSED, unchanged
│       ├── gain-computer.h               # existing (shipped)
│       └── dynamics-modulator.h          # NEW — DynamicsModulator: signed depth + linear/log/exp curve, stateless
└── effects/
    ├── saturation/  svf/  compressor/  modulated-delay/   # existing effects (saturation COMPOSED unchanged)
    └── program-dependent-saturation/     # NEW effect module
        ├── program-dependent-saturation-core.h        # composes SaturationCore + EnvelopeFollower + SvfPrimitive + 4 DynamicsModulator
        ├── program-dependent-saturation-effect.h      # Effect contract, param table, atomic handoff
        └── program-dependent-saturation-parameters.h  # (OPTIONAL split per FR-025: descriptor table + denormalize)

core/primitives/README.md                  # EDIT: move the modulation mapper from prospectus to inhabited (FR-023)

tests/core/
├── (existing measurement-*, envelope-follower-*, saturation-*, compressor-* suites)
├── dynamics-modulator-test.cpp            # signed-offset + curve shape + statelessness (US2, SC-004/007)
├── program-dependent-saturation-orthogonality-test.cpp   # zero-depth == static SaturationEffect (US3, SC-002)
├── program-dependent-saturation-test.cpp  # dynamic drive THD-vs-level + step timing (US1/US7, SC-001/005)
├── program-dependent-saturation-matrix-test.cpp          # bias/tone/mix targets, no cross-talk (US4, SC-003)
├── program-dependent-saturation-topology-test.cpp        # feedforward vs feedback convergence (US6, SC-006)
├── program-dependent-saturation-presets-test.cpp         # preset equivalence to documented configs (US9, SC-008)
├── program-dependent-saturation-sidechain-test.cpp       # sidechain HPF + external key + stereo link (US10/11/12, SC-009/010/011)
├── program-dependent-saturation-effect-test.cpp          # Effect concept, param handoff, static_assert (US8, SC-012)
└── no-allocation-test.cpp                 # EXTEND: cover ProgramDependentSaturationEffect process() across all configs (SC-013)

scripts/check-portability.sh               # EXTEND: cover core/labs/program-dependent-saturation/**, dynamics-modulator.h,
                                           #   and core/effects/program-dependent-saturation/** (FR-024)
```

**Structure Decision**: Single C++ core with the established three-layer taxonomy. The
`DynamicsModulator` kernel is authored in the program-dependent-saturation lab and **graduated**
(`git mv`'d) into `core/primitives/dynamics/dynamics-modulator.h`, refined in place — never re-derived;
the graduation lands in one atomic commit that also moves the modulation mapper from prospectus to
inhabited in `core/primitives/README.md` (FR-023). `ProgramDependentSaturationCore` +
`ProgramDependentSaturationEffect` live in the new `core/effects/program-dependent-saturation/` module
mirroring `core/effects/saturation/` exactly, composing `SaturationCore` **unchanged** (FR-004/026). If
the large descriptor table pushes `program-dependent-saturation-effect.h` past the ~300–500-line budget
(VII/FR-025), the table + denormalize logic split into `program-dependent-saturation-parameters.h` (a
`/speckit-tasks` decision). The lab persists as the living lab (README theory + host-only harness driving
the graduated primitive and the effect).

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none)    | —          | —                                    |

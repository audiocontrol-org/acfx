> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: tape-dynamics — Hysteresis Primitive + TapeDynamicsEffect

**Branch**: `tape-dynamics` | **Date**: 2026-07-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/tape-dynamics/spec.md`

**Design record**: `docs/superpowers/specs/2026-07-03-tape-dynamics-design.md` (operator-approved 2026-07-03)

## Summary

Deliver the **capstone** of `phase-dynamic-systems`: the platform's first **nonlinearity with memory**.
A new **stateful `Hysteresis` primitive** implements the **Jiles-Atherton** magnetic model (`dM/dH`
via the Langevin anhysteretic curve plus the irreversible+reversible split), integrated per sample by
a **selectable solver** — RK2, RK4, and Newton-Raphson, all three in the first cut (clarified OQ1). A
host-facing **`TapeDynamicsEffect`** composes that primitive under the **shipped `Oversampler<Factor>`**
(the JA step is the `evalAtHighRate` callable — the oversampler is reused verbatim, not modified),
exposes physics-mapped macros (`drive`, `saturation`→`Ms`, `width`→`k`, `solver`, `oversampling`,
`mix`, `output`), and layers an **optional** explicit envelope-driven trim composing the shipped
`EnvelopeFollower` + `GainComputer` (included in the first cut, bit-exact no-op when disabled —
clarified OQ2). The feature walks the full three-layer vertical in one item: a `core/labs/tape-dynamics/`
lab (JA theory + solver tradeoff + emergent-compression README, an RT-safe kernel, a host-only harness),
the `Hysteresis` kernel graduated into `core/primitives/nonlinear/hysteresis.h` as that category's
**first *stateful* inhabitant** (contrasted with the stateless waveshaper family — ADAA does not apply
because state carries across samples, so oversampling is the antialiasing route), and
`TapeDynamicsCore` + `TapeDynamicsEffect` in `core/effects/tape-dynamics/` mirroring the shipped
`SaturationEffect`/`CompressorEffect`/`SvfEffect` idiom exactly.

The `oversampling` parameter exposes the menu **{2×, 4×, 8×}**, default **8×** (clarified OQ4; the shipped `Oversampler` caps at Factor 8);
concrete numeric ranges for the physics macros are tuned during implementation against the analysis
harness (OQ3, deferred — not architecture-blocking). Validation reuses the shipped measurement
stimulus/response infrastructure (`host/analysis/`): the **closed `M`-vs-`H` loop with area > 0** is
the defining memory proof (a static waveshaper is single-valued); RK2/RK4/Newton agree within tolerance
and converge as oversampling rises; emergent compression is a monotonic-compressive level curve with a
drive-increasing dynamic-range-reduction metric (trim OFF); THD/aliasing fall with the oversampling
factor; `drive`=0/bypass is unity and no finite input produces NaN/Inf (stiff-solver guard). A dedicated
`tests/core/hysteresis-test.cpp` unit-tests the primitive in addition to the lab harness (clarified
OQ5). Enforcement extends `scripts/check-portability.sh`. **Out of scope (Constitution XI, FR-023):**
wow/flutter (shipped `WowFlutterStage`; `tape-machine`'s job), convolution HF/gap-loss
(phase-convolution), and bias/noise/named-deck EQ/full record-repro path (`tape-machine`,
phase-reference-hardware).

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy build at
C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways).

**Primary Dependencies**: standard library `<cmath>` (`exp`/`tanh`/`cosh`/`fabs`/`fmax`/`isfinite` for
the Langevin coth and the solvers), `<array>`/`<atomic>`/`<cstring>` for the effect wrapper's lock-free
parameter handoff; the shipped primitives `Oversampler<Factor>`
(`core/primitives/oversampling/oversampler.h`), `EnvelopeFollower`
(`core/primitives/dynamics/envelope-follower.h`), and `GainComputer`
(`core/primitives/dynamics/gain-computer.h`); `core/dsp/` (Effect concept, ParameterDescriptor,
AudioBlock, ProcessContext); the shipped `host/analysis/` measurement infrastructure for validation;
doctest; CMake + CPM. No new third-party dependency.

**Storage**: N/A (DSP feature). `Hysteresis` holds a fixed set of scalars — magnetization `M`, previous
field `H_prev`, and the derived field-rate — plus its physical parameters and selected solver;
`TapeDynamicsCore` holds the composed `Oversampler`, the per-channel `Hysteresis` states, the optional
`EnvelopeFollower`+`GainComputer`, and cached macro→physics coefficients. No heap buffer is allocated in
`process()`; the oversampler's internal state is sized at construction/`prepare()`.

**Testing**: doctest host-side unit/measurement suites under `tests/core/` (including the new
`hysteresis-test.cpp`), plus the host-only lab harness under `core/labs/tape-dynamics/harness/`;
`scripts/check-portability.sh` as the explicit portability/layering gate (CI, never a hook).

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench + DAW plugins +
Daisy + Teensy from one source; harness/tests are host-only.

**Project Type**: DSP library feature (a graduated stateful primitive + a composed effect) in a single
C++ core with thin adapters.

**Performance Goals**: real-time-safe per-sample `process()` — no heap allocation, no locks, bounded
work (Constitution VI). Per output sample the cost is `Factor` high-rate JA steps; each step evaluates
the Langevin term (one `coth`/`exp`) and the chosen solver's stage count (RK2 = 2, RK4 = 4 derivative
evals, Newton = a small bounded fixed iteration count). All macro→physics coefficients are recomputed
only in `set*`/`prepare` and cached; the hot path holds no transcendental beyond the JA/Langevin
evaluation.

**Constraints**: no platform headers in `core/` (IV); no heap/locks on the audio path (VI); strict
typing, no `any`/unchecked casts, files ~300–500 lines with the effect wrapper split if the parameter
table exceeds budget (VII); level/loop/stability assertions use analytic truths + named tolerances,
never fabricated figures (V, X); **no NaN/Inf on any finite input** — the stiff-solver guard clamps
non-finite/out-of-range state to a defined stable value (FR-006); parameter edits handed off lock-free
and consumed at the top of `process()`.

**Scale/Scope**: one new stateful primitive (`Hysteresis` with a `Solver` enum + `JAParams`), one
composition kernel (`TapeDynamicsCore`), one host wrapper (`TapeDynamicsEffect`, split into
core/effect/parameters/presets), one lab (README + kernel + host-only harness), the new
`core/effects/tape-dynamics/` module, a dedicated `tests/core/hysteresis-test.cpp`, a portability-gate
extension, and the `core/primitives/README.md` prospectus→inhabited edit for the hysteresis primitive
(noting the first stateful member of `nonlinear/`). One open question (OQ3, physics-macro numeric
ranges) is an implementation-tuning decision against the harness, not a scope item.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec/clarify. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/tape-dynamics`, `core/labs/tape-dynamics/`, `core/primitives/nonlinear/hysteresis.h`, `core/effects/tape-dynamics/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Primitive/core/effect use only `<cmath>`/`<atomic>`/`<array>` + `core/dsp/` + shipped primitives; no JUCE/libDaisy/Teensy headers; harness host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | The stiff-solver guard (clamp/deNaN to a defined stable state, FR-006) and `drive`=0 unity passthrough are explicit defined rules, not hidden fallbacks; missing inputs are guarded to bounded results, never mocked. |
| VI. Real-time safety | PASS | `process()` allocation-free, lock-free, bounded (`Factor` × solver-stage JA evals per sample); coefficients cached in setters/`prepare`; oversampler state sized outside the hot path; asserted by the allocation sentinel (SC-007). |
| VII. Strict typing & small modules | PASS | No `any`/casts; the effect wrapper is split into core/effect/parameters/presets so each file stays within the ~300–500-line budget. |
| VIII. Test core host-side | PASS | doctest suites (`hysteresis-test.cpp` + effect tests) + lab harness validate the loop, solver agreement, emergent compression, aliasing, guards, and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Lab → primitive graduation for `Hysteresis` (first *stateful* inhabitant of `nonlinear/`); the tape-dynamics lab persists as living documentation; the effect composes shipped primitives (Oversampler, EnvelopeFollower, GainComputer). |
| X. Measurable engineering | PASS | Closed-loop area, solver agreement/convergence, level-curve compression + DRR metric, THD/alias sweep, and NaN-freedom are the acceptance evidence (SC-001..007). |
| XI. One concept at a time | PASS | A single new idea — nonlinearity with memory (hysteresis) — applied to a complete effect; wow/flutter, convolution loss, and the full deck are explicitly out of scope (FR-023). |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/tape-dynamics/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (JA model, Langevin coth, solver tradeoff, oversampling, emergent compression, guard)
├── data-model.md        # Phase 1 output (Hysteresis, Solver, JAParams, TapeDynamicsCore, TapeDynamicsEffect, params)
├── quickstart.md        # Phase 1 output (build/run the suite + harness + portability gate)
├── contracts/           # Phase 1 output (public C++ API contracts)
│   ├── hysteresis-api.md         # the graduated stateful primitive's contract
│   └── tape-dynamics-effect-api.md  # the host-facing effect + core contract
├── checklists/
│   └── requirements.md  # spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── labs/tape-dynamics/
│   ├── README.md                       # theory: JA dM/dH, Langevin coth, explicit-vs-implicit solvers,
│   │                                   #   order-of-accuracy/stability under oversampling, emergent
│   │                                   #   compression, why ADAA does not apply (state across samples)
│   ├── kernel/                         # RT-safe hysteresis kernel that graduates into the primitive
│   └── harness/
│       └── tape-dynamics-harness.cpp   # host-only measurement harness (loop, solver agreement, DRR, alias)
├── primitives/
│   ├── nonlinear/
│   │   └── hysteresis.h                # GRADUATED stateful Jiles-Atherton primitive (Solver enum + JAParams)
│   └── README.md                       # prospectus→inhabited edit: nonlinear/ gains its first STATEFUL member
└── effects/tape-dynamics/
    ├── tape-dynamics-core.h            # Oversampler<Factor> ∘ Hysteresis (+ optional trim) RT kernel
    ├── tape-dynamics-effect.h          # host-facing Effect wrapper (prepare(ProcessContext)/process(AudioBlock))
    ├── tape-dynamics-parameters.h      # parameter IDs, ranges, macro→physics mapping
    └── tape-dynamics-presets.h         # named starting points

tests/core/
└── hysteresis-test.cpp                 # dedicated primitive unit test (mirrors delay-line-test.cpp)

scripts/
└── check-portability.sh                # extended to cover the new primitive/effect/lab paths
```

**Structure Decision**: single C++ DSP core with thin adapters (Constitution IV). The feature adds one
lab, one graduated primitive, one effect module, and one unit-test file, following the shipped
`compressors` / `saturation` three-layer idiom exactly. No new top-level structure is introduced.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

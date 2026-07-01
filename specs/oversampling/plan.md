> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Oversampling — Reusable Anti-Aliasing Primitive

**Branch**: `oversampling` | **Date**: 2026-07-01 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/oversampling/spec.md`

**Design record**: `docs/superpowers/specs/2026-07-01-oversampling-design.md` (operator-approved)

## Summary

Deliver the **oversampling** concept as a reusable primitive (stage 3 of the four-stage
graduation model — Theory → Laboratory → **Reusable Primitive**) for `phase-nonlinear-dsp`:
a generic, RT-safe, compile-time-factored block-wrapper `Oversampler<Factor>` (Factor ∈
{2,4,8}) that upsamples one input sample, invokes a caller-supplied `noexcept` nonlinearity
on each oversampled sample, then band-limits and decimates back to one output sample. The
anti-alias/anti-image core is a cascade of 2× **linear-phase polyphase halfband FIR** stages
(`HalfbandUpsampler` 1→2, `HalfbandDownsampler` 2→1) whose coefficients are baked
`static constexpr` (design decision 3). The primitive exposes the effective oversampled rate
(so a caller can `prepare()` its rate-dependent DSP at that rate) and an integer
`latencySamples()` group delay for future host PDC. The concept is authored as the
`core/labs/oversampling/` laboratory (theory README + host-only harness) and then
**graduated** into `core/primitives/oversampling/`, mirroring the `state-variable-filter →
filters/` and `waveshaping → nonlinear/` graduations. Here the primitive (the reusable
headers) and the lab (theory README + host-only harness) are distinct artifacts, so the
primitive is authored directly under `core/primitives/oversampling/` and the lab exercises
it — no code is staged-then-`git mv`'d (that ceremony fits only when lab-kernel and target
are the *same* code, as in saturation). Its **first client** — closing the
reserved saturation `oversampled` seam (saturation FR-015) — proves the primitive end-to-end:
`SaturationCore` gains an `Oversampler` plus a nonlinear stage prepared at the oversampled
rate, the `oversampled` quality case becomes a real oversampled path framed by the base-rate
emphasis filters, `oversampled` becomes user-selectable, and the saturation aliasing test
flips from asserting `oversampled == adaa` to asserting measurably-lower inharmonic energy
than `naive`. Validation reuses the shipped measurement infrastructure (aliasing measure,
Goertzel/THD analyzer, `svf-reference` analytic-tolerance pattern, allocation sentinel).
Enforcement extends `scripts/check-portability.sh` to cover the new lab + primitive trees.

## Technical Context

**Language/Version**: C++ (core compiles under both C++17 and C++20; desktop/test/Daisy build
at C++20, `acfx_core` exposes `cxx_std_17` as the floor — same source both ways, matching the
saturation/SVF precedent)

**Primary Dependencies**: for the primitive itself — **none new** and, deliberately, **no
DaisySP dependency** (DaisySP ships no resampler/decimator/polyphase filter, confirmed in
`cmake/dependencies.cmake`; the halfband FIR is authored in-repo). The shared substrate
(`core/dsp/`) is not required by the primitive's per-sample surface. For the first client:
the shipped `Waveshaper` primitive (`core/primitives/nonlinear/`) and the saturation effect
(`core/effects/saturation/`). For validation: the shipped measurement infrastructure
(`tests/support/measurement/` Goertzel/THD analyzer + sine stimulus, `aliasingMeasure`,
`tests/support/svf-reference.h` analytic-tolerance helpers, the allocation sentinel); doctest
for the host suite; CMake + CPM for build.

**Storage**: N/A (DSP primitive; halfband coefficients are `static constexpr`, filter delay
lines are fixed-size value members).

**Testing**: doctest host-side unit/measurement suites under `tests/core/`, plus the host-only
lab harness under `core/labs/oversampling/harness/`; `scripts/check-portability.sh` as the
explicit portability/layering gate (CI, never a hook — Principle II).

**Target Platform**: platform-independent core (Constitution IV) — desktop workbench + DAW
plugins + Daisy + Teensy from one source; harness/tests are host-only.

**Project Type**: DSP library reusable primitive (single C++ core with thin adapters).

**Performance Goals**: real-time-safe per-sample `process()` — no heap allocation, no locks,
bounded work (Constitution VI); all coefficients `static constexpr` / baked at `init()`;
halfband decomposition keeps per-stage work at the lower rate. Embedded RAM footprint is
statically known (compile-time-sized `std::array` delay lines).

**Constraints**: no platform/vendor headers in `core/` (IV); no heap/locks on the audio path
(VI); strict typing, no `any`/unchecked casts, files ~300–500 lines (VII); filter assertions
use analytic FIR truths + named tolerances, never fabricated exact figures (V, X); the
primitive embeds no nonlinearity — the caller supplies it (IX composition); one new concept
only — oversampling (XI).

**Scale/Scope**: one primitive (the `Oversampler<Factor>` wrapper + the two halfband stages +
one coefficient table), one lab (README + harness), the first-client saturation wiring
(closing FR-015), the measurement-reuse test suite, and one portability-gate extension.
Per-target tap-count tuning, additional quality tiers, the IIR "fast" tier, a runtime factor,
non-power-of-two ratios, and further clients are captured-but-deferred (spec FR-024) — planning
sequencing / later-pass decisions, not scope cuts.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/oversampling`, `core/labs/oversampling/`, `core/primitives/oversampling/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | Primitive uses only in-repo headers + the standard library; no JUCE/libDaisy/Teensy/DaisySP headers; harness is host-only and gate-isolated. |
| V. No fallbacks/mock data | PASS | The primitive raises no silent fallback; the saturation seam moves from a *documented interim* (ADAA) to a *real* oversampled path. Validation uses real measured audio, not mock data. |
| VI. Real-time safety | PASS | `process()` is allocation-free, lock-free, bounded; coefficients `static constexpr`; delay lines are fixed-size value members; asserted by the allocation sentinel (SC-005). |
| VII. Strict typing & small modules | PASS | No `any`/casts; the coefficient table, halfband stages, and `Oversampler` wrapper are split into separate headers within ~300–500 lines (FR-023 of the primitive spec sense). |
| VIII. Test core host-side | PASS | doctest suites + lab harness validate transparency, aliasing reduction, stopband/ripple, latency equality, cascade correctness, and the no-alloc invariant host-side. |
| IX. Progressive layered architecture | PASS | Theory → Lab → **Primitive** graduation (stage 3); the primitive composes a caller nonlinearity + its own halfband stages, invents no effect; lab persists as living documentation. |
| X. Measurable engineering | PASS | Frequency/stopband response, group-delay/latency, aliasing (THD-inharmonic) reduction, and no-allocation are the acceptance evidence (SC-001..008). |
| XI. One concept at a time | PASS | Oversampling is the single new idea; the IIR "fast" tier, quality tiers, runtime factor, non-power-of-two ratios, and further clients are explicitly deferred (spec FR-024). |

**Result: PASS — no violations.** Complexity Tracking is empty.

## Project Structure

### Documentation (this feature)

```text
specs/oversampling/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (filter design, coeff provenance, factor mechanism, latency, wiring)
├── data-model.md        # Phase 1 output (entities: Oversampler, halfband stages, coeff table, client path)
├── quickstart.md        # Phase 1 output (how to build/run the suite + harness)
├── contracts/           # Phase 1 output (the primitive's public C++ API contract)
│   └── oversampling-api.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already created)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── dsp/                                 # shared substrate (unchanged)
├── primitives/
│   ├── nonlinear/                       # existing (SHIPPED) — Waveshaper (the first-client nonlinearity)
│   ├── filters/                         # existing (SHIPPED) — SvfPrimitive
│   └── oversampling/                    # NEW — graduation target (primitive lives here)
│       ├── halfband-coefficients.h      #   static constexpr linear-phase halfband FIR taps (+ provenance note)
│       ├── halfband-stage.h             #   HalfbandUpsampler (1→2) + HalfbandDownsampler (2→1), polyphase
│       ├── oversampler.h                #   Oversampler<Factor> — cascade + wrap-and-process + rate/latency
│       └── README.md                    #   which building blocks it uses, and the RT-safety contract
├── labs/
│   ├── state-variable-filter/           # existing (lab→primitive precedent)
│   ├── waveshaping/                     # existing (lab→primitive precedent)
│   ├── saturation/                      # existing
│   └── oversampling/                    # NEW — the concept's laboratory (persists post-graduation)
│       ├── README.md                     # theory: sampling/aliasing, halfband FIR, polyphase, latency
│       └── harness/                       # host-only: transparency + aliasing-reduction evidence, sweeps
│           └── oversampling-harness.cpp
└── effects/
    └── saturation/                       # MODIFIED (first client — closes saturation FR-015)
        ├── saturation-core.h             #   add Oversampler member + oversampledShaper_ @ oversampled rate;
        │                                 #   real oversampled path in process() (was interim ADAA mapping)
        ├── saturation-voicings.h         #   `oversampled` enum stays; comments de-reserved
        └── saturation-effect.h           #   add `oversampled` to the user-selectable quality labels

tests/core/
├── (existing measurement-*, waveshaper-*, svf-*, saturation-* suites)
├── oversampler-transparency-test.cpp    # identity round-trip ≈ delayed input; silence; reset (US1)
├── oversampler-aliasing-test.cpp        # driven nonlinearity: oversampled << naive inharmonic (US1/US2)
├── oversampler-response-test.cpp        # stopband rejection / passband ripple vs analytic tolerances (US2)
├── oversampler-latency-test.cpp         # measured group delay == latencySamples(), per factor (US3)
└── saturation-aliasing-test.cpp         # MODIFIED: oversampled asserts < naive (was == adaa) (US4)

tests/core/no-allocation-test.cpp        # EXTEND: Oversampler::process() no-alloc, per factor (SC-005)

scripts/check-portability.sh             # EXTEND: cover core/labs/oversampling/** and
                                         #   core/primitives/oversampling/** (platform-free, harness-free)
```

**Structure Decision**: Single C++ core with the established three-layer taxonomy, delivering
at the **primitives** layer. The primitive is split to honor the ~300–500-line module budget
(VII): `halfband-coefficients.h` (the `static constexpr` linear-phase halfband taps + a
provenance note), `halfband-stage.h` (`HalfbandUpsampler` / `HalfbandDownsampler` — the 2×
polyphase interpolation/decimation units with fixed-size delay lines), and `oversampler.h`
(`Oversampler<Factor>` — the cascade wiring, the wrap-and-process contract, `oversampledRate()`
and `latencySamples()`). The concept graduates as **Theory → Lab → Primitive**: the primitive
(coefficient table + halfband stages + `Oversampler`) is authored directly under
`core/primitives/oversampling/`, and `core/labs/oversampling/` is its living companion —
theory README + host-only harness that `#include`s and exercises the primitive to render the
transparency/aliasing evidence. (Unlike saturation, no code is staged-then-`git mv`'d: there
the lab kernel and the effect kernel were the *same* code; here the primitive and the lab are
distinct artifacts.) The primitive **embeds no nonlinearity** — the caller supplies it — and
the saturation effect is its first client, composing it with the shipped `Waveshaper`.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| (none)    | —          | —                                    |

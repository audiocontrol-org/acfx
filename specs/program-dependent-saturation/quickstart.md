# Quickstart — Program-Dependent Saturation (Dynamics Modulator + Effect)

**Feature**: `specs/program-dependent-saturation` | **Date**: 2026-07-03 | **Phase**: 1

A validation/run guide that proves the feature works end-to-end. Implementation lives in `tasks.md`;
API and state details are in `contracts/{dynamics-modulator,program-dependent-saturation-effect}-api.md`
and `data-model.md`.

## Prerequisites
- The acfx host toolchain (CMake + CPM, C++20 desktop build; `acfx_core` floor `cxx_std_17`).
- Offline CPM cache if sandboxed: `export CPM_SOURCE_CACHE=external/.cpm-cache` (see project memory).
- The composed units are shipped and reused **unchanged**: `SaturationCore`, `EnvelopeFollower`,
  `SvfPrimitive`.

## Build & run the host test suite
```bash
make test          # configures + builds the desktop test target and runs the doctest suite
```
Expected: all `tests/core/` suites pass, including the new program-dependent-saturation suites:
- `dynamics-modulator-test.cpp` — signed-offset + curve shapes + statelessness (US2)
- `program-dependent-saturation-orthogonality-test.cpp` — zero-depth == static `SaturationEffect` (US3)
- `program-dependent-saturation-test.cpp` — dynamic drive THD-vs-level + step timing (US1/US7)
- `program-dependent-saturation-matrix-test.cpp` — bias/tone/mix targets, no cross-talk (US4)
- `program-dependent-saturation-topology-test.cpp` — feedforward vs feedback convergence (US6)
- `program-dependent-saturation-presets-test.cpp` — preset equivalence to documented configs (US9)
- `program-dependent-saturation-sidechain-test.cpp` — SC HPF + external key + stereo link (US10/11/12)
- `program-dependent-saturation-effect-test.cpp` — Effect concept, param handoff, `static_assert` (US8)
- `no-allocation-test.cpp` — zero heap allocation on `process()` across all configs (SC-013)

## Validation scenarios (what each proves)
| Scenario | Setup | Expected outcome | Spec |
|---|---|---|---|
| Dynamic drive | +driveDepth, feedForward; tone stepped in level | THD rises with level per `depth·curve(env)` model | SC-001 |
| Zero-depth orthogonality | all depths 0; matching static params; tone/sweep/noise/transient battery | output == static `SaturationEffect` (byte-for-byte where paths coincide) | SC-002 |
| Matrix targets | non-zero depth on bias/tone/mix individually | only that param offsets; others at static base; no cross-talk | SC-003 |
| Signed direction | −driveDepth vs +driveDepth (equal magnitude) | offset trajectories mirror-image; THD falls for negative | SC-004 |
| Response curves | linear vs log vs exp at equal depth; env sweep | each offset-vs-env matches analytic shape; distinguishable | SC-004 |
| Modulation timing | level step, attack 10 ms | modulation ~63% within 10 ms; recovers at release | SC-005 |
| Feedback fixed point | feedBack, steady input | settles to stable fixed point; no oscillation/divergence; defined cold start | SC-006 |
| Modulator statelessness | `modulate(a)` then `modulate(b)` vs reversed | identical results; call-order independent | SC-007 |
| Presets | select opto/variMu/tapeComp; none | realized matrix == documented config; none = all-depths-0 | SC-008 |
| Sidechain HPF | scHpf 120 Hz; 60 Hz vs 1 kHz at same level | 60 Hz → much less modulation than 1 kHz | SC-009 |
| External key | quiet main + loud key | modulation follows key level; applied to main path | SC-010 |
| Stereo linking | linked; transient in L only | both channels get same modulation (character/image stable) | SC-011 |
| Effect handoff | setParameter off-thread, then process() | applied next block; no lock/torn read | SC-012 |
| Silence / safety | silence, DC, impulse, feedback cold start, extreme depth | no NaN/Inf; silence → silence; params in range | SC-014 |
| RT-safety | all targets/curves/topologies | allocation sentinel: 0 allocations in `process()` | SC-013 |

## Lab harness (host-only)
`core/labs/program-dependent-saturation/harness/program-dependent-saturation-harness.cpp` drives the
`DynamicsModulator` and the dynamic saturator through level-swept / step / impulse stimuli and emits
orthogonality + THD-vs-level + step-response measurement evidence. Built host-side only; MUST NOT be
included by any portable unit.

## Portability gate (explicit, CI — never a hook)
```bash
scripts/check-portability.sh
```
Expected: PASS over `core/labs/program-dependent-saturation/**`,
`core/primitives/dynamics/dynamics-modulator.h`, and `core/effects/program-dependent-saturation/**` —
harness isolation, dependency direction (primitives never include effects; the effect includes only
`core/dsp/` + shipped primitives/`SaturationCore`), platform independence (no JUCE/libDaisy/Teensy), and
file-size budget. Confirm `core/primitives/README.md` lists the modulation mapper as an inhabited member
of `dynamics/` (moved from prospectus) in the same commit as the graduated primitive.

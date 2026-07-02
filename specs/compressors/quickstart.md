# Quickstart — Compressors (Gain Computer + Compressor Effect)

**Feature**: `specs/compressors` | **Date**: 2026-07-02 | **Phase**: 1

A validation/run guide that proves the feature works end-to-end. Implementation lives in `tasks.md`;
API and state details are in `contracts/{gain-computer,compressor-effect}-api.md` and `data-model.md`.

## Prerequisites
- The acfx host toolchain (CMake + CPM, C++20 desktop build; `acfx_core` floor `cxx_std_17`).
- Offline CPM cache if sandboxed: `export CPM_SOURCE_CACHE=external/.cpm-cache` (see project memory).
- The composed primitives are shipped: `EnvelopeFollower`, `SvfPrimitive`, `DelayLine`.

## Build & run the host test suite
```bash
make test          # configures + builds the desktop test target and runs the doctest suite
```
Expected: all `tests/core/` suites pass, including the new compressor suites:
- `gain-computer-test.cpp` — static-curve accuracy + unified-knee C¹ continuity, all modes (US2/US4)
- `compressor-test.cpp` — end-to-end compress/limit + attack/release timing (US1/US3)
- `compressor-topology-test.cpp` — feedforward vs feedback convergence; level vs gain site (US5/US6)
- `compressor-sidechain-test.cpp` — sidechain HPF + external key (US8/US9)
- `compressor-lookahead-test.cpp` — lookahead latency == N samples; first-sample limiting (US10)
- `compressor-makeup-link-test.cpp` — makeup/auto-makeup/mix + stereo linking (US11/US12)
- `compressor-effect-test.cpp` — Effect concept, param handoff, `static_assert` table (US13)
- `no-allocation-test.cpp` — zero heap allocation on `process()` across all configs (SC-012)

## Validation scenarios (what each proves)
| Scenario | Setup | Expected outcome | Spec |
|---|---|---|---|
| Static curve | compress, thr −20, ratio 4, hard knee; −10 dBFS tone | output ≈ −17.5 dBFS (± tol) | SC-001 |
| Below threshold | same; −30 dBFS tone | output ≈ input (no reduction) | SC-001 |
| Limit brickwall | limit, thr −6; −1 dBFS tone | output ≈ −6 dBFS (± tol) | SC-001 |
| Knee continuity | soft knee width W; level sweep across knee | GR curve C¹-continuous; == hard curve at knee 0 | SC-003 |
| Attack/release | step across threshold, attack 10 ms | GR ~63% within 10 ms; recovers at release (± tol) | SC-002 |
| Feedback fixed point | feedBack, steady above-threshold input | settles to analytic fixed point; stable | SC-004 |
| Expand/gate | expand thr −40 ratio 2 range −20; −50 dBFS tone | attenuated per curve, ≥ −20 dB floor; unity above | SC-005 |
| Sidechain HPF | scHpf 120 Hz; 60 Hz vs 1 kHz tone at same level | 60 Hz → much less GR than 1 kHz | SC-006 |
| External key | quiet main + loud key above threshold | main attenuated per key level | SC-007 |
| Lookahead latency | lookahead L ms | reported latency == round(L·fs) samples | SC-008 |
| Makeup / mix | manual M dB; mix 0 and mix 1 | +M dB; dry passthrough; fully compressed | SC-009 |
| Auto-makeup unity | autoMakeup on; below-threshold signal | output ≈ unity | SC-009 |
| Stereo linking | linked; transient in L only | both channels get same GR (image stable) | SC-010 |
| Effect handoff | setParameter off-thread, then process() | applied next block; no lock/torn read | SC-011 |
| Silence / safety | silence, DC, impulse, feedback cold start | no NaN/Inf; silence → silence | SC-013 |
| RT-safety | all modes/topologies/sites | allocation sentinel: 0 allocations in `process()` | SC-012 |

## Lab harness (host-only)
`core/labs/compressor/harness/compressor-harness.cpp` drives the `GainComputer` and the compressor
through level-swept / step / impulse stimuli and emits static-curve + attack/release + latency
measurement evidence. Built host-side only; MUST NOT be included by any portable unit.

## Portability gate (explicit, CI — never a hook)
```bash
scripts/check-portability.sh
```
Expected: PASS over `core/labs/compressor/**`, `core/primitives/dynamics/gain-computer.h`, and
`core/effects/compressor/**` — harness isolation, dependency direction (primitives never include
effects; the effect includes only `core/dsp/` + shipped primitives), platform independence (no
JUCE/libDaisy/Teensy), and file-size budget. Confirm `core/primitives/README.md` lists the gain
computer as an inhabited member of `dynamics/` (moved from prospectus) in the same commit as the
graduated primitive.

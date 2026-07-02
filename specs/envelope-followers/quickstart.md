# Quickstart — Envelope Followers (Dynamics Level-Detector Primitive)

**Feature**: `specs/envelope-followers` | **Date**: 2026-07-02 | **Phase**: 1

A validation/run guide that proves the primitive works end-to-end. Implementation lives in
`tasks.md`; API and state details are in `contracts/envelope-follower-api.md` and `data-model.md`.

## Prerequisites
- The acfx host toolchain (CMake + CPM, C++20 desktop build; `acfx_core` floor `cxx_std_17`).
- Offline CPM cache if sandboxed: `export CPM_SOURCE_CACHE=external/.cpm-cache` (see project memory).

## Build & run the host test suite
```bash
make test          # configures + builds the desktop test target and runs the doctest suite
```
Expected: all `tests/core/` suites pass, including the new envelope-follower suites:
- `envelope-follower-test.cpp` — interface/peak/reset/edge cases (US1)
- `envelope-follower-ballistics-test.cpp` — attack/release timing; branching vs decoupled; smooth (US1/US4)
- `envelope-follower-rms-test.cpp` — RMS level (A/√2) + ripple (US2)
- `envelope-follower-hold-test.cpp` — peak-hold dwell + restart-on-higher-peak (US3)
- `envelope-follower-db-test.cpp` — dB level-independence + −120 dBFS floor (US5)
- `no-allocation-test.cpp` — zero heap allocation on `process()` across all configs (SC-007)

## Validation scenarios (what each proves)
| Scenario | Setup | Expected outcome | Spec |
|---|---|---|---|
| Attack timing | peak, branching, attack=10 ms; unit step | envelope ~63% within 10 ms (± tol) | SC-001 |
| Release timing | settled at 1.0, release=100 ms; step to 0 | decays to ~37% within 100 ms (± tol) | SC-002 |
| Peak vs RMS level | steady sine amplitude A | peak → ≈A; rms → ≈A/√2 (± tol) | SC-003 |
| RMS ripple | rms on steady sine, settled | ripple below named bound | SC-004 |
| Peak-hold dwell | peakHold, hold=50 ms; impulse then silence | holds ≈P for ~50 ms, then releases | SC-005 |
| Topology-independent hold | peakHold under branching AND decoupled | both hold then release | FR-015 |
| dB level-independence | decibel; steps 20 dB apart | equal attack time (± tol); linear differs | SC-006 |
| Silence / floor | silence | linear env = 0; dB env = −120 dBFS; no NaN/Inf | SC-008 |
| RT-safety | all modes/topologies/domains | allocation sentinel: 0 allocations in `process()` | SC-007 |

## Lab harness (host-only)
`core/labs/envelope-follower/harness/envelope-follower-harness.cpp` drives the kernel through
step/impulse/sine stimuli and emits attack/release + RMS/hold measurement evidence. It is built
host-side only and MUST NOT be included by any portable unit.

## Portability gate (explicit, CI — never a hook)
```bash
scripts/check-portability.sh
```
Expected: PASS over `core/labs/envelope-follower/**` and `core/primitives/dynamics/**` —
harness isolation, dependency direction (primitives never include effects), platform independence
(no JUCE/libDaisy/Teensy), and file-size budget. Confirm `core/primitives/README.md` lists
`dynamics/` as an inhabited category (moved from prospectus) in the same commit as the primitive.

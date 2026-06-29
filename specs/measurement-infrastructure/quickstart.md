# Quickstart & Validation: Measurement Infrastructure

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

The harness is host-side test tooling under `tests/support/measurement/`, exercised by
`tests/core/measurement-test.cpp` against real effects. Validation is via the doctest suite.

## Build + run (the gate)
```sh
make test          # or: cmake --preset test && cmake --build --preset test && ctest --preset test
```
Expected: the existing suite plus the new measurement tests pass. Per the portability gate,
`core/` is untouched (no platform headers, no audio-path code) and no new dependency is added.

## US1 — effect-agnostic response (SC-001/002)
The measurement test drives the **SVF** and a trivial known processor (e.g. a one-pole) with
the same calls:
- magnitude/frequency response asserted vs analytic bounds (passband≈unity, stopband attenuated);
- impulse response captured and checked;
- phase response vs the analytic phase of a known second-order filter within tolerance;
- the **same** calls measure a second effect with no effect-specific code.

## US2 — distortion, delay, spectra (SC-003)
- THD via Goertzel on a pure tone: ≈0 for a clean linear effect; elevated for a known
  nonlinearity (e.g. a hard-clip callable).
- latency: impulse-peak / cross-correlation lag matches a known processing delay ± tolerance.

## US3 — stability, allocation, cost (SC-004)
- stability verdicts for silence-in→silence-out, DC-offset, denormal-prone input, idle
  noise-floor; no NaN/Inf/denormal output.
- allocation: `AllocationSentinel` around `process()` reports zero.
- relative execution time: a desktop-relative time-per-block figure is produced (labeled a
  proxy, not absolute cycles), with block size recorded.

## US4 — optional CSV report (SC-005)
```sh
# report emission is OFF by default; enable it in the test (or via a flag) to emit a CSV.
```
Expected: with emission on, a well-formed CSV (header + rows) is written; with it off
(default), no file is written and CI passes on assertions alone.

## Scope / governance (SC-006)
```sh
git diff --name-only origin/main...HEAD   # tests/support/measurement/, tests/core/, specs/ only
```
Expected: no `core/`/`adapters/` audio-path changes, no new third-party dependency, no general
FFT (deferred to Phase 8). The portability gate stays green.

## Notes
The harness's analyzer/metric outputs (and the CSV) are the seam later Phase laboratories reuse
for Bode plots, spectra, impulse/step responses, and waterfalls (Principle IX) — no separate
measurement code.

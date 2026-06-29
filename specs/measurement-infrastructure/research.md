# Phase 0 Research: Measurement Infrastructure

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

Most decisions were settled in the approved, externally-reviewed design. Recorded here with
the measurement techniques each metric uses. No `NEEDS CLARIFICATION` remain.

## Decision 1 — Spectral analysis: Goertzel + sine-sweep (no general FFT)
**Decision**: Magnitude via the existing steady-state sine-sweep RMS ratio; harmonic content
(THD) via the **Goertzel** algorithm (single-bin power at the fundamental and its harmonics).
**Rationale**: dependency-free, minimal, sufficient for THD/harmonic checks on known tones; a
general FFT belongs with Phase 8 (Convolution), which needs one regardless.
**Alternatives**: first-party FFT now (more to build, pulls Phase-8 forward — rejected for
Phase 1); CPM FFT dep (new third-party dep for test tooling — rejected).

## Decision 2 — Output: assertions gate CI + opt-in CSV report
**Decision**: doctest assertions vs analytic/named-tolerance bounds gate CI; a CSV report is
opt-in (off by default).
**Rationale**: CI stays deterministic and artifact-free; CSV enables trending/plotting and is
the seam labs reuse (Principle IX).
**Alternatives**: assertions-only (loses the engineering artifact); always-on report (noisy).

## Decision 3 — Host-side test/support placement
**Decision**: the harness lives in `tests/support/measurement/`, not `core/`.
**Rationale**: zero runtime cost, never ships in an adapter (Principles VI/VIII); it is test
tooling. Reuses `allocation-sentinel` + `svf-reference`.
**Alternatives**: a `core/` measurement module (rejected — would risk audio-path coupling and
is unnecessary for offline analysis).

## Decision 4 — Effect-agnostic surface
**Decision**: measurements accept any `Effect` AND any plain per-sample `float(float)` callable
(the SVF test already measures a callable). A small capture helper runs an effect/callable over
a stimulus into an output buffer.
**Rationale**: maximal reuse with no effect-specific code; matches the existing pattern.

## Decision 5 — Separated stimulus / analyzer / metric (clean interfaces, minimal-first)
**Decision**: three single-purpose concepts; new measurements are compositions. Implement the
simplest useful version of each; do not build speculative machinery (review risk note).
**First cut**: generators impulse/step/sine/sweep/noise; analyzers impulse/Goertzel/correlation;
metrics magnitude/phase/latency/THD/relative-exec-time/allocation/stability. **Forward-looking
(captured, deferred)**: multi-tone/MLS generators, FFT analyzer.

## Measurement techniques (per metric)
- **Magnitude / frequency response** — steady-state RMS ratio at a frequency (existing
  `measureMagnitude`), generalized to any effect/callable; sweep across frequencies for a curve.
- **Phase response** — phase of the steady-state output sinusoid vs input (e.g. via the
  Goertzel complex bin, or cross-correlation peak offset) at a frequency; assert vs the analytic
  phase of a known second-order filter within tolerance.
- **Impulse response** — capture the output of a unit-impulse stimulus.
- **THD** — drive a pure tone; Goertzel the fundamental + N harmonics; THD = sqrt(Σ harmonic
  powers)/fundamental amplitude. ≈0 for clean linear; elevated for a known nonlinearity.
- **Latency** — impulse-peak index or cross-correlation lag between input and output; account
  for the effect's own delay.
- **Relative execution time** — wall-clock per processed block over many repeats (median), a
  *desktop-relative* proxy; record block size. NOT absolute cycles (avoids false precision).
- **Allocation** — wrap `process()` in the existing `AllocationSentinel`; assert zero.
- **Numerical stability** — scan output for NaN/Inf/denormal and bounds; explicit cases:
  silence-in→silence-out (output within an idle-noise-floor tolerance), DC-offset input,
  denormal-prone input (tiny decaying values), and idle/noise-floor after signal stops.

## Per-metric reference bounds
Follow the `svf-reference` philosophy: assert **analytic truths + named tolerances** (passband
near unity, stopband attenuated, THD below a named threshold for a linear effect, latency equal
to a known delay ± a sample tolerance), never fabricated exact magnitudes. The exact bound per
metric is finalized at implementation against the effect under test.

No unknowns remain. Ready for Phase 1.

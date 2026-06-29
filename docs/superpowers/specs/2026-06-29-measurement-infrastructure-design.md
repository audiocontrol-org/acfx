# Measurement Infrastructure — Design

**Date:** 2026-06-29 (rev. after external design review)
**Roadmap item:** `design:feature/measurement-infrastructure` (Phase 1, Digital Fundamentals)
**Phase:** designing → (handoff) `/stack-control:define`
**Status:** Approved with minor revisions (external review 2026-06-28); revisions incorporated.

## Problem domain

Constitution Principle X (Measurable Engineering) requires every acfx effect to be
validated by **objective measurements** — frequency response, impulse response, phase
response, harmonic distortion (THD), latency, relative execution time, memory allocation, and
numerical stability — with listening tests complementary, never a replacement. Today the
project has only fragments: an allocation sentinel (no-heap-allocation invariant) and an
ad-hoc sine-sweep magnitude helper (`tests/support/svf-reference.h::measureMagnitude`) used
by the SVF test. There is no reusable, effect-agnostic **measurement harness** that the
existing effects (SVF, modulated-delay) and every future phase can share to produce the
Principle-X metric suite. This sub-project builds that harness. It is the concrete enabler of
Principle X and a Phase-1 deliverable; the program's later phases depend on it.

Constraints (constitution): platform-independent (no platform headers in `core/`); host-side
and hardware-free (Principle VIII); no runtime/audio-path cost (Principle VI); strict typing,
small modules; reusable across all effects via the `Effect` contract; no false precision
(assert analytic truths + named tolerances, not fabricated exact numbers). Scope is
**measurement tooling only** — no new effects.

## Solution space

### Architecture — a Stimulus → Effect → Analyzer → Metrics pipeline (review enhancements 3 & 4)

The harness is composed of three **separated, single-purpose concepts** with clean
interfaces, so new measurements are compositions rather than bespoke utilities:

```
Stimulus generator → Effect (under test) → Analyzer → Metric
```

- **Stimulus generators** — produce known input signals: `ImpulseGenerator`, `StepGenerator`,
  `SineGenerator`, `SweepGenerator`, `NoiseGenerator`, plus forward-looking `MultiToneGenerator`
  and `MLSGenerator`. (Captured for the spec; first implementation cut is sequenced in planning.)
- **Analyzers** — reduce a captured output to raw analysis results: `ImpulseAnalyzer`,
  `GoertzelAnalyzer` (single-bin magnitude/harmonics), `CorrelationAnalyzer` (delay), and a
  forward-looking `FFTAnalyzer` (Phase 8). Analyzers are distinct from the metrics derived
  from them.
- **Metrics** — derived quantities reported/asserted: magnitude, phase, group delay/latency,
  THD, relative execution time, allocation count, stability verdict.

Anti-over-abstraction guard (review risk note): define the clean interfaces now, but
implement the **simplest useful version** of each generator/analyzer/metric; do not build
speculative machinery ahead of a concrete measurement that needs it.

### Metric suite (captured in full — capture over YAGNI; sequencing is a planning pass)

- **frequency response** — magnitude (and phase) vs frequency
- **impulse response** — response to a unit impulse
- **phase response** — phase shift vs frequency
- **harmonic distortion (THD)** — harmonic energy from a pure-tone stimulus (Goertzel bins)
- **latency** — processing/group delay (impulse-peak or cross-correlation)
- **relative execution time** — host wall-clock per processed block, a *desktop-relative*
  proxy (NOT absolute hardware/MCU cycles; named to avoid false precision — review enh. 2)
- **memory allocation** — reuse the existing allocation sentinel (no-heap-alloc invariant)
- **numerical stability** — NaN/Inf/denormal-free and bounded under stress, including the
  explicit checks added by review enh. 1: **silence-in → silence-out**, **DC-offset**,
  **denormal generation**, and **idle noise-floor**.

## Decisions (settled)

1. **Host-side analysis only** — pure offline test/analysis utilities; **no runtime or
   audio-path cost** (Principles VI, VIII). (Review: strongly agree.)
2. **Effect-agnostic + reusable** — works against any `Effect` (and any per-sample callable);
   every current/future effect benefits. Generalizes `measureMagnitude`. (Review: strongly agree.)
3. **Platform-independent** — host test/support layer; `core/` stays free of platform headers.
4. **Build on existing infra** — reuse `tests/support/allocation-sentinel` and the
   `svf-reference` magnitude/analytic-bounds pattern as the seed.
5. **Capture the full Principle-X metric suite**; sequence the implementation in planning,
   not by cutting scope. (Review: agree.)
6. **Decision A — spectral analysis: Goertzel + sine-sweep, no FFT yet.** THD/harmonic
   analysis uses the Goertzel algorithm on known test tones; magnitude uses the existing
   sine-sweep. No new dependency; a general FFT is deferred to **Phase 8 (Convolution)**,
   where it is needed regardless. (Review recommendation, adopted.)
7. **Decision B — output: assertions + optional CSV report.** CI gates rely **exclusively on
   assertions** vs analytic/reference bounds (the `svf-reference` pattern). A **CSV** report is
   **opt-in** (regression visualization, trending, harmonic-spectrum inspection, external
   plotting, future dashboards). (Review recommendation, adopted.)
8. **Separated stimulus / analyzer / metric concepts** with clean interfaces, minimal first
   implementations (review enh. 3 & 4; risk-guarded).
9. **Educational reuse (forward-looking, review enh. 5)** — the same harness (analyzers +
   CSV output) is the foundation later labs reuse for Bode plots, harmonic spectra, impulse/
   step responses, and waterfall plots, rather than maintaining separate visualization code.
   This reinforces Principle IX (Theory → Lab → Primitive → Production). No lab code is built
   in this sub-project; the design simply keeps the interfaces lab-reusable.

## Open questions

- **Implementation sequencing (non-blocking)** — which generators/analyzers land in the first
  cut vs later (e.g. defer `MLSGenerator`, `MultiToneGenerator`, `FFTAnalyzer`). Resolved in
  planning (`/speckit-plan` / `/speckit-tasks`), not a design blocker.
- **Reference-bound sourcing** — for each metric, the analytic truth + named tolerance to
  assert (the `svf-reference` philosophy); enumerated per-metric during `/speckit-clarify` or
  implementation. Not a design blocker.

(The two formerly-open decisions — spectral approach and output form — are now **resolved**
above per the external review; the CPU-terminology concern is resolved by renaming to
"relative execution time.")

## Provenance

- Program vision: `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
  (Phase 1 deliverable; Measurable Engineering principle).
- Constitution Principles X (Measurable Engineering), VI (RT-safety), VIII (host-side), IX
  (progressive layers — educational reuse).
- Existing infra: `tests/support/allocation-sentinel.{h,cpp}`, `tests/support/svf-reference.h`.
- Design session: 2026-06-29, in-session (`/stack-control:design`); operator initially
  deferred Decisions A & B to open issues.
- **External design review (2026-06-28, "Approve with minor revisions"):** recommended
  Goertzel+sine-sweep (Decision A), assertions + optional CSV (Decision B), explicit
  silence/DC/denormal/idle validations, renaming the CPU metric, separated stimulus/analyzer/
  metric concepts with reusable generators, and planning for educational reuse — **all
  incorporated above**.
- Handoff target: `/stack-control:define`.

# Measurement Infrastructure — Design

**Date:** 2026-06-29
**Roadmap item:** `design:feature/measurement-infrastructure` (Phase 1, Digital Fundamentals)
**Phase:** designing → (handoff) `/stack-control:define`
**Status:** Draft — pending operator approval

## Problem domain

Constitution Principle X (Measurable Engineering) requires every acfx effect to be
validated by **objective measurements** — frequency response, impulse response, phase
response, harmonic distortion (THD), latency, CPU usage, memory allocation, and numerical
stability — with listening tests complementary, never a replacement. Today the project has
only fragments: an allocation sentinel (no-heap-allocation invariant) and an ad-hoc
sine-sweep magnitude helper (`tests/support/svf-reference.h::measureMagnitude`) used by the
SVF test. There is no reusable, effect-agnostic **measurement harness** that the existing
effects (SVF, modulated-delay) and every future phase can share to produce the Principle-X
metric suite. This sub-project builds that harness. It is the concrete enabler of Principle X
and a Phase-1 deliverable; the program's later phases depend on it.

Constraints (from the constitution): platform-independent (no platform headers in `core/`);
host-side and hardware-free (Principle VIII); must not impose runtime/audio-path cost
(Principle VI); strict typing, small modules; reusable across all effects via the `Effect`
contract. Scope is **measurement tooling only** — no new effects.

## Solution space

The harness is a set of small, composable **host-side analysis utilities** that drive an
`Effect` (or a per-sample callable) with known stimuli and reduce the output to objective
metrics. It generalizes the existing `measureMagnitude` pattern. The metric suite captured
(all of Principle X — capture over YAGNI; implementation sequencing is a later pass):

- **frequency response** — magnitude (and phase) vs frequency
- **impulse response** — the effect's response to a unit impulse
- **phase response** — phase shift vs frequency
- **harmonic distortion (THD)** — harmonic energy from a pure-tone stimulus
- **latency** — group/processing delay (e.g. impulse-peak or cross-correlation)
- **CPU usage** — host wall-clock per processed block (a relative proxy, not MCU cycles)
- **memory allocation** — reuse the existing allocation sentinel (no-heap-alloc invariant)
- **numerical stability** — NaN/Inf/denormal-free, bounded output under stress

Two design decisions are deliberately left **open** (see Open questions); for each, the
alternatives are recorded here so the solution space has explicit choices.

### Decision A — spectral analysis approach (OPEN)

- **Alternative A1 — Goertzel + sine-sweep (no FFT yet).** Extend the dependency-free
  sine-sweep magnitude with the Goertzel algorithm for specific harmonic bins (THD). Lightest;
  no new dependency; defers a general FFT to Phase 8 (Convolution). No broadband spectrum.
- **Alternative A2 — small first-party FFT primitive.** A radix-2 FFT in `core/primitives/`,
  reused by freq response, THD, and (later) FFT convolution. Reusable, platform-independent;
  more to build/validate now; pulls Phase-8 work forward.
- **Alternative A3 — CPM-pinned FFT dependency** (pffft/KissFFT). Least code; adds a
  third-party dep for test-only tooling; at odds with the first-party-primitive preference.

### Decision B — output form (OPEN)

- **Alternative B1 — doctest assertions vs analytic/reference bounds.** Measurements return
  values; tests assert against bounds (the existing `svf-reference` pattern). CI-gating, no
  artifacts.
- **Alternative B2 — assertions + optional emitted report.** Same assertions plus an opt-in
  text/CSV dump of metric values for human inspection / tracking over time. More infra.

## Decisions (settled now)

1. **Host-side analysis only** — pure test/analysis utilities; **no runtime or audio-path
   cost** (Principles VI, VIII). The harness drives effects offline with synthetic stimuli.
2. **Effect-agnostic + reusable** — works against any `Effect` (and any per-sample callable),
   so SVF, modulated-delay, and all future effects share it. Generalizes `measureMagnitude`.
3. **Platform-independent** — lives in the host test/support layer (and `core/primitives/`
   only if a first-party FFT is chosen); no platform headers leak into `core/`.
4. **Build on existing infra** — reuse `tests/support/allocation-sentinel` for the allocation
   metric and the `svf-reference` magnitude pattern as the seed for frequency response.
5. **Capture the full Principle-X metric suite** (all eight above) in the spec; the
   first-implementation cut / ordering is decided in planning, not by cutting scope here.
6. **Avoid false precision** — assert analytic truths and named tolerance bounds (the
   `svf-reference` philosophy), not fabricated exact magnitudes.

## Open questions

1. **Spectral approach (Decision A)** — Goertzel/sine-sweep vs first-party FFT vs CPM FFT
   dep. To resolve in `/speckit-clarify` (or early implementation). Drives whether a
   `core/primitives/fft` is introduced now and whether broadband spectra are available.
2. **Output form (Decision B)** — assertions-only vs assertions + emitted report. To resolve
   in `/speckit-clarify`. Drives whether a report-emitter utility + its format are specified.
3. **CPU-usage metric meaning** — host wall-clock per block is only a relative proxy (not MCU
   cycles); confirm during clarify whether that proxy is sufficient for Principle X or whether
   it is explicitly scoped as desktop-relative-only.

## Provenance

- Program vision: `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
  (Phase 1 deliverable "measurement infrastructure"; Measurable Engineering principle).
- Constitution Principle X (Measurable Engineering), VI (RT-safety), VIII (host-side testing).
- Existing infra: `tests/support/allocation-sentinel.{h,cpp}`, `tests/support/svf-reference.h`.
- Design session: 2026-06-29, in-session (`/stack-control:design` over the brainstorming
  backend); operator deferred Decisions A and B to open issues.
- Handoff target: `/stack-control:define` (authors the Spec Kit spec from this record).

# Phase 0 Research: Saturation — Composed Production Effect

All decisions below descend from the operator-approved design record
(`docs/superpowers/specs/2026-06-30-saturation-design.md`). The Technical Context in
`plan.md` carries **no** `NEEDS CLARIFICATION` markers; this file records the resolved
decisions and the composition-specific technical choices, plus the items deliberately
deferred to `/speckit-tasks`/tuning (the design's captured open questions).

## Decision 1 — Compose shipped primitives; invent nothing

- **Decision**: Build the effect from the shipped `Waveshaper`
  (`core/primitives/nonlinear/waveshaper.h`, with `ADAAWaveshaper` for the ADAA quality
  mode) and the shipped `SvfPrimitive` (`core/primitives/filters/svf-primitive.h`) for
  pre/post-emphasis and the tone tilt. No new nonlinearity or filter primitive.
- **Rationale**: The Production-Effect stage's thesis (prospectus) is that an effect's
  value is the *composition*; the nonlinearity was fully delivered by the waveshapers
  item, which explicitly deferred "the composed saturation effect" to here.
- **Alternatives considered**: A bespoke saturation nonlinearity (rejected — duplicates
  the shipped primitive and violates one-concept-at-a-time); a new emphasis filter
  (rejected — `SvfPrimitive` already provides the needed shelving/tilt behavior).

## Decision 2 — Two-tier structure: RT-safe kernel + Effect-contract wrapper

- **Decision**: `SaturationCore` is the pure, per-channel, RT-safe DSP composition
  (no thread/parameter-plumbing concerns); `SaturationEffect` wraps it with the
  `Effect` contract — a constexpr `ParameterDescriptor` table (single source of
  parameter truth) and lock-free atomic cross-thread parameter handoff consumed at the
  top of `process()`.
- **Rationale**: Mirrors how `core/effects/svf/svf-effect.h` wraps `SvfPrimitive`;
  keeps the kernel unit-testable and teachable in the lab, and isolates the
  host-integration plumbing at the effect layer. It is also the seam the lab→effect
  graduation needs: the kernel graduates, the wrapper is added at the effects layer.
- **Alternatives considered**: A single monolithic effect class (rejected — mixes RT
  DSP with cross-thread plumbing, harder to test and to teach, and exceeds the module
  size budget).

## Decision 3 — Signal chain and the bias/voicing split

- **Decision**: Per channel:
  `preEmphasis[voicing] → Waveshaper(drive·x + bias, shape[voicing], gainComp on) →
  postDeEmphasis[voicing] → toneTilt(user) → mix·wet + (1−mix)·dry → output`.
  `voicing` bakes the `Waveshaper` shape and the pre/post-emphasis curves; `bias`
  is a **user control** (design Decision 5).
- **Rationale**: Pre/post-emphasis is what makes tape/console/tube spectrally distinct;
  baking it per-voicing while keeping `bias` live resolves the Fork-3/Fork-5 tension
  without contradiction (voicing = fixed spectral/shape identity; bias = live
  asymmetry). The `Waveshaper`'s internal DC-blocker keeps the biased path DC-free.
- **Alternatives considered**: bias baked per-voicing (rejected by operator — wanted a
  live control); no per-voicing emphasis (rejected — voicings collapse together).

## Decision 4 — Anti-aliasing via a `quality` control; oversampled is an unwired seam

- **Decision**: `quality ∈ {naive, adaa}` now, delegating to the `Waveshaper`'s
  evaluation (naive memoryless vs the `ADAAWaveshaper` variant). `quality::oversampled`
  is reserved as a documented but **unwired** seam with a defined, bounded fallback
  until `design:primitive/oversampling` ships. **No roadmap depends-on edge.**
- **Rationale**: ADAA is shipped and independently complete; blocking saturation on the
  unbuilt oversampling sibling would re-shape the roadmap frontier for a purity gain the
  ADAA path substantially delivers. Reserving the seam captures intent without building
  throwaway resampling DSP (one-concept-at-a-time at the boundary).
- **Alternatives considered**: depend-on oversampling and sequence it first (rejected —
  blocks a ready item); a throwaway in-effect resampler now (rejected — duplicates the
  sibling's charter; captured as the seam instead). Constitution V: the reserved tier is
  a **defined bounded behavior**, never a silent partial/aliased fallback.

## Decision 5 — Validation reuses the shipped measurement infrastructure

- **Decision**: The harness + doctest suites drive the shipped Goertzel/THD analyzer +
  sine stimulus to produce: per-voicing harmonic signatures (SC-001), drive→THD
  monotonicity (SC-002), gain-comp loudness band (SC-002), mix dry/wet balance (SC-003),
  naive-vs-ADAA aliasing reduction (SC-004), and RT-safety invariants (SC-005). Named
  tolerances follow the `svf-reference` analytic-bound pattern.
- **Rationale**: The measurement infrastructure was built precisely to validate
  nonlinear harmonic behavior; reusing it keeps acceptance objective (Principle X) and
  consistent with the waveshapers precedent.
- **Alternatives considered**: listening-only validation (rejected — Principle X
  requires objective measurement); a new spectral engine (rejected — out of scope; the
  shipped single-bin analyzer suffices, deeper tooling is the `harmonic-analysis`
  sibling's charter).

## Decision 6 — Lab→effect graduation (first stage-4 walk)

- **Decision**: Author `core/labs/saturation/` (README theory + `SaturationCore` /
  `saturation-voicings` kernel + host-only harness), then `git mv` the kernel into
  `core/effects/saturation/` and add the `SaturationEffect` wrapper there. Extend
  `scripts/check-portability.sh` to cover both new locations.
- **Rationale**: First exercise of the four-stage model's Production-Effect stage; the
  graduation target is an **effect**, not a primitive (a first). The lab persists as
  living documentation driving the graduated effect, per the three-layer precedent.
- **Alternatives considered**: production-effect only, no lab (rejected by operator —
  the composition is a teachable concept and this is the first lab→effect chance).

## Deferred to /speckit-tasks / tuning (design open questions — captured, not blockers)

- Per-voicing emphasis curves + preset numbers (shape/drive/bias defaults, pre/post
  filter frequencies/gains) — a tuning pass validated by the harmonic harness.
- Oversampled quality-arm wiring once the sibling ships (fixed vs selectable factor).
- Multi-stage / cascaded topology (deferred from the single-stage design).
- `tone` control law (single tilt vs shelf pair; center-detent).
- `mix` phase/gain law (equal-power vs linear; dry/wet delay matching against ADAA
  latency).
- Effect-level makeup law atop the `Waveshaper` gain-comp.
- Additional/extreme voicings (e.g. wavefold-based) beyond the four named.
- Harness standardized CSV harmonic-spectra output contract.

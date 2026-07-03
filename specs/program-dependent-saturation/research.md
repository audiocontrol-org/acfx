# Research — Program-Dependent Saturation (Phase 0)

**Feature**: `specs/program-dependent-saturation` | **Date**: 2026-07-03 | **Phase**: 0 (Outline & Research)

All Technical-Context unknowns are resolved below. The four scope/architecture questions were closed in
the 2026-07-03 `/speckit-clarify` session (see `spec.md` `## Clarifications`); the remaining items are
parameterization detail with defensible defaults recorded here and carried as tuning-pass placeholders.

## Decision 1 — Envelope→offset mapping (normalization + depth scaling)

- **Decision**: The `DynamicsModulator` consumes a **0..1 normalized envelope**. The composed
  `EnvelopeFollower` runs in its **decibel domain**; its dB envelope is normalized over a reference
  window (default **−60..0 dBFS**), clamped to `[0,1]`. The modulator's **signed depth `[-1,+1]`**
  scales the curve-shaped normalized value into the **native span** of the target parameter, supplied by
  the caller: `offset = depth · span · curve(norm)`.
- **Rationale**: Level perception is logarithmic, so a dB-domain envelope gives musically even
  modulation; normalizing to 0..1 makes one depth control read consistently across four targets with
  different native ranges (drive dB, bias/tone ±1, mix 0..1). The `EnvelopeFollower` already supplies a
  dB domain with a −120 dBFS floor, so no new detection math is needed.
- **Alternatives considered**: linear-amplitude envelope (simpler but perceptually uneven — loud region
  compressed); raw-dB per-target slope (most "analog" but depth means a different thing per target,
  harder to reason about/test uniformly). Both rejected in clarify.
- **Open (tuning-pass)**: the exact reference-window endpoints (fixed −60..0, per-target, or a user
  parameter) — a Deferred Decision; default −60..0 dBFS.

## Decision 2 — Response-curve law (`ModCurve`)

- **Decision**: `ModCurve {linear, logarithmic, exponential}` shapes the normalized envelope before depth
  scaling. `linear`: `curve(n) = n`. `logarithmic`: a concave map reaching full offset quickly at low
  level then flattening (e.g. `log1p(k·n)/log1p(k)` for a fixed shape constant `k`). `exponential`: a
  convex map staying low until high level then rising fast (e.g. `(exp(k·n)−1)/(exp(k)−1)`). All three
  pass through `(0,0)` and `(1,1)`, are monotonic, bounded, and finite at the endpoints.
- **Rationale**: These are the standard bounded 0..1→0..1 shaping laws; anchoring the endpoints
  guarantees continuity with the linear case and keeps the curve allocation-free and NaN/Inf-free
  (FR-003). The shape constant `k` is a small compile-time/tuning constant.
- **Alternatives considered**: piecewise/lookup curves (unnecessary complexity for a monotone shaping);
  per-target distinct laws (deferred — a single shared law family is the default).
- **Open (tuning-pass)**: the exact `k` and whether the law is shared across targets or per-target — a
  Deferred Decision.

## Decision 3 — Feedback topology tap point

- **Decision**: In `feedBack` topology the detector reads the **previous final output sample `y`** — the
  value returned by `SaturationCore::process()` (post-mix, post-output-trim). Cold start reads a defined
  floor (silence).
- **Rationale**: `SaturationCore` is composed **unchanged** (FR-004/026) and its `process()` returns only
  the final sample; reading that sample is the only tap that requires no modification. It is genuinely
  self-regulating (the realized output drives the modulation) and inherently accounts for the oversampled
  tier's wet-path latency (it reads the realized, already-latent output).
- **Alternatives considered**: a pre-mix / pre-output-trim wet tap (the compressor analog) — rejected
  because it would require exposing an intermediate tap from `SaturationCore`, i.e. modifying it.
- **Stability**: a first-order envelope-driven feedback loop over a bounded, gain-compensated
  nonlinearity settles to a fixed point for steady input; the modulator offsets are clamped (FR-010) and
  the loop reads a one-sample-delayed output, so it cannot algebraically loop. Convergence is asserted in
  the topology test (SC-006).

## Decision 4 — Modulation update rate

- **Decision**: **drive**, **bias**, and **mix** offsets are recomputed and pushed to `SaturationCore`
  **per-sample** (its `setDrive`/`setBias`/`setMix` are cheap scalar assignments — no coefficient
  recompute). The **tone** offset is applied **per-block** (control-rate) via `SaturationCore::setTone`,
  because `setTone` recomputes SVF tilt coefficients (`applyToneTilt`) — running that per-sample would be
  wasteful and MCU-hostile.
- **Rationale**: keeps the per-sample audio path cheap and MCU-viable while preserving true per-sample
  dynamics on the three cheap targets; tone modulation at block rate is musically adequate (tone is a
  slow spectral tilt, not a transient-shaping target).
- **Alternatives considered**: per-sample all four (accepts a per-sample SVF recompute — CPU-hostile);
  control-rate all four (cheapest but coarse, may zipper on fast material). Both rejected in clarify.
- **Open (tuning-pass)**: whether per-block tone needs an explicit ramp/smoother to avoid zippering — a
  Deferred Decision; the block granularity is the default.

## Decision 5 — Static-parameter application seam (cheap fast-path)

- **Decision**: `ProgramDependentSaturationCore` holds the per-target **static base** value and, each
  sample (drive/bias/mix) or block (tone), computes `modulated = clamp(base + offset, nativeRange)` and
  calls the matching `SaturationCore` setter. `setDrive`/`setBias`/`setMix` are already scalar-only in the
  shipped core; `setTone` (SVF recompute) is the one gated to block rate (Decision 4).
- **Rationale**: reuses `SaturationCore`'s existing setters unchanged — no new fast-path API needed on the
  core — because only `setTone` is expensive and it is the one moved off the per-sample path. The clamp
  (FR-010) guarantees a valid parameter and bounded loudness.
- **Alternatives considered**: adding a bulk `setDynamic(...)` to `SaturationCore` — rejected (would
  modify the composed-unchanged core).

## Decision 6 — Zero-depth orthogonality realization

- **Decision**: With every target depth = 0, `DynamicsModulator::modulate` returns `0` for any envelope,
  so `modulated = base + 0 = base`; the pushed setters receive exactly the static values and
  `SaturationCore` runs identically to the standalone static path. The detector/feedback machinery still
  runs but has no effect on the output.
- **Rationale**: makes the orthogonality contract (FR-007, SC-002) a structural identity, not an
  approximation — the only residual difference is whether the detector runs (it does, but its result is
  multiplied by depth 0). Verified byte-for-byte against `SaturationEffect` where the paths coincide.
- **Note**: to keep it exactly byte-for-byte, the core must push the *same* static value the standalone
  `SaturationEffect` would (same denormalization), and must not re-run `setTone` at block rate with a
  different value than the static default when tone depth = 0 (guard: skip the modulated setter call when
  the target depth is 0).

## Decision 7 — Named dynamic-character presets

- **Decision**: `DynamicPreset {none, opto, variMu, tapeComp}` maps to a documented, testable
  modulation-matrix configuration (per-target depths + curves, topology, detector/ballistics). `none` =
  all depths 0 (the static saturator). `opto` ≈ slow, level-smoothed downward drive softening (optical
  compressor character); `variMu` ≈ level-dependent bias + drive push (vari-mu tube character);
  `tapeComp` ≈ drive push + mix/self-compression with tape-ish ballistics. Concrete numbers are a
  tuning-pass placeholder.
- **Rationale**: presets are pure configuration of existing parameters (no new DSP, FR-014); each is a
  fixed vector the preset test asserts (SC-008).
- **Open (tuning-pass)**: the exact per-preset matrix values and whether a preset locks or is an editable
  starting point (US9 override semantics) — a Deferred Decision.

## Decision 8 — Validation approach (reuse shipped infrastructure)

- **Decision**: reuse the shipped measurement (stimulus/response) and harmonic-analysis infrastructure
  (`tests/core/measurement-*`, `measurement-support.h`, harmonic-analysis tooling, `tests/support/
  svf-reference.h`) with analytic truths + named tolerances (the `svf-reference` pattern). Orthogonality
  compares against the shipped `SaturationEffect`; signed-direction uses THD-vs-level; timing reuses the
  `EnvelopeFollower` step-response characterization; feedback convergence asserts a stable fixed point.
- **Rationale**: the measurement infra already measures exactly these properties; no new tooling. RT-safety
  reuses the allocation sentinel (`no-allocation-test.cpp`).

## Constitution re-check (post-research)

No new dependencies, no platform headers, no audio-path allocation/locks introduced; the plan composes
shipped units unchanged and adds one stateless primitive. **Constitution Check remains PASS.**

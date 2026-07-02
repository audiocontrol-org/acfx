# Research — Compressors (Gain Computer + Compressor Effect)

**Feature**: `specs/compressors` | **Date**: 2026-07-02 | **Phase**: 0 (Outline & Research)

The four high-impact spec-level clarifications were resolved in the 2026-07-02 `/speckit-clarify`
session; this document consolidates the resulting technical decisions with rationale and the
alternatives weighed, and characterizes the parameterization/structure decisions parked for
`/speckit-tasks`. Primary reference: J. D. Reiss et al., *"Digital Dynamic Range Compressor Design —
A Tutorial and Analysis"* (feedforward vs feedback, level vs smoothed-gain ballistics, soft-knee
static curves).

## Decision 1 — Stateless GainComputer primitive (the static curve)

- **Decision**: A separate, **stateless** `GainComputer` primitive graduates into
  `core/primitives/dynamics/gain-computer.h`. It exposes `set*` (mode/threshold/ratio/knee/range) and
  a pure `float computeGainDb(float levelDb) const` returning the gain change in dB (≤ 0). No runtime
  state; identical inputs → identical outputs, call-order independent (FR-001).
- **Rationale**: Keeping the curve stateless and free of ballistics makes it a first-class reusable
  `dynamics/` building block (limiters, gates, future multiband/dynamic-EQ compose it) and makes the
  static-curve acceptance test a pure function evaluation against the analytic curve. It is the
  `dynamics/` taxonomy's named "gain computer" inhabitant.
- **Alternatives considered**: inline the curve inside the effect core (effect-only altitude) —
  rejected in design: traps the reusable block. A stateful gain computer that also owns ballistics —
  rejected: conflates the curve (a pure map) with smoothing (a stateful concern) and blocks the
  level/gain ballistics-site choice (Decision 4).

## Decision 2 — Full GainMode catalog with a single unified quadratic knee

- **Decision**: `GainMode { compress, limit, expand, gate }`. Above/below the threshold each mode is a
  piecewise-linear map (compress: slope `1/ratio` above; limit: slope `0` above ≈ ratio → ∞; expand:
  downward slope by ratio below, bounded by `range`; gate: attenuate toward `range` below). The soft
  knee is a **single unified quadratic (C¹-continuous) interpolation** straddling the threshold,
  applied by the same machinery in every mode (above-threshold for compress/limit, below-threshold for
  expand/gate), reducing exactly to the hard corner as `knee → 0` (FR-007, 2026-07-02 clarification).
- **Rationale**: One knee implementation and one continuity test (SC-003) instead of a per-mode
  matrix; the quadratic interpolation is the standard C¹ soft-knee (Reiss eq. for the compressor knee),
  reflected below threshold for the upward-direction modes. Branch-only arithmetic (no transcendental),
  MCU-viable (FR-023).
- **Alternatives considered**: per-mode knee forms (rejected in clarify — multiple formulas to
  characterize/test for marginal voicing gain); a cubic/Hermite knee (rejected — quadratic already
  gives C¹ continuity, cubic adds cost without a required C² property).

## Decision 3 — Detection topology: feedforward + feedback (post-makeup, pre-mix tap)

- **Decision**: `Detection { feedForward, feedBack }`. Feedforward: the detector reads the (optionally
  keyed/filtered) input. Feedback: the detector reads the **previous output sample taken post-makeup
  but pre-mix** — after the gain multiply and makeup gain, before the dry/wet mix and output trim
  (FR-010, 2026-07-02 clarification), so the parallel-mix blend and output trim stay outside the
  detection loop. Cold start uses a defined initial output (silence/floor); the loop must stay stable
  for bounded input.
- **Rationale**: Feedback is the topology behind classic optical/FET "feel" (the detector senses the
  already-compressed signal, giving a gentler, self-regulating curve). Tapping pre-mix keeps the loop's
  effective ratio well-defined (a parallel dry blend inside the loop would fight the detector and
  complicate the fixed point); keeping output trim outside avoids a trivial gain term in the loop. The
  one-sample feedback delay is the natural `env`-from-last-output read, no extra buffer.
- **Alternatives considered**: feedforward-only (rejected in design — drops a whole compressor class);
  feedback tapping the final output y (post-mix/post-output) — rejected in clarify: puts the dry blend
  and trim inside the loop, shifting the effective ratio; feedback tapping pre-makeup — rejected:
  detected level then excludes makeup, so the effective threshold drifts as makeup changes.

## Decision 4 — Ballistics site: level-smoothed + gain-smoothed

- **Decision**: `BallisticsSite { level, gain }`. **level**: the composed `EnvelopeFollower` applies
  attack/release to the detected level; the `GainComputer` maps the already-smoothed level (curve is
  instantaneous). **gain**: the `EnvelopeFollower` runs ~instantaneous (fast/peak), the `GainComputer`
  maps the raw level, and a **second smoother** (a one-pole / second `EnvelopeFollower` on the
  gain-reduction signal) applies attack/release to the gain reduction (FR-011).
- **Rationale**: The two sites voice audibly differently (level smoothing rounds the detector; gain
  smoothing rounds the gain trajectory — the Reiss "smoothed-gain" reference). Reusing
  `EnvelopeFollower` for both the level path and (a second instance for) the gain path avoids new
  smoothing code and inherits the dB-domain, guard, and coefficient machinery.
- **Alternatives considered**: gain-smoothed-only (Reiss reference) or level-smoothed-only (cheapest
  reuse) — both rejected in design in favor of capturing both.

## Decision 5 — Auto-makeup: closed-form at a 0 dBFS reference, off for expand/gate

- **Decision**: Auto-makeup is the closed form `makeupDb = −GainComputer.computeGainDb(0 dBFS)` —
  compensate the gain reduction the curve applies to a full-scale (0 dBFS) signal — recomputed once per
  parameter change (threshold/ratio/knee/mode), no runtime tracking. Auto-makeup is **0 in expand/gate
  modes** (downward-only), so an upward-direction curve never inflates level (FR-016, 2026-07-02
  clarification).
- **Rationale**: Deterministic and analytically testable (SC-009 auto-makeup-unity becomes a closed
  comparison, not a statistical one); RT-safe (computed in the setter path, not per sample); ties
  makeup to the curve so it tracks threshold/ratio automatically.
- **Alternatives considered**: a −20 dBFS program reference (rejected as first-cut default — adds a
  reference-level assumption; can be revisited in a tuning pass); live RMS-tracked compensation
  (rejected — runtime state + a tracking time constant, and it makes the auto-makeup test non-analytic).

## Decision 6 — Compose shipped primitives (EnvelopeFollower / SvfPrimitive / DelayLine)

- **Decision**: `CompressorCore` composes the shipped `EnvelopeFollower` (detection + ballistics + dB
  domain; the level path uses the decibel domain so time constants are level-independent), the shipped
  `SvfPrimitive` in highpass mode for the sidechain filter (cutoff 0 = bypass), and the shipped
  `DelayLine` for lookahead (buffer sized in `prepare()` from the max lookahead; reported latency =
  `round(lookaheadMs·fs)`). The gain application is a per-sample linear multiply folded into the core
  (no separate VCA primitive).
- **Rationale**: Compose-don't-re-derive (Constitution IX / FR-008); the primitives are shipped and
  characterized, so detection ballistics, the −120 dBFS floor, filter guards, and delay semantics are
  inherited rather than re-implemented. Folding gain application into the core keeps the multiply where
  its makeup/mix/output neighbours are.
- **Alternatives considered**: re-implementing detection/filter/delay inside the compressor (rejected —
  duplicates shipped, characterized code); graduating a separate stateful VCA-envelope primitive
  (rejected for this feature — the gain application is a bare multiply; the taxonomy entry stays open
  for a later item that needs a stateful VCA).

## Decision 7 — Host wrapper: the SaturationEffect idiom exactly

- **Decision**: `CompressorEffect` satisfies the `Effect` contract with a single constexpr
  `ParameterDescriptor` table (the ~17-parameter set), a lock-free atomic cross-thread parameter
  handoff (publish on any thread, consume at the top of `process()`), per-channel `CompressorCore`
  state, and a build-time `static_assert` over the descriptor table — copying `SaturationEffect`
  member-for-member. Stereo linking computes one detector value (max across linked channels) and
  applies a common gain.
- **Rationale**: The idiom is shipped and proven (`SvfEffect`/`SaturationEffect`); reusing it verbatim
  keeps the RT-safety and thread-ownership guarantees identical and the review surface familiar.
- **Alternatives considered**: a base-class/vtable effect (rejected — Constitution: no vtable on the
  audio path); immediate coefficient mutation in `setParameter` (rejected — the shipped idiom defers to
  `process()` so coefficient updates stay single-threaded).

## Deferred (parameterization/structure — `/speckit-tasks`, not scope)

- **Effect-wrapper file split (FR-028)**: whether the ~17-parameter `compressor-effect.h` exceeds the
  ~300–500-line budget and splits into a `compressor-parameters.h` (descriptor table + denormalize)
  header. Decided at task time by the realized line count.
- **Ratio/limit parameterization**: `limit` is a distinct `GainMode` (Decision 2); the ratio
  parameter's skew mapping (a log/skewed 1:1…≈20:1 range with ∞ via limit mode) and how `range`/`knee`
  interact for the gate are tuning-pass numeric choices (the descriptor *shapes* are normative; exact
  ranges are placeholders, mirroring `SaturationEffect`'s tuning-pass note).
- **Stereo-link detail**: max-only vs also sum/average, and whether the sidechain HPF / external key is
  per-channel or shared across a linked group.
- **Detector defaults per topology**: whether feedback defaults to a different detector mode/ballistics
  than feedforward given feedback's inherent smoothing.

## Validation approach (reused infrastructure)

- **Decision**: Reuse the shipped measurement stimulus/response tooling (`tests/core/measurement-*`,
  `measurement-support.h`, `tests/support/svf-reference.h`) for static-curve level maps, step-response
  timing, feedback convergence, lookahead latency (sample counting), stereo-link stability, and
  auto-makeup unity, following the `svf-reference` named-tolerance pattern. A no-allocation test
  (allocation sentinel) covers the RT-safety invariant across every config.
- **Rationale**: The infrastructure already provides step/impulse/sine/level-swept stimuli and
  analytic-bound assertion helpers; compressor correctness is a level-map + time-domain property it
  directly measures — no new measurement machinery needed. The `GainComputer` static-curve test is a
  pure function evaluation needing no stimulus at all.
- **Alternatives considered**: bespoke per-test stimulus generation (rejected — duplicates shipped
  infrastructure and diverges the tolerance model).

# Phase 1 Data Model: Saturation — Composed Production Effect

DSP effect — "entities" are the composed processing units and value types, not
persisted records. Fields describe state and parameters, not storage.

## SaturationEffect (the Production Effect)

The host-facing effect. Owns per-channel `SaturationCore` instances and the parameter
plumbing. No base class / vtable on the processing path (the `SvfEffect` idiom).

- **Parameters** (single source of truth — a constexpr `ParameterDescriptor` table):
  | Param | Unit / kind | Range | Default (planning) | Notes |
  |---|---|---|---|---|
  | `drive` | dB, continuous, log skew | e.g. 0…+48 dB | ~0 dB | pre-gain into the nonlinear stage |
  | `voicing` | discrete | {softClip, tape, console, tubePreamp} | softClip | selects shape + pre/post emphasis |
  | `tone` | none, continuous | −1…+1 | 0 (flat) | user tilt (post-de-emphasis) |
  | `mix` | none, continuous | 0…1 | 1 (fully wet) | dry/wet parallel blend |
  | `output` | dB, continuous | e.g. −24…+24 dB | 0 dB | makeup trim |
  | `bias` | none, continuous | −1…+1 | 0 | user asymmetry / even-harmonic dial |
  | `quality` | discrete | {naive, adaa} (+ reserved oversampled) | adaa | anti-aliasing path |

  Exact ranges/skews and per-voicing defaults are a planning/tuning decision (open
  question); the table shape and units are fixed here.

- **State**: per-channel `SaturationCore` (up to a supported channel maximum, matching
  the `SvfEffect` precedent); lock-free atomic pending-parameter store consumed at the
  top of `process()`; sample rate + channel count from `prepare()`.
- **Lifecycle / operations**: `prepare(ProcessContext)` (sample rate + channels; builds
  per-voicing filter coefficients — never on the audio path), `reset()` (clears core
  state), `process(AudioBlock&)` (consumes pending params, then per-channel per-sample
  composition), `setParameter(ParamId, normalized)` (any thread; publishes atomically),
  `parameters()` (the descriptor table).
- **Validation rules**: descriptor table passes the `isValidDescriptor` invariants at
  compile time (as `SvfEffect` does); `process()` allocation-free and lock-free; no
  cross-channel state.

## SaturationCore (the RT-safe composition kernel)

The pure per-channel DSP composition — the lab kernel that graduates into the effects
layer. Independent of thread/parameter-plumbing concerns; unit-testable directly.

- **Composed sub-units** (per channel): `SvfPrimitive preEmphasis`, `Waveshaper shaper`
  (or `ADAAWaveshaper` when `quality == adaa`), `SvfPrimitive postDeEmphasis`,
  `SvfPrimitive toneTilt`.
- **Applied parameter state** (denormalized, set from the effect wrapper): `driveGain`,
  `bias`, current `voicing`, `toneTilt` amount, `mix`, `outputGain`, `quality`.
- **Signal chain** (`process(float x)`):
  `wet = preEmphasis(x)` → `wet = shaper(driveGain·wet + bias)` (shape[voicing], gainComp
  on) → `wet = postDeEmphasis(wet)` → `wet = toneTilt(wet)` →
  `y = mix·wet + (1−mix)·x` → `y = outputGain·y`.
- **Validation rules**: silence-in→silence-out; DC-free output for biased settings
  (the `Waveshaper` DC-blocker); bounded output (no NaN/Inf) under extreme drive; no
  stale state across voicing/quality switch beyond documented filter/DC state.

## Voicing (value/config)

A named character fixing the nonlinear identity and the spectral emphasis.

- **Fields**: `name` (softClip | tape | console | tubePreamp); `shape` (a `Waveshaper`
  `Shape` enum value); `preEmphasis` (SVF mode + cutoff + gain/res); `postDeEmphasis`
  (SVF mode + cutoff + gain/res). Numeric fields are the tuning pass (open question).
- **Relationships**: selected by `SaturationEffect.voicing`; consumed by
  `SaturationCore` to configure its sub-units. Does **not** carry `bias` (that is a
  user parameter — Decision 5).

## QualityMode (value/enum)

The anti-aliasing selection: `naive`, `adaa` (both active), and a reserved
`oversampled` tier that is documented but **unwired** (defined bounded fallback until
the oversampling sibling lands). Maps to which `Waveshaper` evaluation path the core
uses; changing it does not change the parameter surface.

## SaturationLab (documentation + harness)

Theory README + host-only harness. Not shipped in the portable core; produces the
per-voicing harmonic evidence and the naive-vs-ADAA aliasing comparison. Its kernel is
the same code that, post-graduation, lives in the effects layer (verified by the
portability/graduation checks).

## HarmonicEvidence (measurement output)

Per-voicing harmonic signatures + drive→THD series + naive-vs-ADAA aliasing comparison,
produced by the shipped measurement infrastructure. Asserted against named analytic
tolerances; the basis for SC-001…SC-005.

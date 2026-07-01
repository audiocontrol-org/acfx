# Phase 0 Research — Oversampling Primitive

**Feature**: `specs/oversampling/` · **Design record**:
`docs/superpowers/specs/2026-07-01-oversampling-design.md` (operator-approved)

The design record already resolved the load-bearing choices (charter, filter family, factor
mechanism); this document records the technical decisions the plan depends on, with rationale
and alternatives, and pins down the previously-open implementation policies so no
`NEEDS CLARIFICATION` remains for tasking.

## Decision 1 — Anti-alias/anti-image filter family: linear-phase polyphase halfband FIR

- **Decision**: Band-limit with a cascade of 2× linear-phase halfband FIR stages, each polyphase-
  decomposed so its subfilters run at the lower rate.
- **Rationale**: Transparency is the primitive's deliverable — linear phase adds no phase
  distortion, so the oversampled path differs from the naive path *only* in aliasing (exactly
  what `aliasingMeasure` isolates). Halfband FIRs have ~half their taps identically zero, and the
  polyphase form runs each subfilter at the lower rate → far cheaper than a naive interpolated-rate
  FIR. Fixed integer group delay → a clean `latencySamples()` for host PDC. Coefficients are
  analytic and deterministic → provable stopband against a named tolerance (`svf-reference`
  pattern).
- **Alternatives considered**:
  - *IIR polyphase allpass halfband* (HIIR / de Soras): cheaper still, but nonlinear phase +
    frequency-dependent latency make it a worse *first, transparent, provable* primitive. Retained
    as a future "fast" tier (spec FR-025).
  - *Cascaded biquad lowpass* (reuse DaisySP): simplest, but gentle rolloff leaves residual images
    near Nyquist without many stages.

## Decision 2 — Factor mechanism: compile-time template `Oversampler<Factor>`, Factor ∈ {2,4,8}

- **Decision**: The oversampling factor is a template parameter fixed at construction; buffers and
  the stage cascade are sized at compile time. Factor is a power of two built from 2× stages
  (4× = two stages, 8× = three).
- **Rationale**: Matches the codebase's value-member/zero-heap convention (SvfPrimitive,
  Waveshaper), gives embedded targets a statically-known RAM footprint, and yields a branch-free
  per-stage `process()`. A future effect needing a live factor knob holds two `Oversampler<N>`
  instances and switches which it drives — the same dual-path trick `SaturationCore` already uses
  for naive/adaa.
- **Alternatives considered**: *Runtime-selectable factor bounded by a compile-time MAX* — more
  flexible, but larger footprint (buffers sized for MAX) + a runtime stage-count branch, with no
  current client needing live morphing. Deferred (spec FR-025).

## Decision 3 — Per-sample wrap API with a caller-supplied `noexcept` nonlinearity

- **Decision**: The public surface is per-input-sample:
  `template <class Eval> float process(float x, Eval&& evalAtHighRate) noexcept;`. It upsamples `x`
  to `Factor` samples, calls `evalAtHighRate` on each, then decimates to one output sample. The
  callable is invoked at the oversampled rate and must be RT-safe + `noexcept`.
- **Rationale**: Fits `SaturationCore::process(float)`'s per-sample shape directly; keeps the
  primitive nonlinearity-agnostic (composition, Principle IX); templating the callable inlines it
  with no virtual dispatch on the hot path (Principle VI). A `static_assert` on `noexcept(...)`
  documents and enforces the contract at compile time (no `any`/cast — Principle VII).
- **Alternatives considered**: A block API (`processBlock(in[], out[], n, eval)`) — deferred as a
  future variant (spec FR-025); the per-sample form composes into a block trivially and is the
  minimal correct surface now.

## Decision 4 — Rate + latency contract: `oversampledRate()` and integer `latencySamples()`

- **Decision**: `init(float sampleRate)` stores the base rate; `oversampledRate()` returns
  `sampleRate * Factor`; `latencySamples()` returns the integer group delay referred to the base
  rate (the summed per-stage halfband group delays, converted to base-rate samples). `reset()`
  clears the delay lines without discarding configuration.
- **Rationale**: A caller with rate-dependent DSP (e.g. the waveshaper DC-blocker) must
  `prepare()` at the oversampled rate — so the primitive must expose it explicitly rather than
  leaving the caller to recompute it (Principle V: name what's needed, no silent assumption).
  Linear-phase FIR group delay is a known integer, so latency is exact and testable
  (FR-012 / SC-004).
- **Alternatives considered**: Hiding the oversampled rate inside the primitive — rejected; the
  caller's nonlinearity state would silently run at the wrong rate (a Principle V bug factory).

## Decision 5 — Halfband coefficient provenance: committed offline generator emitting the constexpr table

- **Decision**: A small, committed, documented offline generator (a host-side script under the
  lab, e.g. `core/labs/oversampling/tools/`) computes the linear-phase halfband taps and emits the
  `static constexpr` table in `halfband-coefficients.h`; the header records the generator invocation
  and the design parameters (transition band, stopband target) in a comment. The generator is NOT
  on any build/audio path — it is a reproducibility aid; the committed header is the source of
  truth the core compiles.
- **Rationale**: Reviewable, reproducible coefficients beat hand-waved magic numbers (Principle V,
  X). Keeping the generator offline keeps `core/` platform-independent and allocation-free — the
  runtime sees only a `constexpr` array. Resolves the design's open "coefficient provenance"
  question.
- **Alternatives considered**: Hand-authored static table with a citation — acceptable but less
  reproducible; a runtime coefficient computation — rejected (would put filter-design math on
  `init()` needlessly and complicate the constexpr guarantee).

## Decision 6 — Named tolerances (values pinned during tuning; requirements testable now)

- **Decision**: Validation uses explicit named constants (FR-023), with these initial targets the
  tuning pass may tighten:
  - **Stopband rejection** ≥ **80 dB** (initial), transition band placed so the base-rate audio
    band (to ~20 kHz at 48 kHz) is in the passband.
  - **Passband ripple** ≤ **0.1 dB** across the base-rate audio band.
  - **Aliasing reduction margin**: oversampled inharmonic power ≥ **20 dB** below the naive path on
    the driven-nonlinearity stimulus (initial; the real gain is typically far larger).
  - **Latency equality**: exact integer-sample match (no tolerance).
  - **Transparency**: identity round-trip within the passband-ripple bound after latency alignment.
- **Rationale**: The requirements are testable the moment the constant is chosen; leaving the
  *value* to tuning (not the *requirement*) is deliberate deferral, not ambiguity. 80 dB / 0.1 dB
  are conventional transparent-oversampler targets and a safe floor for a first primitive.
- **Alternatives considered**: Per-target (desktop/Daisy/Teensy) tolerance matrix — deferred with
  the multi-quality-tier scope (spec FR-025); one table/one spec now.

## Decision 7 — First-client wiring (closes saturation FR-015)

- **Decision**: `SaturationCore` gains an `Oversampler<K>` member and a dedicated
  `oversampledShaper_` (a `Waveshaper`) prepared at `oversampledRate()`. The `oversampled` case in
  `process()` becomes: pre-emphasis (base) → `oversampler_.process(wet, [&](float s){ return
  oversampledShaper_.process(s); })` → post-de-emphasis (base). `oversampled` is added to
  `SaturationEffect::kQualityLabels` (user-selectable). `saturation-aliasing-test.cpp` flips from
  asserting `oversampled == adaa` to asserting `oversampled` inharmonic power < `naive` by the
  named margin. Default `K = 4` (Decision: initial default; see open question).
- **Rationale**: A primitive with no consumer is unproven; the seam the shipped effect explicitly
  reserved (FR-015) is the natural first client and end-to-end proof. Both shapers stay
  parameter-identical (the existing dual-path discipline), so a quality switch never lands on a
  stale-configured shaper (FR-021).
- **Alternatives considered**: Ship the primitive without a client and wire saturation in a
  follow-up — recorded as the operator's scoping option (spec open question), but the design
  recommends shipping proven, so the plan includes the wiring.

## Decision 8 — Validation reuses the shipped measurement infrastructure

- **Decision**: Reuse `tests/support/measurement/` (Goertzel/THD analyzer, sine stimulus),
  `aliasingMeasure` (from the saturation aliasing test — promote/share as needed),
  `tests/support/svf-reference.h` (analytic-tolerance assertion pattern), and the allocation
  sentinel. New suites: `oversampler-transparency-test`, `oversampler-aliasing-test`,
  `oversampler-response-test`, `oversampler-latency-test`; extend `no-allocation-test`.
- **Rationale**: Principle X (measurable engineering) + no parallel tooling (FR-022). If
  `aliasingMeasure` currently lives inside the saturation test, factor the shared helper into
  `tests/support/measurement/` so both suites use one implementation.
- **Alternatives considered**: New bespoke measurement code — rejected (duplication, Principle X
  intent).

## Open questions carried forward (non-blocking — documented defaults chosen above)

- Default saturation factor 2× vs 4× → **default 4×** (Decision 7); revisit in tuning.
- Per-target tap-count/stopband matrix → **one 80 dB table now** (Decision 6); deferred tiers per
  spec FR-025.
- Coefficient provenance → **committed offline generator** (Decision 5).
- Saturation-wiring scope boundary → **in this feature** (Decision 7), operator may split later.

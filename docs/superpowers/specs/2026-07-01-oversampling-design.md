# Oversampling primitive — design record

- Roadmap item: `design:primitive/oversampling`
- Date: 2026-07-01
- Charter (operator-selected): **generic reusable block-wrapper**
- Anti-alias core (operator-confirmed): **FIR polyphase halfband, cascaded per doubling stage**
- Factor selection (recommended, operator-deferred): **compile-time template `Oversampler<Factor>`**

## Problem domain

Memoryless nonlinearities (waveshapers) generate harmonics above Nyquist. Sampled
at the base rate those harmonics fold back into the baseband as **inharmonic
aliasing** — audible, program-dependent grit that the existing measurement infra
already quantifies (`aliasingMeasure`, Goertzel/THD analyzer).

The shipped saturation effect addresses this today with **ADAA** (antiderivative
anti-aliasing) and reserves an **unwired** `SaturationQuality::oversampled` seam
(`FR-015`, saturation design Decision 4). Selecting `oversampled` currently maps to
the ADAA path as a *defined, bounded interim* — explicitly NOT a real oversampler
(`core/effects/saturation/saturation-core.h:160`, `saturation-voicings.h:34`,
`tests/core/saturation-aliasing-test.cpp:218`).

**Oversampling** is the complementary, general-purpose anti-aliasing strategy: run
the nonlinearity at N× the base sample rate so its harmonics land below the higher
Nyquist, then band-limit and decimate back to the base rate. Two forces make this a
first-class primitive rather than a saturation-local trick:

1. **A consumer already waits.** Closing the `FR-015` seam is the primitive's
   first, concrete client and its end-to-end proof.
2. **Future clients need the same tool.** `design:feature/diode-clippers`,
   tube/console models, and other nonlinear stages in `phase-nonlinear-dsp` will
   all want oversampling. Building it once, reusable, is the roadmap's intent
   (`part-of multi:feature/phase-nonlinear-dsp`).

No reusable oversampler exists in the codebase. The only pinned core DSP library
(DaisySP) provides SVF/biquad filters but **no resampler, decimator, or polyphase
filter** — so the up/down conversion and its anti-alias/anti-image filters are
built here, following the established primitive conventions (thin RT-safe wrappers
under `core/primitives/<category>/`; `init(sr)/reset()/process()`; zero heap/locks
in `process()`; no platform headers; DaisySP wrapped, never reimplemented).

### Constraints carried from the codebase

- **Dual-target reality.** Runs on desktop AND embedded (Daisy: Cortex-M7 @ ~480MHz
  w/ FPU; Teensy 4.x: M7 @ 600MHz w/ FPU). CPU and static RAM footprint both matter.
- **RT-safety (Constitution VI).** No heap allocation or locks in any `process()` /
  audio-callback path. All coefficient/table work at `prepare()`/compile time.
- **Platform-independent core (Constitution IV).** No host-framework or
  embedded-vendor headers in `core/`.
- **Measurement-driven validation.** Assertions use analytic truths + named
  tolerances (the `svf-reference` pattern); reuse the shipped measurement harness.
- **Small modules / strict typing.** Files within ~300–500 lines; no `any`-style
  unchecked casts; value-typed members (the SvfPrimitive/Waveshaper convention).

## Solution space

### Chosen — generic block-wrapper, linear-phase FIR polyphase halfband, compile-time factor

A new primitive category `core/primitives/oversampling/` exposing a generic,
RT-safe, compile-time-factored **block-wrapper** that owns all conversion +
filtering; the caller supplies only the nonlinearity.

**Public surface**

```cpp
template <int Factor>            // Factor in {2, 4, 8} (power of two)
class Oversampler {
public:
    void  init(float sampleRate) noexcept;      // bakes/prepares filter state
    void  reset() noexcept;                      // clears filter delay lines
    float oversampledRate() const noexcept;      // = sampleRate * Factor
    int   latencySamples() const noexcept;       // integer group delay @ base rate

    // Upsample x -> Factor samples, apply evalAtHighRate to each, band-limit +
    // decimate back to one output sample. evalAtHighRate MUST be noexcept + RT-safe.
    template <class Eval>
    float process(float x, Eval&& evalAtHighRate) noexcept;
};
```

**Internals**

- A **cascade of 2× stages**: `HalfbandUpsampler` (1→2) and `HalfbandDownsampler`
  (2→1), each a **linear-phase polyphase halfband FIR**. `Factor=4` = two
  up-stages → eval → two down-stages; `Factor=8` = three each.
- **Halfband efficiency**: ~half the taps of a halfband FIR are exactly zero, and
  the polyphase decomposition runs each subfilter at the lower rate — far cheaper
  than a naive FIR at the interpolated rate.
- **Coefficients** are `static constexpr` tables (design-time computed, windowed
  linear-phase halfband); referenced at `init()`. Provenance is an open question
  (hand-authored table vs committed offline generator) — see below.
- **RT-safe by construction**: all buffers `std::array` sized at compile time from
  `Factor`; zero heap; no locks; branch-free per-stage `process()`.

**Why this shape, for this codebase**

- *Transparency is the deliverable.* Linear-phase FIR adds no phase distortion, so
  the oversampled path differs from the naive path only in aliasing — exactly what
  `aliasingMeasure` isolates. Nonlinear-phase filtering would smear that comparison.
- *Deterministic, reportable latency.* Fixed integer group delay → a clean
  `latencySamples()` for future plugin PDC. IIR latency is frequency-dependent.
- *Static footprint.* Compile-time `Factor` sizes all buffers/tables statically —
  matches the value-member convention (SvfPrimitive, Waveshaper) and gives
  embedded targets a known RAM cost with no dynamic allocation.
- *Reusable by construction.* A future effect wanting a live factor knob holds two
  `Oversampler<N>` instances and switches which one it drives — the same dual-path
  trick `SaturationCore` already uses for naive/adaa.

### Rejected — IIR polyphase allpass halfband (HIIR / de Soras style)

Cheaper per sample and industry-common for oversampling. **Rejected as the primary**
because its nonlinear phase colors the signal and its frequency-dependent latency is
awkward to report and to validate against analytic truth — a worse *first,
transparent, provable* primitive. **Retained as a noted future "fast" tier**,
especially attractive on the tightest embedded budgets.

### Rejected — cascaded biquad lowpass (reuse DaisySP)

Simplest to build from the already-wrapped DaisySP biquad/SVF. **Rejected** because
the gentle rolloff leaves residual images near Nyquist unless many stages are
cascaded, and it lacks the halfband's provable, efficient stopband.

### Rejected — saturation-specific wiring only (no standalone primitive)

Build just enough inside `saturation-core` to make `oversampled` real. **Rejected**
because it contradicts the roadmap's "primitive" charter and leaves future nonlinear
effects to reinvent it — a clone factory.

### Rejected — runtime-selectable factor

A single `Oversampler` with `setFactor()` up to a compile-time MAX. **Deferred**
(captured, not chosen): larger footprint (buffers sized for MAX) and a runtime
stage-count branch in `process()`, with no current client needing live factor
morphing. Recorded as the natural extension when one does.

## Decisions

1. **New primitive category** `core/primitives/oversampling/`, sibling to
   `filters/`, `nonlinear/`, `delays/`, `modulation/`.
2. **Generic block-wrapper** `Oversampler<Factor>` — caller supplies the
   nonlinearity via a `noexcept` callback; the primitive owns up/down conversion +
   anti-alias/anti-image filtering.
3. **Anti-alias core = linear-phase polyphase halfband FIR**, cascaded per 2× stage;
   `static constexpr` coefficient tables baked at compile time.
4. **Compile-time template factor** (`{2,4,8}`) — static buffers, zero heap, known
   embedded RAM footprint.
5. **Rate-aware contract**: `oversampledRate()` lets the client `prepare()` its
   rate-dependent DSP (e.g. the waveshaper DC-blocker) at the high rate;
   `latencySamples()` reports integer group delay at the base rate.
6. **First client = the FR-015 saturation seam.** `SaturationCore` gains an
   `Oversampler<K>` and a dedicated `oversampledShaper_` prepared at
   `oversampledRate()`. Chain: `pre-emphasis (base) → Oversampler{ naive shaper @ N× }
   → post-de-emphasis (base)`. `oversampled` becomes user-selectable in
   `saturation-effect.h`, and `saturation-aliasing-test.cpp` flips from asserting
   `oversampled == adaa` to asserting **measurably lower** inharmonic energy than
   naive. (Whether this wiring ships in *this* spec or a thin follow-up is an open
   scope question — see below — but the design records it as the primitive's proof.)
7. **Validation reuses the shipped measurement infra** (`aliasingMeasure`,
   Goertzel/THD, `svf-reference` tolerance pattern, allocation sentinel):
   - round-trip transparency (identity callback ≈ input delayed by latency, within
     passband ripple tolerance);
   - anti-aliasing rejection vs the naive path on a driven nonlinearity;
   - stopband rejection ≥ / passband ripple ≤ named analytic tolerances;
   - measured group delay == reported `latencySamples()`;
   - cascade correctness for 2×/4×/8×;
   - no-allocation sentinel over `process()`.
8. **Capture over YAGNI**: the items below are recorded as known future scope, to be
   included/cut by the operator's explicit later scoping pass — NOT silently dropped.

### Captured but scoping-deferred (capture-over-YAGNI)

- Block-processing API variant (`processBlock(in[], out[], n, eval)`).
- Multiple filter-quality tiers (fast/standard/steep tap-count tables).
- IIR allpass "fast" tier (the rejected alternative, promoted later).
- Runtime-selectable factor (bounded by a compile-time MAX).
- Arbitrary / non-power-of-two resampling ratios.
- Additional clients: diode clipper, tube/console nonlinear stages.
- Plugin PDC host-latency integration (surfacing `latencySamples()` to the DAW).

## Open questions

- **Default factor** for saturation's oversampled tier — 2× vs 4×? *(tuning pass)*
- **FIR tap counts / stopband spec per target** — one table for all, or
  desktop-vs-Daisy-vs-Teensy variants? *(tuning / per-target budget)*
- **Halfband coefficient provenance** — hand-authored `constexpr` table vs a
  committed, documented offline generator script the build/docs reference?
- **Saturation-wiring scope boundary** — does closing FR-015 land in *this*
  primitive's spec (as its acceptance client) or a thin follow-up item? Design
  recommends including it so the primitive ships proven end-to-end.
- **`Eval` callback ergonomics** — enforce `noexcept` via `static_assert`, and how
  to document the RT-safety contract the caller must uphold.

## Provenance

- Roadmap node: `design:primitive/oversampling`
  (`depends-on multi:feature/phase-digital-fundamentals`,
  `part-of multi:feature/phase-nonlinear-dsp`).
- Reserved seam being closed: saturation `FR-015` —
  `specs/saturation/spec.md:255`, `core/effects/saturation/saturation-core.h:160`,
  `core/effects/saturation/saturation-voicings.h:34`,
  `core/effects/saturation/saturation-effect.h:70`,
  `tests/core/saturation-aliasing-test.cpp:218`.
- Prior design context: `docs/superpowers/specs/2026-06-30-saturation-design.md`
  (Decision 4, quality tiers), `docs/superpowers/specs/2026-06-30-waveshapers-design.md`,
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
  (lists Oversampling in `phase-nonlinear-dsp`).
- Building blocks / conventions: `core/primitives/filters/svf-primitive.h`
  (DaisySP wrap pattern), `core/primitives/nonlinear/waveshaper.h` &
  `adaa-waveshaper.h` (rate-dependent state, dual-path selection),
  `cmake/dependencies.cmake` (DaisySP pin; no resampler upstream).
- Measurement infra to reuse: `tests/support/measurement/` (`analyzers.h`,
  `metrics.h`), `tests/core/saturation-aliasing-test.cpp` (`aliasingMeasure`),
  `tests/support/svf-reference.h` (analytic-tolerance pattern),
  `tests/support/allocation-sentinel.h`.
- Design conversation: superpowers:brainstorming, driven in-session under the
  stack-control design frontend (house rules `stack-control-design-v1`).
  Operator selections: charter = generic block-wrapper; filter = FIR polyphase
  halfband (IIR allpass as noted alternative); factor selection deferred to the
  recommended compile-time template.

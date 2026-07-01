> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Saturation — Lab

A saturation effect applies a musical distortion to audio by composing a nonlinear
waveshaping stage between two frequency-shaping filters, giving different voicings
(Soft Clip, Tape, Console, Tube Preamp) distinct spectral and harmonic character.
This lab demonstrates how a production effect is built from already-shipped primitives
and produces the measured evidence that the composition meets the project's real-time
safety and correctness requirements.

## Theory

### Gain-staging: drive and output makeup

`drive` (`SaturationCore::setDrive`, a *linear* gain — the effect wrapper converts
the user-facing dB parameter with `dbToGain()` before calling it) is the pre-gain
applied immediately ahead of the nonlinear stage: the composed `Waveshaper` /
`ADAAWaveshaper` evaluates `shape(drive·x + bias)` internally (the multiply and
the `bias` offset are realized *inside* the shaper's own `setDrive`/`setBias`, not
pre-multiplied in `SaturationCore::process()` — see the signal-chain comment in
`saturation-core.h`). Driving harder pushes more of the waveform past the
nonlinearity's knee, generating more harmonic energy — this is the single control
that determines how much distortion is applied.

Because harder drive would otherwise make the effect louder as a side effect of
adding distortion, `configureShapers()` unconditionally enables each shaper's
internal gain compensation (`setGainCompensation(true)`) — `SaturationCore`
exposes no toggle for this; it is on by construction so perceived loudness stays
roughly bounded as `drive` rises. `output` (`setOutput`, also linear internally,
dB-denormalized by the wrapper) is a final user makeup trim applied *after* the
dry/wet mix stage, independent of gain compensation — it lets the player restore
or intentionally offset the overall level once the character is dialed in.

### Per-voicing pre- and post-emphasis filtering

Two `SvfPrimitive` stages, `preEmphasis_` and `postDeEmphasis_`, bracket the
nonlinear stage. Both are configured, not processed per-sample: `voicingConfig()`
(`saturation-voicings.h`) fixes each voicing's `EmphasisConfig` (mode, cutoff,
resonance) for both filters, and `SaturationCore::applyEmphasisCoeffs()` pushes
those coefficients into the two `SvfPrimitive` instances whenever the voicing
changes (`setVoicing()`) or the kernel is prepared/reset — never inside
`process()` (Constitution VI: coefficient work is control-thread work).

Frequency-shaping *around* the nonlinearity — rather than only picking a
different transfer curve — is what actually defines a voicing's character,
because a memoryless waveshaper's harmonic output is a function of exactly what
spectral content and level reach it: the **pre-emphasis** filter reshapes the
input before it hits the nonlinearity (e.g. rolling off highs so they don't
generate excess high-order harmonics, or removing sub/rumble so it doesn't eat
headroom), and the **post-de-emphasis** filter reshapes the resulting harmonic
content afterward (e.g. darkening the harmonic series to mimic tape HF loss, or
adding a resonant presence bump). The same nonlinear `Shape` fed through two
different emphasis pairs produces audibly and measurably different voicings — the
per-voicing curves themselves, and why each one was chosen, are documented in
"Voicing: shape plus emphasis curves" below.

### Voicing: shape plus emphasis curves

The four voicings (`saturation-voicings.h::voicingConfig`) each fix a
`Shape` (from the waveshaper primitive) plus a pre-emphasis and
post-de-emphasis `SvfPrimitive` curve pair. Bias remains a separate USER
control (`SaturationCore::setBias`), never baked into the table (design
Decision 5, FR-007) — every voicing is exercised at the same drive/bias so
the difference measured below is attributable to shape + emphasis alone.

- **Soft Clip** — `Shape::softKnee`, near-flat wide lowpass pre/post
  (18 kHz, res 0.1 on both). The least-colored voicing: character comes
  almost entirely from the shape's harder knee (odd harmonics only), not
  from tonal shaping, since the emphasis pair is deliberately transparent
  at audio frequencies.
- **Tape** — `Shape::tanh` (a softer, more rounded odd-only saturator than
  softKnee) through a lowpass pre (9 kHz, res 0.1) that trims extreme highs
  ahead of the nonlinearity, then a **lower**-cutoff lowpass post (4.2 kHz,
  res 0.15) that is the tape HF-loss signature. The post cutoff is
  deliberately pulled down into the measured harmonic band (the 4th-6th
  harmonics of a 1 kHz fundamental land at 4-6 kHz) so the HF loss is
  audible/measurable rather than sitting above the content it is supposed
  to darken — an earlier 8 kHz post cutoff left tape's ratios too close to
  softClip's because the rolloff barely touched anything below Nyquist/6.
  Net: the darkest of the four voicings, dominated by low-order odd
  harmonics with real top-end attenuation.
- **Console** — `Shape::arctan`, the mildest-curvature shape in the
  catalog (lowest THD at a given drive). A gentle highpass pre (120 Hz,
  res 0.1) cleans sub/rumble ("glue"), and a near-flat, slightly resonant
  lowpass post (15 kHz, res 0.2) adds subtle top-end presence without
  darkening. The brightest/cleanest voicing.
- **Tube Preamp** — `Shape::diodeCurve`, the only sign-asymmetric shape in
  the voicing set that still has an analytic antiderivative (ADAA-safe),
  so it is the only voicing with a genuine even-harmonic + DC signature. A
  low-mid lowpass pre (5 kHz, res 0.2) pushes the body of the signal into
  the asymmetry (warmth), and a resonant lowpass post (3.5 kHz, res 0.5)
  adds a presence bump near its cutoff while rolling off the top.

The exact cutoff/resonance numbers above are the tuning-pass OPEN QUESTION
(data-model.md / design open questions): they were chosen and iterated
against the mutual-distinguishability check in
`tests/core/saturation-voicings-test.cpp` (every pairwise spectral distance
>= 0.02 across harmonics 1-6 of a 1 kHz fundamental), not derived from a
reference tape/console/tube measurement.

### Anti-aliasing: naive versus ADAA

A memoryless nonlinearity evaluated sample-by-sample ("naive") generates harmonics
without regard for the Nyquist limit: at high drive and/or high input frequency,
harmonics that land above `fs/2` fold back and alias into the audible band as
inharmonic (non-musical) energy — the same effect measured and documented in the
waveshaping lab. `saturation-core.h` composes both of the shipped `Waveshaper`
backends side by side (`naiveShaper_` and `adaaShaper_`, always kept
parameter-identical — see the `setQuality()` comment) and `quality_`
(`SaturationQuality::naive | adaa | oversampled`) selects which one drives
`process()` for a given channel:

- **`naive`** — the plain `Waveshaper`: exact per-sample evaluation, cheapest, most
  prone to aliasing at high drive/frequency.
- **`adaa`** — the `ADAAWaveshaper`: replaces the per-sample evaluation with a
  first-order antiderivative difference quotient (`(F(u[n]) − F(u[n−1])) /
  (u[n] − u[n−1])`), which band-limits the nonlinearity and measurably reduces
  aliased energy without a separate oversampler (see the waveshaping lab's
  naive-vs-ADAA measurement for the mechanism and typical magnitude of the
  reduction). This is the effect's default quality.
- **`oversampled`** — a **reserved, documented-but-unwired** seam (`FR-015`,
  design doc Decision 4). It has no dependency on the `oversampling` primitive,
  which is still `planned`. Until that sibling ships, selecting it maps to the
  `adaa` path as a defined, bounded interim behavior — a deliberate fallback, not
  a silent or partial one (Constitution V) — and it is not exposed in
  `SaturationEffect::kQualityLabels`, so a user cannot select it; only direct
  `SaturationCore` use (e.g. a test exercising the enum value) can reach that
  branch.

Switching `quality` never disturbs the parameter surface: `drive`, `bias`,
`voicing`, `tone`, `mix`, and `output` are pushed identically to both shapers
regardless of which one is active, so toggling `quality` only changes the
anti-aliasing behavior, not the requested character.

### Dry/wet blending and signal chain composition

The full per-channel chain realized in `SaturationCore::process()` is:

```
wet = preEmphasis[voicing](x)             // SvfPrimitive, per-voicing fixed curve
wet = shaper(drive · wet + bias)          // Waveshaper or ADAAWaveshaper per `quality`;
                                           //   shape[voicing]; internal gain-comp on
wet = postDeEmphasis[voicing](wet)        // SvfPrimitive, per-voicing fixed curve
wet = toneTilt(wet)                       // SvfPrimitive, user 'tone' (-1..+1)
y   = mix · wet + (1 - mix) · x           // parallel dry/wet blend against the input
y   = outputGain · y                      // user makeup trim
```

`mix` is a parallel blend, not a series bypass: the dry tap is the untouched input
`x`, sampled at the same instant the wet path is computed, so `mix = 0` reproduces
the input and `mix = 1` passes only the fully saturated path; intermediate values
linearly cross-fade between them. `tone` is a single post-de-emphasis tilt shared
across all voicings (independent of the per-voicing baked emphasis): negative
values fade from a wide-open lowpass toward a dark ~1.5 kHz cutoff, positive
values fade from a near-DC highpass toward a ~1 kHz cutoff that thins the low end
(`SaturationCore::applyToneTilt()`); the interpolation is log-scaled and flat
(zero resonance) throughout. The exact tone law and the per-voicing emphasis
numbers are both named open questions of the tuning pass (see "Voicing: shape
plus emphasis curves" below and `specs/saturation/data-model.md`).

## Walkthrough

The lab is organized into three portable headers that graduate together to
`core/effects/saturation/` (see "Graduation target" below) plus a host-only
`harness/` directory that stays behind. The walkthrough below describes each
unit and its role in the composition:

### `saturation-core.h`

`SaturationCore` — the RT-safe, allocation-free per-channel composition kernel:
three `SvfPrimitive` instances (pre-emphasis, post-de-emphasis, tone tilt), a
`Waveshaper` and an `ADAAWaveshaper` (parameter-identical, `quality_` selects
between them), and the scalar applied-parameter state (`driveGain_`, `bias_`,
`toneAmount_`, `mix_`, `outputGain_`). `prepare(sampleRate)` and `reset()` do all
coefficient/table work; `process(x)` realizes the signal chain above and is
`noexcept` with no heap allocation, no locks, and bounded work per sample. It has
no knowledge of any host framework, parameter descriptors, or cross-thread
concerns — those belong to the wrapper below.

### `saturation-voicings.h`

The voicing table: `enum class SaturationVoicing` (`softClip`, `tape`, `console`,
`tubePreamp`), `enum class SaturationQuality` (`naive`, `adaa`, and the reserved
`oversampled`), the plain-aggregate `EmphasisConfig` / `VoicingConfig` structs, and
the `constexpr voicingConfig(SaturationVoicing)` selector — an exhaustive switch
returning each voicing's fixed `Shape` plus its pre- and post-emphasis
`EmphasisConfig`. Carries no state and performs no allocation; `bias` is
deliberately absent from `VoicingConfig` because it is a user control, not a baked
voicing property (Decision 5).

### `saturation-effect.h`

`SaturationEffect` — the host-facing `Effect`-contract wrapper composed around
`SaturationCore` (mirrors `core/effects/svf/svf-effect.h` exactly: no base class,
no vtable on the hot path). It owns one `SaturationCore` per channel plus:

- the constexpr `kParams` table of seven `ParameterDescriptor`s (`drive`,
  `voicing`, `tone`, `mix`, `output`, `bias`, `quality`) — the single source of
  parameter truth, validated at compile time via `isValidDescriptor`;
- a lock-free atomic cross-thread parameter handoff (`setParameter()` may be
  called from any thread; `applyPending()` consumes dirty values at the top of
  `process()` on the audio thread, denormalizing dB→linear and index→enum before
  pushing each value into every channel's `SaturationCore`);
- the `prepare(ProcessContext&)/reset()/process(AudioBlock&)/setParameter()`
  surface.

These three headers are the units that graduate — by relocation, not
re-derivation — into `core/effects/saturation/` (see "Graduation target" below).

### Harness (`harness/`)

The host-only harness directory graduates nothing — it stays in the lab
permanently, the same pattern as the waveshaping and SVF labs. Per FR-022 /
`specs/saturation/quickstart.md`, a harness may consume the lab kernel (and, after
graduation, the relocated effect), but nothing portable may ever include a
harness; `scripts/check-portability.sh` enforces the direction. See "Measured
evidence" below for what it emits.

#### Build and run

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_saturation_harness
./build/test/acfx_lab_saturation_harness         # human-readable report
./build/test/acfx_lab_saturation_harness --csv   # machine-readable CSV dump (open question — FR/T023)
```

## Composition: which primitives and why

Per the program prospectus's stance that each effect "documents which primitives
it uses, making the implementation transparent and educational," saturation
composes two already-shipped primitives and invents **no new DSP primitive** (FR-001,
design doc Decision 1):

- **`Waveshaper` / `ADAAWaveshaper`** (`core/primitives/nonlinear/`) — the
  nonlinear stage. `SaturationCore` holds one instance of each, kept
  parameter-identical, and `quality_` selects between them at `process()` time:
  `Waveshaper` is the memoryless-and-stateful wrapper providing drive, bias, shape
  selection, DC-blocking, and optional gain compensation via naive per-sample
  evaluation; `ADAAWaveshaper` is its sibling, replacing naive evaluation with the
  first-order antiderivative difference quotient for anti-aliasing. Neither
  backend is re-implemented here — both are used exactly as shipped.
- **`SvfPrimitive`** (`core/primitives/filters/`) — the state-variable filter used
  for three independent roles per channel: pre-emphasis, post-de-emphasis, and the
  user tone tilt, each a separate instance with its own coefficients computed at
  `prepare()`/`setVoicing()`/`setTone()` time, never in `process()`.

No new nonlinear transfer function and no new filter topology are introduced —
the four voicings' distinctness comes entirely from *which* shipped `Shape` and
*which* `SvfPrimitive` coefficients each one selects (see "Voicing: shape plus
emphasis curves" above), not from any new DSP.

## Measured evidence

All measured evidence in this section comes from running the host-only harness
(`core/labs/saturation/harness/saturation-harness.cpp`, `acfx_lab_saturation_harness`
— see "Harness" above for the build/run commands); none of it is fabricated. The
harness is authored as part of this feature's US5 work; once it lands, running it
populates the tables below in this same section, following the format the
waveshaping lab's harness established (`core/labs/waveshaping/README.md`
"Measured evidence").

Per the design's validation decision (Decision 8) and `quickstart.md`, the
harness reuses the shipped Goertzel/THD analyzer and sine stimulus to produce:

- **Per-voicing harmonic signatures** — for each of the four voicings, the
  absolute Goertzel amplitude at the first several harmonics (f0…6f0) of a
  standard sine stimulus, under the default `quality` and a fixed `drive`/`bias`,
  demonstrating the pairwise-distinct spectral fingerprint each voicing's
  shape+emphasis pair produces (the same mutual-distinguishability property that
  `tests/core/saturation-voicings-test.cpp` asserts numerically, `>= 0.02` across
  harmonics 1–6).
- **Naive vs ADAA aliasing comparison** — for a high-frequency, high-drive
  stimulus (harmonics folding above Nyquist), total/harmonic/inharmonic power for
  the `naive` and `adaa` quality paths and the resulting inharmonic-energy
  reduction, mirroring the waveshaping lab's naive-vs-ADAA table.

Drive→THD monotonicity, the gain-compensation unity-at-low-signal band, and the
`mix` dry/wet balance are asserted by the doctest suites
(`tests/core/saturation-harmonics-test.cpp`, `saturation-effect-test.cpp` — see
`quickstart.md` validation scenarios 3 and 5) rather than printed by the harness;
they are correctness gates, not comparative character evidence, so they live in
the test suite the same way the equivalent waveshaper checks do.

## Graduation target

All three portable headers — the composition kernel (`saturation-core.h`), the
voicing table (`saturation-voicings.h`), and the effect-contract wrapper
(`saturation-effect.h`) — are authored here in the lab, then **graduated** by
`git mv` relocation (not re-derivation) into `core/effects/saturation/`, preserving
git history exactly as the waveshaping primitives' T024 graduation did. This lab
folder persists afterward as the living lab record — this README (theory +
walkthrough + measured evidence) and the host-only `harness/` — now driving the
graduated effect via updated `#include` paths, the same relationship the
waveshaping lab's harness has to `core/primitives/nonlinear/`.

This is the first concept to walk the Theory → Laboratory → **Production Effect**
graduation chain all the way to its final stage: every prior graduation (SVF,
waveshapers) targeted a *primitive* (`core/primitives/...`); saturation is the
first to graduate a composition kernel into the *effects* layer
(`core/effects/saturation/`), exercising Constitution Principle IX's
four-stage model end to end for the first time.

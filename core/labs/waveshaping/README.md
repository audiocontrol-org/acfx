> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Waveshaping — Lab

A waveshaper applies a memoryless nonlinear transfer function to audio samples,
generating harmonic content from a clean input through controlled nonlinearity.
This lab introduced waveshaping to the acfx DSP core and produced the evidence
that the implementation meets the project's RT-safety and correctness
requirements.

## Theory

Memoryless nonlinear transfer functions transform an input signal sample-by-sample
without retaining state. A waveshaper wraps such a function with gain staging
(drive, bias, and gain compensation) and anti-aliasing techniques to produce
musically useful harmonic distortion.

Key concepts covered in this section (to be filled in T022):

- **Memoryless transfer functions** — pure `float → float` mapping with no
  state, forming the kernel of the waveshaper.
- **Harmonic generation** — how nonlinearity creates harmonic content from a
  sinusoidal input, with the harmonic series shaped by the function's symmetry.
- **Symmetry and harmonic content** — symmetric functions generate odd harmonics
  only; asymmetric (biased) functions generate even and odd harmonics.
- **Drive, bias, and gain-compensation staging** — the signal chain through
  which input scaling (drive), DC offset (bias), and output normalization
  (gain compensation) interact to manage distortion amount and output level.
- **DC-blocking** — removing any DC bias the nonlinear function introduces,
  ensuring the output is AC-coupled and free of unwanted offsets.
- **LUT vs closed-form evaluation** — choosing between exact closed-form
  computation (tanh, algebraic, chebyshev polynomials) and table-based
  interpolation for real-time, memory-constrained platforms.
- **ADAA anti-aliasing** — first-order anti-aliased distortion approximation
  using antiderivatives to reduce aliasing artifacts when a closed-form
  antiderivative exists.

## Walkthrough

The waveshaper is organized into four kernel headers and a measurement harness,
all residing in `core/labs/waveshaping/`:

### `waveshaper-shapes.h`

Defines the catalog of memoryless transfer functions and the shape-selection
enum. Intended inhabitants (T009, T013):

- **Closed-form shapes** — `tanhShape`, `hardClip`, `cubicSoftClip`,
  `arctanShape`, `algebraic`, `softKnee`, `chebyshev(n)`, `biasedAsym`,
  `diodeCurve`, `sineFold`, `triangleFold`.
- **Antiderivatives** — closed-form antiderivatives for ADAA (T019), with
  explicit flagging of uncovered shapes.
- **Contracts** — `enum class Shape` and `enum class Evaluation` for runtime
  dispatch.

### `waveshaper.h`

The stateful wrapper `Waveshaper` applying a selected shape with gain staging,
DC-blocking, and gain compensation. Responsible for (T010):

- Parameter control: `setShape()`, `setEvaluation()`, `setDrive()`, `setBias()`,
  `setGainCompensation()`.
- Per-sample processing: `process(sample)` in RT-safe `noexcept` context.
- State management: `init()` and `reset()` for initialization and silence events.
- The internal one-pole DC-blocker (T005) and gain-compensation law (T011).

### `waveshaper-lut.h`

Optional LUT (lookup table) backend for real-time evaluation (T015, T016).
Provides:

- Fixed-size table built during `init()`, never in `process()`.
- Linear interpolation between table entries.
- Bounded edge-clamp policy for out-of-domain inputs.
- Maximum deviation from the closed-form reference, quantified analytically.

### `adaa-waveshaper.h`

Optional anti-aliasing wrapper `ADAAWaveshaper` using first-order antiderivative
approximation (T019, T020). Covers:

- First-order ADAA calculation: `(F(u) − F(u_prev)) / (u − u_prev)` with
  midpoint fallback for small denominators.
- Reduced inharmonic energy (aliasing) compared to naive waveshaping.
- Error handling: raises a descriptive error if the selected shape has no
  documented antiderivative (Constitution V).

### Harness

The host-only measurement harness at `core/labs/waveshaping/harness/` drives
each shape and emits measured evidence (T021, T022):

- **Per-shape harmonic signatures** — the measured harmonic series for each
  transfer function under a standardized drive and sine input, confirming
  agreement with analytic prediction.
- **Naive vs ADAA comparison** — inharmonic energy (aliasing) reduction
  achieved by the ADAA variant relative to naive waveshaping.

## Graduation target

The kernel headers (`waveshaper-shapes.h`, `waveshaper.h`, `waveshaper-lut.h`,
`adaa-waveshaper.h`) are graduated from this lab to `core/primitives/nonlinear/`
in task T024.

The lab folder persists with this README and the host-only harness, serving as
a reference and measurement record for the graduated primitive family.

## Measurements

The host-only harness (implementation in T021, evidence to be recorded in T022)
measures the following:

### Per-shape harmonic signatures

For each transfer function in the catalog, the harness configures the
`Waveshaper` at a standard drive level and samples frequency, then drives a
pure sine tone and measures the resulting harmonic amplitude at each harmonic
via Goertzel analysis or FFT. Evidence is compared against:

- Analytic harmonic prediction for the shape (DC-free output verification,
  harmonic ordering for symmetric vs asymmetric functions).
- The specified harmonic magnitude bounds (assertion of analytic bounds, not
  fabricated magic numbers).

### Naive vs ADAA inharmonic-energy comparison

For shapes with documented antiderivatives (covered for ADAA), the harness
compares the inharmonic energy (out-of-harmonic frequency content / aliasing
artifact power) between the naive `Waveshaper` and `ADAAWaveshaper` variants
under an aggressive input (high drive, high stimulus frequency). The ADAA
variant is asserted to reduce inharmonic energy by a named minimum margin,
quantifying the anti-aliasing benefit.

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

### Memoryless transfer functions

A memoryless transfer function f: R -> R maps each input sample x[n] to output
y[n] = f(x[n]) with no dependence on any past sample. Because identical input
always produces identical output regardless of call history, these functions:

- carry no state and require no initialization;
- form a pure `float -> float` contract in `namespace acfx::shape` (no DC-block,
  drive, bias, or gain compensation live here);
- are the mathematical kernel the stateful wrapper stages around.

### Harmonic generation

When a sinusoidal input x = A·sin(ωt) is passed through a nonlinear function
f(x), Taylor expansion shows that the x^n term generates energy at n·ω:

    f(x) = c₀ + c₁·x + c₂·x² + c₃·x³ + ...

Each coefficient cₙ determines the amplitude of the n-th harmonic. Even-order
terms (x², x⁴, …) add even harmonics and a DC offset; odd-order terms (x³, x⁵,
…) add only odd harmonics. The relative harmonic magnitudes define the timbre of
the distortion.

### Symmetry and harmonic content

Symmetry of the transfer function directly controls which harmonics appear:

- **Odd functions** — f(-u) = -f(u) (e.g. tanh, hardClip, arctan, cubicSoft,
  algebraic, softKnee): contain no even-order Taylor terms, so only odd harmonics
  (f0, 3f0, 5f0, …) appear in the output. Even bins measure as zero.
- **Asymmetric (biased) functions** — (e.g. biasedAsym, diodeCurve): contain
  even-order terms, so both even and odd harmonics appear. DC introduced by the
  asymmetry is removed by the wrapper's DC-blocker.
- **Harmonic-targeting functions** — Chebyshev T_n(u) maps a unit-amplitude sine
  to (predominantly) the n-th harmonic. T_2 targets the 2nd harmonic; T_3 targets
  the 3rd; and so on.
- **Folding shapes** — sineFold and triangleFold reflect the signal back on itself
  at fold boundaries, producing rich, fold-depth-dependent harmonic spectra.

### Wrapper signal chain (research.md Decision 2)

The stateful `Waveshaper` stages four operations around the memoryless kernel:

    u = drive · x + bias          -- input scaling + fixed DC offset
    y = shape(u)                  -- memoryless transfer (closedForm or lut)
    y = dcBlock(y)                -- one-pole HP removes bias-induced DC
    y = gainCompFactor · y        -- optional makeup gain (if enabled)

Bias is applied after drive, so it is a fixed operating-point offset independent
of the drive level. This matches the analog grid-bias convention and keeps the
asymmetry amount independent of how hard the signal is driven.

### DC-blocker (research.md Decision 3)

A one-pole high-pass filter removes the DC component the bias introduces:

    y[n] = x[n] - x[n-1] + R · y[n-1]      R = 0.995

With R = 0.995 at 48 kHz the cutoff is approximately f_c = (1-R)·fs/(2π) ~ 38 Hz
— low enough not to colour the audio band, high enough to track slow DC shifts.
The state variables (xPrev, yPrev) are owned entirely by the wrapper; the
memoryless shape contract in `acfx::shape::*` never sees or touches them.

### LUT vs closed-form evaluation (research.md Decision 4)

Two evaluation backends are provided:

- **closedForm** — calls the shape function directly. Exact, always available,
  the analytic reference against which the LUT is validated.
- **lut** — a 512-point table over u ∈ [-4, +4] built during `init()`, never
  in `process()`. Per-sample cost is a clamp, a multiply, and a linear
  interpolation — ideal for MCU targets where `tanh`/`exp` are expensive.

Linear-interpolation error bound (C2 functions, h = 8/511 ≈ 0.01565):

    |error| ≤ (h² / 8) · max|f''(u)|  ≤  1.0e-3  (kMaxDeviation)

Worst case is the algebraic shape (max|f''| ≈ 0.859), giving ~2.63e-5 — a 38x
safety margin under kMaxDeviation. Out-of-domain inputs clamp to the table edge,
which is a defined, bounded policy (not a silent fallback).

### First-order ADAA anti-aliasing (research.md Decision 5)

When a waveshaper is driven hard at high frequencies its output harmonics can
alias back into the audio band. First-order antiderivative anti-aliasing (ADAA)
replaces the naive per-sample shape evaluation with:

    y[n] = (F(u[n]) − F(u[n−1])) / (u[n] − u[n−1])

where F' = shape (F is the closed-form antiderivative). This difference quotient
band-limits the nonlinearity — the averaged result across each sample interval
suppresses high-order aliased images without requiring a separate oversampler.

When |u[n] − u[n−1]| < ε (the denominator degenerates), the limit of the
difference quotient is the shape at the midpoint: y = shape((u[n]+u[n−1])/2).
This is a defined numerical guard, not a silent data fallback.

`ADAAWaveshaper::setShape()` refuses any shape with no closed-form antiderivative
with a descriptive `std::invalid_argument` (Constitution V) rather than silently
mis-shaping. Shape::biasedAsym is wrapper-realized and therefore naive-only.

### Diode-curve altitude boundary (research.md Decision 6)

`diodeCurve` is a **memoryless transfer curve** — a single-valued, time-invariant
`float -> float` closed form in `namespace acfx::shape`. It is explicitly **not**
a circuit-solved diode clipper.

The stateful circuit primitive — which numerically integrates the diode I-V
characteristic and solves the nonlinear circuit equations at each sample — belongs
to the `phase-circuit-modeling` project's `diode-clippers` item (FR-004).

- **This shape** (`diodeCurve`): gives the *form* of a diode-like soft-asymmetric
  transfer. Memoryless, allocation-free, no circuit state.
- **Circuit primitive** (`phase-circuit-modeling/diode-clippers`): gives the
  *physics* of a diode-loaded circuit via numerical integration. Stateful.

They are not duplicates — they operate at different design altitudes and are
deliberately kept in separate projects.

## Walkthrough

The waveshaper is organized into four kernel headers and a measurement harness.
The kernel headers graduated to `core/primitives/nonlinear/` in T024; the harness
(`core/labs/waveshaping/harness/waveshaping-harness.cpp`) remains here and now
drives the graduated primitive. The walkthrough below describes each header in its
graduated location:

### `waveshaper-shapes.h`

Defines the catalog of memoryless transfer functions, their antiderivatives, and
the `Shape` / `Evaluation` enums.

- **Closed-form shapes** — `tanhShape`, `arctanShape`, `cubicSoftClip`,
  `algebraic`, `hardClip`, `softKnee`, `chebyshev(u,n)`, `biasedAsym`
  (wrapper-local), `diodeCurve`, `sineFold(u,gain)`, `triangleFold(u,gain)`.
- **Antiderivatives** — paired `*Antideriv()` functions for every ADAA-covered
  shape; `hasAntiderivative(Shape)` reports coverage so `ADAAWaveshaper` can
  refuse uncovered shapes.
- **Enums** — `enum class Shape` (11 members) and `enum class Evaluation`
  (`closedForm`, `lut`) for runtime dispatch.

### `waveshaper.h`

The stateful wrapper `Waveshaper` applying a selected shape with gain staging,
DC-blocking, and optional gain compensation. Provides:

- Parameter control: `setShape()`, `setEvaluation()`, `setDrive()`, `setBias()`,
  `setGainCompensation()`.
- `init(sampleRate)` — clears DC-block state, builds the LUT if
  `Evaluation::lut` is selected. The LUT is NEVER built inside `process()`.
- `process(x)` — RT-safe `noexcept`, no allocation, bounded work (FR-020).
- `reset()` — clears DC-block state without touching parameters or the LUT.

Rebuild-on-init semantics: shape or evaluation changes after `init()` take effect
on the NEXT `init()` call. The audio path sees only a cached float multiply for
gain compensation.

### `waveshaper-lut.h`

`WaveshaperLut` — the fixed-size table backend:

- `build(fn)` — fills 512 entries from a callable; called from `Waveshaper::init()`,
  never from `process()`. No heap allocation (`std::array` member).
- `evaluate(u)` — linear interpolation with edge-clamp; RT-safe O(1).
- `kMaxDeviation = 1e-3` — the named interpolation-error bound, verified in tests.

### `adaa-waveshaper.h`

`ADAAWaveshaper` — first-order ADAA variant. Mirrors the `Waveshaper` signal chain
but replaces naive evaluation with the antiderivative difference quotient:

- `setShape()` throws `std::invalid_argument` for any naive-only shape.
- `process(x)` computes the ADAA quotient with midpoint fallback for tiny `du`.
- History state (uPrev_, FPrev_) is cleared by `reset()` with F(0) seeded
  correctly so the very first sample's quotient is exact.

### Harness

The host-only harness at `core/labs/waveshaping/harness/waveshaping-harness.cpp`
drives all 11 shapes through `Waveshaper` and `ADAAWaveshaper` and prints
measured evidence to stdout:

- **Per-shape harmonic signatures** — absolute Goertzel amplitude at each of the
  first 6 harmonics (f0 = 1000 Hz, drive = 2, 4800-sample integer-cycle window,
  2000-sample DC-blocker warmup).
- **Naive vs ADAA aliasing comparison** — inharmonic power for hardClip at 10 kHz,
  drive = 4, 4800 samples (1000 integer cycles).

#### Build and run

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test -j8
./build/test/acfx_lab_waveshaping_harness
```

#### How the tests assert analytic truths

The test suite (`tests/core/waveshaper-*.cpp`) asserts closed-form analytic facts:

- Symmetric shapes (`f(-u) = -f(u)`) measure zero at even harmonic bins.
- Anchor values — `tanh(0) = 0`, `arctan(1) = π/4`, `hardClip(2) = 1` — are
  exact within float precision, not approximate thresholds.
- LUT deviation from closed-form is bounded by `kMaxDeviation = 1e-3`.
- ADAA reduces inharmonic energy by a named minimum margin (currently > 3 dB;
  the harness below shows the real reduction).

No fabricated magic numbers appear in the test suite — every threshold derives
from a closed-form expression or a named analytic bound.

## Measured evidence

All numbers below were captured by running the harness on the built binary;
they are not fabricated.

### Per-shape harmonic signatures

Conditions: sample rate 48 000 Hz, drive 2.0, f0 = 1000 Hz, 4800-sample
integer-cycle window, 2000-sample DC-blocker warmup. Cell values are absolute
Goertzel amplitudes (dimensionless; a unit sine at exactly that frequency = 1.0).

```
  shape          |   f0    |   2f0   |   3f0   |   4f0   |   5f0   |   6f0   |
  -------------- +---------+---------+---------+---------+---------+---------+
  tanh           |  1.1199 |  0.0000 |  0.1892 |  0.0000 |  0.0431 |  0.0000 |
  arctan         |  0.7883 |  0.0000 |  0.1004 |  0.0000 |  0.0230 |  0.0000 |
  cubicSoft      |  0.8285 |  0.0000 |  0.2228 |  0.0000 |  0.0829 |  0.0000 |
  algebraic      |  1.0368 |  0.0000 |  0.1786 |  0.0000 |  0.0493 |  0.0000 |
  hardClip       |  1.2217 |  0.0000 |  0.2795 |  0.0000 |  0.0569 |  0.0000 |
  softKnee       |  1.2046 |  0.0000 |  0.2466 |  0.0000 |  0.0423 |  0.0000 |
  chebyshev T2   |  0.0000 |  4.0093 |  0.0000 |  0.0000 |  0.0000 |  0.0000 |
  biasedAsym     |  1.0704 |  0.1915 |  0.1401 |  0.0672 |  0.0184 |  0.0188 |
  diodeCurve     |  0.6720 |  0.1277 |  0.1135 |  0.0673 |  0.0259 |  0.0354 |
  sineFold       |  0.5702 |  0.0000 |  0.6685 |  0.0000 |  0.1045 |  0.0000 |
  triangleFold   |  0.4399 |  0.0000 |  0.5590 |  0.0000 |  0.1138 |  0.0000 |
```

Interpretation:

- Odd shapes (tanh, arctan, cubicSoft, algebraic, hardClip, softKnee) show
  exactly zero energy at all even bins (2f0, 4f0, 6f0), confirming odd-symmetry.
  Odd harmonics (3f0, 5f0) are non-zero, consistent with analytic prediction.
- Chebyshev T2 shows the 2nd harmonic (2f0 = 4.01) as the dominant component
  and zero everywhere else, confirming that T_2(sin θ) = -cos(2θ) targets 2f0.
  (The amplitude exceeds 1 because T_2 maps [-1,1] to [-1,1] with gain at 2f0;
  drive = 2 pushes outside the Chebyshev design range.)
- biasedAsym and diodeCurve produce both even and odd harmonics, confirming
  that asymmetric shapes inject even-order harmonic content.
- sineFold and triangleFold redistribute energy into odd harmonics strongly
  (3f0 exceeds f0 for both), characteristic of fold-depth = 1.

### Naive vs ADAA aliasing comparison

Conditions: hardClip, 10 000 Hz, drive 4.0, 4800 samples (1000 integer cycles).
Odd harmonics of a 10 kHz input fold above Nyquist (24 kHz) and alias to
non-harmonic bins in the 0–24 kHz range, contributing inharmonic power.

```
  arm    | totalPower | harmPower  | inharmonic
  -------+------------+------------+------------
  naive  |   0.92125  |   0.80534  |   0.115910
  ADAA   |   0.68304  |   0.67112  |   0.011924

  ADAA/naive inharmonic ratio: 0.1029  (9.9 dB reduction)
```

The ADAA variant reduces aliased (inharmonic) energy by 9.9 dB relative to the
naive waveshaper at this stimulus. The harmonic power in the ADAA arm is also
lower (0.671 vs 0.805), reflecting that ADAA suppresses some of the higher-order
harmonics that would otherwise alias rather than simply relocating them.

## Graduation (T024 — completed)

The kernel headers (`waveshaper-shapes.h`, `waveshaper.h`, `waveshaper-lut.h`,
`adaa-waveshaper.h`) graduated from this lab to `core/primitives/nonlinear/`
via `git mv` in T024, preserving full git history.

This lab folder persists as the lab record: README (theory + measured evidence)
and the host-only harness. The harness now includes the graduated primitive's
paths (`primitives/nonlinear/...`) and continues to serve as the measurement
record for the primitive family. No kernel headers remain in this directory.

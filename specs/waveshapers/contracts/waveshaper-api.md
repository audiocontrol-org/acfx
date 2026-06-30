# Contract — Waveshapers public C++ API

The primitive's contract is the C++ surface that effects and tests consume. Names are
the contract; signatures are illustrative (final headers may split across files for the
size budget). All audio-path methods are `noexcept`, allocation-free, and lock-free.

## Namespace and headers

- `namespace acfx` (and `namespace acfx::shape` for the pure functions).
- Pre-graduation include root: `core/labs/waveshaping/…`. Post-graduation:
  `core/primitives/nonlinear/…`. Consumers include the primitive path after graduation.

## Memoryless shape contract (pure, stateless)

```cpp
namespace acfx::shape {
  // Each: pure float→float, no state, no sample rate. Defined over the audio range;
  // bounded for all finite inputs.
  float tanhShape(float u) noexcept;
  float arctanShape(float u) noexcept;
  float cubicSoftClip(float u) noexcept;
  float algebraic(float u) noexcept;        // u / sqrt(1+u*u)
  float hardClip(float u) noexcept;         // clamp(u,-1,1)
  float softKnee(float u) noexcept;         // piecewise, C1 at the knee
  float chebyshev(float u, int n) noexcept; // T_n on [-1,1]
  float diodeCurve(float u) noexcept;       // asymmetric memoryless curve (NOT a circuit model)
  float sineFold(float u, float foldGain) noexcept;
  float triangleFold(float u, float foldGain) noexcept;

  // Antiderivatives for ADAA where one exists (used only by ADAAWaveshaper).
  // A shape without an analytic antiderivative has NO entry here and is naive-only.
  float tanhAntideriv(float u) noexcept;    // log(cosh(u))
  float hardClipAntideriv(float u) noexcept;
  // ... per covered shape
}
```

**Contract invariants** (FR-001/002): no DC-block, drive, bias, gain-comp, or
anti-aliasing in this namespace; identical input → identical output regardless of call
history.

## Stateful wrapper

```cpp
namespace acfx {

enum class Shape : std::uint8_t {
  tanh, arctan, cubicSoft, algebraic, hardClip, softKnee,
  chebyshev, biasedAsym, diodeCurve, sineFold, triangleFold
};
enum class Evaluation : std::uint8_t { closedForm, lut };

class Waveshaper {
public:
  void  init(float sampleRate) noexcept;       // builds LUT if selected; clears DC-block state
  void  setShape(Shape) noexcept;
  void  setEvaluation(Evaluation) noexcept;    // a lut change may rebuild the table on next init
  void  setDrive(float drive) noexcept;        // pre-gain (input scale)
  void  setBias(float bias) noexcept;          // fixed offset applied AFTER drive
  void  setGainCompensation(bool on) noexcept; // auto-makeup toward unity
  void  reset() noexcept;                       // clears DC-block state (keeps LUT)
  float process(float x) noexcept;              // u=drive*x+bias; shape; dcBlock; gainComp

private:
  // selected Shape/Evaluation, drive, bias, gainComp flag+factor,
  // DC-block state (xPrev,yPrev), sampleRate, optional owned LUT
};

} // namespace acfx
```

**Contract invariants**: `process()` is RT-safe (FR-020); DC-blocker is a member
(FR-008); `reset()` clears only DC-block state (FR-009); LUT built in `init()` (FR-011).

## ADAA variant (opt-in, layered)

```cpp
namespace acfx {

enum class AdaaOrder : std::uint8_t { first /*, second (open question) */ };

class ADAAWaveshaper {
public:
  void  init(float sampleRate) noexcept;
  void  setShape(Shape) noexcept;              // must be an antiderivative-covered shape
  void  setAdaaOrder(AdaaOrder) noexcept;
  void  setDrive(float) noexcept;
  void  setBias(float) noexcept;
  void  setGainCompensation(bool) noexcept;
  void  reset() noexcept;                       // clears ADAA history + DC-block state
  float process(float x) noexcept;              // first-order ADAA over the staged signal

private:
  // underlying shape + antiderivative, uPrev/FPrev history, same staging as Waveshaper
};

} // namespace acfx
```

**Contract invariants** (FR-013/014): does not modify `acfx::shape::*` or `Waveshaper`;
selecting an uncovered shape raises a descriptive error (naive-only, Constitution V);
reduces inharmonic energy vs naive for covered shapes (SC-003).

## Validation contract (host-side, tests + harness)

- Per-shape harmonic signatures via the shipped Goertzel/THD analyzer + sine stimulus,
  asserted against analytic harmonic truths + named tolerances (FR-016).
- RT-safety: silence-in→silence-out, DC-free output for asymmetric shapes, no
  NaN/Inf/denormal under stress; zero heap allocation / no locks via the allocation
  sentinel (FR-017, SC-005).
- LUT-vs-closed-form max deviation ≤ named bound at stated resolution (SC-004).
- naive-vs-ADAA inharmonic-energy reduction ≥ named margin for a covered aggressive
  shape (SC-003). Oversampled arm contingent only (FR-018).

## Portability/layering contract

- No platform headers in any kernel/primitive unit; harness host-only; nothing portable
  includes a harness; `core/primitives` never includes `core/effects` (FR-021/022).
- `scripts/check-portability.sh` extended to assert the above for
  `core/labs/waveshaping/**` and `core/primitives/nonlinear/**`.

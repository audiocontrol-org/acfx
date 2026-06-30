#pragma once

#include <cstdint>

namespace acfx {

// ---------------------------------------------------------------------------
// Shape catalog — selects which transfer function the wrapper applies.
// ---------------------------------------------------------------------------
enum class Shape : std::uint8_t {
    tanh,
    arctan,
    cubicSoft,
    algebraic,
    hardClip,
    softKnee,
    chebyshev,
    biasedAsym,
    diodeCurve,
    sineFold,
    triangleFold
};

// ---------------------------------------------------------------------------
// Evaluation backend — closed-form (exact) or table-based (RT-efficient).
// ---------------------------------------------------------------------------
enum class Evaluation : std::uint8_t {
    closedForm,
    lut
};

// ---------------------------------------------------------------------------
// Memoryless shape functions — pure float→float, no state, no sample rate.
//
// Contract invariants (FR-001/002):
//   • No DC-block, drive, bias, gain-comp, or anti-aliasing in this namespace.
//   • Identical input → identical output regardless of call history.
//   • Bounded for all finite inputs.
// ---------------------------------------------------------------------------
namespace shape {

// Hyperbolic tangent soft-saturation; symmetric, bounded to (−1, 1).
float tanhShape(float u) noexcept;

// Arctangent soft-saturation; symmetric, bounded to (−π/2, π/2) then normalised.
float arctanShape(float u) noexcept;

// Cubic soft-clip: piecewise linear-cubic with knee at |u|=1; symmetric.
float cubicSoftClip(float u) noexcept;

// Algebraic saturation: u / sqrt(1 + u*u); symmetric, bounded to (−1, 1).
float algebraic(float u) noexcept;

// Hard clip: clamp(u, −1, 1); symmetric, bounded.
float hardClip(float u) noexcept;

// Soft knee: piecewise, C1-continuous at the knee; symmetric.
float softKnee(float u) noexcept;

// Chebyshev polynomial T_n evaluated on [−1, 1].
// n is the polynomial order (n >= 1).
float chebyshev(float u, int n) noexcept;

// Asymmetric memoryless curve modelling diode-like rectification.
// NOTE: this is a memoryless curve, NOT a circuit-solved diode-clipper model.
float diodeCurve(float u) noexcept;

// Sine-based folding: folds the waveform back on itself using a sine.
float sineFold(float u, float foldGain) noexcept;

// Triangle-wave folding: folds the waveform back on itself using a triangle function.
float triangleFold(float u, float foldGain) noexcept;

// ---------------------------------------------------------------------------
// Antiderivatives — used ONLY by ADAAWaveshaper (T020).
// A shape with no analytic antiderivative has no entry here and is naive-only.
//
// tanhAntideriv and hardClipAntideriv are declared now as representative examples
// from the contract (waveshaper-api.md). The remaining covered-shape antiderivatives
// (e.g. cubicSoftClip, algebraic, ...) are added alongside their shapes in T019.
// ---------------------------------------------------------------------------

// Antiderivative of tanh: log(cosh(u)).
float tanhAntideriv(float u) noexcept;

// Antiderivative of hardClip: piecewise quadratic with clamp at |u|=1.
float hardClipAntideriv(float u) noexcept;

// Remaining antiderivatives for covered shapes are added in T019.

} // namespace shape
} // namespace acfx

#pragma once

#include <algorithm>
#include <cmath>
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
// Closed form: tanh(u).  Odd, monotone, f(0)=0, range(-1,1).
inline float tanhShape(float u) noexcept { return std::tanh(u); }

// Arctangent soft-saturation; symmetric, bounded to (−1, 1).
// Closed form: (2/π)·atan(u).  Odd, monotone, f(0)=0, range (-1,1).
// Anchor: (2/π)·atan(1) = (2/π)·(π/4) = 0.5 exactly.
inline float arctanShape(float u) noexcept {
    constexpr float kTwoOverPi = 0.636619772367581343076f;  // 2/π
    return kTwoOverPi * std::atan(u);
}

// Cubic soft-clip: piecewise linear-cubic with knee at |u|=1; symmetric.
// Closed form:
//   |u| <= 1 : f(u) = u - u^3/3
//   u  >  1  : f(u) = +2/3
//   u  < -1  : f(u) = -2/3
// Odd, f(0)=0, slope 1 at 0, C1-continuous at |u|=1
// (f(1)=2/3, f'(1)=0 = slope of the hard-limit region), bounded for all finite inputs.
inline float cubicSoftClip(float u) noexcept {
    if (u > 1.0f)  return  2.0f / 3.0f;
    if (u < -1.0f) return -2.0f / 3.0f;
    return u - (u * u * u) / 3.0f;
}

// Algebraic saturation: u / sqrt(1 + u*u); symmetric, bounded to (−1, 1).
// Closed form: u/√(1+u²).  Odd, monotone, f(0)=0, range (-1,1).
// Anchors: f(1)=1/√2, f(3)=3/√10.
inline float algebraic(float u) noexcept {
    return u / std::sqrt(1.0f + u * u);
}

// Hard clip: clamp(u, −1, 1); symmetric, bounded.
// Closed form: std::clamp(u, -1, 1).  Odd, exact ±1 saturation, f(0)=0.
inline float hardClip(float u) noexcept { return std::clamp(u, -1.0f, 1.0f); }

// Soft knee: odd, C1-continuous everywhere, bounded to [−1, 1].
// Closed form (on |u|, then sign-restored):
//   |u| <= kKnee (0.5)            : f = u                      (linear core, slope 1)
//   kKnee < |u| < kFlat (1.5)     : f = -0.5·s² + s + 0.5      (s = |u|−kKnee)
//   |u| >= kFlat                  : f = ±1                     (flat limit)
// The quadratic knee is the unique Hermite segment with value/slope (0.5,1) at
// |u|=0.5 and (1,0) at |u|=1.5, so f and f' are continuous at both seams (C1) and
// f' = 1−s decreases monotonically 1→0 across the knee (monotone non-decreasing).
inline float softKnee(float u) noexcept {
    constexpr float kKnee = 0.5f;  // linear-core boundary
    constexpr float kFlat = 1.5f;  // saturation boundary
    const float a = std::abs(u);
    float mag;
    if (a <= kKnee) {
        mag = a;
    } else if (a >= kFlat) {
        mag = 1.0f;
    } else {
        const float s = a - kKnee;
        mag = -0.5f * s * s + s + 0.5f;
    }
    return std::copysign(mag, u);
}

// Chebyshev polynomial T_n(u) via the stable recurrence
//   T_0 = 1, T_1 = u, T_k = 2u·T_{k-1} − T_{k-2}.
// Iterative, bounded loop on n.  For |u| <= 1, T_n stays in [−1, 1].
// Identities: T_n(1)=1, T_n(−1)=(−1)^n, T_n(cos θ)=cos(nθ).
// Degenerate orders are clamped to T_0: n <= 0 returns 1 (documented; n >= 0 is
// the intended domain).
inline float chebyshev(float u, int n) noexcept {
    if (n <= 0) return 1.0f;  // T_0
    if (n == 1) return u;     // T_1
    float tkm2 = 1.0f;        // T_{k-2}, starts at T_0
    float tkm1 = u;           // T_{k-1}, starts at T_1
    float tk   = u;
    for (int k = 2; k <= n; ++k) {
        tk   = 2.0f * u * tkm1 - tkm2;
        tkm2 = tkm1;
        tkm1 = tk;
    }
    return tk;
}

// Asymmetric memoryless diode-style transfer CURVE (research Decision 6).
// NOT a circuit-solved diode clipper (numerically integrated I–V, stateful) —
// that distinct item belongs to phase-circuit-modeling's diode-clippers.
// Closed form (sign-asymmetric tanh): the forward branch (u>=0) conducts softly
// to +1; the reverse branch (u<0) is strongly attenuated toward −0.2.  Result is
// monotone non-decreasing, continuous at 0, NOT odd (so it injects even
// harmonics + DC), and bounded to (−0.2, 1) ⊂ [−2, 2].
inline float diodeCurve(float u) noexcept {
    constexpr float kReverse = 0.2f;  // reverse-branch attenuation
    if (u >= 0.0f) return std::tanh(u);
    return kReverse * std::tanh(u);
}

// Sine-based wavefolding.  Closed form: sin(foldGain · u · π/2).
// Inherently bounded to [−1, 1] for every foldGain; increasing foldGain folds the
// waveform back on itself more times (richer, fold-dependent harmonics with sign
// reversals).  foldGain is the fold depth; at foldGain=1, u=1 reaches full scale.
inline float sineFold(float u, float foldGain) noexcept {
    constexpr float kHalfPi = 1.570796326794896619231f;  // π/2
    return std::sin(foldGain * u * kHalfPi);
}

// Reflective triangle wavefolding.  Closed form: (2/π)·asin(sin(foldGain·u·π/2)).
// asin∘sin is exactly a triangle wave (piecewise-linear), here scaled to span
// [−1, 1].  The slope magnitude between fold points equals foldGain (so the
// central-difference slope stays bounded by foldGain), the output is bounded to
// [−1, 1] for every foldGain, and larger foldGain yields more folds / sign
// reversals.  The sin output is clamped to [−1, 1] to keep asin finite against
// float rounding at large arguments.
inline float triangleFold(float u, float foldGain) noexcept {
    constexpr float kHalfPi    = 1.570796326794896619231f;  // π/2
    constexpr float kTwoOverPi = 0.636619772367581343076f;  // 2/π
    const float s = std::clamp(std::sin(foldGain * u * kHalfPi), -1.0f, 1.0f);
    return kTwoOverPi * std::asin(s);
}

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

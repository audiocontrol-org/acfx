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

// Asymmetric memoryless diode-style transfer CURVE (FR-004 / research Decision 6).
// NOT a circuit-solved diode clipper (numerically integrated I–V, stateful) —
// that distinct item belongs to phase-circuit-modeling's diode-clippers.
// See core/labs/waveshaping/README.md for the altitude boundary explanation.
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
// Antiderivatives F (where F'(u) = shape(u) EXACTLY) — used ONLY by
// ADAAWaveshaper (T020) for first-order antiderivative anti-aliasing
//   y[n] = (F(u[n]) − F(u[n−1])) / (u[n] − u[n−1]).
// The additive constant cancels in that difference quotient, but each F below is
// the TRUE antiderivative of its shape and is piecewise-CONTINUOUS (pieces joined
// so F is C0; for these shapes the derivative — the shape — is continuous so F is
// also C1). A shape with no analytic antiderivative has NO entry here and is
// naive-only; ADAAWaveshaper refuses it (Constitution V).
//
// COVERED (antiderivative provided): tanh, arctan, cubicSoftClip, algebraic,
//   hardClip, softKnee, chebyshev, diodeCurve, sineFold, triangleFold.
// UNCOVERED (no pure shape function, hence no antiderivative): Shape::biasedAsym
//   (wrapper-realized via bias; see waveshaper.h). hasAntiderivative() reports the
//   covered set so the ADAA type can refuse the rest.
// ---------------------------------------------------------------------------

namespace detail {

// Numerically-stable log(cosh(u)) = |u| − log(2) + log1p(exp(−2|u|)).
// Avoids the cosh overflow of the naive std::log(std::cosh(u)) for large |u|
// while remaining exact at u=0 (→ 0) and differentiating to tanh(u) everywhere.
inline float logCosh(float u) noexcept {
    constexpr float kLog2 = 0.6931471805599453094f;  // log(2)
    const float a = std::abs(u);
    return a - kLog2 + std::log1p(std::exp(-2.0f * a));
}

} // namespace detail

// Antiderivative of tanh:  F(u) = log(cosh(u)).   F'(u) = tanh(u).
// Continuous and C1 on all of R; F(0)=0 (stable form, no cosh overflow).
inline float tanhAntideriv(float u) noexcept { return detail::logCosh(u); }

// Antiderivative of hardClip=clamp(u,−1,1):
//   |u| <= 1 : F = u²/2
//   |u|  > 1 : F = |u| − 1/2
// F'(u) = u (core) / sign(u) (clamp) = clamp(u,−1,1).  Continuous at |u|=1:
// both pieces equal 1/2 there (C1: derivative ±1 matches).
inline float hardClipAntideriv(float u) noexcept {
    const float a = std::abs(u);
    if (a <= 1.0f) return 0.5f * u * u;
    return a - 0.5f;
}

// Antiderivative of cubicSoftClip (u − u³/3 core; ±2/3 beyond):
//   |u| <= 1 : F = u²/2 − u⁴/12
//   u  >  1  : F = 5/12 + (2/3)(u − 1)
//   u  < −1  : F = 5/12 + (2/3)(−u − 1)         [= 5/12 + (2/3)(|u|−1)]
// Even (antiderivative of the odd shape). F'(u) = cubicSoftClip(u).  Continuous
// at |u|=1: core gives 1/2 − 1/12 = 5/12, matching the linear pieces (C1: slope
// ±2/3 matches the cubic-side slope u−u³/3 = ±2/3 at |u|=1).
inline float cubicSoftClipAntideriv(float u) noexcept {
    constexpr float kCore1 = 5.0f / 12.0f;   // F(±1) on the core
    constexpr float kSlope = 2.0f / 3.0f;    // |F'| beyond the knee
    if (u > 1.0f)  return kCore1 + kSlope * (u - 1.0f);
    if (u < -1.0f) return kCore1 + kSlope * (-u - 1.0f);
    const float u2 = u * u;
    return 0.5f * u2 - (u2 * u2) / 12.0f;
}

// Antiderivative of arctanShape=(2/π)atan(u):
//   F(u) = (2/π)( u·atan(u) − ½·log(1+u²) ).   F'(u) = (2/π)atan(u).
// Continuous and C1 on all of R; F(0)=0.
inline float arctanAntideriv(float u) noexcept {
    constexpr float kTwoOverPi = 0.636619772367581343076f;  // 2/π
    return kTwoOverPi * (u * std::atan(u) - 0.5f * std::log1p(u * u));
}

// Antiderivative of algebraic=u/√(1+u²):
//   F(u) = √(1+u²).   F'(u) = u/√(1+u²).   Continuous and C1 on all of R.
inline float algebraicAntideriv(float u) noexcept {
    return std::sqrt(1.0f + u * u);
}

// Antiderivative of softKnee (linear core |u|<=0.5; quadratic knee; flat ±1).
// Even (antiderivative of the odd shape), expressed on a=|u|; F'(u)=softKnee(u).
//   a <= 0.5      : F = a²/2
//   0.5 < a < 1.5 : F = 1/8 + (−S³/6 + S²/2 + S/2),  S = a − 0.5
//   a >= 1.5      : F = 23/24 + (a − 1.5)
// Continuous/C1 at both seams: a=0.5 → 1/8 (=0.125); a=1.5 → 1/8 + 5/6 = 23/24.
inline float softKneeAntideriv(float u) noexcept {
    constexpr float kKnee   = 0.5f;
    constexpr float kFlat   = 1.5f;
    constexpr float kAtKnee = 0.125f;        // F(|u|=0.5) = (0.5)²/2 = 1/8
    constexpr float kAtFlat = 23.0f / 24.0f; // F(|u|=1.5) = 1/8 + 5/6 = 23/24
    const float a = std::abs(u);
    if (a <= kKnee) return 0.5f * a * a;
    if (a >= kFlat) return kAtFlat + (a - kFlat);
    const float s = a - kKnee;
    return kAtKnee + (-(s * s * s) / 6.0f + 0.5f * s * s + 0.5f * s);
}

// Antiderivative of chebyshev T_n:
//   n <= 0 : ∫T_0 = u
//   n == 1 : ∫T_1 = u²/2
//   n >= 2 : ½( T_{n+1}(u)/(n+1) − T_{n-1}(u)/(n-1) )
// (standard ∫T_n identity). T_{n±1} evaluated via the chebyshev() recurrence.
// Polynomial in u, hence continuous/C1; F'(u) = T_n(u) = chebyshev(u,n).
inline float chebyshevAntideriv(float u, int n) noexcept {
    if (n <= 0) return u;
    if (n == 1) return 0.5f * u * u;
    const float tnp1 = chebyshev(u, n + 1);
    const float tnm1 = chebyshev(u, n - 1);
    return 0.5f * (tnp1 / static_cast<float>(n + 1)
                 - tnm1 / static_cast<float>(n - 1));
}

// Antiderivative of sineFold = sin(foldGain·u·π/2):
//   F(u) = −cos(foldGain·u·π/2) / (foldGain·π/2).   F'(u) = sineFold(u, foldGain).
// Continuous/C1 in u. foldGain→0 makes the shape ≡ 0 (constant antiderivative);
// guarded to return 0 to avoid a 0/0 (no NaN, RT-safe).
inline float sineFoldAntideriv(float u, float foldGain) noexcept {
    constexpr float kHalfPi = 1.570796326794896619231f;  // π/2
    const float k = foldGain * kHalfPi;
    if (std::abs(k) < 1.0e-12f) return 0.0f;             // degenerate flat shape
    return -std::cos(k * u) / k;
}

// Antiderivative of triangleFold = (2/π)·asin(sin(foldGain·u·π/2)) (a unit
// triangle wave). Writing φ = k·u with k = foldGain·π/2, the triangle wave has
// zero mean, so its integral G(φ) is itself 2π-periodic and bounded — joining the
// pieces over one period keeps F continuous with no secular growth:
//   φ in [0, π/2]      : G = φ²/π
//   φ in [π/2, 3π/2]   : G = 2φ − φ²/π − π/2
//   φ in [3π/2, 2π]    : G = φ²/π − 4φ + 4π
// F(u) = G(φ mod 2π)/k.  G(0)=G(2π)=0 so the wrap is seamless → F is continuous;
// F'(u) = G'(φ)·k/k = triangleFold(u, foldGain).  G ranges [0, π/2] (bounded).
// foldGain→0 → constant antiderivative, guarded to 0.
inline float triangleFoldAntideriv(float u, float foldGain) noexcept {
    constexpr float kPi      = 3.14159265358979323846f;
    constexpr float kHalfPi  = 1.570796326794896619231f;  // π/2
    constexpr float kTwoPi   = 6.28318530717958647692f;    // 2π
    constexpr float kThreeHalfPi = 4.712388980384689857694f; // 3π/2
    const float k = foldGain * kHalfPi;
    if (std::abs(k) < 1.0e-12f) return 0.0f;              // degenerate flat shape
    float phi = k * u;
    phi -= kTwoPi * std::floor(phi / kTwoPi);             // reduce to [0, 2π)
    float g;
    if (phi <= kHalfPi) {
        g = phi * phi / kPi;
    } else if (phi <= kThreeHalfPi) {
        g = 2.0f * phi - phi * phi / kPi - kHalfPi;
    } else {
        g = phi * phi / kPi - 4.0f * phi + 4.0f * kPi;
    }
    return g / k;
}

// Antiderivative of the asymmetric diodeCurve (tanh forward, 0.2·tanh reverse):
//   u >= 0 : F = log(cosh(u))
//   u <  0 : F = 0.2·log(cosh(u))
// Both pieces are 0 at u=0 → continuous (and C1: both derivatives are 0 there).
// F'(u) = diodeCurve(u).
inline float diodeCurveAntideriv(float u) noexcept {
    constexpr float kReverse = 0.2f;  // matches diodeCurve's reverse attenuation
    if (u >= 0.0f) return detail::logCosh(u);
    return kReverse * detail::logCosh(u);
}

// ---------------------------------------------------------------------------
// Antiderivative-coverage predicate (FR-013/014, Constitution V).
// Returns true iff the shape has an analytic antiderivative above; false for
// shapes that are naive-only. ADAAWaveshaper::setShape (T020) uses this to refuse
// uncovered shapes with a descriptive error rather than silently mis-shaping.
//   Covered  : tanh, arctan, cubicSoft, algebraic, hardClip, softKnee,
//              chebyshev, diodeCurve, sineFold, triangleFold.
//   Uncovered: biasedAsym (wrapper-realized; no pure acfx::shape function).
// ---------------------------------------------------------------------------
inline bool hasAntiderivative(Shape s) noexcept {
    switch (s) {
        case Shape::tanh:
        case Shape::arctan:
        case Shape::cubicSoft:
        case Shape::algebraic:
        case Shape::hardClip:
        case Shape::softKnee:
        case Shape::chebyshev:
        case Shape::diodeCurve:
        case Shape::sineFold:
        case Shape::triangleFold:
            return true;
        case Shape::biasedAsym:
            return false;
    }
    return false;  // unreachable for the closed enum; conservative default
}

} // namespace shape
} // namespace acfx

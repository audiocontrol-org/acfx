// waveshaper-shapes-test.cpp
// T012 — US2 per-shape analytic correctness: range, symmetry class, monotonicity,
// and closed-form / anchor-point assertions for every catalog pure function in
// namespace acfx::shape (research.md Decision 1).
//
// Expected RED: tanhShape / cubicSoftClip / hardClip are INLINE (compile+link).
// arctanShape / algebraic / softKnee / chebyshev / diodeCurve / sineFold /
// triangleFold are DECLARED-ONLY — link fails with "undefined reference" on
// those 7 symbols.  That is the correct RED state.  Compile errors in this TU
// must be fixed before the RED is accepted.
//
// Note: biasedAsym has NO pure acfx::shape function — it is realized at the
// wrapper level via bias (research Decision 1) and is therefore NOT tested here.
//
// Tolerance rationale:
//   kEps = 1e-5f — float32 has ~7 significant digits; values near 1.0 have
//     ULP ≈ 1.2e-7; 1e-5 gives several orders of safety without being loose.
//   kSlopeEps = 1e-3f — central-difference error for f'(0) with h=1e-4 is
//     O(h^2 * |f'''|); for these shapes |f'''(0)| <= 2, error < 2e-8.
//   kC1Tol = 0.5f (per 0.01-unit sweep step) — smooth shape: |Δf'| ≤ M*h,
//     M ≤ 2 → |Δf'| ≤ 0.02 << 0.5.  A hard corner (e.g. hardClip at |u|=1)
//     would produce |Δf'| ≈ 1.0 >> 0.5, correctly flagging non-C1 behaviour.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "labs/waveshaping/waveshaper-shapes.h"

using namespace acfx::shape;

namespace {

constexpr float kPi      = 3.14159265358979323846f;
constexpr float kEps     = 1.0e-5f;
constexpr float kSlopeEps = 1.0e-3f;

} // namespace

// ===========================================================================
// tanhShape: tanh(u).  Odd, monotone, range(-1,1), f(0)=0.
// ===========================================================================

TEST_CASE("tanhShape: odd symmetry, f(0)=0, range (-1,1)") {
    CHECK(tanhShape(0.0f) == doctest::Approx(0.0f).epsilon(kEps));

    const float us[] = {0.1f, 0.5f, 1.0f, 2.0f, 5.0f};
    for (float u : us) {
        CHECK(tanhShape(-u) == doctest::Approx(-tanhShape(u)).epsilon(kEps));
        CHECK(tanhShape(u)  >  -1.0f);
        CHECK(tanhShape(u)  <   1.0f);
        CHECK(tanhShape(-u) >  -1.0f);
        CHECK(tanhShape(-u) <   1.0f);
    }
}

TEST_CASE("tanhShape: monotone increasing on [-3, 3]") {
    float prev = tanhShape(-3.0f);
    for (int i = -29; i <= 30; ++i) {
        const float cur = tanhShape(static_cast<float>(i) * 0.1f);
        CHECK(cur > prev);
        prev = cur;
    }
}

// ===========================================================================
// arctanShape: (2/pi)*atan(u).  Odd, monotone, range(-1,1), f(0)=0.
// Anchor: arctanShape(1) = (2/pi)*atan(1) = (2/pi)*(pi/4) = 0.5 — exact.
// ===========================================================================

TEST_CASE("arctanShape: f(0)=0, closed-form anchor at u=1, odd, range (-1,1)") {
    CHECK(arctanShape(0.0f) == doctest::Approx(0.0f).epsilon(kEps));

    // (2/pi)*atan(1) = (2/pi)*(pi/4) = 0.5 — analytic exact value
    CHECK(arctanShape(1.0f) == doctest::Approx(0.5f).epsilon(kEps));

    const float us[] = {0.1f, 0.5f, 1.0f, 2.0f, 5.0f};
    for (float u : us) {
        CHECK(arctanShape(-u) == doctest::Approx(-arctanShape(u)).epsilon(kEps));
        CHECK(arctanShape(u)  >  -1.0f);
        CHECK(arctanShape(u)  <   1.0f);
    }
}

TEST_CASE("arctanShape: monotone increasing on [-3, 3]") {
    float prev = arctanShape(-3.0f);
    for (int i = -29; i <= 30; ++i) {
        const float cur = arctanShape(static_cast<float>(i) * 0.1f);
        CHECK(cur > prev);
        prev = cur;
    }
}

// ===========================================================================
// cubicSoftClip: u - u^3/3 (|u|<=1), +-2/3 beyond.
// Anchor values from the closed form; C1-continuous at |u|=1 (f'(1-)=0).
// cubicSoftClip(0.5) = 0.5 - (0.5)^3/3 = 0.5 - 1/24 = 11/24.
// cubicSoftClip(1.0) = 1 - 1/3 = 2/3 (knee; slope from cubic side = 0).
// ===========================================================================

TEST_CASE("cubicSoftClip: f(0)=0, odd, closed-form anchors, hard-limit to +-2/3") {
    CHECK(cubicSoftClip(0.0f) == doctest::Approx(0.0f).epsilon(kEps));

    // Anchor: 0.5 - (0.125)/3 = 0.5 - 0.04167 = 11/24
    CHECK(cubicSoftClip(0.5f) == doctest::Approx(11.0f / 24.0f).epsilon(kEps));

    // Knee: f(1) = 1 - 1/3 = 2/3 exactly; f(-1) = -2/3 (odd)
    CHECK(cubicSoftClip( 1.0f) == doctest::Approx( 2.0f / 3.0f).epsilon(kEps));
    CHECK(cubicSoftClip(-1.0f) == doctest::Approx(-2.0f / 3.0f).epsilon(kEps));

    // Odd symmetry
    const float us[] = {0.1f, 0.5f, 1.0f, 1.5f, 3.0f};
    for (float u : us) {
        CHECK(cubicSoftClip(-u) == doctest::Approx(-cubicSoftClip(u)).epsilon(kEps));
    }

    // Hard-limited beyond |u|=1
    const float overs[] = {1.001f, 1.5f, 2.0f, 5.0f};
    for (float u : overs) {
        CHECK(cubicSoftClip( u) == doctest::Approx( 2.0f / 3.0f).epsilon(kEps));
        CHECK(cubicSoftClip(-u) == doctest::Approx(-2.0f / 3.0f).epsilon(kEps));
    }
}

TEST_CASE("cubicSoftClip: slope=1 at origin; C1 at |u|=1 (slope -> 0 from both sides)") {
    constexpr float kH = 1.0e-4f;

    // f'(0) = 1 - u^2|_{u=0} = 1; central-difference estimate
    const float slope0 = (cubicSoftClip(kH) - cubicSoftClip(-kH)) / (2.0f * kH);
    CHECK(slope0 == doctest::Approx(1.0f).epsilon(kSlopeEps));

    // C1 at u=1: slope from cubic side f'(1-)=0; slope in flat region f'(1+)=0
    const float slopeLeft  = (cubicSoftClip(1.0f) - cubicSoftClip(1.0f - kH)) / kH;
    const float slopeRight = (cubicSoftClip(1.0f + kH) - cubicSoftClip(1.0f)) / kH;
    CHECK(std::abs(slopeLeft)  < kSlopeEps);
    CHECK(std::abs(slopeRight) < kSlopeEps);
}

// ===========================================================================
// algebraic: u/sqrt(1+u*u).  Odd, monotone, range(-1,1), f(0)=0.
// Anchor: algebraic(1) = 1/sqrt(2) — exact from the closed form.
// ===========================================================================

TEST_CASE("algebraic: f(0)=0, closed-form anchor at u=1, odd, range (-1,1)") {
    CHECK(algebraic(0.0f) == doctest::Approx(0.0f).epsilon(kEps));

    // u/sqrt(1+u^2) at u=1: 1/sqrt(2)
    CHECK(algebraic(1.0f) == doctest::Approx(1.0f / std::sqrt(2.0f)).epsilon(kEps));

    // u/sqrt(1+u^2) at u=3: 3/sqrt(10)
    CHECK(algebraic(3.0f) == doctest::Approx(3.0f / std::sqrt(10.0f)).epsilon(kEps));

    const float us[] = {0.1f, 0.5f, 1.0f, 2.0f, 5.0f};
    for (float u : us) {
        CHECK(algebraic(-u) == doctest::Approx(-algebraic(u)).epsilon(kEps));
        CHECK(algebraic(u)  >  -1.0f);
        CHECK(algebraic(u)  <   1.0f);
    }
}

TEST_CASE("algebraic: monotone increasing on [-3, 3]") {
    float prev = algebraic(-3.0f);
    for (int i = -29; i <= 30; ++i) {
        const float cur = algebraic(static_cast<float>(i) * 0.1f);
        CHECK(cur > prev);
        prev = cur;
    }
}

// ===========================================================================
// hardClip: clamp(u,-1,1).  Odd, exact ±1 saturation, f(0)=0.
// ===========================================================================

TEST_CASE("hardClip: f(0)=0, odd, exact pass-through and exact saturation") {
    CHECK(hardClip(0.0f) == 0.0f);

    // Exact pass-through in the linear region
    const float ins[] = {0.25f, 0.5f, 0.75f, -0.25f, -0.5f, -0.75f};
    for (float u : ins) {
        CHECK(hardClip(u) == u);
    }

    // Odd symmetry (use exact == in the clamped region where values are exact)
    const float us[] = {0.1f, 0.5f, 1.0f, 1.5f, 3.0f};
    for (float u : us) {
        CHECK(hardClip(-u) == -hardClip(u));
    }

    // Exact ±1 saturation at and beyond the knee
    const float overs[] = {1.0f, 1.001f, 1.5f, 2.0f, 10.0f};
    for (float u : overs) {
        CHECK(hardClip( u) ==  1.0f);
        CHECK(hardClip(-u) == -1.0f);
    }
}

// ===========================================================================
// softKnee: piecewise (linear core, smooth knee).  Odd, C0+C1 at knee.
// No closed-form anchor (implementation-defined knee location); assertions
// are structural: odd, bounded, monotone, C1-continuous numerically.
// ===========================================================================

TEST_CASE("softKnee: f(0)=0, odd symmetry, bounded to +-1") {
    CHECK(softKnee(0.0f) == doctest::Approx(0.0f).epsilon(kEps));

    const float us[] = {0.1f, 0.3f, 0.5f, 1.0f, 2.0f, 5.0f};
    for (float u : us) {
        CHECK(softKnee(-u) == doctest::Approx(-softKnee(u)).epsilon(kEps));
        CHECK(std::abs(softKnee(u)) <= 1.0f + kEps);
    }
}

TEST_CASE("softKnee: monotone non-decreasing on [-5, 5]") {
    float prev = softKnee(-5.0f);
    for (int i = -49; i <= 50; ++i) {
        const float cur = softKnee(static_cast<float>(i) * 0.1f);
        // Allow tiny float rounding margin
        CHECK(cur >= prev - kEps);
        prev = cur;
    }
}

TEST_CASE("softKnee: C1 continuous — numerical derivative has no sharp corner") {
    // Sweep u in [-3, 3] (step=0.01); verify the central-difference derivative
    // changes smoothly.  Threshold kC1Tol=0.5: a smooth saturation produces
    // |Δf'| ≈ M*h ≤ 2*0.01=0.02; a hard corner produces |Δf'| ≈ 1.0.
    constexpr float kH     = 1.0e-3f;
    constexpr float kStep  = 0.01f;
    constexpr float kC1Tol = 0.5f;

    const float uStart = -3.0f;
    float prevDeriv = (softKnee(uStart + kH) - softKnee(uStart - kH)) / (2.0f * kH);

    for (int i = 1; i <= 600; ++i) {
        const float u     = uStart + static_cast<float>(i) * kStep;
        const float deriv = (softKnee(u + kH) - softKnee(u - kH)) / (2.0f * kH);
        CHECK(std::abs(deriv - prevDeriv) < kC1Tol);
        prevDeriv = deriv;
    }
}

// ===========================================================================
// chebyshev(u, n): T_n(u) on [-1,1].
// Closed forms:  T_1(u)=u,  T_2(u)=2u^2-1,  T_3(u)=4u^3-3u.
// Identities: T_n(1)=1, T_n(-1)=(-1)^n, T_n(cos θ)=cos(nθ).
// Recurrence: T_{n+1}(u) = 2*u*T_n(u) - T_{n-1}(u).
// ===========================================================================

TEST_CASE("chebyshev n=2: closed-form anchors from T_2(u) = 2u^2 - 1") {
    // T_2(0)  = 2*0   - 1 = -1
    CHECK(chebyshev(0.0f, 2) == doctest::Approx(-1.0f).epsilon(kEps));
    // T_2(0.5)= 2*0.25- 1 = -0.5  (also: cos(2*pi/3)=-0.5, cos(pi/3)=0.5)
    CHECK(chebyshev(0.5f, 2) == doctest::Approx(-0.5f).epsilon(kEps));
    CHECK(chebyshev(0.5f, 2) ==
          doctest::Approx(std::cos(2.0f * kPi / 3.0f)).epsilon(kEps));
    // T_2(1)  = 1;  T_2(-1) = 1 = (-1)^2
    CHECK(chebyshev( 1.0f, 2) == doctest::Approx(1.0f).epsilon(kEps));
    CHECK(chebyshev(-1.0f, 2) == doctest::Approx(1.0f).epsilon(kEps));
}

TEST_CASE("chebyshev n=3: closed-form anchors from T_3(u) = 4u^3 - 3u") {
    // T_3(0)   = 0
    CHECK(chebyshev(0.0f, 3) == doctest::Approx(0.0f).epsilon(kEps));
    // T_3(0.5) = 4*0.125 - 1.5 = -1.0  (also: cos(pi)=-1, cos(pi/3)=0.5)
    CHECK(chebyshev(0.5f, 3) == doctest::Approx(-1.0f).epsilon(kEps));
    // T_3(cos(pi/6)) = cos(pi/2) = 0;  cos(pi/6) = sqrt(3)/2
    const float cos_pi6 = std::sqrt(3.0f) / 2.0f;
    CHECK(chebyshev(cos_pi6, 3) == doctest::Approx(0.0f).epsilon(kEps));
    // T_3(1)  = 1;  T_3(-1) = -1 = (-1)^3
    CHECK(chebyshev( 1.0f, 3) == doctest::Approx( 1.0f).epsilon(kEps));
    CHECK(chebyshev(-1.0f, 3) == doctest::Approx(-1.0f).epsilon(kEps));
}

TEST_CASE("chebyshev: T_n(1)=1 and T_n(-1)=(-1)^n for n=1,2,3") {
    CHECK(chebyshev( 1.0f, 1) == doctest::Approx( 1.0f).epsilon(kEps));
    CHECK(chebyshev( 1.0f, 2) == doctest::Approx( 1.0f).epsilon(kEps));
    CHECK(chebyshev( 1.0f, 3) == doctest::Approx( 1.0f).epsilon(kEps));
    CHECK(chebyshev(-1.0f, 1) == doctest::Approx(-1.0f).epsilon(kEps));  // (-1)^1
    CHECK(chebyshev(-1.0f, 2) == doctest::Approx( 1.0f).epsilon(kEps));  // (-1)^2
    CHECK(chebyshev(-1.0f, 3) == doctest::Approx(-1.0f).epsilon(kEps));  // (-1)^3
}

TEST_CASE("chebyshev: recurrence T_{n+1}(u) = 2u*T_n(u) - T_{n-1}(u) at u=0.7") {
    // T_1(0.7) = 0.7
    // T_2(0.7) = 2*0.49 - 1 = -0.02
    // T_3(0.7) = 2*0.7*(-0.02) - 0.7 = -0.028 - 0.7 = -0.728
    //          = 4*(0.343) - 3*(0.7)  = 1.372 - 2.1 = -0.728  (closed form check)
    const float u  = 0.7f;
    const float t1 = chebyshev(u, 1);
    const float t2 = chebyshev(u, 2);
    const float t3 = chebyshev(u, 3);
    CHECK(t1 == doctest::Approx(0.7f).epsilon(kEps));
    CHECK(t2 == doctest::Approx(-0.02f).epsilon(kEps));
    CHECK(t3 == doctest::Approx(-0.728f).epsilon(kEps));
    // Recurrence identity: T_3(u) = 2*u*T_2(u) - T_1(u)
    CHECK(t3 == doctest::Approx(2.0f * u * t2 - t1).epsilon(kEps));
}

// ===========================================================================
// diodeCurve: asymmetric memoryless curve — NOT a circuit model (Decision 6).
// Monotone, asymmetric (f(-u) != -f(u)), bounded.
// ===========================================================================

TEST_CASE("diodeCurve: asymmetric (not odd), monotone, bounded") {
    // Asymmetry: f(u) + f(-u) != 0 for at least one positive u.
    // (An odd function would satisfy f(u) + f(-u) == 0 everywhere.)
    const float pos1 = diodeCurve(1.0f);
    const float neg1 = diodeCurve(-1.0f);
    INFO("diodeCurve(1)=" << pos1 << "  diodeCurve(-1)=" << neg1
         << "  sum=" << (pos1 + neg1));
    CHECK(std::abs(pos1 + neg1) > 0.01f);  // NOT odd: f(1)+f(-1) != 0

    // Monotone non-decreasing and bounded
    float prev = diodeCurve(-5.0f);
    for (int i = -49; i <= 50; ++i) {
        const float u   = static_cast<float>(i) * 0.1f;
        const float cur = diodeCurve(u);
        CHECK(std::isfinite(cur));
        CHECK(std::abs(cur) <= 2.0f);       // generous finite bound
        CHECK(cur >= prev - kEps);          // non-decreasing
        prev = cur;
    }
}

// ===========================================================================
// sineFold(u, foldGain): sine-based folding, bounded to [-1, 1].
// With sufficient foldGain, sign reversals occur for large u.
// ===========================================================================

TEST_CASE("sineFold: bounded to [-1, 1] for multiple foldGain values") {
    const float gains[] = {1.0f, 2.0f, 5.0f, 10.0f};
    for (float g : gains) {
        for (int i = -50; i <= 50; ++i) {
            const float u = static_cast<float>(i) * 0.2f;
            const float y = sineFold(u, g);
            CHECK(std::isfinite(y));
            CHECK(y >= -1.0f - kEps);
            CHECK(y <=  1.0f + kEps);
        }
    }
}

TEST_CASE("sineFold: sign reversals for large fold depth") {
    // With sufficient foldGain the output oscillates; sweep u > 0 and verify
    // both positive and negative outputs exist (the fold produces sign reversals).
    const float foldGain = 4.0f;
    bool hasPositive = false;
    bool hasNegative = false;
    for (int i = 1; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.25f;
        const float y = sineFold(u, foldGain);
        if (y >  0.1f) hasPositive = true;
        if (y < -0.1f) hasNegative = true;
    }
    CHECK(hasPositive);
    CHECK(hasNegative);
}

// ===========================================================================
// triangleFold(u, foldGain): reflective folding into [-1,1]; piecewise-linear.
// ===========================================================================

TEST_CASE("triangleFold: bounded to [-1, 1] for multiple foldGain values") {
    const float gains[] = {1.0f, 2.0f, 5.0f, 10.0f};
    for (float g : gains) {
        for (int i = -50; i <= 50; ++i) {
            const float u = static_cast<float>(i) * 0.2f;
            const float y = triangleFold(u, g);
            CHECK(std::isfinite(y));
            CHECK(y >= -1.0f - kEps);
            CHECK(y <=  1.0f + kEps);
        }
    }
}

TEST_CASE("triangleFold: sign reversals for large fold depth") {
    const float foldGain = 4.0f;
    bool hasPositive = false;
    bool hasNegative = false;
    for (int i = 1; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.25f;
        const float y = triangleFold(u, foldGain);
        if (y >  0.1f) hasPositive = true;
        if (y < -0.1f) hasNegative = true;
    }
    CHECK(hasPositive);
    CHECK(hasNegative);
}

TEST_CASE("triangleFold: piecewise-linear (bounded numerical slope)") {
    // A triangle fold has slopes of magnitude = foldGain between fold points
    // and 0 at fold points themselves. The central-difference slope should stay
    // bounded by a generous multiple of foldGain.
    constexpr float kH        = 1.0e-4f;
    constexpr float kFoldGain = 2.0f;
    constexpr float kMaxSlope = 4.0f;  // 2x foldGain headroom for fold edges

    for (int i = -30; i <= 30; ++i) {
        const float u     = static_cast<float>(i) * 0.1f;
        const float slope = (triangleFold(u + kH, kFoldGain)
                           - triangleFold(u - kH, kFoldGain)) / (2.0f * kH);
        CHECK(std::abs(slope) < kMaxSlope);
    }
}

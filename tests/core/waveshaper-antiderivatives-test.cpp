// waveshaper-antiderivatives-test.cpp
// T019 — antiderivative correctness for the COVERED catalog shapes and the
// hasAntiderivative(Shape) coverage predicate (namespace acfx::shape).
//
// For each covered shape the antiderivative F must satisfy F'(u) = shape(u)
// EXACTLY.  We verify this NUMERICALLY via the central finite difference
//   F'(u) ≈ (F(u+h) − F(u−h)) / (2h),
// whose truncation error is O(h²·|F'''|).  With h = 1e-3 and these shapes'
// bounded curvature, the FD error is well under kFdTol away from corner points
// (where F'' jumps and the FD straddles two slopes).  We therefore skip a small
// neighbourhood around each known corner; between corners the identity is tight.
//
// This is the proof that the closed-form antiderivatives are the true integrals
// of the shapes — analytic truth, not fabricated numbers (Constitution V/X).
//
// Split from waveshaper-shapes-test.cpp to honour the ≤500-line file budget.

#include <doctest/doctest.h>

#include <cmath>

#include "labs/waveshaping/waveshaper-shapes.h"

using namespace acfx::shape;

namespace {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kEps   = 1.0e-5f;
constexpr float kFdH   = 1.0e-3f;   // central-difference step
constexpr float kFdTol = 2.0e-3f;   // FD tolerance away from corners

// Central finite-difference derivative of a unary antiderivative.
template <typename F>
float fdDeriv(F&& f, float u) {
    return (f(u + kFdH) - f(u - kFdH)) / (2.0f * kFdH);
}

} // namespace

TEST_CASE("tanhAntideriv: F'(u) = tanh(u); F = log(cosh(u)), F(0)=0") {
    CHECK(tanhAntideriv(0.0f) == doctest::Approx(0.0f).epsilon(kEps));
    for (int i = -30; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        CHECK(fdDeriv([](float x) { return tanhAntideriv(x); }, u)
              == doctest::Approx(tanhShape(u)).epsilon(kFdTol));
    }
}

TEST_CASE("hardClipAntideriv: F'(u) = clamp(u,-1,1); continuous at |u|=1") {
    // Continuity at the |u|=1 corner: u²/2 = 1/2 and |u|-1/2 = 1/2.
    CHECK(hardClipAntideriv( 1.0f) == doctest::Approx(0.5f).epsilon(kEps));
    CHECK(hardClipAntideriv(-1.0f) == doctest::Approx(0.5f).epsilon(kEps));
    for (int i = -30; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        if (std::abs(std::abs(u) - 1.0f) < 0.05f) continue;  // skip corner
        CHECK(fdDeriv([](float x) { return hardClipAntideriv(x); }, u)
              == doctest::Approx(hardClip(u)).epsilon(kFdTol));
    }
}

TEST_CASE("cubicSoftClipAntideriv: F'(u) = cubicSoftClip(u); continuous at |u|=1") {
    // Core/limit join: u²/2 − u⁴/12 at |u|=1 = 5/12.
    CHECK(cubicSoftClipAntideriv( 1.0f) == doctest::Approx(5.0f / 12.0f).epsilon(kEps));
    CHECK(cubicSoftClipAntideriv(-1.0f) == doctest::Approx(5.0f / 12.0f).epsilon(kEps));
    for (int i = -30; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        if (std::abs(std::abs(u) - 1.0f) < 0.05f) continue;  // skip corner
        CHECK(fdDeriv([](float x) { return cubicSoftClipAntideriv(x); }, u)
              == doctest::Approx(cubicSoftClip(u)).epsilon(kFdTol));
    }
}

TEST_CASE("arctanAntideriv: F'(u) = (2/pi)atan(u); F(0)=0") {
    CHECK(arctanAntideriv(0.0f) == doctest::Approx(0.0f).epsilon(kEps));
    for (int i = -30; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        CHECK(fdDeriv([](float x) { return arctanAntideriv(x); }, u)
              == doctest::Approx(arctanShape(u)).epsilon(kFdTol));
    }
}

TEST_CASE("algebraicAntideriv: F'(u) = u/sqrt(1+u^2); F = sqrt(1+u^2)") {
    CHECK(algebraicAntideriv(0.0f) == doctest::Approx(1.0f).epsilon(kEps));
    for (int i = -30; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        CHECK(fdDeriv([](float x) { return algebraicAntideriv(x); }, u)
              == doctest::Approx(algebraic(u)).epsilon(kFdTol));
    }
}

TEST_CASE("softKneeAntideriv: F'(u) = softKnee(u); continuous at the seams") {
    // Seam values: F(|u|=0.5)=1/8, F(|u|=1.5)=23/24 (even function).
    CHECK(softKneeAntideriv( 0.5f) == doctest::Approx(0.125f).epsilon(kEps));
    CHECK(softKneeAntideriv(-0.5f) == doctest::Approx(0.125f).epsilon(kEps));
    CHECK(softKneeAntideriv( 1.5f) == doctest::Approx(23.0f / 24.0f).epsilon(kEps));
    for (int i = -50; i <= 50; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        const float a = std::abs(u);
        if (std::abs(a - 0.5f) < 0.05f || std::abs(a - 1.5f) < 0.05f) continue;
        CHECK(fdDeriv([](float x) { return softKneeAntideriv(x); }, u)
              == doctest::Approx(softKnee(u)).epsilon(kFdTol));
    }
}

TEST_CASE("chebyshevAntideriv: F'(u) = T_n(u) for n=0..4 on [-0.9,0.9]") {
    // n=0 → u, n=1 → u²/2, n>=2 → identity formula. Restrict |u|<=0.9 so the
    // higher-degree polynomial curvature stays within the FD tolerance.
    constexpr float kPolyTol = 5.0e-3f;
    for (int n = 0; n <= 4; ++n) {
        for (int i = -9; i <= 9; ++i) {
            const float u = static_cast<float>(i) * 0.1f;
            CHECK(fdDeriv([n](float x) { return chebyshevAntideriv(x, n); }, u)
                  == doctest::Approx(chebyshev(u, n)).epsilon(kPolyTol));
        }
    }
}

TEST_CASE("sineFoldAntideriv: F'(u) = sineFold(u, foldGain)") {
    const float gains[] = {0.5f, 1.0f, 2.0f};
    for (float g : gains) {
        for (int i = -20; i <= 20; ++i) {
            const float u = static_cast<float>(i) * 0.1f;
            CHECK(fdDeriv([g](float x) { return sineFoldAntideriv(x, g); }, u)
                  == doctest::Approx(sineFold(u, g)).epsilon(kFdTol));
        }
    }
    // Degenerate foldGain=0 → flat shape, guarded antiderivative is finite.
    CHECK(std::isfinite(sineFoldAntideriv(1.0f, 0.0f)));
}

TEST_CASE("triangleFoldAntideriv: F'(u) = triangleFold(u, foldGain); continuous wrap") {
    // Triangle-wave corners (where F'' jumps) sit at k·u = pi/2 + n*pi.  For
    // foldGain=1, k=pi/2, corners at u = 1 + 2n; skip a neighbourhood of each.
    const float gains[] = {0.5f, 1.0f, 2.0f};
    for (float g : gains) {
        const float k = g * (kPi / 2.0f);
        for (int i = -40; i <= 40; ++i) {
            const float u   = static_cast<float>(i) * 0.1f;
            // distance (in phase) to nearest corner pi/2 + n*pi
            const float phi = k * u;
            const float rel = phi - kPi * std::floor(phi / kPi);  // [0,pi)
            const float distToCorner = std::abs(rel - kPi / 2.0f);
            if (distToCorner < k * 0.05f + 0.02f) continue;  // skip near corners
            CHECK(fdDeriv([g](float x) { return triangleFoldAntideriv(x, g); }, u)
                  == doctest::Approx(triangleFold(u, g)).epsilon(5.0e-3f));
        }
    }
    CHECK(std::isfinite(triangleFoldAntideriv(1.0f, 0.0f)));
}

TEST_CASE("diodeCurveAntideriv: F'(u) = diodeCurve(u); continuous at u=0") {
    // Both branches are log(cosh) scaled, equal to 0 at u=0 → continuous.
    CHECK(diodeCurveAntideriv(0.0f) == doctest::Approx(0.0f).epsilon(kEps));
    for (int i = -30; i <= 30; ++i) {
        const float u = static_cast<float>(i) * 0.1f;
        CHECK(fdDeriv([](float x) { return diodeCurveAntideriv(x); }, u)
              == doctest::Approx(diodeCurve(u)).epsilon(kFdTol));
    }
}

// ===========================================================================
// hasAntiderivative(Shape) coverage predicate.
// COVERED: every pure shape with an analytic antiderivative.
// UNCOVERED: biasedAsym (wrapper-realized, no pure function).
// ===========================================================================

TEST_CASE("hasAntiderivative: covered shapes true, biasedAsym false") {
    using acfx::Shape;
    CHECK(hasAntiderivative(Shape::tanh));
    CHECK(hasAntiderivative(Shape::arctan));
    CHECK(hasAntiderivative(Shape::cubicSoft));
    CHECK(hasAntiderivative(Shape::algebraic));
    CHECK(hasAntiderivative(Shape::hardClip));
    CHECK(hasAntiderivative(Shape::softKnee));
    CHECK(hasAntiderivative(Shape::chebyshev));
    CHECK(hasAntiderivative(Shape::diodeCurve));
    CHECK(hasAntiderivative(Shape::sineFold));
    CHECK(hasAntiderivative(Shape::triangleFold));

    CHECK_FALSE(hasAntiderivative(Shape::biasedAsym));
}

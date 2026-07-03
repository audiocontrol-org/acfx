#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "primitives/dynamics/dynamics-modulator.h"

// Contract accuracy + shape + statelessness tests for acfx::DynamicsModulator
// (task T014, User Story 2).
//
// Written against core/primitives/dynamics/dynamics-modulator.h (the
// implemented, graduated primitive) and
// specs/program-dependent-saturation/contracts/dynamics-modulator-api.md
// (SC-004, SC-007). modulate() is a PURE function of its two stored fields
// (depth, curve) and the envNorm argument — it holds no runtime state — so
// these cases need no stimulus/ballistics: they assert the analytic map
// directly, `modulate(env) = depth * curve(clamp(env, 0, 1))`.
//
// Expected curve values are derived from the header's exact arithmetic and
// its fixed shape constants:
//   linear      : curve(n) = n
//   logarithmic : curve(n) = log1p(kLogShape * n) / log1p(kLogShape),  kLogShape = 9.0
//   exponential : curve(n) = (exp(kExpShape * n) - 1) / (exp(kExpShape) - 1), kExpShape = 3.5

using acfx::DynamicsModulator;
using acfx::ModCurve;

namespace {

// Named tolerances (gain-computer-test.cpp style: assert analytic truths,
// not fabricated precision).
constexpr float kTol       = 1e-4f;  // analytic curve/endpoint values (float32 math)
constexpr float kShapeTol  = 1e-3f;  // curve(0.5) concave/convex comparisons

constexpr float kLogShapeK = 9.0f;
constexpr float kExpShapeK = 3.5f;

// Reference curve laws, computed independently of the header (mirrors the
// documented formulas) so tests do not just re-derive the implementation.
float refLinear(float n) { return n; }
float refLog(float n) { return std::log1p(kLogShapeK * n) / std::log1p(kLogShapeK); }
float refExp(float n) {
    return (std::exp(kExpShapeK * n) - 1.0f) / (std::exp(kExpShapeK) - 1.0f);
}

constexpr ModCurve kAllCurves[] = {ModCurve::linear, ModCurve::logarithmic,
                                    ModCurve::exponential};

} // namespace

// ---------------------------------------------------------------------------
// Endpoint anchoring — curve(0) == 0, curve(1) == 1 exactly, for every curve,
// so modulate(0) == 0 and modulate(1) == depth for every depth (T014/US2,
// contract "Curve law" + "Endpoints finite" rows; SC-004).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - endpoints are exact for every curve and depth (T014/US2)") {
    DynamicsModulator mod;

    for (ModCurve curve : kAllCurves) {
        mod.setCurve(curve);
        for (float depth : {1.0f, 0.5f, -1.0f}) {
            mod.setDepth(depth);
            CHECK(mod.modulate(0.0f) == doctest::Approx(0.0f).epsilon(kTol));
            CHECK(mod.modulate(1.0f) == doctest::Approx(depth).epsilon(kTol));
        }
    }
}

// ---------------------------------------------------------------------------
// Orthogonality identity — depth == 0 => modulate(env) == 0 for ALL env, for
// every curve (T014/US2, FR-007/SC-002 as exercised at the primitive level
// per SC-007).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - zero depth is exactly zero across the full envelope sweep (T014/US2)") {
    DynamicsModulator mod;
    mod.setDepth(0.0f);

    for (ModCurve curve : kAllCurves) {
        mod.setCurve(curve);
        for (int i = 0; i <= 20; ++i) {
            const float env = static_cast<float>(i) / 20.0f; // sweep [0,1]
            CHECK(mod.modulate(env) == doctest::Approx(0.0f).epsilon(kTol));
        }
    }
}

// ---------------------------------------------------------------------------
// Sign follows sign(depth); magnitude is monotone non-decreasing in envNorm,
// for every curve (T014/US2, contract "Signed depth" + "Curve law" rows;
// SC-004).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - sign follows depth sign; magnitude is monotone in envNorm (T014/US2)") {
    DynamicsModulator mod;

    for (ModCurve curve : kAllCurves) {
        mod.setCurve(curve);

        SUBCASE("positive depth: non-negative, non-decreasing") {
            mod.setDepth(0.75f);
            float prev = mod.modulate(0.0f);
            CHECK(prev >= -kTol);
            for (int i = 1; i <= 20; ++i) {
                const float env = static_cast<float>(i) / 20.0f;
                const float val = mod.modulate(env);
                CHECK(val >= -kTol);
                CHECK(val >= prev - kTol); // monotone non-decreasing
                prev = val;
            }
        }

        SUBCASE("negative depth: non-positive, magnitude non-decreasing (value non-increasing)") {
            mod.setDepth(-0.75f);
            float prev = mod.modulate(0.0f);
            CHECK(prev <= kTol);
            for (int i = 1; i <= 20; ++i) {
                const float env = static_cast<float>(i) / 20.0f;
                const float val = mod.modulate(env);
                CHECK(val <= kTol);
                CHECK(val <= prev + kTol); // magnitude non-decreasing -> value non-increasing
                prev = val;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// The three curves are distinguishable and have the contractually-documented
// shape: logarithmic is CONCAVE (curve(0.5) > 0.5, early onset), exponential
// is CONVEX (curve(0.5) < 0.5, late onset), linear is exactly 0.5 at env 0.5
// (T014/US2, contract "Curve law" row; SC-004).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - curves are distinguishable with the documented shape (T014/US2)") {
    DynamicsModulator mod;
    mod.setDepth(1.0f); // unity depth: modulate(n) == curve(n) directly

    mod.setCurve(ModCurve::linear);
    const float linHalf = mod.modulate(0.5f);
    CHECK(linHalf == doctest::Approx(0.5f).epsilon(kShapeTol));
    CHECK(linHalf == doctest::Approx(refLinear(0.5f)).epsilon(kShapeTol));

    mod.setCurve(ModCurve::logarithmic);
    const float logHalf = mod.modulate(0.5f);
    CHECK(logHalf == doctest::Approx(refLog(0.5f)).epsilon(kShapeTol));
    CHECK(logHalf > 0.5f); // concave: above the diagonal at the midpoint

    mod.setCurve(ModCurve::exponential);
    const float expHalf = mod.modulate(0.5f);
    CHECK(expHalf == doctest::Approx(refExp(0.5f)).epsilon(kShapeTol));
    CHECK(expHalf < 0.5f); // convex: below the diagonal at the midpoint

    // Distinguishable from one another at the midpoint.
    CHECK(std::fabs(logHalf - linHalf) > kShapeTol);
    CHECK(std::fabs(expHalf - linHalf) > kShapeTol);
    CHECK(std::fabs(logHalf - expHalf) > kShapeTol);
    CHECK(logHalf > linHalf);
    CHECK(expHalf < linHalf);

    // Cross-check the analytic curve values at a second interior point too,
    // to confirm the shapes hold beyond just the midpoint.
    mod.setCurve(ModCurve::logarithmic);
    CHECK(mod.modulate(0.25f) == doctest::Approx(refLog(0.25f)).epsilon(kShapeTol));
    mod.setCurve(ModCurve::exponential);
    CHECK(mod.modulate(0.25f) == doctest::Approx(refExp(0.25f)).epsilon(kShapeTol));
}

// ---------------------------------------------------------------------------
// Statelessness / call-order independence — modulate(a) then modulate(b)
// equals modulate(b) then modulate(a); modulate is const and holds no
// runtime state (T014/US2, contract "Statelessness" row; SC-007).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - modulate is stateless / call-order independent (T014/US2, SC-007)") {
    DynamicsModulator mod;
    mod.setDepth(0.6f);
    mod.setCurve(ModCurve::logarithmic);

    const float refA = mod.modulate(0.2f);
    const float refB = mod.modulate(0.8f);

    // Interleave in a different order; results must be bit-identical (pure fn).
    CHECK(mod.modulate(0.8f) == refB);
    CHECK(mod.modulate(0.2f) == refA);
    CHECK(mod.modulate(0.2f) == refA);
    CHECK(mod.modulate(0.8f) == refB);

    // (a) then (b) matches (b) then (a) exactly, order-independent.
    const float ab_a = mod.modulate(0.2f);
    const float ab_b = mod.modulate(0.8f);
    const float ba_b = mod.modulate(0.8f);
    const float ba_a = mod.modulate(0.2f);
    CHECK(ab_a == ba_a);
    CHECK(ab_b == ba_b);

    // A second, identically-configured instance agrees exactly.
    DynamicsModulator mod2;
    mod2.setDepth(0.6f);
    mod2.setCurve(ModCurve::logarithmic);
    CHECK(mod2.modulate(0.2f) == refA);
    CHECK(mod2.modulate(0.8f) == refB);
}

// ---------------------------------------------------------------------------
// envNorm is clamped to [0,1]: out-of-range inputs saturate to the nearest
// endpoint, no NaN/Inf (T014/US2, contract "Numerical safety" row; FR-021,
// SC-014).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - envNorm is clamped to [0,1] for out-of-range input (T014/US2, FR-021)") {
    DynamicsModulator mod;

    for (ModCurve curve : kAllCurves) {
        mod.setCurve(curve);
        for (float depth : {1.0f, -0.5f, 0.0f}) {
            mod.setDepth(depth);
            CHECK(mod.modulate(-0.5f) == doctest::Approx(mod.modulate(0.0f)).epsilon(kTol));
            CHECK(mod.modulate(-100.0f) == doctest::Approx(mod.modulate(0.0f)).epsilon(kTol));
            CHECK(mod.modulate(1.5f) == doctest::Approx(mod.modulate(1.0f)).epsilon(kTol));
            CHECK(mod.modulate(100.0f) == doctest::Approx(mod.modulate(1.0f)).epsilon(kTol));
            CHECK(std::isfinite(mod.modulate(-0.5f)));
            CHECK(std::isfinite(mod.modulate(1.5f)));
        }
    }
}

// ---------------------------------------------------------------------------
// depth is clamped to [-1,+1] (T014/US2, contract "Signed depth" row;
// FR-021).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - depth is clamped to [-1,+1] (T014/US2, FR-021)") {
    DynamicsModulator mod;
    mod.setCurve(ModCurve::linear);

    mod.setDepth(5.0f);
    CHECK(mod.modulate(1.0f) == doctest::Approx(1.0f).epsilon(kTol));
    CHECK(mod.modulate(0.5f) == doctest::Approx(0.5f).epsilon(kTol));

    mod.setDepth(-5.0f);
    CHECK(mod.modulate(1.0f) == doctest::Approx(-1.0f).epsilon(kTol));
    CHECK(mod.modulate(0.5f) == doctest::Approx(-0.5f).epsilon(kTol));
}

// ---------------------------------------------------------------------------
// Finiteness sweep — no NaN/Inf for env in {0,0.25,0.5,0.75,1} x depth in
// {-1,-0.5,0,0.5,1} x all curves (T014/US2, contract "Numerical safety" row;
// FR-021, SC-014).
// ---------------------------------------------------------------------------
TEST_CASE("DynamicsModulator - finite output across the full env x depth x curve grid (T014/US2, SC-014)") {
    DynamicsModulator mod;

    constexpr float envs[]   = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    constexpr float depths[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};

    for (ModCurve curve : kAllCurves) {
        mod.setCurve(curve);
        for (float depth : depths) {
            mod.setDepth(depth);
            for (float env : envs) {
                const float val = mod.modulate(env);
                CHECK(std::isfinite(val));
                CHECK_FALSE(std::isnan(val));
            }
        }
    }
}

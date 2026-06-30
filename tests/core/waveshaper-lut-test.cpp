// waveshaper-lut-test.cpp
// T015 — US3: WaveshaperLut deviation + edge-clamp + no-alloc tests.
//
// Expected RED: core/labs/waveshaping/waveshaper-lut.h does not exist yet.
// The build will fail with a compile error ("No such file or directory").
// T016 creates that header to turn this suite GREEN.
//
// =========================================================================
// API CONTRACT — T016 must implement this interface exactly
// =========================================================================
//
// File:      core/labs/waveshaping/waveshaper-lut.h
// Namespace: acfx
//
// class WaveshaperLut {
// public:
//   // Table resolution: kTableSize uniformly-spaced points over the domain.
//   static constexpr int   kTableSize    = 512;
//
//   // Input domain: the LUT covers u in [-kDomain, +kDomain].
//   static constexpr float kDomain       = 4.0f;
//
//   // Named interpolation-error bound (SC-004, FR-011/012).
//   // Derivation — linear-interpolation error for a C2 function:
//   //   error ≤ (h² / 8) · max|f''(u)|
//   //   h = 2·kDomain/(kTableSize - 1) = 8/511 ≈ 0.01565
//   //   h² ≈ 2.449e-4,   h²/8 ≈ 3.062e-5
//   //   Shape-specific max|f''|:
//   //     tanh:      f''(u) = -2 tanh(u) sech²(u)
//   //                maximum at |tanh(u)| = 1/√3, max|f''| ≈ 0.770
//   //                → analytic worst: 3.062e-5 × 0.770 ≈ 2.36e-5
//   //     arctan:    f(u) = (2/π)atan(u),  f''(u) = -(4u/π)/(1+u²)²
//   //                maximum at u = 1/√3,  max|f''| ≈ (2/π)·(9/(8√3)) ≈ 0.413
//   //                → analytic worst: 3.062e-5 × 0.413 ≈ 1.26e-5
//   //     algebraic: f(u) = u/√(1+u²),    f''(u) = -3u/(1+u²)^(5/2)
//   //                maximum at u = 1/2,  max|f''| ≈ 1.5/(1.25)^2.5 ≈ 0.859
//   //                → analytic worst: 3.062e-5 × 0.859 ≈ 2.63e-5
//   //   Worst analytic deviation: ≈ 2.63e-5.
//   //   kMaxDeviation = 1e-3 gives a 38× safety margin over the analytic bound.
//   static constexpr float kMaxDeviation = 1.0e-3f;
//
//   // build(): fill the table from a float(float) callable.
//   // table[i] = fn(u_i) where u_i = -kDomain + i · h.
//   // NOT RT-safe: called once from init(), never on the audio path.
//   // std::array member storage ensures no heap allocation from the table itself.
//   template <typename Fn>
//   void build(Fn fn) noexcept;
//
//   // evaluate(): linear interpolation between table points. RT-safe:
//   //   noexcept, allocation-free, O(1) bounded work.
//   // Out-of-domain policy (research.md Decision 4 — defined bounded policy):
//   //   u < -kDomain → return table[0]          (left edge clamp)
//   //   u >  kDomain → return table[kTableSize-1] (right edge clamp)
//   // This is documented, bounded behaviour — NOT a silent fallback.
//   float evaluate(float u) const noexcept;
//
// private:
//   std::array<float, kTableSize> table_{};
// };
//
// =========================================================================

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "labs/waveshaping/waveshaper-lut.h"
#include "labs/waveshaping/waveshaper-shapes.h"
#include "labs/waveshaping/waveshaper.h"
#include "support/allocation-sentinel.h"

using namespace acfx;
using namespace acfx::shape;
using acfx::test::AllocationSentinel;

namespace {

// Dense sweep for deviation tests: > 2× kTableSize to hit every table interval
// more than once and catch any interpolation pathology between adjacent points.
constexpr int kSweepSteps = 5000;

constexpr float kPi = 3.14159265358979323846f;

} // namespace

// ===========================================================================
// GROUP 1 — Per-shape max deviation from closed-form reference (SC-004)
//
// research.md Decision 4: "closed-form is exact and portable (the reference);
// LUT error is asserted against closed-form as ground truth within a named
// bound for the chosen resolution."
//
// For each smooth shape: build the LUT, sweep u across [-kDomain, kDomain],
// and assert |lut.evaluate(u) - closedForm(u)| <= kMaxDeviation at every point.
//
// Shapes are C2 everywhere on R, so the analytic interpolation-error bound
// applies exactly (see bound derivation in the file header above).
// ===========================================================================

TEST_CASE("WaveshaperLut tanh: max deviation from closed-form <= kMaxDeviation") {
    WaveshaperLut lut;
    lut.build([](float u) noexcept -> float { return tanhShape(u); });

    const float D    = WaveshaperLut::kDomain;
    const float step = (2.0f * D) / static_cast<float>(kSweepSteps);

    for (int i = 0; i <= kSweepSteps; ++i) {
        const float u   = -D + static_cast<float>(i) * step;
        const float dev = std::abs(lut.evaluate(u) - tanhShape(u));
        CHECK_MESSAGE(dev <= WaveshaperLut::kMaxDeviation,
            "tanh deviation ", dev,
            " exceeds kMaxDeviation=", WaveshaperLut::kMaxDeviation,
            " at u=", u);
    }
}

TEST_CASE("WaveshaperLut arctan: max deviation from closed-form <= kMaxDeviation") {
    WaveshaperLut lut;
    lut.build([](float u) noexcept -> float { return arctanShape(u); });

    const float D    = WaveshaperLut::kDomain;
    const float step = (2.0f * D) / static_cast<float>(kSweepSteps);

    for (int i = 0; i <= kSweepSteps; ++i) {
        const float u   = -D + static_cast<float>(i) * step;
        const float dev = std::abs(lut.evaluate(u) - arctanShape(u));
        CHECK_MESSAGE(dev <= WaveshaperLut::kMaxDeviation,
            "arctan deviation ", dev,
            " exceeds kMaxDeviation=", WaveshaperLut::kMaxDeviation,
            " at u=", u);
    }
}

TEST_CASE("WaveshaperLut algebraic: max deviation from closed-form <= kMaxDeviation") {
    WaveshaperLut lut;
    lut.build([](float u) noexcept -> float { return algebraic(u); });

    const float D    = WaveshaperLut::kDomain;
    const float step = (2.0f * D) / static_cast<float>(kSweepSteps);

    for (int i = 0; i <= kSweepSteps; ++i) {
        const float u   = -D + static_cast<float>(i) * step;
        const float dev = std::abs(lut.evaluate(u) - algebraic(u));
        CHECK_MESSAGE(dev <= WaveshaperLut::kMaxDeviation,
            "algebraic deviation ", dev,
            " exceeds kMaxDeviation=", WaveshaperLut::kMaxDeviation,
            " at u=", u);
    }
}

// ===========================================================================
// GROUP 2 — Edge-clamp: out-of-domain inputs return the edge table value
//
// research.md Decision 4: "Out-of-domain inputs clamp to the table edge
// (matching closed-form edge behavior within tolerance) — a defined bounded
// policy, not a silent fallback."
//
// Assertions:
//   A. evaluate(D + delta) equals evaluate(D) for several positive deltas.
//   B. evaluate(-D - delta) equals evaluate(-D) for several positive deltas.
//   C. Edge table values are within kMaxDeviation of the closed-form at +-D
//      (the LUT was built from the shape, so the endpoint itself is the
//      shape value at +-D, with no interpolation error).
// ===========================================================================

TEST_CASE("WaveshaperLut edge-clamp: out-of-domain returns edge table value") {
    WaveshaperLut lut;
    lut.build([](float u) noexcept -> float { return tanhShape(u); });

    const float D        = WaveshaperLut::kDomain;
    const float edgeHigh = lut.evaluate(D);
    const float edgeLow  = lut.evaluate(-D);

    SUBCASE("right edge clamped: evaluate(D + delta) == evaluate(D)") {
        const float deltas[] = {0.001f, 0.1f, 0.5f, 1.0f, 5.0f, 100.0f};
        for (float delta : deltas) {
            CHECK_MESSAGE(
                lut.evaluate(D + delta) == doctest::Approx(edgeHigh).epsilon(1.0e-7f),
                "right clamp failed at delta=", delta);
        }
    }

    SUBCASE("left edge clamped: evaluate(-D - delta) == evaluate(-D)") {
        const float deltas[] = {0.001f, 0.1f, 0.5f, 1.0f, 5.0f, 100.0f};
        for (float delta : deltas) {
            CHECK_MESSAGE(
                lut.evaluate(-D - delta) == doctest::Approx(edgeLow).epsilon(1.0e-7f),
                "left clamp failed at delta=", delta);
        }
    }

    SUBCASE("edge table values are within kMaxDeviation of closed-form at +-D") {
        // The endpoint is set directly from the shape function during build(),
        // so deviation = |table[endpoint] - shape(D)| is at most float rounding
        // (< 1 ULP), well within kMaxDeviation.
        const float refHigh = tanhShape( D);
        const float refLow  = tanhShape(-D);
        CHECK(std::abs(edgeHigh - refHigh) <= WaveshaperLut::kMaxDeviation);
        CHECK(std::abs(edgeLow  - refLow ) <= WaveshaperLut::kMaxDeviation);
    }
}

// ===========================================================================
// GROUP 3 — No per-sample allocation: evaluate() must be RT-safe (SC-004, FR-011)
//
// research.md Decision 4: "Building the table in init() keeps process()
// allocation-free (FR-011)."
//
// Procedure:
//   1. build() is called once OUTSIDE the measured region (building may
//      compute freely; std::array member means no heap alloc from the table).
//   2. AllocationSentinel::reset() arms the counter.
//   3. evaluate() is called kEvalLoops times with inputs spanning the full
//      domain and beyond (exercises both interpolation and edge-clamp paths).
//   4. AllocationSentinel::allocations() must be zero.
// ===========================================================================

TEST_CASE("WaveshaperLut::evaluate is allocation-free (RT-safety SC-004/FR-011)") {
    WaveshaperLut lut;
    lut.build([](float u) noexcept -> float { return tanhShape(u); });

    constexpr int kEvalLoops = 1000;

    AllocationSentinel::reset();
    for (int i = 0; i < kEvalLoops; ++i) {
        // Sweep through [-6, +6], deliberately exceeding domain [-4, +4]
        // to exercise both linear-interpolation and edge-clamp code paths.
        const float u = 6.0f * std::sin(2.0f * kPi
                                        * static_cast<float>(i)
                                        / static_cast<float>(kEvalLoops));
        (void)lut.evaluate(u);
    }
    const std::size_t allocs = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocs == 0,
        "WaveshaperLut::evaluate allocated ", allocs,
        " time(s) — must be zero for RT-safety");
}

// ===========================================================================
// GROUP 4 — T017/US3 wrapper-level integration tests
//
// These tests verify that Evaluation::lut is correctly wired into
// Waveshaper::process() — i.e. the LUT table built in init() is used by
// process() when evaluation_ == Evaluation::lut.
//
// T017 requirement: "select backend without per-sample branching cost where
// avoidable" — a single predictable branch on evaluation_ in process() is
// acceptable.  Build happens in init(), never in process().
// ===========================================================================

// ---------------------------------------------------------------------------
// TEST 4A — LUT-mode process() output matches closedForm-mode within 2*kMaxDeviation
//
// Bound derivation (analytic):
//   Let e[n] = shape_lut(u[n]) - shape_cf(u[n]) be the per-sample LUT error.
//   Both Waveshaper instances run the same DC-blocker H(z):
//     H(z) = (1 - z^{-1}) / (1 - R*z^{-1}),  R = kDcR = 0.995
//   The output difference y_lut[n] - y_cf[n] = (H * e)[n].
//
//   L-inf bound:  ||H*e||_inf <= ||h||_1 * ||e||_inf
//   Impulse response: h[0]=1, h[n]=-R^{n-1}*(1-R) for n>=1.
//   ||h||_1 = 1 + (1-R)*sum_{n=1}^inf R^{n-1} = 1 + (1-R)/(1-R) = 2.
//
//   Therefore: |y_lut[n] - y_cf[n]| <= 2 * kMaxDeviation  (for all n >= 0,
//   for both DC-blockers starting at zero, regardless of input frequency).
//
// Shape: tanh, drive=1, bias=0, gainComp=off.
// Sweep: 2000-sample sine at 100 Hz (gently varying, well inside LUT domain).
// Both instances init()'d from zero state; outputs compared sample-by-sample.
// ---------------------------------------------------------------------------

TEST_CASE("T017: Waveshaper lut-mode output matches closedForm within 2*kMaxDeviation") {
    constexpr float kSampleRate = 48000.0f;
    constexpr float kFreqHz     = 100.0f;   // 100 Hz: slowly-varying u, smooth LUT error
    constexpr float kAmplitude  = 0.5f;     // well inside tanh's active range
    constexpr int   kNumSamples = 2000;

    Waveshaper lutWs;
    lutWs.setShape(Shape::tanh);
    lutWs.setEvaluation(Evaluation::lut);
    lutWs.setDrive(1.0f);
    lutWs.setBias(0.0f);
    lutWs.setGainCompensation(false);
    lutWs.init(kSampleRate);   // LUT is built here (FR-011); never in process()

    Waveshaper cfWs;
    cfWs.setShape(Shape::tanh);
    cfWs.setEvaluation(Evaluation::closedForm);
    cfWs.setDrive(1.0f);
    cfWs.setBias(0.0f);
    cfWs.setGainCompensation(false);
    cfWs.init(kSampleRate);

    // Analytic bound: DC-blocker L1-norm of impulse response = 2, so the
    // output difference is bounded by 2*kMaxDeviation at every sample.
    const float kOutputBound = 2.0f * WaveshaperLut::kMaxDeviation;

    for (int i = 0; i < kNumSamples; ++i) {
        const float t   = static_cast<float>(i) / kSampleRate;
        const float x   = kAmplitude * std::sin(2.0f * kPi * kFreqHz * t);
        const float yL  = lutWs.process(x);
        const float yCF = cfWs.process(x);
        const float dev = std::abs(yL - yCF);
        CHECK_MESSAGE(dev <= kOutputBound,
            "sample=", i, " lut=", yL, " cf=", yCF,
            " deviation=", dev, " bound=", kOutputBound);
    }
}

// ---------------------------------------------------------------------------
// TEST 4B — Waveshaper::process() with Evaluation::lut is allocation-free
//
// The LUT is built in init() (outside the measured region); process() must
// not allocate on every call.  AllocationSentinel traps any heap allocation
// on the calling thread.
// ---------------------------------------------------------------------------

TEST_CASE("T017: Waveshaper lut-mode process() is allocation-free (RT-safety FR-020)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int   kNumSamples = 500;

    Waveshaper ws;
    ws.setShape(Shape::tanh);
    ws.setEvaluation(Evaluation::lut);
    ws.setDrive(1.5f);
    ws.setBias(0.0f);
    ws.setGainCompensation(false);
    ws.init(kSampleRate);   // LUT built here — outside the measured region

    AllocationSentinel::reset();   // arm the sentinel after init()
    for (int i = 0; i < kNumSamples; ++i) {
        const float x = 0.3f * std::sin(2.0f * kPi * 1000.0f
                                         * static_cast<float>(i) / kSampleRate);
        (void)ws.process(x);
    }
    const std::size_t allocs = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocs == 0,
        "Waveshaper lut-mode process() allocated ", allocs,
        " time(s) — must be zero for RT-safety (FR-020)");
}

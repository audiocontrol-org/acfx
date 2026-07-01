// oversampler-latency-test.cpp
// T015 -- User Story 3 suite: proves acfx::Oversampler<Factor>::latencySamples()
// equals the MEASURED group delay of the cascade, for every supported factor
// (FR-012, SC-004).
//
// MEASUREMENT + TOLERANCE DESIGN (deliberate choice -- read before "fixing"
// this test, per the oversampler author's explicit guidance for this task):
//
//   Factor 2's analytic (true) group delay is 45, an EXACT integer
//   (oversampler.h's latency-derivation comment: L_base = 90*(1-1/Factor)).
//   Its impulse-response peak index is therefore a stable, unambiguous
//   argmax, so Factor 2 gets a STRICT `argmaxPeak == latencySamples()` check.
//
//   Factor 4's analytic group delay is 67.5 -- a HALF-INTEGER tie. The
//   impulse energy splits essentially evenly between samples 67 and 68, and
//   which one wins the floating-point argmax is decided by a ~1e-6-scale
//   margin that is NOT guaranteed stable across compilers / optimization
//   levels / platforms. latencySamples() resolves the tie toward the lower
//   index (round-half-down, see oversampler.h), reporting 67. A strict
//   argmax == 67 check would therefore be a FRAGILE test: it could flip to a
//   spurious failure on a toolchain that resolves the FP tie the other way,
//   despite the underlying filter being unchanged. So Factor 4 (and,
//   defensively, Factor 8, whose 78.75 delay is not a tie but is still
//   fractional and close to a bin boundary) get a SLACK-bounded check
//   `|argmaxPeak - latencySamples()| <= kPeakSlackSamples` instead of strict
//   equality (kPeakSlackSamples named + justified below).
//
//   As a tie-break-INDEPENDENT cross-check, every factor additionally gets an
//   ENERGY-CENTROID measurement (the energy-weighted first moment of sample
//   index over the impulse response). Because the halfband cascade is exactly
//   linear-phase (symmetric impulse response), the centroid converges to the
//   ANALYTIC group delay itself (45 / 67.5 / 78.75) regardless of which side
//   of an integer/half-integer bin the discrete peak happens to land on -- it
//   is robust to the exact tie that makes the raw argmax fragile for
//   Factor 4/8 (per the author's guidance: "measure the delay by energy
//   centroid ... which is robust to the tie").
//
// Self-check (throwaway program against the real implementation, run during
// authoring per this task's VERIFY step) measured, at 48 kHz:
//   Factor 2: argmaxPeak=45  centroid=45.000000  (latencySamples()=45)
//   Factor 4: argmaxPeak=67  centroid=67.500000  (latencySamples()=67)
//   Factor 8: argmaxPeak=79  centroid=78.805329  (latencySamples()=79)
// i.e. the argmax landed exactly on latencySamples() for all three factors on
// this toolchain (no defect found), and the centroid matched (or nearly
// matched, for Factor 8) the analytic group delay -- consistent with the
// author's brief. The slack-bounded checks below are kept regardless, since
// the author's guidance is that the exact argmax tie is not guaranteed stable
// across toolchains/opt levels.
//
// References: specs/oversampling/spec.md FR-012/FR-023, SC-004;
// specs/oversampling/contracts/oversampling-api.md (latencySamples());
// specs/oversampling/quickstart.md Scenario 4;
// core/primitives/oversampling/oversampler.h (latency-derivation comment).

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "primitives/oversampling/oversampler.h"
#include "support/measurement/stimulus.h"

using namespace acfx;
using acfx::measure::ImpulseGenerator;
using acfx::measure::SineGenerator;

namespace {

// ---------------------------------------------------------------------------
// NAMED TOLERANCES (FR-023 -- no magic numbers)
// ---------------------------------------------------------------------------

// Discrete-peak slack for Factor 4/8: the analytic group delay is a
// half-integer (Factor 4: 67.5) or otherwise fractional (Factor 8: 78.75)
// number of samples, so the impulse response's energy is split across (at
// most) the two integer sample indices adjacent to the analytic delay. Which
// of those two indices wins the discrete argmax is decided by a sub-ULP-scale
// floating-point margin (see file header) -- so the allowed slack is exactly
// the width of that adjacency: +/-1 sample from latencySamples(). A slack
// larger than 1 would risk hiding a real multi-sample latency defect; a slack
// of 0 (strict equality) would make the test fragile to the unstable tie
// (per the oversampler author's explicit guidance for this task).
constexpr int kPeakSlackSamples = 1;

// Energy-centroid tolerance: the energy centroid is a continuous (not
// bin-quantized) estimator of group delay, so in exact arithmetic it would
// equal the analytic delay (45 / 67.5 / 78.75) exactly for this perfectly
// linear-phase (symmetric-impulse-response) cascade. The measured centroid in
// the self-check above matched to within 0.06 samples (Factor 8) and
// exactly (Factor 2/4); 0.25 samples is used as the pass bound here -- tight
// enough to catch a real group-delay defect (which would show up as an error
// of many samples, not a fraction of one) while leaving headroom for
// float-vs-double summation-order variation across toolchains.
constexpr double kCentroidToleranceSamples = 0.25;

// Sine latency-alignment tolerance (US1 tie-in, FR-007): the cascade's
// passband gain error is bounded by the same 0.1 dB design ripple target used
// in oversampler-transparency-test.cpp, expressed as a linear-amplitude
// fraction: 10^(0.1/20) - 1. For a unit-amplitude sine, a latency-aligned
// sample-by-sample compare against the input is therefore bounded by that
// same fraction.
constexpr double kSineAlignTolerance = 0.0115794; // 10^(0.1/20) - 1

// ---------------------------------------------------------------------------
// SETTINGS
// ---------------------------------------------------------------------------
constexpr float kSampleRate = 48000.0f;

// Impulse-response capture length: the halfband cascade is a finite cascade
// of FIR stages (no feedback), so its impulse response is exactly bounded;
// 400 base-rate samples is far beyond the worst-case group delay (Factor 8 ->
// ~79 base samples) plus the settling tail of the widest (kStages == 3)
// cascade, so the captured window contains the ENTIRE nonzero response (the
// self-check confirmed the centroid denominator captures effectively all the
// impulse energy).
constexpr std::size_t kImpulseCapture = 400;

constexpr auto kIdentityEval = [](float s) noexcept { return s; };

// Known analytic (fractional, per oversampler.h's derivation comment) and
// reported (rounded, per latencySamples()) group delays, base-rate samples.
constexpr int    kExpectedLatency[]       = {45, 67, 79};
constexpr double kAnalyticGroupDelay[]    = {45.0, 67.5, 78.75};

template <int Factor>
constexpr std::size_t factorIndex() {
    return (Factor == 2) ? 0 : (Factor == 4) ? 1 : 2;
}

// Drive `osc` (already init()'d) with a unit impulse and return the first
// kImpulseCapture output samples (the full, bounded impulse response).
template <int Factor>
std::vector<float> impulseResponse(Oversampler<Factor>& osc) {
    std::vector<float> in(kImpulseCapture, 0.0f);
    ImpulseGenerator{1.0f}.fill(acfx::span<float>(in));

    std::vector<float> out(kImpulseCapture, 0.0f);
    for (std::size_t n = 0; n < kImpulseCapture; ++n)
        out[n] = osc.process(in[n], kIdentityEval);
    return out;
}

// Index of the largest-magnitude sample (the discrete group-delay estimate).
std::size_t argmaxAbs(const std::vector<float>& v) {
    std::size_t best = 0;
    float bestVal = std::fabs(v[0]);
    for (std::size_t i = 1; i < v.size(); ++i) {
        const float a = std::fabs(v[i]);
        if (a > bestVal) {
            bestVal = a;
            best = i;
        }
    }
    return best;
}

// Energy-weighted first moment of sample index: sum(i * v[i]^2) / sum(v[i]^2).
// A tie-independent, continuous estimator of group delay for a symmetric
// (linear-phase) impulse response -- see file header.
double energyCentroid(const std::vector<float>& v) {
    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double e = static_cast<double>(v[i]) * static_cast<double>(v[i]);
        num += static_cast<double>(i) * e;
        den += e;
    }
    REQUIRE(den > 0.0); // a non-degenerate impulse response must carry energy
    return num / den;
}

// Core FR-012/SC-004 assertion for one Factor: latencySamples() reports the
// documented constant, and the measured group delay (both discrete peak and
// energy centroid) agrees with it.
template <int Factor>
void checkLatencyMatchesMeasuredGroupDelay() {
    const std::size_t idx = factorIndex<Factor>();

    Oversampler<Factor> osc;
    osc.init(kSampleRate);

    const int reported = osc.latencySamples();
    INFO("Factor=" << Factor << " latencySamples()=" << reported
                    << " expected=" << kExpectedLatency[idx]);
    CHECK(reported == kExpectedLatency[idx]); // documents the 45/67/79 contract

    const std::vector<float> ir = impulseResponse<Factor>(osc);
    const std::size_t peak = argmaxAbs(ir);
    const double centroid = energyCentroid(ir);

    INFO("Factor=" << Factor << " argmaxPeak=" << peak << " centroid=" << centroid
                    << " reportedLatency=" << reported
                    << " analyticGroupDelay=" << kAnalyticGroupDelay[idx]);

    if constexpr (Factor == 2) {
        // Integer analytic group delay -> stable, unambiguous peak (see file
        // header): strict equality is appropriate here.
        CHECK(peak == static_cast<std::size_t>(reported));
    } else {
        // Fractional/half-integer analytic group delay -> the discrete peak
        // can legitimately land one sample either side of latencySamples();
        // see kPeakSlackSamples rationale above.
        const long diff =
            static_cast<long>(peak) - static_cast<long>(reported);
        CHECK(diff >= -kPeakSlackSamples);
        CHECK(diff <= kPeakSlackSamples);
    }

    // Tie-independent cross-check for every factor: the energy centroid must
    // agree with the ANALYTIC (not rounded) group delay.
    CHECK(std::fabs(centroid - kAnalyticGroupDelay[idx]) <= kCentroidToleranceSamples);
}

} // namespace

// ---------------------------------------------------------------------------
// TEST CASES -- one per Factor in {2, 4, 8} (FR-012, SC-004).
// ---------------------------------------------------------------------------

TEST_CASE("Oversampler<2>: latencySamples() equals the measured group delay (FR-012, SC-004)") {
    checkLatencyMatchesMeasuredGroupDelay<2>();
}

TEST_CASE("Oversampler<4>: latencySamples() equals the measured group delay within the "
          "half-integer-tie slack (FR-012, SC-004)") {
    checkLatencyMatchesMeasuredGroupDelay<4>();
}

TEST_CASE("Oversampler<8>: latencySamples() equals the measured group delay within the "
          "fractional-delay slack (FR-012, SC-004)") {
    checkLatencyMatchesMeasuredGroupDelay<8>();
}

// ---------------------------------------------------------------------------
// BONUS CHECK (US1 tie-in) -- a low-in-band sine, delayed by latencySamples(),
// aligns with the output within the passband-gain-error-derived tolerance.
//
// Factor 2 ONLY (deliberate, verified during authoring -- do not extend to
// 4/8): Factor 2's latencySamples() (45) equals its ANALYTIC group delay
// exactly, so latency-aligning by that integer introduces no residual phase
// error and kSineAlignTolerance (a pure passband-gain-error bound) is the
// right, sufficient bound. Factor 4/8's latencySamples() (67, 79) is a
// ROUNDED integer standing in for a fractional analytic delay (67.5, 78.75,
// see file header) -- latency-aligning by the rounded integer leaves a
// residual sub-sample phase error of up to 0.5 (Factor 4) / 0.25 (Factor 8)
// samples. At the 500 Hz probe used here that residual alone measured
// ~0.033 (Factor 4) and ~0.016 (Factor 8) during self-check authoring --
// both ABOVE kSineAlignTolerance (~0.0116) on their own, before any gain
// error is even added. That is an expected, correct consequence of comparing
// against an INTEGER-rounded latency, not a defect in the oversampler
// (mirrors oversampler-transparency-test.cpp's identical Factor-2-only
// restriction for the same reason). Factor 4/8's true latency accuracy is
// covered by checkLatencyMatchesMeasuredGroupDelay's centroid check instead.
// ---------------------------------------------------------------------------

void checkLatencyAlignedSineMatchesInput() {
    constexpr int Factor = 2;
    Oversampler<Factor> osc;
    osc.init(kSampleRate);

    constexpr double      freqHz  = 500.0;
    constexpr std::size_t kSettle = 500;
    constexpr std::size_t total   = kSettle + 2000;

    std::vector<float> in(total, 0.0f);
    SineGenerator{freqHz, static_cast<double>(kSampleRate), 1.0f, 0.0}.fill(
        acfx::span<float>(in));

    std::vector<float> out(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        out[n] = osc.process(in[n], kIdentityEval);

    const int delay = osc.latencySamples();
    for (std::size_t n = kSettle; n < total; ++n) {
        const float delayed = in[n - static_cast<std::size_t>(delay)];
        INFO("Factor=" << Factor << " n=" << n << " out=" << out[n]
                        << " delayed-in=" << delayed);
        CHECK(std::abs(out[n] - delayed) <= kSineAlignTolerance);
    }
}

TEST_CASE("Oversampler<2>: latency-aligned low-in-band sine matches input (FR-007/FR-012)") {
    checkLatencyAlignedSineMatchesInput();
}

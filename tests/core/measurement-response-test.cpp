// measurement-response-test.cpp
// Doctest cases for US1: analyzer correctness and SVF response (T007).
//
// Tolerance philosophy (anti-false-precision): we assert ANALYTIC relationships
// (recovered amplitude == stimulus amplitude; passband near unity; stopband
// attenuated; phase difference == imposed offset) within NAMED, documented
// tolerances — never fabricated exact magnitudes.  The dominant error source for
// a single-bin Goertzel is spectral leakage, which we minimise (where we
// control the buffer) by choosing an integer number of cycles in the analysis
// window, leaving a generous ~1e-2 epsilon for residual leakage and float
// round-trip.

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

#include "support/measurement/stimulus.h"
#include "support/measurement/analyzers.h"
#include "support/measurement/metrics.h"
#include "support/svf-reference.h"
#include "effects/svf/svf-effect.h"
#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"

#include "measurement-support.h"

using namespace acfx::measure;
using namespace acfx::meastest;

using acfx::test::kPassbandFreqHz;
using acfx::test::kPassbandGainMin;
using acfx::test::kRefCutoffHz;
using acfx::test::kRefSampleRate;
using acfx::test::kStopbandFreqHz;
using acfx::test::kStopbandGainMax;

namespace {

// Generous epsilon for single-bin Goertzel recovery on an integer-cycle window.
// Residual error is dominated by spectral leakage (finite window) plus the
// float<->double round-trip in the stimulus and analyzer; 1e-2 is a meaningful
// bound (orders of magnitude tighter than the response thresholds) without
// asserting false precision.
constexpr double kGoertzelEps = 1.0e-2;

// Settling / measurement split mirrors svf-reference's measureMagnitude scale:
// the SVF transient decays well within kSettle samples, so the steady-state
// gain is read from the trailing kMeasure samples only.
constexpr int kSettle  = 8192;
constexpr int kMeasure = 16384;

// Drive a fresh lowpass SVF with a unit sine at freqHz, capture(), and return
// the steady-state magnitude over the trailing measurement window.
double svfLowpassSteadyStateMagnitude(double freqHz) {
    const std::size_t total = static_cast<std::size_t>(kSettle + kMeasure);
    std::vector<float> in(total, 0.0f);
    std::vector<float> out(total, 0.0f);

    SineGenerator sine;
    sine.freqHz     = freqHz;
    sine.sampleRate = kRefSampleRate;
    sine.amplitude  = 1.0f;
    sine.phase      = 0.0;
    sine.fill(acfx::span<float>(in));

    acfx::SvfEffect fx;
    configureLowpass(fx, kRefCutoffHz);

    // ProcessContext{sampleRate, maxBlockSize, numChannels}.
    capture(fx, acfx::ProcessContext{kRefSampleRate, 512, 1},
            acfx::span<const float>(in), acfx::span<float>(out));

    return magnitude(acfx::span<const float>(out.data() + kSettle,
                                             static_cast<std::size_t>(kMeasure)),
                     freqHz, kRefSampleRate);
}

} // namespace

TEST_CASE("Goertzel recovers a known sine amplitude (FR-005/007 closed-form)") {
    // Integer number of cycles in the window minimises leakage:
    // freq * N / sampleRate = 1000 * 4800 / 48000 = 100 cycles exactly.
    constexpr std::size_t N          = 4800;
    constexpr double      freqHz     = 1000.0;
    constexpr double      sampleRate = 48000.0;
    constexpr float       amplitude  = 0.5f;

    std::vector<float> buf(N, 0.0f);
    SineGenerator sine;
    sine.freqHz     = freqHz;
    sine.sampleRate = sampleRate;
    sine.amplitude  = amplitude;
    sine.phase      = 0.0;
    sine.fill(acfx::span<float>(buf));

    const GoertzelAnalyzer ga{freqHz, sampleRate};
    const GoertzelAnalyzer::Bin bin = ga.analyze(acfx::span<const float>(buf));

    // Amplitude-normalised magnitude => recovered amplitude ~= stimulus A.
    CHECK(bin.magnitude == doctest::Approx(static_cast<double>(amplitude)).epsilon(kGoertzelEps));
}

TEST_CASE("Goertzel phase tracks a known phase offset (FR-007 closed-form)") {
    // Two sines that differ ONLY by a known phase offset; the Goertzel phase
    // difference must equal that offset (wrapped). Same integer-cycle window.
    constexpr std::size_t N          = 4800;
    constexpr double      freqHz     = 1000.0;
    constexpr double      sampleRate = 48000.0;
    const double          phase0     = 0.0;
    const double          phase1     = std::numbers::pi / 3.0; // ~1.047 rad

    std::vector<float> a(N, 0.0f);
    std::vector<float> b(N, 0.0f);

    SineGenerator s0;
    s0.freqHz = freqHz; s0.sampleRate = sampleRate; s0.amplitude = 1.0f; s0.phase = phase0;
    s0.fill(acfx::span<float>(a));
    SineGenerator s1;
    s1.freqHz = freqHz; s1.sampleRate = sampleRate; s1.amplitude = 1.0f; s1.phase = phase1;
    s1.fill(acfx::span<float>(b));

    const GoertzelAnalyzer ga{freqHz, sampleRate};
    const GoertzelAnalyzer::Bin binA = ga.analyze(acfx::span<const float>(a));
    const GoertzelAnalyzer::Bin binB = ga.analyze(acfx::span<const float>(b));

    const double measuredOffset = detail::wrapToPi(binB.phaseRad - binA.phaseRad);
    CHECK(measuredOffset == doctest::Approx(phase1 - phase0).epsilon(kGoertzelEps));
}

TEST_CASE("SVF magnitude response respects passband/stopband bounds (FR-005, SC-002)") {
    const double passband = svfLowpassSteadyStateMagnitude(kPassbandFreqHz);
    const double stopband = svfLowpassSteadyStateMagnitude(kStopbandFreqHz);

    INFO("passband gain = " << passband << ", stopband gain = " << stopband);
    CHECK(passband >= kPassbandGainMin); // a decade below cutoff: ~unity
    CHECK(stopband <= kStopbandGainMax); // three octaves above cutoff: attenuated
    CHECK(passband > stopband);          // monotone lowpass ordering
}

TEST_CASE("SVF impulse response is finite, non-trivial, peaks early (FR-006)") {
    constexpr std::size_t N = 1024;
    std::vector<float> ir(N, 0.0f);

    acfx::SvfEffect fx;
    configureLowpass(fx, kRefCutoffHz);
    captureImpulseResponse(fx, acfx::ProcessContext{kRefSampleRate, 512, 1},
                           acfx::span<float>(ir), 1.0f);

    double energy = 0.0;
    for (std::size_t i = 0; i < N; ++i) {
        REQUIRE(std::isfinite(ir[i])); // no NaN / Inf anywhere in the response
        energy += static_cast<double>(ir[i]) * static_cast<double>(ir[i]);
    }
    CHECK(energy > 0.0); // the filter actually responded

    const int peak = ImpulseAnalyzer::peakIndex(acfx::span<const float>(ir));
    INFO("impulse-response peak index = " << peak);
    CHECK(peak >= 0);
    CHECK(peak < 64); // a stable lowpass impulse response peaks near the front
}

TEST_CASE("SVF passband phase is a finite principal-value angle (FR-007)") {
    const std::size_t total = static_cast<std::size_t>(kSettle + kMeasure);
    std::vector<float> in(total, 0.0f);
    std::vector<float> out(total, 0.0f);

    SineGenerator sine;
    sine.freqHz = kPassbandFreqHz; sine.sampleRate = kRefSampleRate;
    sine.amplitude = 1.0f; sine.phase = 0.0;
    sine.fill(acfx::span<float>(in));

    acfx::SvfEffect fx;
    configureLowpass(fx, kRefCutoffHz);
    capture(fx, acfx::ProcessContext{kRefSampleRate, 512, 1},
            acfx::span<const float>(in), acfx::span<float>(out));

    const acfx::span<const float> inWin(in.data() + kSettle,
                                        static_cast<std::size_t>(kMeasure));
    const acfx::span<const float> outWin(out.data() + kSettle,
                                         static_cast<std::size_t>(kMeasure));
    const double phi = phaseRad(inWin, outWin, kPassbandFreqHz, kRefSampleRate);

    INFO("passband phase (rad) = " << phi);
    REQUIRE(std::isfinite(phi));       // magnitude well above the floor => defined
    CHECK(phi > -std::numbers::pi);    // principal value (-pi, pi]
    CHECK(phi <= std::numbers::pi);
}

TEST_CASE("phaseRad returns NaN below the magnitude floor (FR-007 C1)") {
    // OUTPUT magnitude below kPhaseMagnitudeFloor: phase is meaningless and MUST
    // be skipped (NaN), never a spurious value. Here the output is silence.
    constexpr std::size_t N          = 4800;
    constexpr double      freqHz     = 1000.0;
    constexpr double      sampleRate = kRefSampleRate;

    std::vector<float> in(N, 0.0f);
    std::vector<float> silent(N, 0.0f);

    SineGenerator sine;
    sine.freqHz = freqHz; sine.sampleRate = sampleRate; sine.amplitude = 1.0f; sine.phase = 0.0;
    sine.fill(acfx::span<float>(in));

    // Capture a muted callable into `silent` — output amplitude is exactly zero.
    captureCallable([](float) { return 0.0f; },
                    acfx::span<const float>(in), acfx::span<float>(silent));

    const double phi = phaseRad(acfx::span<const float>(in),
                                acfx::span<const float>(silent),
                                freqHz, sampleRate);
    CHECK(std::isnan(phi));
}

TEST_CASE("Same metric calls measure a plain callable (FR-004, SC-001 agnosticism)") {
    // The SVF is an Effect; a 0.5x gain is a per-sample callable. The IDENTICAL
    // magnitude() call measures both — no effect-specific measurement code. The
    // recovered gain must equal the analytic 0.5.
    constexpr std::size_t N          = 4800; // 100 integer cycles @ 1kHz/48k
    constexpr double      freqHz     = 1000.0;
    constexpr double      sampleRate = 48000.0;

    std::vector<float> in(N, 0.0f);
    std::vector<float> out(N, 0.0f);

    SineGenerator sine;
    sine.freqHz = freqHz; sine.sampleRate = sampleRate; sine.amplitude = 1.0f; sine.phase = 0.0;
    sine.fill(acfx::span<float>(in));

    captureCallable([](float x) { return 0.5f * x; },
                    acfx::span<const float>(in), acfx::span<float>(out));

    const double gain = magnitude(acfx::span<const float>(out), freqHz, sampleRate);
    INFO("recovered gain = " << gain);
    CHECK(gain == doctest::Approx(0.5).epsilon(kGoertzelEps));
}

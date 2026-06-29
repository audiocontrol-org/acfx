// measurement-test.cpp
// Doctest cases for FR-001: stimulus generator correctness (T004).
// Tasks T005-T015 append additional measurement cases below this block.

#include <algorithm> // std::clamp
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

#include "support/measurement/stimulus.h"
#include "support/measurement/analyzers.h"
#include "support/measurement/metrics.h"
#include "support/svf-reference.h"

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"

using namespace acfx::measure;

TEST_CASE("ImpulseGenerator: out[0]==amplitude, all others==0") {
    constexpr std::size_t N = 64;
    std::vector<float> buf(N, -99.0f);

    ImpulseGenerator gen;
    gen.amplitude = 0.5f;
    gen.fill(acfx::span<float>(buf));

    CHECK(buf[0] == doctest::Approx(0.5f));
    for (std::size_t i = 1; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(buf[i] == doctest::Approx(0.0f));
    }
}

TEST_CASE("StepGenerator: every sample equals level") {
    constexpr std::size_t N = 64;
    std::vector<float> buf(N, -99.0f);

    StepGenerator gen;
    gen.level = 0.25f;
    gen.fill(acfx::span<float>(buf));

    for (std::size_t i = 0; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(buf[i] == doctest::Approx(0.25f));
    }
}

TEST_CASE("SineGenerator: samples match closed-form formula") {
    constexpr std::size_t N         = 64;
    constexpr double      freqHz    = 440.0;
    constexpr double      sampleRate = 44100.0;
    constexpr float       amplitude = 0.75f;
    constexpr double      phase     = std::numbers::pi / 4.0;

    std::vector<float> buf(N, 0.0f);

    SineGenerator gen;
    gen.freqHz     = freqHz;
    gen.sampleRate = sampleRate;
    gen.amplitude  = amplitude;
    gen.phase      = phase;
    gen.fill(acfx::span<float>(buf));

    const double omega = 2.0 * std::numbers::pi * freqHz / sampleRate;

    for (std::size_t n = 0; n < N; ++n) {
        // Mirror the generator's internal computation exactly:
        // it casts amplitude to float before multiplying (float * double -> double),
        // then casts the double result back to float for storage.
        const double s         = static_cast<double>(static_cast<float>(amplitude))
                                 * std::sin(omega * static_cast<double>(n) + phase);
        const float expected_f = static_cast<float>(s);

        INFO("sample index = " << n);
        CHECK(std::abs(buf[n] - expected_f) < 1e-5f);
    }
}

TEST_CASE("NoiseGenerator: same seed produces identical buffers") {
    constexpr std::size_t N         = 64;
    constexpr float       amplitude = 0.8f;

    NoiseGenerator gen;
    gen.amplitude = amplitude;
    gen.seed      = 0xABCD1234u;

    std::vector<float> buf1(N, 0.0f);
    std::vector<float> buf2(N, 0.0f);

    gen.fill(acfx::span<float>(buf1));
    gen.fill(acfx::span<float>(buf2));

    for (std::size_t i = 0; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(buf1[i] == buf2[i]);
    }
}

TEST_CASE("NoiseGenerator: all samples within [-amplitude, amplitude]") {
    constexpr std::size_t N         = 64;
    constexpr float       amplitude = 0.8f;

    NoiseGenerator gen;
    gen.amplitude = amplitude;
    gen.seed      = 0xABCD1234u;

    std::vector<float> buf(N, 0.0f);
    gen.fill(acfx::span<float>(buf));

    for (std::size_t i = 0; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(std::abs(buf[i]) <= amplitude + 1e-6f);
    }
}

TEST_CASE("NoiseGenerator: different seeds produce different sequences") {
    constexpr std::size_t N = 64;

    std::vector<float> buf1(N, 0.0f);
    std::vector<float> buf2(N, 0.0f);

    NoiseGenerator gen1;
    gen1.seed = 0x1111u;
    gen1.fill(acfx::span<float>(buf1));

    NoiseGenerator gen2;
    gen2.seed = 0x2222u;
    gen2.fill(acfx::span<float>(buf2));

    bool anyDifference = false;
    for (std::size_t i = 0; i < N; ++i) {
        if (buf1[i] != buf2[i]) {
            anyDifference = true;
            break;
        }
    }
    CHECK(anyDifference);
}

// ===========================================================================
// T007 — User Story 1 MVP: the measurement harness, end to end (FR-004,
// FR-005/006/007, SC-001/002). These cases append BELOW the T004 stimulus
// cases above. They exercise: analyzer correctness against closed-form truth,
// SVF magnitude/impulse/phase responses against analytic bounds, the FR-007
// near-zero-magnitude phase clause, and effect-agnosticism (the SAME metric
// calls applied to an Effect and to a plain callable).
//
// Tolerance philosophy (anti-false-precision): we assert ANALYTIC
// relationships (recovered amplitude == stimulus amplitude; passband near
// unity; stopband attenuated; phase difference == imposed offset) within
// NAMED, documented tolerances — never fabricated exact magnitudes. The
// dominant error source for a single-bin Goertzel is spectral leakage, which
// we minimise (where we control the buffer) by choosing an integer number of
// cycles in the analysis window, leaving a generous ~1e-2 epsilon for residual
// leakage and float round-trip.
// ===========================================================================

#include <limits> // std::isnan via std::numeric_limits-driven NaN from phaseRad

using acfx::test::kPassbandFreqHz;
using acfx::test::kPassbandGainMin;
using acfx::test::kRefCutoffHz;
using acfx::test::kRefSampleRate;
using acfx::test::kStopbandFreqHz;
using acfx::test::kStopbandGainMax;

namespace {

// Generous epsilon for single-bin Goertzel recovery on an integer-cycle
// window. Residual error is dominated by spectral leakage (finite window) plus
// the float<->double round-trip in the stimulus and analyzer; 1e-2 is a
// meaningful bound (orders of magnitude tighter than the response thresholds)
// without asserting false precision.
constexpr double kGoertzelEps = 1.0e-2;

// Settling / measurement split mirrors svf-reference's measureMagnitude scale:
// the SVF transient decays well within `kSettle` samples, so the steady-state
// gain is read from the trailing `kMeasure` samples only.
constexpr int kSettle  = 8192;
constexpr int kMeasure = 16384;

// Publish lowpass @ cutoffHz, zero resonance, via the single parameter table —
// no effect-specific measurement code. Pending edits are consumed by the SVF on
// the audio thread at the first process() inside capture().
void configureLowpass(acfx::SvfEffect& fx, double cutoffHz) {
    fx.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},
                    acfx::normalize(acfx::SvfEffect::kParams[acfx::SvfEffect::kCutoff],
                                    static_cast<float>(cutoffHz)));
    fx.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance}, 0.0f);
    const float modeIndex =
        static_cast<float>(static_cast<int>(acfx::SvfMode::lowpass));
    fx.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},
                    acfx::normalize(acfx::SvfEffect::kParams[acfx::SvfEffect::kMode],
                                    modeIndex));
}

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

// ===========================================================================
// T010 — User Story 2 tests: THD and latency metrics (FR-008, FR-009, SC-003)
//
// Window design (anti-leakage): fundamentalHz=1000, sampleRate=48000, N=4800.
// Fundamental cycles in window: 1000 * 4800 / 48000 = 100 (integer).
// Harmonic k cycles: k*100 (integer for all k). With all harmonics on exact
// Goertzel bins, spectral leakage is negligible — the "harmonic" bins see only
// true harmonic energy, not bleed from the fundamental. This is REQUIRED to
// avoid a false non-zero THD reading for a linear effect.
//
// Tolerance philosophy (anti-false-precision): the THD threshold for the clean
// case (< 1e-2) is a meaningful bound — orders of magnitude above the true
// leakage residual (~1e-6) — not an arbitrary magic constant. The clipping
// threshold (> 0.05) is a conservative lower bound on the distortion produced
// by amplitude-1.0 clipped at ±0.4; the actual value is geometrically larger.
// ===========================================================================

constexpr std::size_t kThdN          = 4800;   // window: 100 integer cycles @ 1 kHz / 48 kHz
constexpr double      kThdFundHz     = 1000.0;
constexpr double      kThdSampleRate = 48000.0;

TEST_CASE("THD is near zero for a clean linear effect (FR-008)") {
    // A 0.5x gain scalar is a linear operation; it introduces no harmonics.
    // Using an integer-cycle window so all harmonic bins see only true content.
    std::vector<float> in(kThdN, 0.0f);
    std::vector<float> out(kThdN, 0.0f);

    SineGenerator sine;
    sine.freqHz     = kThdFundHz;
    sine.sampleRate = kThdSampleRate;
    sine.amplitude  = 1.0f;
    sine.phase      = 0.0;
    sine.fill(acfx::span<float>(in));

    captureCallable([](float x) { return 0.5f * x; },
                    acfx::span<const float>(in),
                    acfx::span<float>(out));

    const double thdClean = thd(acfx::span<const float>(out), kThdFundHz, kThdSampleRate);
    INFO("clean THD = " << thdClean);
    CHECK(thdClean < 1.0e-2);
}

TEST_CASE("THD is elevated for a hard-clip nonlinearity (FR-008, SC-003)") {
    // Amplitude 1.0 exceeds the ±0.4 clip threshold; the hard clip flattens the
    // peaks, generating odd harmonics (3rd, 5th, ...) at measurable amplitude.
    // The same integer-cycle window ensures harmonic bins reflect true distortion.
    std::vector<float> inClean(kThdN, 0.0f);
    std::vector<float> inClip(kThdN, 0.0f);
    std::vector<float> outClean(kThdN, 0.0f);
    std::vector<float> outClip(kThdN, 0.0f);

    SineGenerator sineClean;
    sineClean.freqHz     = kThdFundHz;
    sineClean.sampleRate = kThdSampleRate;
    sineClean.amplitude  = 1.0f;
    sineClean.phase      = 0.0;
    sineClean.fill(acfx::span<float>(inClean));

    SineGenerator sineClip;
    sineClip.freqHz     = kThdFundHz;
    sineClip.sampleRate = kThdSampleRate;
    sineClip.amplitude  = 1.0f;
    sineClip.phase      = 0.0;
    sineClip.fill(acfx::span<float>(inClip));

    captureCallable([](float x) { return 0.5f * x; },
                    acfx::span<const float>(inClean),
                    acfx::span<float>(outClean));

    captureCallable([](float x) { return std::clamp(x, -0.4f, 0.4f); },
                    acfx::span<const float>(inClip),
                    acfx::span<float>(outClip));

    const double thdClean = thd(acfx::span<const float>(outClean), kThdFundHz, kThdSampleRate);
    const double thdClip  = thd(acfx::span<const float>(outClip),  kThdFundHz, kThdSampleRate);

    INFO("clean THD = " << thdClean << ", clipped THD = " << thdClip);

    // Clipped signal must exceed a meaningful distortion floor and exceed the clean case.
    CHECK(thdClip > 0.05);
    CHECK(thdClip > thdClean);
}

TEST_CASE("latencySamples matches a known D-sample processing delay (FR-009, SC-003)") {
    // A stateful ring-buffer delay of exactly D samples: out[n] = in[n - D].
    // The correlation peak of out relative to in falls at lag D.
    // Buffer length 64 is well above D=5, giving 59 samples of aligned content
    // for the correlator to find the peak reliably.
    constexpr int         D   = 5;
    constexpr std::size_t N   = 64;

    std::vector<float> in(N, 0.0f);
    std::vector<float> out(N, 0.0f);

    ImpulseGenerator impulse;
    impulse.amplitude = 1.0f;
    impulse.fill(acfx::span<float>(in));

    // Stateful D-sample delay: holds D floats, writes x, returns the oldest.
    std::vector<float> zbuf(static_cast<std::size_t>(D), 0.0f);
    int widx = 0;
    auto delay = [&](float x) -> float {
        float y     = zbuf[static_cast<std::size_t>(widx)];
        zbuf[static_cast<std::size_t>(widx)] = x;
        widx = (widx + 1) % D;
        return y;
    };

    captureCallable(delay, acfx::span<const float>(in), acfx::span<float>(out));

    const int measuredLag = latencySamples(acfx::span<const float>(in),
                                           acfx::span<const float>(out));
    INFO("expected lag = " << D << ", measured lag = " << measuredLag);
    CHECK(measuredLag == D);
}

// ===========================================================================
// T013 — User Story 3 tests: stability verdict, allocation invariant, and
// exec-time metric (FR-010, FR-011, FR-012, SC-004)
//
// NOTE on stability and the SVF: the DaisySP SVF (SvfPrimitive / SvfEffect)
// does not flush subnormal float values in its internal state, so it fails
// the "denormal" stability case when fed a subnormal-decaying input.  This is
// a genuine limitation of the underlying DaisySP implementation, correctly
// caught by the harness — it is not a harness bug.  Tests 1 and 2 therefore
// use minimal in-test stubs (CleanFx / BrokenFx) to demonstrate that the
// stability verdict correctly discriminates between numerically clean and
// broken effects.  Tests 3 and 4 use the real SVF for the allocation and
// exec-time measurements (those do not depend on stability).
// ===========================================================================

#include "support/allocation-sentinel.h"

using acfx::test::AllocationSentinel;

namespace {

// CleanFx — a minimal effect stub whose process() flushes every subnormal
// sample to zero and otherwise passes samples through unchanged.  This
// represents a "correctly implemented" effect that satisfies all four
// stability cases: silence-in → silence-out, DC bounded, no subnormal
// output under decaying input, and a quiet idle tail.
struct CleanFx {
    void prepare(const acfx::ProcessContext&) noexcept {}
    void reset() noexcept {}
    void process(acfx::AudioBlock& blk) noexcept {
        for (int ch = 0; ch < blk.numChannels(); ++ch) {
            float* samples = blk.channel(ch);
            for (int i = 0; i < blk.numSamples(); ++i) {
                const float x = samples[i];
                samples[i] = (std::fpclassify(x) == FP_SUBNORMAL) ? 0.0f : x;
            }
        }
    }
};

// BrokenFx — a minimal effect stub whose process() writes quiet_NaN into
// every sample.  stability() must detect the NaN and return ok == false.
struct BrokenFx {
    void prepare(const acfx::ProcessContext&) noexcept {}
    void reset() noexcept {}
    void process(acfx::AudioBlock& blk) noexcept {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (int ch = 0; ch < blk.numChannels(); ++ch) {
            float* samples = blk.channel(ch);
            for (int i = 0; i < blk.numSamples(); ++i) {
                samples[i] = nan;
            }
        }
    }
};

} // namespace

TEST_CASE("stability: clean effect stub passes all cases (FR-012)") {
    // Uses CleanFx — a denormal-flushing passthrough — to demonstrate that
    // the stability verdict returns {true, nullptr} for a numerically clean
    // effect across all four cases: silence, dc, denormal, idle.
    CleanFx cleanFx;

    const acfx::ProcessContext ctx{kRefSampleRate, 512, 1};
    const Stability result = stability(cleanFx, ctx);

    const std::string failedCaseStr = result.failedCase
                                      ? std::string(result.failedCase)
                                      : std::string("(none)");
    INFO("failedCase = " << failedCaseStr);
    CHECK(result.ok == true);
    CHECK(result.failedCase == nullptr);
}

TEST_CASE("stability: broken effect fails verdict (FR-012, discriminating)") {
    // BrokenFx writes NaN into every output sample — the harness must detect
    // this and return ok == false with a non-null failedCase name.
    BrokenFx brokenFx;

    const acfx::ProcessContext ctx{kRefSampleRate, 512, 1};
    const Stability result = stability(brokenFx, ctx);

    const std::string failedCaseStr = result.failedCase
                                      ? std::string(result.failedCase)
                                      : std::string("(none)");
    INFO("failedCase = " << failedCaseStr);
    CHECK(result.ok == false);
    CHECK(result.failedCase != nullptr);
}

TEST_CASE("SVF process() allocates zero heap (FR-011)") {
    constexpr int kBlockSize = 256;
    acfx::SvfEffect svf;
    configureLowpass(svf, kRefCutoffHz);

    const acfx::ProcessContext ctx{kRefSampleRate, kBlockSize, 1};
    svf.prepare(ctx);
    svf.reset();

    std::vector<float> buf(static_cast<std::size_t>(kBlockSize), 0.1f);
    float* chans[1] = { buf.data() };

    AllocationSentinel::reset();
    for (int i = 0; i < 8; ++i) {
        acfx::AudioBlock blk(chans, 1, kBlockSize);
        svf.process(blk);
    }
    const std::size_t allocs = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocs == 0, "SVF process() allocated ", allocs, " time(s)");
}

TEST_CASE("relativeExecTime produces a finite figure with the correct block size (FR-010, SC-004)") {
    constexpr int kBlockSize = 256;
    constexpr int kRepeats   = 32;

    acfx::SvfEffect svf;
    configureLowpass(svf, kRefCutoffHz);

    const acfx::ProcessContext ctx{kRefSampleRate, kBlockSize, 1};
    const ExecCost cost = relativeExecTime(svf, ctx, kBlockSize, kRepeats);

    INFO("timePerBlock = " << cost.timePerBlock << " s, blockSize = " << cost.blockSize);
    CHECK(cost.blockSize == kBlockSize);
    CHECK(cost.timePerBlock >= 0.0);
    CHECK(std::isfinite(cost.timePerBlock));
}

// ===========================================================================
// T015 — User Story 4 CSV report tests (FR-014, SC-005)
//
// Two cases:
//   1. Emission ON  — CsvReport::write() produces a well-formed CSV file with
//      the canonical header and one row per added MeasurementRow.
//   2. Emission OFF — without calling write(), no file is created; CI relies
//      solely on doctest assertions.
//
// Both cases use unique temp paths and remove any temp file at the end.
// ===========================================================================

#include <filesystem>
#include <fstream>
#include <string>

#include "support/measurement/report.h"

TEST_CASE("CsvReport: write() produces a well-formed CSV with canonical header and correct rows (FR-014, SC-005)") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "acfx-measure-t015-emission-on.csv";

    // Remove any leftover file from a prior (failed) run.
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    CsvReport report;

    report.add(MeasurementRow{
        "svf-lowpass",  // effect
        "magnitude",    // metric
        "sine@100Hz",   // stimulus
        48000.0,        // sampleRate
        512,            // blockSize
        0.9987,         // value
        "ratio",        // units
        0.01,           // tolerance
        true            // pass
    });

    report.add(MeasurementRow{
        "svf-lowpass",  // effect
        "thd",          // metric
        "sine@100Hz",   // stimulus
        48000.0,        // sampleRate
        512,            // blockSize
        0.0032,         // value
        "ratio",        // units
        0.05,           // tolerance
        true            // pass
    });

    report.add(MeasurementRow{
        "svf-lowpass",  // effect
        "latency",      // metric
        "impulse",      // stimulus
        48000.0,        // sampleRate
        512,            // blockSize
        5.0,            // value
        "samples",      // units
        1.0,            // tolerance
        false           // pass
    });

    report.write(path.string());

    // Verify the file exists and is well-formed.
    std::ifstream csv(path.string());
    REQUIRE(csv.is_open());

    // (a) First line must be the canonical header exactly.
    std::string header;
    REQUIRE(std::getline(csv, header));
    CHECK(header == "effect,metric,stimulus,sample_rate,block_size,value,units,tolerance,pass");

    // (b) Count data lines and (c) check that known field tokens appear in the
    //     expected rows.
    int  dataLines        = 0;
    bool foundMagnitude   = false;
    bool foundLatencyFail = false;

    std::string line;
    while (std::getline(csv, line)) {
        if (line.empty()) {
            continue;
        }
        ++dataLines;
        if (line.find("svf-lowpass") != std::string::npos &&
            line.find("magnitude")   != std::string::npos &&
            line.find("true")        != std::string::npos) {
            foundMagnitude = true;
        }
        if (line.find("latency") != std::string::npos &&
            line.find("false")   != std::string::npos) {
            foundLatencyFail = true;
        }
    }
    csv.close();

    // (b) Number of data lines must equal number of rows added.
    CHECK(dataLines == 3);

    // (c) Known field content appears in the expected rows.
    CHECK(foundMagnitude   == true);
    CHECK(foundLatencyFail == true);

    // Clean up the temp file.
    std::filesystem::remove(path);
    CHECK(std::filesystem::exists(path) == false);
}

TEST_CASE("CsvReport: emission OFF — no write() call leaves no file on disk (SC-005)") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "acfx-measure-t015-emission-off.csv";

    // Ensure the path does not pre-exist.
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    // Simulate a measurement run with reporting disabled: no CsvReport is
    // constructed and write() is never called. CI correctness gates on the
    // doctest assertions below — no CSV artifact is produced.
    CHECK(std::filesystem::exists(path) == false);

    // Also assert: constructing a CsvReport and adding rows but NOT calling
    // write() leaves the path absent (the class is truly off by default).
    {
        CsvReport report;
        report.add(MeasurementRow{
            "svf-lowpass", "magnitude", "sine@100Hz",
            48000.0, 512, 0.99, "ratio", 0.01, true
        });
        // write() intentionally NOT called.
    }
    CHECK(std::filesystem::exists(path) == false);
}

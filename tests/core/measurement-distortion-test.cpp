// measurement-distortion-test.cpp
// Doctest cases for US2: THD and latency metrics (T010).
// FR-008, FR-009, SC-003.
//
// Window design (anti-leakage): fundamentalHz=1000, sampleRate=48000, N=4800.
// Fundamental cycles in window: 1000 * 4800 / 48000 = 100 (integer).
// Harmonic k cycles: k*100 (integer for all k).  With all harmonics on exact
// Goertzel bins, spectral leakage is negligible — the "harmonic" bins see only
// true harmonic energy, not bleed from the fundamental.  This is REQUIRED to
// avoid a false non-zero THD reading for a linear effect.
//
// Tolerance philosophy (anti-false-precision): the THD threshold for the clean
// case (< 1e-2) is a meaningful bound — orders of magnitude above the true
// leakage residual (~1e-6) — not an arbitrary magic constant.  The clipping
// threshold (> 0.05) is a conservative lower bound on the distortion produced
// by amplitude-1.0 clipped at +/-0.4; the actual value is geometrically larger.

#include <algorithm>
#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "support/measurement/stimulus.h"
#include "support/measurement/metrics.h"
#include "dsp/audio-block.h"

using namespace acfx::measure;

namespace {

constexpr std::size_t kThdN          = 4800;   // window: 100 integer cycles @ 1 kHz / 48 kHz
constexpr double      kThdFundHz     = 1000.0;
constexpr double      kThdSampleRate = 48000.0;

} // namespace

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
    // Amplitude 1.0 exceeds the +/-0.4 clip threshold; the hard clip flattens
    // the peaks, generating odd harmonics (3rd, 5th, ...) at measurable
    // amplitude.  The same integer-cycle window ensures harmonic bins reflect
    // true distortion.
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

TEST_CASE("THD is NaN (not 0.0) for a dead/silent effect (FR-008, AUDIT-20260629-06)") {
    // A dead effect (no output) has no measurable fundamental. THD must report
    // NaN, NOT 0.0 — a 0.0 would masquerade as a perfectly linear effect and
    // silently pass `thd < threshold` for a completely broken effect.
    std::vector<float> in(kThdN, 0.0f);
    std::vector<float> out(kThdN, 0.0f);

    SineGenerator sine;
    sine.freqHz = kThdFundHz; sine.sampleRate = kThdSampleRate; sine.amplitude = 1.0f; sine.phase = 0.0;
    sine.fill(acfx::span<float>(in));

    // A "muted" effect: output is silence regardless of input.
    captureCallable([](float) { return 0.0f; },
                    acfx::span<const float>(in), acfx::span<float>(out));

    const double thdDead = thd(acfx::span<const float>(out), kThdFundHz, kThdSampleRate);
    INFO("dead-effect THD = " << thdDead);
    CHECK(std::isnan(thdDead));
}

TEST_CASE("THD is NaN (not 0.0) when no harmonic falls below Nyquist (FR-008, AUDIT-20260629-06)") {
    // Fundamental 15 kHz at 44.1 kHz: the 2nd harmonic (30 kHz) already exceeds
    // Nyquist (22.05 kHz), so no harmonic is measurable. Distortion is
    // UNMEASURABLE here — distinct from "linear" — so THD must be NaN, not 0.0.
    constexpr double      fundHz = 15000.0;
    constexpr double      srHz   = 44100.0;
    constexpr std::size_t N      = 4096;

    std::vector<float> in(N, 0.0f);
    std::vector<float> out(N, 0.0f);

    SineGenerator sine;
    sine.freqHz = fundHz; sine.sampleRate = srHz; sine.amplitude = 1.0f; sine.phase = 0.0;
    sine.fill(acfx::span<float>(in));

    captureCallable([](float x) { return 0.5f * x; },  // linear, but harmonics out of band
                    acfx::span<const float>(in), acfx::span<float>(out));

    const double thdOutOfBand = thd(acfx::span<const float>(out), fundHz, srHz);
    INFO("out-of-band THD = " << thdOutOfBand);
    CHECK(std::isnan(thdOutOfBand));
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

TEST_CASE("latencySamples detects a POLARITY-INVERTED delay (FR-009, AUDIT-20260629-04)") {
    // out[n] = -in[n - D]: a delayed AND polarity-inverted processor (inverting
    // all-pass / 180-degree flip). The true-delay correlation peak is strongly
    // NEGATIVE; a signed-maximum correlator would wrongly report lag 0. The
    // magnitude-based selection must still recover D.
    constexpr int         D = 5;
    constexpr std::size_t N = 64;

    std::vector<float> in(N, 0.0f);
    std::vector<float> out(N, 0.0f);

    ImpulseGenerator impulse;
    impulse.amplitude = 1.0f;
    impulse.fill(acfx::span<float>(in));

    // Stateful D-sample delay that ALSO inverts polarity: out[n] = -in[n - D].
    std::vector<float> zbuf(static_cast<std::size_t>(D), 0.0f);
    int widx = 0;
    auto invertingDelay = [&](float x) -> float {
        float y     = zbuf[static_cast<std::size_t>(widx)];
        zbuf[static_cast<std::size_t>(widx)] = x;
        widx = (widx + 1) % D;
        return -y;
    };

    captureCallable(invertingDelay, acfx::span<const float>(in), acfx::span<float>(out));

    const int measuredLag = latencySamples(acfx::span<const float>(in),
                                           acfx::span<const float>(out));
    INFO("expected lag = " << D << " (inverted), measured lag = " << measuredLag);
    CHECK(measuredLag == D);
}

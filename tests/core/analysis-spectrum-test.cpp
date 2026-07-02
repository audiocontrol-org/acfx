// analysis-spectrum-test.cpp
// T008 -- harmonic-analysis feature, User Story 1: RED test for the full
// harmonic spectrum (contracts/analysis-engine-api.md "spectrum.h",
// data-model.md "HarmonicSpectrum", FR-001/FR-008). Asserts:
//   - a symmetric (odd-only) synthesized nonlinearity output shows odd
//     harmonics at their analytic magnitude+phase and even harmonics at
//     ~zero magnitude, matching the classic "symmetric nonlinearity -> odd
//     harmonics only" analytic prediction (US1 scenario 1).
//   - a biased/asymmetric synthesized signal (DC + even AND odd harmonics)
//     shows both even and odd harmonics present.
//   - a harmonic whose magnitude falls below the phase floor reports phase
//     as NaN (FR-008), never a spurious value, while its magnitude is still
//     a real (near-zero) measurement.
//   - a harmonic at or above Nyquist is reported NOT-MEASURED (NaN for both
//     magnitude and phase), never fabricated 0.0 (FR-008, edge case
//     "Harmonics above Nyquist").
//   - at(k) is a 1-based accessor (at(1) == fundamental); k outside
//     [1, numHarmonics] is reported NOT-MEASURED, not fabricated 0.0.
//
// host/analysis/spectrum.h does not exist yet at RED time -- this test is
// expected to FAIL TO BUILD until T009 lands it. Do NOT implement
// spectrum.h to make this test pass; that is a separate task.
//
// Integer-cycle windows throughout (fundamentalHz * N / sampleRate a whole
// number for every harmonic used) so every harmonic lands exactly on its own
// Goertzel bin with zero frequency-alignment leakage, mirroring every other
// suite in this tree (analysis-fft-test.cpp, measurement-*-test.cpp).

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <vector>

#include "analysis/spectrum.h"
#include "dsp/span.h"

using acfx::analysis::HarmonicSpectrum;
using acfx::analysis::harmonicSpectrum;
using acfx::analysis::kPhaseMagnitudeFloor;

namespace {

// Tolerance philosophy (anti-false-precision, mirrors analysis-fft-test.cpp
// and measurement-response-test.cpp's kGoertzelEps): the dominant error
// source for an integer-cycle single-bin Goertzel readout is the float
// round-trip in the synthesized stimulus, several orders of magnitude below
// this bound. 2% is a generous, named tolerance that still fails a wrong
// scale factor or a mislocated harmonic.
constexpr double kMagnitudeTolerance = 0.02; // relative (doctest::Approx::epsilon)

// Absolute tolerance on a phase reading in radians. Same rationale as
// kMagnitudeTolerance; phase differences here are compared absolutely
// because doctest::Approx's default scale=1 already yields an
// effectively-absolute bound of this order for values near zero.
constexpr double kPhaseToleranceRad = 0.02;

// A harmonic whose analytic amplitude is exactly zero (absent from the
// synthesized signal) must read back below this ceiling -- several orders of
// magnitude above the float round-trip noise floor (~1e-6) yet safely below
// kPhaseMagnitudeFloor, so the "near-zero magnitude -> NaN phase" contract
// (FR-008) is exercised meaningfully rather than by coincidence.
constexpr double kAbsentHarmonicMagnitudeCeiling = 1.0e-3;

constexpr double kPi     = std::numbers::pi;
constexpr double kHalfPi = std::numbers::pi / 2.0;

// Wraps an angle in radians to the principal-value range (-pi, pi]. Local,
// self-contained helper (mirrors acfx::measure::detail::wrapToPi) -- this
// test file does not reach into tests/support/measurement/, per
// research.md Decision 1's one-way dependency (tests/support ->
// host/analysis, never the reverse; a test file depends on neither
// direction here, it is simply self-contained).
double wrapToPi(double angleRad) {
    const double twoPi = 2.0 * kPi;
    double wrapped = std::fmod(angleRad, twoPi);
    if (wrapped <= -kPi)
        wrapped += twoPi;
    else if (wrapped > kPi)
        wrapped -= twoPi;
    return wrapped;
}

// Adds amplitude*sin(2*pi*freqHz*n/sampleRate + phaseParam) into `out`
// (accumulates -- does not overwrite), matching SineGenerator's phase
// convention (stimulus.h) so multiple harmonics can be superposed.
void addSine(std::vector<float>& out, double freqHz, double sampleRate, double amplitude, double phaseParam) {
    const double omega = 2.0 * kPi * freqHz / sampleRate;
    for (std::size_t n = 0; n < out.size(); ++n) {
        const double s = amplitude * std::sin(omega * static_cast<double>(n) + phaseParam);
        out[n] += static_cast<float>(s);
    }
}

// Analytic prediction for the Goertzel/DFT phase readout of a pure
// sin(w*n + phaseParam) component (negative-exponent DFT convention, per
// analyzers.h's GoertzelAnalyzer doc comment): the readout phase is
// phaseParam - pi/2 (derivation: X[k] = sum x[n]*e^{-jwn} for an
// integer-cycle window reduces to (A*N/2) * e^{j(phaseParam - pi/2)}).
// Independently verified in measurement-response-test.cpp's
// "Goertzel phase tracks a known phase offset" case (phase DIFFERENCES
// track exactly); this derives the ABSOLUTE constant so a single-harmonic
// phase can be asserted directly rather than only a relative offset.
double expectedGoertzelPhase(double phaseParam) {
    return wrapToPi(phaseParam - kHalfPi);
}

} // namespace

TEST_CASE("harmonicSpectrum: symmetric (odd-only) signal -- odd harmonics present, even ~zero (FR-001/008, US1)") {
    // Integer-cycle window: fundamentalHz=1000, sampleRate=48000, N=4800 =>
    // 100 cycles of the fundamental (and an integer cycle count for every
    // harmonic up to Nyquist).
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr int         kNumHarm    = 8;

    // A symmetric memoryless nonlinearity (e.g. tanh on a sine) produces
    // ONLY odd harmonics; synthesize that analytic prediction directly so
    // the expected magnitudes/phases are exact, not approximated by a
    // specific nonlinearity's Taylor series.
    struct { int k; double amplitude; double phaseParam; } odd[] = {
        {1, 1.00, 0.3},
        {3, 0.30, 1.0},
        {5, 0.10, 2.0},
        {7, 0.05, -0.5},
    };

    std::vector<float> in(kN, 0.0f);
    for (const auto& h : odd) {
        addSine(in, static_cast<double>(h.k) * kFundHz, kSampleRate, h.amplitude, h.phaseParam);
    }

    const HarmonicSpectrum spectrum =
        harmonicSpectrum(acfx::span<const float>(in), kFundHz, kSampleRate, kNumHarm);

    CHECK(spectrum.fundamentalHz == doctest::Approx(kFundHz));
    CHECK(spectrum.numHarmonics == kNumHarm);

    // Present odd harmonics: magnitude and phase match the analytic
    // prediction within tolerance.
    for (const auto& h : odd) {
        const HarmonicSpectrum::Bin bin = spectrum.at(h.k);
        INFO("odd harmonic k=" << h.k);
        REQUIRE(!std::isnan(bin.magnitude));
        CHECK(bin.magnitude == doctest::Approx(h.amplitude).epsilon(kMagnitudeTolerance));
        REQUIRE(!std::isnan(bin.phaseRad));
        CHECK(bin.phaseRad == doctest::Approx(expectedGoertzelPhase(h.phaseParam)).epsilon(kPhaseToleranceRad));
    }

    // Absent even harmonics: near-zero magnitude, undefined (NaN) phase.
    for (int k : {2, 4, 6, 8}) {
        const HarmonicSpectrum::Bin bin = spectrum.at(k);
        INFO("even harmonic k=" << k);
        REQUIRE(!std::isnan(bin.magnitude)); // a real (near-zero) measurement, not "unmeasurable"
        CHECK(bin.magnitude < kAbsentHarmonicMagnitudeCeiling);
        CHECK(std::isnan(bin.phaseRad)); // FR-008: below the floor, phase is undefined
    }
}

TEST_CASE("harmonicSpectrum: biased/asymmetric signal -- even AND odd harmonics present (FR-001, US1)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr int         kNumHarm    = 4;
    constexpr float       kDcBias     = 0.2f;

    // A biased/asymmetric nonlinearity (rectification, a DC-offset clip, an
    // even-order-heavy waveshaper) produces DC plus BOTH even and odd
    // harmonics. Synthesize that analytic prediction directly, plus a DC
    // offset -- an integer-cycle window makes DC exactly orthogonal to every
    // nonzero harmonic bin, so it must not perturb the harmonic readout.
    struct { int k; double amplitude; double phaseParam; } present[] = {
        {1, 0.80, 0.0},
        {2, 0.40, 1.2},
        {3, 0.15, -1.0},
    };

    std::vector<float> in(kN, kDcBias);
    for (const auto& h : present) {
        addSine(in, static_cast<double>(h.k) * kFundHz, kSampleRate, h.amplitude, h.phaseParam);
    }

    const HarmonicSpectrum spectrum =
        harmonicSpectrum(acfx::span<const float>(in), kFundHz, kSampleRate, kNumHarm);

    for (const auto& h : present) {
        const HarmonicSpectrum::Bin bin = spectrum.at(h.k);
        INFO("harmonic k=" << h.k);
        REQUIRE(!std::isnan(bin.magnitude));
        CHECK(bin.magnitude == doctest::Approx(h.amplitude).epsilon(kMagnitudeTolerance));
        REQUIRE(!std::isnan(bin.phaseRad));
        CHECK(bin.phaseRad == doctest::Approx(expectedGoertzelPhase(h.phaseParam)).epsilon(kPhaseToleranceRad));
    }

    // 4th harmonic is absent from the synthesized signal: near-zero.
    const HarmonicSpectrum::Bin bin4 = spectrum.at(4);
    REQUIRE(!std::isnan(bin4.magnitude));
    CHECK(bin4.magnitude < kAbsentHarmonicMagnitudeCeiling);
}

TEST_CASE("harmonicSpectrum: sub-floor magnitude -> NaN phase, not a spurious value (FR-008)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr int         kNumHarm    = 5;
    constexpr double      kAmplitude  = 0.7;
    constexpr double      kPhaseParam = 1.2;

    // A pure single tone: the fundamental is present and well-defined; every
    // other requested harmonic is absent (magnitude near the float
    // round-trip noise floor, several orders of magnitude below
    // kPhaseMagnitudeFloor), so its phase MUST read back as NaN.
    std::vector<float> in(kN, 0.0f);
    addSine(in, kFundHz, kSampleRate, kAmplitude, kPhaseParam);

    const HarmonicSpectrum spectrum =
        harmonicSpectrum(acfx::span<const float>(in), kFundHz, kSampleRate, kNumHarm);

    const HarmonicSpectrum::Bin fundamental = spectrum.at(1);
    REQUIRE(!std::isnan(fundamental.magnitude));
    CHECK(fundamental.magnitude == doctest::Approx(kAmplitude).epsilon(kMagnitudeTolerance));
    REQUIRE(!std::isnan(fundamental.phaseRad));
    CHECK(fundamental.phaseRad == doctest::Approx(expectedGoertzelPhase(kPhaseParam)).epsilon(kPhaseToleranceRad));

    for (int k = 2; k <= kNumHarm; ++k) {
        const HarmonicSpectrum::Bin bin = spectrum.at(k);
        INFO("absent harmonic k=" << k);
        REQUIRE(!std::isnan(bin.magnitude));
        CHECK(bin.magnitude < kPhaseMagnitudeFloor); // below the floor that gates phase validity
        CHECK(std::isnan(bin.phaseRad));             // FR-008: undefined, never a spurious value
    }
}

TEST_CASE("harmonicSpectrum: harmonics at/above Nyquist are NOT-MEASURED, never fabricated 0.0 (FR-008)") {
    // fundamentalHz=8000, sampleRate=48000 => Nyquist=24000. k=3 lands
    // EXACTLY on Nyquist (24000 >= 24000); k=4 clearly exceeds it. Both are
    // out-of-band and unmeasurable. N=480 keeps every in-band harmonic on an
    // integer cycle count (8000*480/48000 = 80 cycles).
    constexpr std::size_t kN          = 480;
    constexpr double      kFundHz     = 8000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr int         kNumHarm    = 4;

    std::vector<float> in(kN, 0.0f);
    addSine(in, 1.0 * kFundHz, kSampleRate, 1.0, 0.0);
    addSine(in, 2.0 * kFundHz, kSampleRate, 0.4, 1.0);

    const HarmonicSpectrum spectrum =
        harmonicSpectrum(acfx::span<const float>(in), kFundHz, kSampleRate, kNumHarm);

    // In-band harmonics (k=1, k=2) are measured normally.
    const HarmonicSpectrum::Bin bin1 = spectrum.at(1);
    REQUIRE(!std::isnan(bin1.magnitude));
    CHECK(bin1.magnitude == doctest::Approx(1.0).epsilon(kMagnitudeTolerance));

    const HarmonicSpectrum::Bin bin2 = spectrum.at(2);
    REQUIRE(!std::isnan(bin2.magnitude));
    CHECK(bin2.magnitude == doctest::Approx(0.4).epsilon(kMagnitudeTolerance));

    // Out-of-band harmonics (k=3 at exactly Nyquist, k=4 above it): NOT-
    // MEASURED -- both magnitude AND phase are the NaN sentinel, never a
    // fabricated 0.0.
    for (int k : {3, 4}) {
        const HarmonicSpectrum::Bin bin = spectrum.at(k);
        INFO("out-of-band harmonic k=" << k);
        CHECK(std::isnan(bin.magnitude));
        CHECK(std::isnan(bin.phaseRad));
    }
}

TEST_CASE("harmonicSpectrum: at(k) is 1-based; out-of-range k is NOT-MEASURED, not fabricated 0.0 (FR-001)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr int         kNumHarm    = 4;

    std::vector<float> in(kN, 0.0f);
    addSine(in, kFundHz, kSampleRate, 1.0, 0.0);

    const HarmonicSpectrum spectrum =
        harmonicSpectrum(acfx::span<const float>(in), kFundHz, kSampleRate, kNumHarm);

    // at(1) is the fundamental: well-defined, magnitude ~= the driven amplitude.
    const HarmonicSpectrum::Bin fundamental = spectrum.at(1);
    REQUIRE(!std::isnan(fundamental.magnitude));
    CHECK(fundamental.magnitude == doctest::Approx(1.0).epsilon(kMagnitudeTolerance));

    // k=0 and k > numHarmonics are out of the requested range: NOT-MEASURED
    // (NaN), never a fabricated 0.0.
    const HarmonicSpectrum::Bin belowRange = spectrum.at(0);
    CHECK(std::isnan(belowRange.magnitude));
    CHECK(std::isnan(belowRange.phaseRad));

    const HarmonicSpectrum::Bin aboveRange = spectrum.at(kNumHarm + 1);
    CHECK(std::isnan(aboveRange.magnitude));
    CHECK(std::isnan(aboveRange.phaseRad));
}

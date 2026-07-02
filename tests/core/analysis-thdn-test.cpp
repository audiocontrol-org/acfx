// analysis-thdn-test.cpp
// T010 -- harmonic-analysis feature, User Story 1: RED test for THD+N by the
// RESIDUAL (notch-the-fundamental) method plus the noise-floor / SNR figures
// (contracts/analysis-engine-api.md "thdn.h", data-model.md "ThdnResult",
// research.md Decision 4, FR-002/FR-008).
//
// Definition under test (research Decision 4, FR-002):
//   THD+N = RMS(everything except the fundamental) / RMS(fundamental).
//   Notch the fundamental (remove its bin/energy); ALL remaining energy
//   (harmonics + broadband noise together) is the numerator. This is DISTINCT
//   from the shipped single-bin thd() in tests/support/measurement/metrics.h,
//   which sums only a fixed set of HARMONIC bins and ignores broadband noise.
//   SNR is referenced to the fundamental level. Unmeasurable (no fundamental)
//   -> NaN sentinel (FR-008), never a fabricated 0.0.
//
// Asserted contract for the NOT-YET-EXISTING host/analysis/thdn.h:
//   - a pure sine (no distortion, no noise) -> THD+N ~ 0 within a named,
//     generous ceiling (only float round-trip residual remains).
//   - a sine + known deterministic harmonics (no noise) -> THD+N equals the
//     analytic sqrt(sum of harmonic powers)/fundamental, and noiseFloor / snr
//     match their analytic values (cross-check against the residual definition;
//     with no noise the residual IS the harmonic energy).
//   - a sine + known deterministic white noise -> THD+N, noiseFloor and snr
//     match analytic predictions derived INDEPENDENTLY from the noise buffer's
//     own measured RMS; snr referenced to the fundamental level.
//   - no fundamental / silence -> THD+N is NaN (FR-008), never 0.0.
//
// host/analysis/thdn.h does not exist yet at RED time -- this test is expected
// to FAIL TO BUILD until T011 lands it. Do NOT implement thdn.h to make this
// test pass; that is a separate task.
//
// Integer-cycle windows throughout (freqHz * N / sampleRate a whole number for
// the fundamental and every synthesized harmonic) so each component lands
// exactly on its own bin and the residual is leakage-free, mirroring every
// other suite in this tree (analysis-fft-test.cpp, analysis-spectrum-test.cpp,
// measurement-*-test.cpp).

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "analysis/stimulus.h"  // acfx::measure::SineGenerator, NoiseGenerator
#include "analysis/thdn.h"      // UNDER TEST (does not exist at RED time)
#include "dsp/span.h"

using acfx::analysis::ThdnResult;
using acfx::analysis::thdPlusN;
using acfx::measure::NoiseGenerator;
using acfx::measure::SineGenerator;

namespace {

// A pure integer-cycle sine's residual after the notch is only the float
// round-trip quantization noise of the synthesized buffer (~1e-7 RMS relative
// to a unit fundamental). This ceiling is several orders of magnitude above
// that floor yet still fails any real (>=0.1%) distortion or a mislocated
// notch. Anti-false-precision: a named bound, not a fitted constant.
constexpr double kPureSineThdnCeiling = 1.0e-4;

// Relative tolerance for the deterministic (noise-free) harmonic case. The
// residual is exact analytic harmonic energy up to the float round-trip; 2%
// mirrors analysis-spectrum-test.cpp's kMagnitudeTolerance -- generous, yet it
// fails a wrong scale factor or a mis-summed residual.
constexpr double kHarmonicRelTol = 0.02;

// Relative tolerance for the noise case. The residual method's single-bin notch
// also removes the noise energy that happens to fall in the fundamental bin,
// and the finite-length sine/noise cross-correlation is not exactly zero; both
// are bounded well under 2% for the (large N, moderate noise) parameters below.
// 5% is a comfortable, honest envelope over those finite-length statistical
// terms without masking a real error.
constexpr double kNoiseRelTol = 0.05;

// Absolute dB tolerance for the SNR readout in the noise case (5% in linear
// residual terms is ~0.45 dB near these levels; 0.6 dB covers it with margin).
constexpr double kSnrTolDb = 0.6;

// RMS of a real buffer (double accumulation), computed independently of the
// unit under test so the noise-case predictions are a genuine external
// reference.
double rms(const std::vector<float>& x) {
    double acc = 0.0;
    for (const float s : x) {
        acc += static_cast<double>(s) * static_cast<double>(s);
    }
    return std::sqrt(acc / static_cast<double>(x.size()));
}

// Accumulate amplitude*sin(2*pi*freqHz*n/sampleRate) into `out` (matches
// SineGenerator's phase convention so harmonics superpose correctly).
void addSine(std::vector<float>& out, double freqHz, double sampleRate, double amplitude) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const double omega = kTwoPi * freqHz / sampleRate;
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] += static_cast<float>(amplitude * std::sin(omega * static_cast<double>(n)));
    }
}

} // namespace

TEST_CASE("thdPlusN: pure sine (no distortion, no noise) -> THD+N ~ 0 (FR-002, US1)") {
    // Integer-cycle: f=1000, fs=48000, N=4800 => 100 cycles of the fundamental.
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr float       kAmplitude  = 1.0f;

    std::vector<float> in(kN, 0.0f);
    SineGenerator{kFundHz, kSampleRate, kAmplitude, 0.0}.fill(acfx::span<float>{in});

    const ThdnResult r = thdPlusN(acfx::span<const float>(in), kFundHz, kSampleRate);

    REQUIRE(!std::isnan(r.thdPlusN));
    CHECK(r.thdPlusN < kPureSineThdnCeiling);
    // noiseFloor (residual level) is likewise near zero for an undistorted tone.
    REQUIRE(!std::isnan(r.noiseFloor));
    CHECK(r.noiseFloor < kPureSineThdnCeiling);
    // SNR referenced to the fundamental is very high (fundamental >> residual):
    // finite and comfortably above 80 dB for a leakage-free unit tone.
    REQUIRE(!std::isnan(r.snr));
    CHECK(r.snr > 80.0);
}

TEST_CASE("thdPlusN: sine + known harmonics (no noise) -> analytic residual, noiseFloor, snr (FR-002, US1)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;

    // Fundamental + two deterministic harmonics; all integer-cycle (2f, 3f
    // land on 200, 300 cycles) so the residual is exactly their combined power.
    constexpr double kA1 = 1.00;  // fundamental
    constexpr double kA2 = 0.10;  // 2nd harmonic
    constexpr double kA3 = 0.05;  // 3rd harmonic

    std::vector<float> in(kN, 0.0f);
    addSine(in, 1.0 * kFundHz, kSampleRate, kA1);
    addSine(in, 2.0 * kFundHz, kSampleRate, kA2);
    addSine(in, 3.0 * kFundHz, kSampleRate, kA3);

    const ThdnResult r = thdPlusN(acfx::span<const float>(in), kFundHz, kSampleRate);

    // Analytic residual method (RMS everything-but-fundamental over RMS
    // fundamental). RMS of amplitude A sine = A/sqrt(2); the sqrt(2) cancels in
    // the ratio, leaving sqrt(A2^2 + A3^2)/A1.
    const double harmEnergy   = kA2 * kA2 + kA3 * kA3;         // sum of harmonic amplitude^2
    const double expectedThdn = std::sqrt(harmEnergy) / kA1;    // ~0.1118
    const double expectedFloor = std::sqrt(harmEnergy / 2.0);   // residual RMS (absolute)
    const double rmsFund       = kA1 / std::sqrt(2.0);
    const double expectedSnrDb = 20.0 * std::log10(rmsFund / expectedFloor);

    REQUIRE(!std::isnan(r.thdPlusN));
    CHECK(r.thdPlusN == doctest::Approx(expectedThdn).epsilon(kHarmonicRelTol));
    REQUIRE(!std::isnan(r.noiseFloor));
    CHECK(r.noiseFloor == doctest::Approx(expectedFloor).epsilon(kHarmonicRelTol));
    REQUIRE(!std::isnan(r.snr));
    CHECK(r.snr == doctest::Approx(expectedSnrDb).epsilon(kHarmonicRelTol));
}

TEST_CASE("thdPlusN: sine + known white noise -> THD+N/noiseFloor/snr vs analytic (FR-002, US1)") {
    // Large N (1000 fundamental cycles) keeps the finite-length sine/noise
    // cross-terms and the single-bin notch leakage well under the tolerance.
    constexpr std::size_t  kN          = 48000;
    constexpr double       kFundHz     = 1000.0;
    constexpr double       kSampleRate = 48000.0;
    constexpr float        kAmplitude  = 1.0f;
    constexpr float        kNoiseAmp   = 0.5f;
    constexpr std::uint32_t kSeed      = 0xC0FFEEu;

    // Deterministic noise in its own buffer so we can measure its TRUE RMS as
    // an external reference (independent of the unit under test).
    std::vector<float> noise(kN, 0.0f);
    NoiseGenerator{kNoiseAmp, kSeed}.fill(acfx::span<float>{noise});
    const double noiseRms = rms(noise);  // ~kNoiseAmp/sqrt(3) but exact here

    // Combined stimulus = fundamental + that same noise realization.
    std::vector<float> in(kN, 0.0f);
    SineGenerator{kFundHz, kSampleRate, kAmplitude, 0.0}.fill(acfx::span<float>{in});
    for (std::size_t i = 0; i < kN; ++i) {
        in[i] += noise[i];
    }

    const ThdnResult r = thdPlusN(acfx::span<const float>(in), kFundHz, kSampleRate);

    // The fundamental is notched out; the entire residual is the broadband
    // noise. Predictions from the measured noise RMS and the known fundamental.
    const double rmsFund       = static_cast<double>(kAmplitude) / std::sqrt(2.0);
    const double expectedThdn  = noiseRms / rmsFund;
    const double expectedFloor = noiseRms;
    const double expectedSnrDb = 20.0 * std::log10(rmsFund / noiseRms);

    REQUIRE(!std::isnan(r.thdPlusN));
    CHECK(r.thdPlusN == doctest::Approx(expectedThdn).epsilon(kNoiseRelTol));
    REQUIRE(!std::isnan(r.noiseFloor));
    CHECK(r.noiseFloor == doctest::Approx(expectedFloor).epsilon(kNoiseRelTol));
    REQUIRE(!std::isnan(r.snr));
    CHECK(r.snr == doctest::Approx(expectedSnrDb).epsilon(kSnrTolDb));
}

TEST_CASE("thdPlusN: no fundamental / silence -> NaN, never 0.0 (FR-008)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kFundHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;

    // All-zeros: there is no measurable fundamental to reference the residual
    // to, so every figure is UNMEASURABLE -- NaN, never a misleading 0.0 that
    // would masquerade as a perfectly clean signal (FR-008).
    std::vector<float> silence(kN, 0.0f);

    const ThdnResult r = thdPlusN(acfx::span<const float>(silence), kFundHz, kSampleRate);

    CHECK(std::isnan(r.thdPlusN));
    CHECK(std::isnan(r.noiseFloor));
    CHECK(std::isnan(r.snr));
}

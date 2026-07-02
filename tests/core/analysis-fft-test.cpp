// analysis-fft-test.cpp
// T003 -- harmonic-analysis feature, Foundational phase: RED test for the
// self-contained windowed radix-2 FFT (research.md Decision 2,
// contracts/analysis-engine-api.md "Fft", FR-009/FR-026). Asserts:
//   - a windowed radix-2 FFT reconstructs an integer-cycle single-tone
//     signal: the magnitude peak lands at the correct bin, matching the
//     analytic windowed-DFT prediction (amplitude * coherent gain / 2)
//     within a named tolerance. The coherent gain is computed independently
//     from the already-GREEN Window class (window.h, T005/T006), not
//     hard-coded here, so a wrong or missing window inside fft.h is caught
//     rather than assumed away.
//   - DC input concentrates energy in bin 0; a two-tone input shows two
//     distinct peaks at the expected bins (linearity/DC sanity).
//   - a non-power-of-two transform length is REJECTED with a descriptive
//     error -- never silently zero-padded (FR-026, Constitution V).
//
// host/analysis/fft.h does not exist yet at RED time -- this test is
// expected to FAIL TO BUILD until a later task (T004) lands it. Do NOT
// implement fft.h to make this test pass; that is a separate task.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "analysis/fft.h"
#include "analysis/window.h"
#include "dsp/span.h"

using acfx::analysis::Complex; // expected alias in fft.h, e.g. std::complex<double>
using acfx::analysis::Fft;
using acfx::analysis::Window;
using acfx::analysis::WindowKind;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Relative tolerance on the windowed-DFT peak-magnitude prediction. The
// analytic derivation (x[n] = A*sin(2*pi*k*n/N)*w[n] => |X[k]| ~= A/2 *
// sum(w[n]), since the window's own N-point DFT is negligible away from bin
// 0 at the ~-92 dB Blackman-Harris sidelobe level) puts the true error
// several orders of magnitude below this bound; 5% is a generous,
// svf-reference-style named tolerance that still fails a broken scale
// factor, a missing window, or a bin-mislocated peak.
constexpr double kPeakMagnitudeTolerance = 0.05; // 5%

// A bin "far" from a tone's bin (outside the Blackman-Harris main lobe, which
// is ~8 bins wide null-to-null) must sit at the window's sidelobe floor --
// this factor is a generous (not tight) floor given ~-92 dB sidelobes imply
// a ~40000x amplitude ratio; 20x is already a strong, implementation-robust
// bound that a non-windowed or broken FFT cannot meet.
constexpr double kFarBinSuppressionFactor = 20.0;
constexpr int kFarBinGuardBins = 5; // bins within this distance are "near" (excluded from the far-bin check)

// Fills `out` with an integer-cycle real sine: exactly `cycles` full periods
// over `out.size()` samples, so its energy lands exactly on FFT bin `cycles`
// with zero *frequency-alignment* leakage (window main-lobe leakage into
// neighboring bins is separate and expected/asserted for).
void fillIntegerCycleSine(std::vector<float>& out, int cycles, double amplitude) {
    const std::size_t n = out.size();
    for (std::size_t i = 0; i < n; ++i) {
        const double phase =
            2.0 * kPi * static_cast<double>(cycles) * static_cast<double>(i) / static_cast<double>(n);
        out[i] = static_cast<float>(amplitude * std::sin(phase));
    }
}

void addIntegerCycleSine(std::vector<float>& out, int cycles, double amplitude) {
    const std::size_t n = out.size();
    for (std::size_t i = 0; i < n; ++i) {
        const double phase =
            2.0 * kPi * static_cast<double>(cycles) * static_cast<double>(i) / static_cast<double>(n);
        out[i] += static_cast<float>(amplitude * std::sin(phase));
    }
}

// Sum of the default (Blackman-Harris) window's coefficients -- the
// (unnormalized) coherent gain a windowed-DFT peak scales by. Computed from
// the already-GREEN Window class (window.h), independent of whatever fft.h
// does internally.
double blackmanHarrisCoherentGainSum(int size) {
    const Window w(size, WindowKind::BlackmanHarris);
    double sum = 0.0;
    for (double c : w.coeffs()) sum += c;
    return sum;
}

double magnitude(const Complex& c) { return std::abs(c); }

// Index (within [begin, end)) of the largest-magnitude bin.
std::size_t argMaxBin(const std::vector<Complex>& spectrum, std::size_t begin, std::size_t end) {
    std::size_t best = begin;
    double bestMag = magnitude(spectrum[begin]);
    for (std::size_t k = begin + 1; k < end; ++k) {
        const double m = magnitude(spectrum[k]);
        if (m > bestMag) {
            bestMag = m;
            best = k;
        }
    }
    return best;
}

// Largest magnitude among bins in [begin, end) that are farther than
// `guard` bins from every index in `exclude`.
double maxFarBinMagnitude(const std::vector<Complex>& spectrum, std::size_t begin, std::size_t end,
                          const std::vector<std::size_t>& exclude, int guard) {
    double worst = 0.0;
    for (std::size_t k = begin; k < end; ++k) {
        bool near = false;
        for (std::size_t e : exclude) {
            const long dist = std::llabs(static_cast<long>(k) - static_cast<long>(e));
            if (dist <= guard) {
                near = true;
                break;
            }
        }
        if (near) continue;
        worst = std::max(worst, magnitude(spectrum[k]));
    }
    return worst;
}

} // namespace

TEST_CASE("Fft rejects non-power-of-two sizes with a descriptive error") {
    CHECK_THROWS_AS(Fft(0), std::invalid_argument);
    CHECK_THROWS_AS(Fft(3), std::invalid_argument);
    CHECK_THROWS_AS(Fft(5), std::invalid_argument);
    CHECK_THROWS_AS(Fft(6), std::invalid_argument);
    CHECK_THROWS_AS(Fft(100), std::invalid_argument);
    CHECK_THROWS_AS(Fft(1000), std::invalid_argument);
    CHECK_THROWS_AS(Fft(-4), std::invalid_argument);

    // Sanity: legitimate power-of-two sizes must NOT throw -- proves the
    // guard checks "is a power of two", not merely "is small"/"is odd".
    CHECK_NOTHROW(Fft(2));
    CHECK_NOTHROW(Fft(64));
    CHECK_NOTHROW(Fft(256));
}

TEST_CASE("Fft: non-power-of-two rejection never silently zero-pads (no output produced)") {
    // FR-026 / Constitution V: a caller who mistakenly asks for a non-pow2
    // transform gets a descriptive error, never a silently zero-padded
    // transform that would shift bin frequencies out from under them.
    CHECK_THROWS_WITH_AS(Fft(100), doctest::Contains("power of two"), std::invalid_argument);
}

TEST_CASE("Fft windowed single tone: magnitude peak lands at the correct bin") {
    constexpr int kSize = 256;
    constexpr int kBin = 10; // integer cycles -> bin-exact, no frequency-alignment leakage
    constexpr double kAmplitude = 1.0;

    std::vector<float> in(kSize, 0.0f);
    fillIntegerCycleSine(in, kBin, kAmplitude);

    std::vector<Complex> out(kSize);
    const Fft fft(kSize);
    fft.forward(acfx::span<const float>(in), acfx::span<Complex>(out));

    // The tone's bin must be the single largest magnitude among all
    // positive-frequency, non-Nyquist bins.
    const std::size_t peakBin = argMaxBin(out, 1, kSize / 2);
    CHECK(peakBin == static_cast<std::size_t>(kBin));

    // Peak amplitude matches the analytic windowed-DFT prediction: the
    // window's own N-point DFT is concentrated near bin 0, so
    // |X[kBin]| ~= amplitude/2 * sum(window coefficients).
    const double coherentGainSum = blackmanHarrisCoherentGainSum(kSize);
    const double expectedPeak = kAmplitude * coherentGainSum / 2.0;
    const double actualPeak = magnitude(out[static_cast<std::size_t>(kBin)]);
    CHECK(actualPeak == doctest::Approx(expectedPeak).epsilon(kPeakMagnitudeTolerance));

    // Bins well outside the window's main lobe sit at the sidelobe floor,
    // far below the peak.
    const double farBinMax =
        maxFarBinMagnitude(out, 1, kSize / 2, {static_cast<std::size_t>(kBin)}, kFarBinGuardBins);
    CHECK(actualPeak > kFarBinSuppressionFactor * farBinMax);
}

TEST_CASE("Fft DC input concentrates energy in bin 0") {
    constexpr int kSize = 256;
    constexpr double kAmplitude = 0.75;

    const std::vector<float> in(kSize, static_cast<float>(kAmplitude));
    std::vector<Complex> out(kSize);
    const Fft fft(kSize);
    fft.forward(acfx::span<const float>(in), acfx::span<Complex>(out));

    // Bin 0 must be the single largest magnitude across the positive half
    // of the spectrum (inclusive of Nyquist).
    const std::size_t peakBin = argMaxBin(out, 0, kSize / 2 + 1);
    CHECK(peakBin == 0);

    // Analytic prediction for a windowed DC input: X[0] = amplitude *
    // sum(window coefficients) (no 1/2 factor -- unlike a two-sided sine,
    // DC has all its energy at m=0, not split across +-k).
    const double coherentGainSum = blackmanHarrisCoherentGainSum(kSize);
    const double expectedDc = kAmplitude * coherentGainSum;
    const double actualDc = magnitude(out[0]);
    CHECK(actualDc == doctest::Approx(expectedDc).epsilon(kPeakMagnitudeTolerance));

    const double farBinMax = maxFarBinMagnitude(out, 0, kSize / 2 + 1, {0}, kFarBinGuardBins);
    CHECK(actualDc > kFarBinSuppressionFactor * farBinMax);
}

TEST_CASE("Fft two-tone input shows two distinct peaks (linearity)") {
    constexpr int kSize = 256;
    constexpr int kBin1 = 10;
    constexpr int kBin2 = 60; // well separated from kBin1: main lobes do not overlap
    constexpr double kAmplitude1 = 1.0;
    constexpr double kAmplitude2 = 0.6;

    std::vector<float> in(kSize, 0.0f);
    addIntegerCycleSine(in, kBin1, kAmplitude1);
    addIntegerCycleSine(in, kBin2, kAmplitude2);

    std::vector<Complex> out(kSize);
    const Fft fft(kSize);
    fft.forward(acfx::span<const float>(in), acfx::span<Complex>(out));

    // Collect the positive-frequency, non-Nyquist bins sorted by magnitude,
    // descending; the top two must be exactly {kBin1, kBin2}.
    std::vector<std::size_t> bins;
    for (std::size_t k = 1; k < static_cast<std::size_t>(kSize / 2); ++k) bins.push_back(k);
    std::sort(bins.begin(), bins.end(), [&out](std::size_t a, std::size_t b) {
        return magnitude(out[a]) > magnitude(out[b]);
    });

    REQUIRE(bins.size() >= 2);
    const std::size_t top1 = std::min(bins[0], bins[1]);
    const std::size_t top2 = std::max(bins[0], bins[1]);
    CHECK(top1 == static_cast<std::size_t>(kBin1));
    CHECK(top2 == static_cast<std::size_t>(kBin2));

    // Each peak independently matches the single-tone analytic prediction
    // (superposition holds -- the FFT is linear).
    const double coherentGainSum = blackmanHarrisCoherentGainSum(kSize);
    const double expectedPeak1 = kAmplitude1 * coherentGainSum / 2.0;
    const double expectedPeak2 = kAmplitude2 * coherentGainSum / 2.0;
    CHECK(magnitude(out[static_cast<std::size_t>(kBin1)]) ==
          doctest::Approx(expectedPeak1).epsilon(kPeakMagnitudeTolerance));
    CHECK(magnitude(out[static_cast<std::size_t>(kBin2)]) ==
          doctest::Approx(expectedPeak2).epsilon(kPeakMagnitudeTolerance));
}

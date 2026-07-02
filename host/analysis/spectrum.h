#pragma once

// host/analysis/spectrum.h
//
// Full harmonic spectrum -- per-harmonic magnitude AND phase at an arbitrary
// caller-specified harmonic count (contracts/analysis-engine-api.md
// "spectrum.h"; data-model.md "HarmonicSpectrum"; FR-001, FR-008, US1;
// harmonic-analysis T009, GREEN for T008).
//
// Namespace: acfx::analysis. Host-side / offline ONLY -- may allocate.
//
// Method: for each requested harmonic k = 1..numHarmonics, read the exact,
// leakage-free single-bin Goertzel amplitude+phase at k*fundamentalHz
// (acfx::measure::GoertzelAnalyzer, analyzers.h) -- the SAME retained
// integer-cycle path FR-007/010 keeps for known-bin regression checks (no
// new spectral machinery for this metric; the FFT serves the broadband
// view elsewhere, per research.md Decision 6's precedent). Callers MUST
// present an integer-cycle capture (fundamentalHz * in.size() / sampleRate a
// whole number) for every harmonic to land exactly on its own Goertzel bin
// with zero frequency-alignment leakage, mirroring every other suite in this
// tree (analysis-fft-test.cpp, measurement-*-test.cpp).
//
// Unmeasurable quantities use the established NaN sentinel (FR-008), never a
// fabricated 0.0:
//   - a harmonic at or above Nyquist (k*fundamentalHz >= sampleRate/2) is
//     OUT-OF-BAND: both magnitude and phaseRad are NaN ("not-measured").
//   - an in-band harmonic whose magnitude falls below kPhaseMagnitudeFloor
//     has a well-defined (near-zero) magnitude, but its phase is numerically
//     meaningless: phaseRad is NaN (mirrors tests/support/measurement/
//     metrics.h's phaseRad() floor convention, kPhaseMagnitudeFloor = 1e-4;
//     duplicated here rather than included from the test tree, honoring
//     research.md Decision 1's one-way dependency: tests/support ->
//     host/analysis, never the reverse).
//   - HarmonicSpectrum::at(k) is 1-based; k outside [1, numHarmonics] returns
//     the same NaN "not-measured" sentinel rather than fabricating 0.0.

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analyzers.h"  // acfx::measure::GoertzelAnalyzer
#include "dsp/span.h"

namespace acfx::analysis {

// Minimum Goertzel magnitude below which phase is considered undefined for a
// harmonic bin. Mirrors tests/support/measurement/metrics.h's
// kPhaseMagnitudeFloor exactly (same rationale: phase at near-zero amplitude
// is numerically meaningless).
inline constexpr double kPhaseMagnitudeFloor = 1.0e-4;

// Full per-harmonic magnitude+phase spectrum for an arbitrary requested
// harmonic count N (data-model.md "HarmonicSpectrum").
class HarmonicSpectrum {
public:
    // Per-harmonic magnitude+phase pair returned by at(k).
    struct Bin {
        double magnitude;
        double phaseRad;
    };

    double fundamentalHz = 0.0;
    int numHarmonics = 0;

    // magnitude[i] / phaseRad[i] hold harmonic (i+1); prefer at(k) (1-based)
    // over indexing these directly -- it applies the out-of-range guard.
    std::vector<double> magnitude;
    std::vector<double> phaseRad;

    // 1-based accessor: at(1) == fundamental, at(2) == 2nd harmonic, etc.
    // k outside [1, numHarmonics] returns the NaN "not-measured" sentinel
    // (FR-008) -- never a fabricated 0.0.
    [[nodiscard]] Bin at(int k) const noexcept {
        if (k < 1 || k > numHarmonics) {
            return notMeasured();
        }
        const std::size_t idx = static_cast<std::size_t>(k - 1);
        return Bin{magnitude[idx], phaseRad[idx]};
    }

private:
    static Bin notMeasured() noexcept {
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        return Bin{nan, nan};
    }
};

// Computes the full harmonic spectrum of `in` at integer multiples of
// `fundamentalHz`, for k = 1..numHarmonics.
//
// `numHarmonics` MUST be >= 1: a non-positive count is rejected with a
// descriptive error, never a silently empty spectrum (mirrors Fft's
// non-power-of-two guard, fft.h, FR-026's sibling fail-loud convention).
//
// Per harmonic k:
//   - freqHz = k * fundamentalHz. If freqHz >= sampleRate/2 (Nyquist), the
//     harmonic is OUT-OF-BAND and unmeasurable: magnitude and phaseRad are
//     both the NaN "not-measured" sentinel (FR-008).
//   - Otherwise the exact single-bin Goertzel readout at freqHz supplies
//     magnitude and phase. If that magnitude falls below
//     kPhaseMagnitudeFloor, phaseRad is NaN (phase is numerically
//     meaningless at near-zero amplitude); magnitude is still reported as
//     measured (a real, near-zero value -- not "unmeasurable").
//
// Deviation from contracts/analysis-engine-api.md's sketch signature
// `harmonicSpectrum(Signal in, double fundamentalHz, int numHarmonics)`:
// this takes an explicit `sampleRate` parameter. "Signal" in the contract
// sketch is a placeholder, not a literal type; converting a harmonic count
// to a Hz-domain Goertzel readout requires a sample rate, and every sibling
// entry point in this file tree that reads a frequency off a captured
// buffer takes it explicitly in the same position (aliasing.h's
// aliasingMeasure(out, fundamentalHz, sampleRate), GoertzelAnalyzer's own
// {freqHz, sampleRate} construction) -- this mirrors that established
// convention rather than inventing a new one.
inline HarmonicSpectrum harmonicSpectrum(acfx::span<const float> in,
                                         double fundamentalHz,
                                         double sampleRate,
                                         int numHarmonics) {
    if (numHarmonics < 1) {
        throw std::invalid_argument(
            "acfx::analysis::harmonicSpectrum: numHarmonics must be >= 1; got " +
            std::to_string(numHarmonics));
    }

    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    const double nyquist = sampleRate / 2.0;

    HarmonicSpectrum spectrum;
    spectrum.fundamentalHz = fundamentalHz;
    spectrum.numHarmonics = numHarmonics;
    spectrum.magnitude.resize(static_cast<std::size_t>(numHarmonics));
    spectrum.phaseRad.resize(static_cast<std::size_t>(numHarmonics));

    for (int k = 1; k <= numHarmonics; ++k) {
        const std::size_t idx = static_cast<std::size_t>(k - 1);
        const double freqHz = static_cast<double>(k) * fundamentalHz;

        if (freqHz >= nyquist) {
            // Out-of-band: unmeasurable (FR-008) -- never a fabricated 0.0.
            spectrum.magnitude[idx] = nan;
            spectrum.phaseRad[idx] = nan;
            continue;
        }

        const acfx::measure::GoertzelAnalyzer::Bin bin =
            acfx::measure::GoertzelAnalyzer{freqHz, sampleRate}.analyze(in);

        spectrum.magnitude[idx] = bin.magnitude;
        spectrum.phaseRad[idx] = (bin.magnitude < kPhaseMagnitudeFloor) ? nan : bin.phaseRad;
    }

    return spectrum;
}

} // namespace acfx::analysis

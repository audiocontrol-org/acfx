#pragma once

// host/analysis/thdn.h
//
// THD+N by the RESIDUAL (notch-the-fundamental) method plus the noise-floor /
// SNR figures (contracts/analysis-engine-api.md "thdn.h", data-model.md
// "ThdnResult", research.md Decision 4, FR-002/FR-008; harmonic-analysis
// T011, GREEN for T010).
//
// Namespace: acfx::analysis. Host-side / offline ONLY -- may allocate. NEVER
// reachable from portable core/ (Constitution IV); no audio-thread use.
//
// Definition (research Decision 4, FR-002):
//   THD+N = RMS(everything except the fundamental) / RMS(fundamental).
//   "Noise" here is ALL energy except the fundamental -- harmonics AND
//   broadband noise together. This is DISTINCT from the shipped single-bin
//   thd() (tests/support/measurement/metrics.h), which sums only a fixed set of
//   HARMONIC bins and is blind to broadband noise.
//
// Method (Parseval / notch, consistent with spectrum.h's Goertzel path):
//   - The fundamental's amplitude is read with the exact, leakage-free single-
//     bin Goertzel (acfx::measure::GoertzelAnalyzer, analyzers.h) -- the SAME
//     retained integer-cycle path FR-007/010 keeps and spectrum.h already uses,
//     so the fundamental estimate is numerically identical across metrics (no
//     new spectral machinery introduced for this figure).
//   - Total power is the mean-square of the buffer (double accumulation).
//     For an integer-cycle capture, DC and every harmonic bin are mutually
//     orthogonal, so subtracting the fundamental's power from the total power
//     EXACTLY notches the fundamental bin and leaves the sum of all remaining
//     bins' energy (harmonics + noise) -- Parseval. RMS(fundamental) =
//     fundamentalAmplitude / sqrt(2); its power is fundamentalAmplitude^2 / 2.
//   - residualPower = totalPower - fundamentalPower (clamped at 0 to absorb the
//     float round-trip of an otherwise-pure tone, where the two nearly-equal
//     quantities can differ by a sub-ULP negative amount).
//
// Callers MUST present an integer-cycle capture (fundamentalHz * in.size() /
// sampleRate a whole number) so the fundamental lands exactly on its Goertzel
// bin and the Parseval notch is leakage-free -- mirroring every other suite in
// this tree (analysis-fft-test.cpp, analysis-spectrum-test.cpp,
// measurement-*-test.cpp).
//
// Unmeasurable (FR-008): with no measurable fundamental (silence, a dead/
// unwired effect, or a wrong-band capture) there is nothing to reference the
// residual to. Every figure is quiet_NaN -- NEVER a fabricated 0.0, which would
// masquerade as a perfectly clean signal and silently pass an assertion like
// `thdPlusN < 0.01` for a completely dead capture (mirrors thd()'s
// AUDIT-20260629-06 sentinel convention; Constitution V forbids failure-hiding
// fallbacks, doubly so in the measurement layer).
//
// Deviation from contracts/analysis-engine-api.md's sketch signature
// `thdPlusN(Signal in, double fundamentalHz)`: this takes an explicit
// `sampleRate` parameter, exactly as spectrum.h's harmonicSpectrum does and for
// the same reason -- "Signal" in the contract sketch is a placeholder, not a
// literal type, and converting a Hz fundamental to a Goertzel bin requires the
// sample rate. Mirrors the established convention of every sibling entry point
// (GoertzelAnalyzer's {freqHz, sampleRate}, aliasingMeasure(out, f, sr)) rather
// than inventing a new one.

#include <cmath>      // std::sqrt, std::log10, std::isnan
#include <cstddef>    // std::size_t
#include <limits>     // std::numeric_limits

#include "analysis/analyzers.h"  // acfx::measure::GoertzelAnalyzer
#include "dsp/span.h"

namespace acfx::analysis {

// Amplitude below which the fundamental is considered ABSENT (unmeasurable).
// Matches thd()'s kEpsilon (tests/support/measurement/metrics.h): a Goertzel
// amplitude under this floor is float round-off, not a real tone.
inline constexpr double kThdnFundamentalFloor = 1.0e-12;

// THD+N + noise-floor / SNR figures for a captured buffer (data-model.md
// "ThdnResult"). All fields are quiet_NaN when the fundamental is unmeasurable
// (FR-008); none is ever a fabricated 0.0.
struct ThdnResult {
    // RMS(non-fundamental) / RMS(fundamental) -- the residual-method THD+N
    // ratio (linear, not dB). ~0 for an undistorted, noise-free tone.
    double thdPlusN;

    // Absolute RMS level of the non-fundamental residual (harmonics + noise).
    // The numerator of thdPlusN before dividing by the fundamental RMS.
    double noiseFloor;

    // Signal-to-(noise+distortion) ratio in dB, referenced to the fundamental:
    // 20*log10(RMS(fundamental) / RMS(residual)) = -20*log10(thdPlusN).
    // +infinity for an exactly-zero residual (a mathematically clean tone).
    double snr;
};

// Computes THD+N (residual method) + noiseFloor + snr for `in`, treating
// `fundamentalHz` as the reference fundamental.
//
// Returns all-NaN (FR-008) when the fundamental amplitude falls below
// kThdnFundamentalFloor (no measurable fundamental) or when the buffer is
// empty.
inline ThdnResult thdPlusN(acfx::span<const float> in,
                           double fundamentalHz,
                           double sampleRate) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    const std::size_t n = in.size();
    if (n == 0) {
        return ThdnResult{nan, nan, nan};
    }

    // Fundamental amplitude via the exact leakage-free single-bin Goertzel
    // (same path as spectrum.h). Amplitude-normalized: ~1.0 for a unit sine.
    const double fundAmplitude =
        acfx::measure::GoertzelAnalyzer{fundamentalHz, sampleRate}.analyze(in).magnitude;

    if (!(fundAmplitude > kThdnFundamentalFloor)) {
        // No measurable fundamental: every figure is unmeasurable (FR-008).
        return ThdnResult{nan, nan, nan};
    }

    // Total power = mean-square of the buffer (double accumulation).
    double sumSq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = static_cast<double>(in[i]);
        sumSq += s * s;
    }
    const double totalPower = sumSq / static_cast<double>(n);

    // Fundamental power = (amplitude)^2 / 2; RMS = amplitude / sqrt(2).
    const double fundPower = 0.5 * fundAmplitude * fundAmplitude;
    const double rmsFund = std::sqrt(fundPower);

    // Residual power = total minus fundamental (Parseval notch). Clamp at 0 to
    // absorb the sub-ULP negative that a nearly-pure tone can produce when the
    // two nearly-equal powers are subtracted.
    double residualPower = totalPower - fundPower;
    if (residualPower < 0.0) {
        residualPower = 0.0;
    }
    const double rmsResidual = std::sqrt(residualPower);

    ThdnResult result;
    result.thdPlusN = rmsResidual / rmsFund;
    result.noiseFloor = rmsResidual;
    // SNR referenced to the fundamental. Exactly-zero residual -> +infinity
    // (a mathematically clean tone), which is the honest limit, not a fabricated
    // cap. log10 of a large finite ratio yields a large finite dB value.
    result.snr = 20.0 * std::log10(rmsFund / rmsResidual);

    return result;
}

} // namespace acfx::analysis

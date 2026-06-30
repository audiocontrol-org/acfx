// measurement-support.h
// Shared test helpers for the measurement-infrastructure doctest suite.
// Include from any measurement-*-test.cpp that uses these helpers.
// All helpers live in namespace acfx::meastest as inline functions or
// inline constexpr constants so multiple TUs can include this without
// ODR violations.  Do NOT add "using namespace ..." in this header.

#pragma once

#include <cmath>     // std::abs
#include <cstddef>   // std::size_t
#include <limits>    // std::numeric_limits
#include <utility>   // std::forward
#include <vector>    // offline scratch buffers (test code only — not an audio path)

#include "effects/svf/svf-effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/span.h"
#include "support/measurement/analyzers.h"  // GoertzelAnalyzer, captureCallable
#include "support/measurement/stimulus.h"   // SineGenerator

namespace acfx::meastest {

// Configure fx as a lowpass at cutoffHz with zero resonance via the SVF
// parameter table.  Pending parameter edits are consumed on the audio thread
// at the first process() call inside capture().
inline void configureLowpass(acfx::SvfEffect& fx, double cutoffHz) {
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

// ===========================================================================
// Waveshaper measurement helpers (T006)
//
// Foundation for the harmonic / anti-aliasing / DC assertions in US1/US2/US4.
// Every helper RETURNS a measured quantity computed from the SHIPPED
// measurement infrastructure (acfx::measure::SineGenerator,
// acfx::measure::captureCallable, acfx::measure::GoertzelAnalyzer); tests
// assert these against analytic truths + named tolerances.  No fabricated
// magic numbers live in here.
//
// WINDOW CONTRACT (anti-leakage): callers MUST choose a window length so the
// fundamental completes an INTEGER number of cycles in `numSamples`
// (fundamentalHz * numSamples / sampleRate is a whole number).  With an
// integer-cycle window every harmonic — and, crucially, every aliased/folded
// image of a high harmonic — lands exactly on a DFT bin (a multiple of
// sampleRate/numSamples).  That makes the single-bin Goertzel readout
// leakage-free and makes Parseval's identity exact, which the aliasing measure
// below relies on.  This mirrors the integer-cycle window already used in
// measurement-distortion-test.cpp.
// ===========================================================================

// ---------------------------------------------------------------------------
// captureSineResponse
//
// Drive a pure unit (or `amplitude`) sine of `fundamentalHz` through a
// per-sample callable float(float) and return the captured output buffer.
// Reuses acfx::measure::SineGenerator (stimulus) + acfx::measure::captureCallable.
// Allocation is fine — this is offline test code, never an audio path.
// ---------------------------------------------------------------------------
template <class Fn>
inline std::vector<float> captureSineResponse(Fn&& fn,
                                              double fundamentalHz,
                                              double sampleRate,
                                              std::size_t numSamples,
                                              float amplitude = 1.0f) {
    std::vector<float> in(numSamples, 0.0f);
    std::vector<float> out(numSamples, 0.0f);
    acfx::measure::SineGenerator{fundamentalHz, sampleRate, amplitude, 0.0}
        .fill(acfx::span<float>(in));
    acfx::measure::captureCallable(std::forward<Fn>(fn),
                                   acfx::span<const float>(in),
                                   acfx::span<float>(out));
    return out;
}

// ---------------------------------------------------------------------------
// HarmonicSignature  (helper 1 — pure-tone harmonic-signature capture)
//
// Per-harmonic Goertzel amplitudes of a captured output.  mags[i] is the
// amplitude at frequency (i+1) * fundamentalHz: mags[0] is the fundamental,
// mags[1] the 2nd harmonic, and so on.  A harmonic whose frequency reaches
// Nyquist (sampleRate/2) is recorded as 0.0 (out of band — not measurable).
//
// Supports the US1/US2 assertions, e.g.:
//   * symmetric shape  -> even harmonics ~0, odd harmonics present;
//   * biased  shape    -> even AND odd harmonics present, plus DC (dcOffset);
//   * Chebyshev-N      -> at(N) dominates the other harmonics.
// ---------------------------------------------------------------------------
struct HarmonicSignature {
    // mags[i] = Goertzel amplitude at (i+1)*fundamentalHz.
    std::vector<double> mags;

    // 1-based harmonic accessor: at(1) == fundamental, at(2) == 2nd harmonic.
    // Out-of-range harmonic numbers return 0.0 (not measured).
    double at(int harmonicNumber) const noexcept {
        const int i = harmonicNumber - 1;
        if (i < 0 || static_cast<std::size_t>(i) >= mags.size())
            return 0.0;
        return mags[static_cast<std::size_t>(i)];
    }

    double fundamental() const noexcept { return at(1); }

    // Amplitude of harmonic `harmonicNumber` relative to the fundamental.
    // Returns NaN when the fundamental is effectively zero (ratio undefined),
    // mirroring the shipped thd() convention — never a fabricated 0.0 that
    // would masquerade as "no harmonic".
    double ratio(int harmonicNumber) const noexcept {
        const double f = fundamental();
        constexpr double kEpsilon = 1.0e-12;
        if (f < kEpsilon)
            return std::numeric_limits<double>::quiet_NaN();
        return at(harmonicNumber) / f;
    }
};

// Analyze an already-captured output buffer at the first `numHarmonics`
// integer multiples of `fundamentalHz` using the shipped GoertzelAnalyzer
// (one single-bin DFT per harmonic).
inline HarmonicSignature harmonicSignature(acfx::span<const float> out,
                                           double fundamentalHz,
                                           double sampleRate,
                                           int numHarmonics) {
    HarmonicSignature sig;
    if (numHarmonics <= 0)
        return sig;
    sig.mags.assign(static_cast<std::size_t>(numHarmonics), 0.0);

    const double nyquist = sampleRate / 2.0;
    for (int k = 1; k <= numHarmonics; ++k) {
        const double freqHz = static_cast<double>(k) * fundamentalHz;
        if (freqHz >= nyquist)
            break;  // leave 0.0 for out-of-band harmonics
        sig.mags[static_cast<std::size_t>(k - 1)] =
            acfx::measure::GoertzelAnalyzer{freqHz, sampleRate}
                .analyze(out)
                .magnitude;
    }
    return sig;
}

// Driving convenience: capture a sine through `fn`, then take its signature.
template <class Fn>
inline HarmonicSignature harmonicSignature(Fn&& fn,
                                           double fundamentalHz,
                                           double sampleRate,
                                           std::size_t numSamples,
                                           int numHarmonics,
                                           float amplitude = 1.0f) {
    const std::vector<float> out =
        captureSineResponse(std::forward<Fn>(fn),
                            fundamentalHz, sampleRate, numSamples, amplitude);
    return harmonicSignature(acfx::span<const float>(out),
                             fundamentalHz, sampleRate, numHarmonics);
}

// ---------------------------------------------------------------------------
// AliasingMeasure  (helper 2 — inharmonic / aliased-energy measure)
//
// Quantifies output energy that is NOT part of the input's harmonic series —
// i.e. the aliased/folded content a naive (non-bandlimited) nonlinearity
// produces when a high-frequency tone's harmonics exceed Nyquist and fold back
// onto non-harmonic frequencies.
//
// METHOD (power-domain residual; "everything not at an integer multiple of
// the fundamental"):
//   totalPower    = mean-square of the whole output      = (1/N) * sum(x[n]^2)
//                   (Parseval: equals the summed power of all DFT bins)
//   harmonicPower = DC power  (mean^2, the k=0 bin)
//                 + sum over every in-band harmonic k>=1 (k*fundamental <
//                   Nyquist) of (A_k^2 / 2), where A_k is the GoertzelAnalyzer
//                   amplitude at k*fundamental and A^2/2 is a sinusoid's power.
//   inharmonicPower = totalPower - harmonicPower
//
// Whatever remains after removing DC and the true f0-harmonic series is the
// aliased/folded residual (plus any broadband noise).  With the integer-cycle
// window contract above, all energy — including each folded image — sits
// exactly on a DFT bin, so Parseval is exact and the subtraction is clean.
// (A folded image can, in rare arithmetic coincidences, land exactly on an
// f0-multiple and be miscounted as harmonic; this is negligible for the
// naive-vs-ADAA comparison and affects both arms equally.)
//
// Supports US4: assert ADAA.inharmonicPower <= naive.inharmonicPower * margin.
// Returns POWER (mean-square, per-sample) so the value is independent of the
// window length N; the US4 comparison is a ratio, so units cancel either way.
// ---------------------------------------------------------------------------
struct AliasingMeasure {
    double totalPower;       // mean-square of the whole output
    double harmonicPower;    // power at DC + the in-band f0-harmonic series
    double inharmonicPower;  // totalPower - harmonicPower (>= 0): aliased residual
};

inline AliasingMeasure aliasingMeasure(acfx::span<const float> out,
                                       double fundamentalHz,
                                       double sampleRate) {
    const std::size_t N = out.size();
    if (N == 0)
        return AliasingMeasure{0.0, 0.0, 0.0};

    double sumSq = 0.0;
    double sum   = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        const double x = static_cast<double>(out[n]);
        sumSq += x * x;
        sum   += x;
    }
    const double totalPower = sumSq / static_cast<double>(N);
    const double dc         = sum   / static_cast<double>(N);

    double harmonicPower = dc * dc;  // k = 0 (DC) bin power
    const double nyquist = sampleRate / 2.0;
    for (int k = 1; static_cast<double>(k) * fundamentalHz < nyquist; ++k) {
        const double freqHz = static_cast<double>(k) * fundamentalHz;
        const double aK =
            acfx::measure::GoertzelAnalyzer{freqHz, sampleRate}
                .analyze(out)
                .magnitude;
        harmonicPower += 0.5 * aK * aK;  // sinusoid power = A^2 / 2
    }

    // Energy is physically non-negative; clamp a tiny negative from floating-
    // point round-off in the subtraction (occurs when there is essentially no
    // aliasing and totalPower ~= harmonicPower).  Numerical hygiene, not a
    // fallback masking missing functionality.
    double inharmonic = totalPower - harmonicPower;
    if (inharmonic < 0.0)
        inharmonic = 0.0;

    return AliasingMeasure{totalPower, harmonicPower, inharmonic};
}

// Driving convenience: capture a sine through `fn`, then measure aliasing.
template <class Fn>
inline AliasingMeasure aliasingMeasure(Fn&& fn,
                                       double fundamentalHz,
                                       double sampleRate,
                                       std::size_t numSamples,
                                       float amplitude = 1.0f) {
    const std::vector<float> out =
        captureSineResponse(std::forward<Fn>(fn),
                            fundamentalHz, sampleRate, numSamples, amplitude);
    return aliasingMeasure(acfx::span<const float>(out),
                           fundamentalHz, sampleRate);
}

// ---------------------------------------------------------------------------
// dcOffset  (helper 3 — DC-offset measure)
//
// Arithmetic mean of the output, i.e. the DC (0 Hz) component.  Over an
// integer-cycle window (the contract above) a symmetric shape integrates to
// ~0; an asymmetric/biased shape leaves a non-zero mean.  Supports the
// "asymmetric-bias DC-free output" assertions: drive the shape, then assert
// the wrapper's DC-blocker drives this measure toward zero.
//
// Returns 0.0 for an empty span (documented sentinel; callers guard input).
// ---------------------------------------------------------------------------
inline double dcOffset(acfx::span<const float> out) noexcept {
    const std::size_t N = out.size();
    if (N == 0)
        return 0.0;
    double sum = 0.0;
    for (std::size_t n = 0; n < N; ++n)
        sum += static_cast<double>(out[n]);
    return sum / static_cast<double>(N);
}

// Driving convenience: capture a sine through `fn`, then measure DC offset.
template <class Fn>
inline double dcOffset(Fn&& fn,
                       double fundamentalHz,
                       double sampleRate,
                       std::size_t numSamples,
                       float amplitude = 1.0f) {
    const std::vector<float> out =
        captureSineResponse(std::forward<Fn>(fn),
                            fundamentalHz, sampleRate, numSamples, amplitude);
    return dcOffset(acfx::span<const float>(out));
}

} // namespace acfx::meastest

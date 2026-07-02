// measurement-support.h
// Shared test helpers for the measurement-infrastructure doctest suite.
// Include from any measurement-*-test.cpp that uses these helpers.
// All helpers live in namespace acfx::meastest as inline functions or
// inline constexpr constants so multiple TUs can include this without
// ODR violations.  Do NOT add "using namespace ..." in this header.
//
// CONSOLIDATION NOTE (harmonic-analysis T020, research.md Decision 8,
// FR-007): every measurement building block this file uses (SineGenerator,
// GoertzelAnalyzer/captureCallable, AliasingMeasure/aliasingMeasure) is
// obtained THROUGH host/analysis/ — via the tests/support/measurement/
// one-line re-export shims below (relocated by T007) — never via a second,
// locally-duplicated implementation. `meastest::HarmonicSignature` /
// `harmonicSignature()` and the saturation composites further down are
// genuinely new composites BUILT ON TOP of those shipped primitives, not
// reimplementations of them. Verified T020: no duplicate GoertzelAnalyzer /
// AliasingMeasure / SineGenerator exists outside host/analysis/ and these
// shims; host/analysis/ and core/ never include tests/.

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
#include "support/measurement/aliasing.h"   // AliasingMeasure, aliasingMeasure (T006)
#include "support/measurement/analyzers.h"  // GoertzelAnalyzer, captureCallable
#include "support/measurement/metrics.h"    // thd (single-bin THD)
#include "support/measurement/stimulus.h"   // SineGenerator

#include <algorithm> // std::min

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
// T006: the measure itself (struct + span-based aliasingMeasure) now lives in
// the SHARED header support/measurement/aliasing.h (namespace acfx::measure)
// so the saturation suites and the oversampler suites (US1/US4) consume ONE
// implementation (research.md Decision 8, FR-022). Re-exposed here via
// using-declarations so existing `meastest::AliasingMeasure` /
// `meastest::aliasingMeasure` call sites are unaffected.
//
// Supports US4: assert ADAA.inharmonicPower <= naive.inharmonicPower * margin.
// See support/measurement/aliasing.h for the full method/window-contract
// documentation.
// ---------------------------------------------------------------------------
using acfx::measure::AliasingMeasure;
using acfx::measure::aliasingMeasure;

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

// ===========================================================================
// SATURATION measurement helpers
//
// The SaturationEffect / SaturationCore validation suites (FR-016, FR-017,
// SC-001..SC-005) reuse the measurement primitives above rather than adding a
// second spectral engine.  Three of the five signatures the saturation suites
// need already ship above and are consumed AS-IS (no saturation-specific
// duplicate is introduced):
//
//   (a) per-voicing harmonic-signature capture  -> harmonicSignature(...)
//       Drive an identical sine through each voicing's processing callable and
//       compare the returned per-harmonic magnitudes (SC-001, US2).
//   (c) inharmonic / aliased-energy measure      -> aliasingMeasure(...)
//       The naive-vs-ADAA comparison (SC-004, US4) asserts
//       adaa.inharmonicPower <= naive.inharmonicPower * margin, exactly as the
//       waveshaper ADAA suite already does.
//   (e) DC-offset measure                        -> dcOffset(...)
//       The biased-setting DC-free assertion (SC-005, US3) drives a biased
//       voicing and asserts the composed DC-blocker holds dcOffset near zero.
//
// The two remaining signatures — a drive->THD series and a mix dry/wet balance
// measure — are the saturation-specific composites added below.  Both are thin
// wrappers over the shipped captureSineResponse + acfx::measure::thd and over
// plain buffer arithmetic; neither invents a new tolerance (helpers MEASURE,
// tests assert against named bounds).
// ===========================================================================

// ---------------------------------------------------------------------------
// DriveThdSeries  (helper (b) — drive -> THD series)
//
// Measures total harmonic distortion as a function of drive so the suite can
// assert drive->THD MONOTONICITY (FR-017, SC-002): rising drive raises measured
// distortion.  `makeProcessor(drive)` returns a per-sample callable float(float)
// configured at that drive (e.g. a lambda closing over a Waveshaper /
// SaturationCore whose drive has been set).  For each drive a fresh sine is
// captured through that callable and reduced to a single THD reading via the
// shipped acfx::measure::thd (which itself returns NaN when THD is unmeasurable
// — no fabricated 0.0).  Returns the (drive, thd) pairs; the test asserts the
// thd sequence is non-decreasing within its named tolerance.
//
// Window contract: obey the integer-cycle window (header note) so every harmonic
// bin is leakage-free.
// ---------------------------------------------------------------------------
struct DriveThdPoint {
    double drive;  // the drive value applied for this measurement
    double thd;    // measured THD at that drive (NaN when unmeasurable — thd() convention)
};

template <class MakeProcessor>
inline std::vector<DriveThdPoint> driveThdSeries(MakeProcessor&& makeProcessor,
                                                 acfx::span<const double> driveValues,
                                                 double fundamentalHz,
                                                 double sampleRate,
                                                 std::size_t numSamples,
                                                 int harmonics = 5,
                                                 float amplitude = 1.0f) {
    std::vector<DriveThdPoint> series;
    series.reserve(driveValues.size());
    for (std::size_t i = 0; i < driveValues.size(); ++i) {
        const double drive = driveValues[i];
        const std::vector<float> out =
            captureSineResponse(makeProcessor(drive),
                                fundamentalHz, sampleRate, numSamples, amplitude);
        const double t = acfx::measure::thd(acfx::span<const float>(out),
                                            fundamentalHz, sampleRate, harmonics);
        series.push_back(DriveThdPoint{drive, t});
    }
    return series;
}

// ---------------------------------------------------------------------------
// relativeRmsError  (mix building block)
//
// Root-mean-square of (measured - reference) normalised by the RMS of
// reference: sqrt( sum (m-r)^2 / sum r^2 ).  A scale-independent "how far is
// `measured` from `reference`" figure.  0 means identical; ~1 means the error
// is as large as the reference itself.
//
// Directly supports the mix EXTREMES of SC-003:
//   * fully-dry:  relativeRmsError(out, dryInput) -> ~0 (wet path contributes
//     nothing, so the output reproduces the input).
//   * fully-wet:  relativeRmsError(out, wetPath)  -> ~0 (only the saturated path
//     survives).
//
// Returns NaN when the reference is effectively silent (ratio undefined),
// mirroring the shipped thd()/phaseRad() unmeasurable convention rather than a
// fabricated 0.0.  Compares over the common prefix when lengths differ.
// ---------------------------------------------------------------------------
inline double relativeRmsError(acfx::span<const float> measured,
                               acfx::span<const float> reference) noexcept {
    const std::size_t N = std::min(measured.size(), reference.size());
    if (N == 0)
        return std::numeric_limits<double>::quiet_NaN();
    double sumDiffSq = 0.0;
    double sumRefSq  = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        const double r = static_cast<double>(reference[n]);
        const double d = static_cast<double>(measured[n]) - r;
        sumDiffSq += d * d;
        sumRefSq  += r * r;
    }
    constexpr double kEpsilon = 1.0e-24;
    if (sumRefSq < kEpsilon)
        return std::numeric_limits<double>::quiet_NaN();
    return std::sqrt(sumDiffSq / sumRefSq);
}

// ---------------------------------------------------------------------------
// MixBalance  (helper (d) — mix dry/wet balance measure)
//
// Quantifies how faithfully a blended output realises the documented LINEAR
// dry/wet law y = mix*wet + (1-mix)*dry (data-model SaturationCore signal
// chain; FR-012).  Given the dry input, the fully-wet path, the actually-
// blended output, and the mix value in [0,1], it returns:
//   dryRms/wetRms/outRms — RMS levels of each buffer (informative; lets the
//     test assert the blended level sits between the dry and wet extremes), and
//   blendResidual — relativeRmsError(out, mix*wet + (1-mix)*dry): ~0 when the
//     blend obeys the linear law across the range (SC-003 intermediate settings,
//     US3 blend-law assertion).
//
// The predicted-blend buffer is allocated here (offline test code, never an
// audio path).  Buffers are compared over their common prefix.
// ---------------------------------------------------------------------------
struct MixBalance {
    double dryRms;         // RMS of the dry input
    double wetRms;         // RMS of the fully-wet (saturated) path
    double outRms;         // RMS of the actual blended output
    double blendResidual;  // relative RMS error of out vs the linear-law blend
};

// RMS level of a buffer (mix building block; not a nested `detail` namespace so
// this header can coexist with acfx::measure::detail under `using namespace`).
inline double rmsLevel(acfx::span<const float> buf) noexcept {
    const std::size_t N = buf.size();
    if (N == 0)
        return 0.0;
    double sumSq = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        const double x = static_cast<double>(buf[n]);
        sumSq += x * x;
    }
    return std::sqrt(sumSq / static_cast<double>(N));
}

inline MixBalance mixBalance(acfx::span<const float> dry,
                             acfx::span<const float> wet,
                             acfx::span<const float> out,
                             double mix) {
    const std::size_t N =
        std::min(dry.size(), std::min(wet.size(), out.size()));

    std::vector<float> predicted(N, 0.0f);
    for (std::size_t n = 0; n < N; ++n) {
        const double blended = mix * static_cast<double>(wet[n])
                             + (1.0 - mix) * static_cast<double>(dry[n]);
        predicted[n] = static_cast<float>(blended);
    }

    const acfx::span<const float> dryN{dry.data(), N};
    const acfx::span<const float> wetN{wet.data(), N};
    const acfx::span<const float> outN{out.data(), N};

    return MixBalance{
        rmsLevel(dryN),
        rmsLevel(wetN),
        rmsLevel(outN),
        relativeRmsError(outN, acfx::span<const float>(predicted)),
    };
}

} // namespace acfx::meastest

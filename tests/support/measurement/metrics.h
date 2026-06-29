#pragma once

// tests/support/measurement/metrics.h
//
// Response metrics (FR-005, FR-006, FR-007) for the acfx measurement harness.
// Namespace: acfx::measure.  Host-side / offline ONLY — may allocate.
//
// Free functions operating on already-captured span<const float> buffers
// (magnitude, phaseRad) or driving an effect/callable with an impulse stimulus
// (captureImpulseResponse*).  The test composer: stimulus -> capture -> metric.

#include <cmath>
#include <limits>
#include <vector>

#include "dsp/process-context.h"
#include "dsp/span.h"
#include "support/measurement/analyzers.h"
#include "support/measurement/stimulus.h"

namespace acfx::measure {

// ---------------------------------------------------------------------------
// kPhaseMagnitudeFloor (FR-007)
//
// Minimum Goertzel output magnitude below which phase is considered undefined.
// When the analyzed output magnitude falls below this floor, phaseRad()
// returns std::numeric_limits<double>::quiet_NaN() rather than a spurious
// phase value. Phase at near-zero amplitude is numerically meaningless and
// should not be asserted upon by any caller.
// ---------------------------------------------------------------------------
inline constexpr double kPhaseMagnitudeFloor = 1.0e-4;

// ---------------------------------------------------------------------------
// detail::wrapToPi  (internal helper)
//
// Wraps an angle in radians to the principal-value range (-pi, pi].
// ---------------------------------------------------------------------------
namespace detail {

inline double wrapToPi(double angleRad) noexcept {
    constexpr double pi    = 3.141592653589793238462643383279502884;
    constexpr double twoPi = 2.0 * pi;
    double wrapped = std::fmod(angleRad, twoPi);
    if (wrapped <= -pi)
        wrapped += twoPi;
    else if (wrapped > pi)
        wrapped -= twoPi;
    return wrapped;
}

} // namespace detail

// ---------------------------------------------------------------------------
// magnitude  (FR-005)
//
// Returns the gain ratio at freqHz by applying the Goertzel analyzer to the
// already-captured output buffer `out`.
//
// Assumes `out` was produced by driving the effect under test with a
// unit-amplitude sine at freqHz, so the recovered amplitude IS the gain.
// Directly returns GoertzelAnalyzer{freqHz, sampleRate}.analyze(out).magnitude.
// ---------------------------------------------------------------------------
inline double magnitude(acfx::span<const float> out,
                        double freqHz,
                        double sampleRate) {
    return GoertzelAnalyzer{freqHz, sampleRate}.analyze(out).magnitude;
}

// ---------------------------------------------------------------------------
// phaseRad  (FR-007)
//
// Returns the phase SHIFT of `out` relative to `in` at `freqHz`, in radians,
// wrapped to the principal-value range (-pi, pi].
//
// Phase-floor guard: when the Goertzel magnitude of `out` is below
// kPhaseMagnitudeFloor the output amplitude is too close to zero for phase to
// carry meaning. In that case quiet_NaN() is returned; callers must check
// std::isnan() before interpreting the result.
//
// Typical usage: `in` holds the original sine stimulus; `out` holds the
// captured effect output at the same frequency and length.
// ---------------------------------------------------------------------------
inline double phaseRad(acfx::span<const float> in,
                       acfx::span<const float> out,
                       double freqHz,
                       double sampleRate) {
    const GoertzelAnalyzer ga{freqHz, sampleRate};
    const GoertzelAnalyzer::Bin outBin = ga.analyze(out);

    if (outBin.magnitude < kPhaseMagnitudeFloor)
        return std::numeric_limits<double>::quiet_NaN();

    const GoertzelAnalyzer::Bin inBin = ga.analyze(in);
    return detail::wrapToPi(outBin.phaseRad - inBin.phaseRad);
}

// ---------------------------------------------------------------------------
// captureImpulseResponse  (FR-006)
//
// Drives `fx` with an impulse of `amplitude` over a buffer of out.size()
// samples and writes the resulting impulse response into `out`.
//
// Allocates a scratch input buffer internally (offline path — allocation OK).
// Calls fx.prepare(ctx) and fx.reset() via capture().
// ---------------------------------------------------------------------------
template <class FX>
void captureImpulseResponse(FX& fx,
                            const acfx::ProcessContext& ctx,
                            acfx::span<float> out,
                            float amplitude = 1.0f) {
    std::vector<float> impulse(out.size());
    ImpulseGenerator{amplitude}.fill(acfx::span<float>{impulse});
    capture(fx, ctx, acfx::span<const float>{impulse}, out);
}

// ---------------------------------------------------------------------------
// captureImpulseResponseCallable  (FR-006)
//
// Drives a per-sample callable float(float) with an impulse of `amplitude`
// and writes the resulting impulse response into `out`.
//
// Allocates a scratch input buffer internally (offline path — allocation OK).
// ---------------------------------------------------------------------------
template <class Fn>
void captureImpulseResponseCallable(Fn&& fn,
                                    acfx::span<float> out,
                                    float amplitude = 1.0f) {
    std::vector<float> impulse(out.size());
    ImpulseGenerator{amplitude}.fill(acfx::span<float>{impulse});
    captureCallable(std::forward<Fn>(fn),
                    acfx::span<const float>{impulse},
                    out);
}

// ---------------------------------------------------------------------------
// thd  (FR-008)
//
// Total harmonic distortion of `out` for a pure-tone stimulus at
// `fundamentalHz`, measured via single-bin Goertzel (no FFT).
//
// Algorithm:
//   V1 = Goertzel amplitude of `out` at fundamentalHz.
//   V2..V(harmonics) = amplitudes at 2*fundamentalHz, 3*fundamentalHz, etc.
//   Harmonics at or above Nyquist (sampleRate/2) are skipped silently.
//   THD = sqrt(V2^2 + V3^2 + ...) / V1
//
// If V1 is below a small epsilon (effectively zero), returns 0.0 to avoid a
// divide-by-zero; this is appropriate because with no fundamental there is no
// meaningful distortion ratio to report.
//
// Result is ~0.0 for a linear effect, elevated for a nonlinear one.
// ---------------------------------------------------------------------------
inline double thd(acfx::span<const float> out,
                  double fundamentalHz,
                  double sampleRate,
                  int harmonics = 5) {
    constexpr double kEpsilon = 1.0e-12;
    const double nyquist = sampleRate / 2.0;

    const double v1 =
        GoertzelAnalyzer{fundamentalHz, sampleRate}.analyze(out).magnitude;

    if (v1 < kEpsilon)
        return 0.0;

    double sumSq = 0.0;
    for (int n = 2; n <= harmonics; ++n) {
        const double harmHz = static_cast<double>(n) * fundamentalHz;
        if (harmHz >= nyquist)
            break;
        const double vn =
            GoertzelAnalyzer{harmHz, sampleRate}.analyze(out).magnitude;
        sumSq += vn * vn;
    }

    return std::sqrt(sumSq) / v1;
}

// ---------------------------------------------------------------------------
// latencySamples  (FR-009)
//
// Returns the processing delay of `out` relative to `in`, in samples (>= 0).
//
// Implemented via CorrelationAnalyzer::lagSamples(in, out), which returns the
// lag at peak cross-correlation — the number of samples by which `out` lags
// `in`. This is the effect's own processing delay (e.g. algorithmic latency or
// look-ahead), not a measurement artefact. Works correctly for an impulse
// stimulus and for general broadband signals.
// ---------------------------------------------------------------------------
inline int latencySamples(acfx::span<const float> in,
                          acfx::span<const float> out) {
    return CorrelationAnalyzer::lagSamples(in, out);
}

} // namespace acfx::measure

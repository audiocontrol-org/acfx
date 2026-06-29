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

} // namespace acfx::measure

#pragma once

// tests/support/measurement/metrics.h
//
// Response metrics (FR-005, FR-006, FR-007) for the acfx measurement harness.
// Namespace: acfx::measure.  Host-side / offline ONLY — may allocate.
//
// Free functions operating on already-captured span<const float> buffers
// (magnitude, phaseRad) or driving an effect/callable with an impulse stimulus
// (captureImpulseResponse*).  The test composer: stimulus -> capture -> metric.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "dsp/audio-block.h"
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

    // No measurable fundamental (silence, a dead/unwired effect, or a wrong-band
    // capture): THD is UNDEFINED. Return NaN rather than 0.0 — a 0.0 here would
    // masquerade as a perfectly linear effect and silently pass an assertion like
    // `thd < 0.05` for a completely dead effect (AUDIT-20260629-06; Constitution V
    // forbids failure-hiding fallbacks, doubly so inside the measurement harness).
    if (v1 < kEpsilon)
        return std::numeric_limits<double>::quiet_NaN();

    double sumSq = 0.0;
    int    measured = 0;
    for (int n = 2; n <= harmonics; ++n) {
        const double harmHz = static_cast<double>(n) * fundamentalHz;
        if (harmHz >= nyquist)
            break;
        const double vn =
            GoertzelAnalyzer{harmHz, sampleRate}.analyze(out).magnitude;
        sumSq += vn * vn;
        ++measured;
    }

    // Not a single harmonic fell below Nyquist (e.g. a 15 kHz fundamental at
    // 44.1 kHz): distortion is UNMEASURABLE for this fundamental/sample-rate,
    // which is distinct from "linear". Return NaN, not 0.0 (AUDIT-20260629-06).
    if (measured == 0)
        return std::numeric_limits<double>::quiet_NaN();

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

// ---------------------------------------------------------------------------
// kIdleNoiseFloor (FR-012)
//
// Maximum acceptable absolute output amplitude when the effect's input is
// silence (all zeros). Values exceeding this floor indicate self-noise, idle
// oscillation, or denormal-driven feedback. Chosen at 1e-6 (~-120 dBFS):
// inaudible for any sane audio context yet comfortably above floating-point
// round-off for a correctly implemented filter.
// ---------------------------------------------------------------------------
inline constexpr double kIdleNoiseFloor = 1.0e-6;

// ---------------------------------------------------------------------------
// kStabilityBound (FR-012)
//
// Maximum acceptable absolute output amplitude for the DC and denormal
// stability cases. Guards against unbounded growth (runaway feedback) or
// NaN/Inf propagation. Chosen at 1e3 (~+60 dBFS above unity): permissive
// enough for any compressor/limiter with realistic make-up gain, yet tight
// enough to catch catastrophic numerical blow-up.
// ---------------------------------------------------------------------------
inline constexpr double kStabilityBound = 1.0e3;

// ---------------------------------------------------------------------------
// Stability  (FR-012)
//
// Result of the stability battery. ok == true means all named cases passed;
// failedCase is nullptr in that case. When ok == false, failedCase holds a
// pointer to a static string naming the first failing case.
// ---------------------------------------------------------------------------
struct Stability {
    bool ok;
    const char* failedCase;  // nullptr when ok == true
};

// ---------------------------------------------------------------------------
// detail::isClean  (internal helper for stability)
//
// Returns true when every sample in `buf` satisfies all four criteria:
//   1. Not NaN  (std::isnan)
//   2. Not infinite  (std::isinf)
//   3. Not subnormal  (std::fpclassify != FP_SUBNORMAL)
//   4. |x| <= bound
//
// Returns true vacuously for an empty span.
// Used by stability() to validate each case's output buffer.
// ---------------------------------------------------------------------------
namespace detail {

inline bool isClean(acfx::span<const float> buf, double bound) noexcept {
    const float fBound = static_cast<float>(bound);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const float x = buf[i];
        if (std::isnan(x) || std::isinf(x))
            return false;
        if (std::fpclassify(x) == FP_SUBNORMAL)
            return false;
        if (std::abs(x) > fBound)
            return false;
    }
    return true;
}

} // namespace detail

// ---------------------------------------------------------------------------
// stability  (FR-012)
//
// Drives `fx` through a battery of four named numerical-stability cases and
// returns the first failure, or {true, nullptr} if all cases pass.
//
// Each case calls capture(fx, ctx, in, out), which calls fx.prepare(ctx) and
// fx.reset() before streaming the input — so every case runs on a freshly
// prepared, reset effect. Buffer length: 4096 samples (~93 ms at 44100 Hz),
// enough for most effects to settle from any initialization transient.
//
// Cases (in order):
//
//   "silence"  — All-zeros input. The entire output must stay below
//                kIdleNoiseFloor. Detects self-noise, idle oscillation, or
//                denormal-driven feedback with no excitation.
//
//   "dc"       — Constant 1.0f input. Output must be bounded within
//                kStabilityBound and contain no NaN, Inf, or subnormals.
//                Detects instability under sustained DC excitation.
//
//   "denormal" — Decaying exponential: input[n] = 10^(-40 * n / (kBufLen-1)).
//                Starts at 1.0f, decays to ~1e-40 (deep in float subnormal
//                range, below FLT_MIN ~= 1.175e-38) by the last sample.
//                The decay multiplier is computed in double precision (~0.97774)
//                so the multiplier itself is a normal float; subnormals arise
//                naturally in the running product as it approaches zero.
//                The output must contain no subnormals, NaN, or Inf, and must
//                remain within kStabilityBound.
//
//   "idle"     — Tail of a fresh silence run: only the second half of a new
//                all-zeros capture is checked (the first half is the settling
//                window). After kBufLen/2 silent samples the effect is
//                considered settled; the tail must stay below kIdleNoiseFloor.
//                Distinct from "silence": targets slowly-building idle drift
//                rather than any-sample exceedance across the full buffer.
// ---------------------------------------------------------------------------
template <class FX>
Stability stability(FX& fx, const acfx::ProcessContext& ctx) {
    constexpr int kBufLen = 4096;

    std::vector<float> input(static_cast<std::size_t>(kBufLen));
    std::vector<float> output(static_cast<std::size_t>(kBufLen));
    const acfx::span<float>       outSpan {output};
    const acfx::span<const float> outConst{output};

    // ---- "silence" ---------------------------------------------------------
    // Feed all-zeros; every output sample must stay below kIdleNoiseFloor.
    std::fill(input.begin(), input.end(), 0.0f);
    capture(fx, ctx, acfx::span<const float>{input}, outSpan);
    if (!detail::isClean(outConst, kIdleNoiseFloor))
        return {false, "silence"};

    // ---- "dc" --------------------------------------------------------------
    // Feed constant 1.0f DC; output must be bounded and free of NaN/Inf/subnormal.
    std::fill(input.begin(), input.end(), 1.0f);
    capture(fx, ctx, acfx::span<const float>{input}, outSpan);
    if (!detail::isClean(outConst, kStabilityBound))
        return {false, "dc"};

    // ---- "denormal" --------------------------------------------------------
    // Decaying exponential from 1.0 to ~1e-40 over kBufLen samples.
    // The per-sample multiplier = 10^(-40/(kBufLen-1)), computed in double
    // (~0.97774) and cast to float — a normal value. Subnormals appear
    // naturally in the running product near the end of the buffer.
    {
        const float kDenormalDecay = static_cast<float>(
            std::pow(10.0, -40.0 / static_cast<double>(kBufLen - 1)));
        float val = 1.0f;
        for (int i = 0; i < kBufLen; ++i) {
            input[static_cast<std::size_t>(i)] = val;
            val *= kDenormalDecay;
        }
    }
    capture(fx, ctx, acfx::span<const float>{input}, outSpan);
    if (!detail::isClean(outConst, kStabilityBound))
        return {false, "denormal"};

    // ---- "idle" ------------------------------------------------------------
    // Run a fresh silence capture; check only the tail (second half) so the
    // effect has kBufLen/2 samples to settle first.
    std::fill(input.begin(), input.end(), 0.0f);
    capture(fx, ctx, acfx::span<const float>{input}, outSpan);
    {
        constexpr std::size_t tailStart = static_cast<std::size_t>(kBufLen / 2);
        constexpr std::size_t tailLen   = static_cast<std::size_t>(kBufLen) - tailStart;
        const acfx::span<const float> tail{output.data() + tailStart, tailLen};
        if (!detail::isClean(tail, kIdleNoiseFloor))
            return {false, "idle"};
    }

    return {true, nullptr};
}

// ---------------------------------------------------------------------------
// ExecCost  (FR-010)
//
// Result of relativeExecTime().
//
// timePerBlock — MEDIAN wall-clock duration of a single fx.process() call,
//               in SECONDS (double).  This is a desktop-relative host-time
//               proxy: it reflects scheduling on the measurement host and
//               MUST NOT be treated as absolute hardware or MCU cycle counts.
// blockSize    — the block size (in samples) used during measurement.
// ---------------------------------------------------------------------------
struct ExecCost {
    double timePerBlock;  // seconds — desktop-relative proxy (NOT hardware cycles)
    int    blockSize;
};

// ---------------------------------------------------------------------------
// relativeExecTime  (FR-010)
//
// Desktop-relative host time-per-block PROXY for effect `fx`.
// IMPORTANT: this metric is a proxy for relative desktop performance only.
// It does NOT measure absolute hardware or MCU execution cycles and MUST NOT
// be interpreted as such.
//
// Procedure:
//   1. Calls fx.prepare(ctx) and fx.reset() once to initialize state.
//   2. Allocates a single-channel scratch buffer of `blockSize` samples.
//   3. Fills the scratch buffer with a deterministic mid-frequency sine
//      stimulus (440 Hz) using SineGenerator so the effect always receives a
//      representative non-decaying signal.
//   4. Runs `repeats` timed iterations. For each iteration:
//        a. Refills the scratch buffer (OUTSIDE the timed region) so the
//           effect always processes a fresh, consistent signal.
//        b. Times ONLY the fx.process(blk) call using
//           std::chrono::steady_clock.
//   5. Collects per-iteration durations (in seconds), sorts them, and returns
//      the MEDIAN — more robust to OS scheduling jitter than the mean.
//
// Guard: if repeats <= 0, it is treated as 1.
// Allocation is permitted (offline measurement path).
//
// Returns: ExecCost{ timePerBlock = median duration in seconds,
//                    blockSize    = blockSize }.
// ---------------------------------------------------------------------------
template <class FX>
ExecCost relativeExecTime(FX& fx,
                          const acfx::ProcessContext& ctx,
                          int blockSize,
                          int repeats) {
    const int safeRepeats = (repeats <= 0) ? 1 : repeats;

    fx.prepare(ctx);
    fx.reset();

    // Allocate scratch buffer and wrap it in a single-channel AudioBlock.
    std::vector<float> scratch(static_cast<std::size_t>(blockSize));
    float* chans[1] = { scratch.data() };
    acfx::AudioBlock blk(chans, 1, blockSize);

    // Build the sine generator used to refill the buffer before each iteration.
    // 440 Hz is a representative mid-frequency signal; constant across calls so
    // every iteration presents an identical input to the effect.
    const SineGenerator gen{440.0, ctx.sampleRate, 1.0f, 0.0};

    std::vector<double> durations(static_cast<std::size_t>(safeRepeats));

    for (int i = 0; i < safeRepeats; ++i) {
        // Refill scratch OUTSIDE the timed region so timing captures only
        // the effect's processing cost, not the stimulus generation cost.
        gen.fill(acfx::span<float>{scratch});

        const auto t0 = std::chrono::steady_clock::now();
        fx.process(blk);
        const auto t1 = std::chrono::steady_clock::now();

        durations[static_cast<std::size_t>(i)] =
            std::chrono::duration<double>(t1 - t0).count();
    }

    // Compute median: sort, take middle element.
    // For even counts the lower-middle element is returned (consistent,
    // deterministic, and avoids a spurious average of two values).
    std::sort(durations.begin(), durations.end());
    const double median = durations[static_cast<std::size_t>(safeRepeats) / 2u];

    return ExecCost{ median, blockSize };
}

} // namespace acfx::measure

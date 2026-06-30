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
// look-ahead), not a measurement artefact.
//
// STIMULUS CONTRACT (AUDIT-20260629-10): drive this with an IMPULSE (or a
// broadband WHITE) stimulus. The underlying unnormalized correlator is only
// well-defined for a sharply-peaked autocorrelation; a periodic/tonal `in`
// (sine, sustained tone, program material) can select a side-lobe lag and
// return the wrong delay — that is out of contract for this minimal-first
// metric (see CorrelationAnalyzer's contract note).
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
// Returns true when every sample in `buf` is numerically stable and bounded:
//   1. Not NaN  (std::isnan)
//   2. Not infinite  (std::isinf)
//   3. |x| <= bound
//
// Subnormals are deliberately NOT rejected here (AUDIT-20260629-12): a subnormal
// is finite and bounded, so it does not threaten numerical stability — the FR-012
// stability contract is about NaN/Inf/runaway growth. Rejecting subnormals in the
// general check produced false-positive "failures" for perfectly stable, correct
// effects (a bypass passing a tiny input through, a high-pass decaying DC toward
// zero, a quiet decaying tail below the idle floor). The denormal-GENERATION
// concern (a CPU-stall hazard, separate from stability) is handled by the
// dedicated "denormal" case via detail::hasSubnormal, which probes denormals the
// effect manufactures internally rather than ones it merely passes through.
//
// Returns true vacuously for an empty span.
// ---------------------------------------------------------------------------
namespace detail {

inline bool isClean(acfx::span<const float> buf, double bound) noexcept {
    const float fBound = static_cast<float>(bound);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        const float x = buf[i];
        if (std::isnan(x) || std::isinf(x))
            return false;
        if (std::abs(x) > fBound)
            return false;
    }
    return true;
}

// True if any sample is subnormal (FP_SUBNORMAL). Used ONLY by the "denormal"
// case, applied to the SILENT decay tail after a normal-amplitude excitation, so
// it detects denormals the effect GENERATES, not subnormal inputs passed through.
inline bool hasSubnormal(acfx::span<const float> buf) noexcept {
    for (std::size_t i = 0; i < buf.size(); ++i)
        if (std::fpclassify(buf[i]) == FP_SUBNORMAL)
            return true;
    return false;
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
//                kIdleNoiseFloor (and be NaN/Inf-free). Detects self-noise or
//                idle oscillation with no excitation.
//
//   "dc"       — Constant 1.0f input. Output must be NaN/Inf-free and bounded
//                within kStabilityBound. Detects instability under sustained DC.
//
//   "denormal" — Denormal GENERATION probe (not passthrough): a normal-amplitude
//                step (1.0f) for the first half, then silence (0.0f) for the
//                second. The output must be NaN/Inf-free and bounded over the
//                whole buffer, AND the SILENT decay tail (second half) must
//                contain no subnormals. Because the stimulus is normal (never a
//                subnormal input), an effect that merely passes a small value
//                through is not flagged — only subnormals the effect MANUFACTURES
//                as its state decays toward zero (a CPU-stall hazard) fail
//                (AUDIT-20260629-12).
//
//   "idle"     — Tail of a fresh silence run: only the second half of a new
//                all-zeros capture is checked (the first half is the settling
//                window). The tail must stay below kIdleNoiseFloor (NaN/Inf-free).
//                Distinct from "silence": targets slowly-building idle drift.
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

    // ---- "denormal" (generation probe, not passthrough) --------------------
    // Normal-amplitude step (1.0f) for the first half, then silence for the
    // second. The stimulus is always NORMAL (never a subnormal input), so an
    // effect that merely passes a small value through is NOT flagged; only
    // denormals the effect MANUFACTURES as its state decays into the silent tail
    // count as a failure (a CPU-stall hazard; AUDIT-20260629-12).
    {
        const std::size_t half = static_cast<std::size_t>(kBufLen / 2);
        std::fill(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(half), 1.0f);
        std::fill(input.begin() + static_cast<std::ptrdiff_t>(half), input.end(), 0.0f);
    }
    capture(fx, ctx, acfx::span<const float>{input}, outSpan);
    if (!detail::isClean(outConst, kStabilityBound))
        return {false, "denormal"};   // NaN / Inf / runaway growth
    {
        // The SILENT decay tail (second half): any subnormal here was generated
        // by the effect's internal state decay, not passed through from input.
        const std::size_t tailStart = static_cast<std::size_t>(kBufLen / 2);
        const acfx::span<const float> tail{output.data() + tailStart,
                                           static_cast<std::size_t>(kBufLen) - tailStart};
        if (detail::hasSubnormal(tail))
            return {false, "denormal"};
    }

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
// Precondition: blockSize MUST be > 0. A non-positive blockSize is caller misuse
// (and a negative value would, via static_cast<size_t>, request a huge scratch
// allocation). Rather than allocate/clamp silently, the function returns
// ExecCost{ quiet_NaN(), blockSize } WITHOUT allocating — surfacing the invalid
// request via NaN (mirroring thd()/phaseRad()'s unmeasurable sentinel) while
// echoing back the offending blockSize for diagnosis. No measurement is run.
// Allocation is permitted (offline measurement path).
//
// Returns: ExecCost{ timePerBlock = median duration in seconds,
//                    blockSize    = blockSize }, or {NaN, blockSize} if blockSize <= 0.
// ---------------------------------------------------------------------------
template <class FX>
ExecCost relativeExecTime(FX& fx,
                          const acfx::ProcessContext& ctx,
                          int blockSize,
                          int repeats) {
    // Defensive precondition (before any allocation): a non-positive blockSize is
    // invalid; a negative one would cast to an enormous size_t allocation.
    if (blockSize <= 0)
        return ExecCost{ std::numeric_limits<double>::quiet_NaN(), blockSize };

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

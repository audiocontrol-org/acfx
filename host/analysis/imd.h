#pragma once

// host/analysis/imd.h
//
// Twin-tone intermodulation distortion (IMD) by the SMPTE and CCIF methods
// (contracts/analysis-engine-api.md "imd.h", data-model.md "ImdResult",
// research.md Decision 5, spec.md US2 / FR-003; harmonic-analysis T015, GREEN
// for T014).
//
// Namespace: acfx::analysis. Host-side / offline ONLY -- may allocate. NEVER
// reachable from portable core/ (Constitution IV); no audio-thread use.
//
// Definition (FR-003, research Decision 5):
//   The engine builds the twin-tone stimulus INTERNALLY, drives it through the
//   supplied effect/callable, and measures the intermodulation PRODUCT bins,
//   reporting DIFFERENCE and SUM products.
//     - SMPTE: f1 = 60 Hz + f2 = 7000 Hz, amplitude ratio 4:1 (low tone 4x the
//       high tone). The high tone is modulated by the low tone; the products are
//       its sidebands at 7000 +/- k*60 -- the LOWER sidebands (7000 - k*60) are
//       reported as difference products, the UPPER sidebands (7000 + k*60) as
//       sum products, for k = 1, 2.
//     - CCIF: f1 = 19 kHz + f2 = 20 kHz, equal amplitude. Difference products
//       f2-f1 = 1 kHz (2nd order) and the 3rd-order 2f1-f2 = 18 kHz, 2f2-f1 =
//       21 kHz; the sum product f1+f2 = 39 kHz lies above Nyquist (unmeasurable).
//
// Method:
//   The stimulus is built at a FIXED, integer-cycle configuration (kImdSampleRate
//   = 48000 Hz, kImdNumSamples = 4800 -> 10 Hz/bin) so 60, 7000, 19000, 20000 and
//   every sum/difference/harmonic frequency land EXACTLY on their own bin. Each
//   product amplitude is read with the exact, leakage-free single-bin Goertzel
//   (acfx::measure::GoertzelAnalyzer, analyzers.h) -- the SAME retained integer-
//   cycle path FR-007/010 keeps and spectrum.h/thdn.h already use, so a product
//   amplitude is numerically identical to a harmonic amplitude read elsewhere
//   (no new spectral machinery for this metric).
//
// Out-of-band / unmeasurable (FR-008): a product frequency at or above Nyquist
//   (or <= 0) is UNMEASURABLE. Its amplitude is quiet_NaN -- NEVER a fabricated
//   0.0 that would masquerade as "no product". (The CCIF sum product at 39 kHz
//   is the standing example.) A sampled memoryless nonlinearity would alias such
//   a term back below Nyquist, but attributing an aliased artifact's energy to
//   the nominal sum frequency would be dishonest; the honest report is NaN,
//   consistent with spectrum.h's Nyquist convention.
//
// Attribution / no double-counting (data-model.md rule, spec edge case):
//   A product frequency is attributed to exactly ONE product class (difference OR
//   sum) and appears at most once across the two lists -- the frequency tables
//   below are disjoint by construction. Low-order tone HARMONICS (2f1, 2f2, 3f1,
//   ... -- e.g. SMPTE 2f1 = 120 Hz, 2f2 = 14000 Hz) are NOT intermodulation
//   products; they are owned by the spectrum/THD path and are never inserted into
//   these product lists. If a product frequency happened to coincide with a tone
//   harmonic, the stimulus being a pure twin-tone through a memoryless
//   nonlinearity means that bin's energy IS the intermodulation product, so it is
//   reported once as the product (never additionally counted as a harmonic). For
//   the standard SMPTE/CCIF tone pairs no such coincidence occurs.
//
// Deviation from contracts/analysis-engine-api.md's sketch
//   `ImdResult imd(EffectOrCallable fx, ImdMethod method)`:
//   - The stimulus sample rate / length are FIXED internal constants (the twin-
//     tone is "built internally" per the contract), so the signature carries no
//     sampleRate/length -- exactly as the contract sketch shows.
//   - ImdResult adds `differenceFreqHz[]` / `sumFreqHz[]` companion arrays
//     alongside the `differenceProducts[]` / `sumProducts[]` amplitudes from
//     data-model.md. This is a documented EXTENSION that makes the data-model's
//     own "attributed unambiguously" rule observable: each amplitude is paired
//     with the exact frequency it was measured at, so a consumer can attribute a
//     bin without re-deriving the product table.

#include <cmath>       // std::isnan
#include <cstddef>     // std::size_t
#include <limits>      // std::numeric_limits
#include <stdexcept>   // std::invalid_argument
#include <string>      // std::to_string
#include <utility>     // std::forward
#include <vector>      // std::vector (offline scratch; NOT audio path)

#include "analysis/analyzers.h"  // GoertzelAnalyzer, capture, captureCallable
#include "dsp/process-context.h" // acfx::ProcessContext (Effect overload)
#include "dsp/span.h"

namespace acfx::analysis {

// The two industry-standard twin-tone IMD methods (data-model.md "ImdResult").
enum class ImdMethod { SMPTE, CCIF };

// Fixed integer-cycle stimulus configuration. 48000 Hz over 4800 samples gives
// 10 Hz/bin, so 60/7000/19000/20000 and every sum/difference/harmonic frequency
// land exactly on a Goertzel bin (leakage-free), mirroring every other suite in
// this tree. Exposed so an Effect caller can construct a matching ProcessContext.
inline constexpr double      kImdSampleRate = 48000.0;
inline constexpr std::size_t kImdNumSamples = 4800;

// Amplitude below which a carrier is considered ABSENT (unmeasurable). Mirrors
// thdn.h's kThdnFundamentalFloor: a Goertzel amplitude under this floor is float
// round-off, not a real tone.
inline constexpr double kImdCarrierFloor = 1.0e-12;

// Twin-tone IMD result (data-model.md "ImdResult").
struct ImdResult {
    ImdMethod method;

    // Amplitudes (Goertzel amplitude-normalized: ~1.0 for a unit sine) at each
    // difference / sum product frequency, in the documented per-method order.
    // An out-of-band product (freq >= Nyquist or <= 0) is quiet_NaN (FR-008),
    // never a fabricated 0.0.
    std::vector<double> differenceProducts;
    std::vector<double> sumProducts;

    // Companion frequency (Hz) for each amplitude above -- same index/order --
    // so every product bin is attributed unambiguously to its frequency
    // (data-model rule; documented extension over the sketch).
    std::vector<double> differenceFreqHz;
    std::vector<double> sumFreqHz;

    // Product energy vs carrier energy: sqrt(sum of in-band product amplitude^2)
    // / sqrt(carrier1^2 + carrier2^2), where the carriers are the two input tones
    // measured in the OUTPUT. Out-of-band (NaN) products are excluded from the
    // numerator. quiet_NaN when no carrier is measurable (FR-008).
    double imdRatio;
};

namespace detail {

// Per-method stimulus + product-frequency description.
struct ImdSpec {
    double f1Hz;
    double f2Hz;
    double a1;  // amplitude of tone 1 (f1)
    double a2;  // amplitude of tone 2 (f2)
    std::vector<double> differenceFreqHz;  // lower sidebands / difference tones
    std::vector<double> sumFreqHz;         // upper sidebands / sum tones
};

inline ImdSpec imdSpec(ImdMethod method) {
    if (method == ImdMethod::SMPTE) {
        // f1 = 60 Hz (low, 4x), f2 = 7000 Hz (high, 1x). Products are the k=1,2
        // sidebands of the high tone: lower = difference, upper = sum.
        constexpr double f1 = 60.0;
        constexpr double f2 = 7000.0;
        return ImdSpec{
            f1, f2, /*a1=*/0.4, /*a2=*/0.1,
            /*difference=*/{f2 - 1.0 * f1, f2 - 2.0 * f1},  // 6940, 6880
            /*sum=*/       {f2 + 1.0 * f1, f2 + 2.0 * f1}}; // 7060, 7120
    }
    // CCIF: f1 = 19 kHz, f2 = 20 kHz, equal amplitude.
    constexpr double f1 = 19000.0;
    constexpr double f2 = 20000.0;
    return ImdSpec{
        f1, f2, /*a1=*/0.4, /*a2=*/0.4,
        /*difference=*/{f2 - f1, 2.0 * f1 - f2, 2.0 * f2 - f1},  // 1000, 18000, 21000
        /*sum=*/       {f1 + f2}};                               // 39000 (out-of-band)
}

// Fill `out` with the twin-tone stimulus a1*sin(w1 n) + a2*sin(w2 n), matching
// SineGenerator's phase convention so the components superpose correctly.
inline void fillTwinTone(acfx::span<float> out, const ImdSpec& spec) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const double w1 = kTwoPi * spec.f1Hz / kImdSampleRate;
    const double w2 = kTwoPi * spec.f2Hz / kImdSampleRate;
    for (std::size_t n = 0; n < out.size(); ++n) {
        const double nd = static_cast<double>(n);
        out[n] = static_cast<float>(spec.a1 * std::sin(w1 * nd) +
                                    spec.a2 * std::sin(w2 * nd));
    }
}

// Amplitude at `freqHz` in `out`, or quiet_NaN if the frequency is out of band
// (>= Nyquist or <= 0) -- unmeasurable, never a fabricated 0.0 (FR-008).
inline double productAmplitude(acfx::span<const float> out, double freqHz) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    const double nyquist = kImdSampleRate / 2.0;
    if (!(freqHz > 0.0) || freqHz >= nyquist) {
        return nan;
    }
    return acfx::measure::GoertzelAnalyzer{freqHz, kImdSampleRate}.analyze(out).magnitude;
}

// Measure the difference/sum products and the imdRatio from a captured output.
inline ImdResult measure(acfx::span<const float> out, ImdMethod method,
                         const ImdSpec& spec) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    ImdResult r;
    r.method = method;
    r.differenceFreqHz = spec.differenceFreqHz;
    r.sumFreqHz = spec.sumFreqHz;

    double productPowerSum = 0.0;  // sum of in-band product amplitude^2

    r.differenceProducts.reserve(spec.differenceFreqHz.size());
    for (const double f : spec.differenceFreqHz) {
        const double amp = productAmplitude(out, f);
        r.differenceProducts.push_back(amp);
        if (!std::isnan(amp)) productPowerSum += amp * amp;
    }
    r.sumProducts.reserve(spec.sumFreqHz.size());
    for (const double f : spec.sumFreqHz) {
        const double amp = productAmplitude(out, f);
        r.sumProducts.push_back(amp);
        if (!std::isnan(amp)) productPowerSum += amp * amp;
    }

    // Carriers measured in the OUTPUT at f1 and f2.
    const double c1 = productAmplitude(out, spec.f1Hz);
    const double c2 = productAmplitude(out, spec.f2Hz);
    const double carrierPowerSum =
        (std::isnan(c1) ? 0.0 : c1 * c1) + (std::isnan(c2) ? 0.0 : c2 * c2);

    r.imdRatio = (carrierPowerSum > kImdCarrierFloor * kImdCarrierFloor)
                     ? std::sqrt(productPowerSum) / std::sqrt(carrierPowerSum)
                     : nan;
    return r;
}

} // namespace detail

// imd(fn, method): drive a per-sample callable float(float) `fn` (a memoryless
// or stateless-per-block nonlinearity) with the internally-built twin-tone
// stimulus for `method`, via captureCallable, and report the difference/sum
// intermodulation products (data-model.md "ImdResult").
template <class Fn>
inline ImdResult imd(Fn&& fn, ImdMethod method) {
    const detail::ImdSpec spec = detail::imdSpec(method);

    std::vector<float> stimulus(kImdNumSamples, 0.0f);
    std::vector<float> out(kImdNumSamples, 0.0f);
    detail::fillTwinTone(acfx::span<float>{stimulus}, spec);

    acfx::measure::captureCallable(std::forward<Fn>(fn),
                                   acfx::span<const float>(stimulus),
                                   acfx::span<float>{out});

    return detail::measure(acfx::span<const float>(out), method, spec);
}

// imd(fx, ctx, method): the Effect-contract front door (FR-006). Drives an
// Effect implementation with the internally-built twin-tone via capture(). The
// caller MUST pass a ProcessContext whose sampleRate == kImdSampleRate so the
// stimulus stays integer-cycle (a mismatched rate is a caller error, not
// silently corrected -- enforced below by throwing, never silently coerced
// or ignored; code-review finding D2). Distinct arity from imd(fn, method) ->
// no overload ambiguity.
template <class FX>
inline ImdResult imd(FX& fx, const acfx::ProcessContext& ctx, ImdMethod method) {
    if (ctx.sampleRate != kImdSampleRate) {
        throw std::invalid_argument(
            "acfx::analysis::imd: ctx.sampleRate must equal kImdSampleRate (" +
            std::to_string(kImdSampleRate) + "); got " +
            std::to_string(ctx.sampleRate));
    }

    const detail::ImdSpec spec = detail::imdSpec(method);

    std::vector<float> stimulus(kImdNumSamples, 0.0f);
    std::vector<float> out(kImdNumSamples, 0.0f);
    detail::fillTwinTone(acfx::span<float>{stimulus}, spec);

    acfx::measure::capture(fx, ctx,
                           acfx::span<const float>(stimulus),
                           acfx::span<float>{out});

    return detail::measure(acfx::span<const float>(out), method, spec);
}

} // namespace acfx::analysis

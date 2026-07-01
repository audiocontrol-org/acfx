#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "primitives/oversampling/halfband-stage.h"

// Oversampler<Factor>: generic power-of-two oversampling block-wrapper
// (contract "Oversampler", data-model "Oversampler<int Factor>").
//
// Cascade structure
// -----------------
// Factor in {2, 4, 8} is realized as kStages = log2(Factor) cascaded 2x
// half-band stages (nested doubling), never a single length-N filter at the
// top rate:
//
//   up_[0]   base -> 2x     down_[0]   2x   -> base
//   up_[1]   2x   -> 4x     down_[1]   4x   -> 2x
//   up_[2]   4x   -> 8x     down_[2]   8x   -> 4x
//
// process() interpolates x up through up_[0..kStages-1] to Factor oversampled
// samples (each 2x stage turns one input sample into an even/odd phase pair, in
// oversampled-time order), invokes the caller's nonlinearity on each of the
// Factor samples, then decimates back down through down_[kStages-1..0] to one
// output sample. The even/odd phase pairing produced by HalfbandUpsampler
// (out0 = even, out1 = odd) is preserved into HalfbandDownsampler (in0 = even,
// in1 = odd) at every 2x level.
//
// Latency
// -------
// Each stage's half-band group delay is HalfbandUpsampler/Downsampler::latency()
// == kCenter (45), expressed at THAT stage's high (oversampled) rate. Referred
// to the base rate, a stage running at rate 2^k * base contributes 45 / 2^k base
// samples. Summing the up and down cascades:
//
//   L_base = 2 * sum_{k=1..kStages} 45 / 2^k = 90 * (1 - 1/Factor)
//
//   Factor 2 -> 45     (integer)
//   Factor 4 -> 67.5   (half-integer tie -> 67, see latencySamples())
//   Factor 8 -> 78.75  (rounds to 79)
//
// The composite (identity eval) is linear phase, so its integer group delay is
// round(90 * (1 - 1/Factor)) with half-integer ties resolved toward the lower
// index to match the measured (first-max) impulse peak; verified against the
// measured impulse-peak delay (FR-006/012).
//
// RT-safety (Constitution VI): all storage is compile-time-sized std::array
// (stages + two ping-pong scratch buffers of Factor floats); no heap, no locks,
// bounded work; every method is noexcept. Platform-independent: standard library
// only (Constitution IV).

namespace acfx {

template <int Factor>
class Oversampler {
public:
    static_assert(Factor == 2 || Factor == 4 || Factor == 8,
                  "Oversampler Factor must be 2, 4, or 8");

    // Prepare for a base sample rate. Clears filter state. No allocation
    // (coefficients are static constexpr; nothing to compute).
    void init(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        reset();
    }

    // Clear every stage's delay line; configuration (Factor, rate) preserved.
    void reset() noexcept {
        for (int s = 0; s < kStages; ++s) {
            up_[static_cast<std::size_t>(s)].reset();
            down_[static_cast<std::size_t>(s)].reset();
        }
    }

    // Effective internal rate the caller's nonlinearity runs at.
    float oversampledRate() const noexcept {
        return sampleRate_ * static_cast<float>(Factor);
    }

    // The EXACT linear-phase group delay referred to the BASE rate, in samples:
    //   90 * (1 - 1/Factor)  =  2 * kCenter * (Factor - 1) / Factor.
    // Integer for Factor 2 (45); FRACTIONAL for Factor 4 (67.5) and 8 (78.75) —
    // the composite of stages running at different rates has a genuinely
    // sub-sample group delay. Callers needing sub-sample-accurate alignment
    // (e.g. an internal dry/wet delay match, saturation-core.h) use this.
    float groupDelaySamples() const noexcept {
        return static_cast<float>(2 * halfband_detail::kCenter * (Factor - 1))
             / static_cast<float>(Factor);
    }

    // The INTEGER host-PDC latency: groupDelaySamples() rounded to the nearest
    // whole sample (ties toward the lower index). This is what a host / integer-
    // sample delay-compensation scheme uses — hosts compensate in whole samples,
    // so a rounded integer is the correct reported latency; the exact (possibly
    // fractional) delay is groupDelaySamples() above. For Factor 4 the true delay
    // is 67.5 and the linear-phase impulse energy splits equally between samples
    // 67 and 68, so the measured (first-max) peak is 67. Round-half-down =
    // floor((2*numer + Factor - 1) / (2*Factor)).
    int latencySamples() const noexcept {
        constexpr int numer = 2 * halfband_detail::kCenter * (Factor - 1);
        return (2 * numer + Factor - 1) / (2 * Factor);
    }

    // Process one input sample: upsample x to Factor oversampled samples, invoke
    // evalAtHighRate on EACH (in oversampled-time order), then decimate back to
    // exactly one output sample. Allocation-free, lock-free, bounded.
    template <class Eval>
    float process(float x, Eval&& evalAtHighRate) noexcept {
        static_assert(noexcept(evalAtHighRate(0.0f)),
                      "evalAtHighRate must be noexcept (RT-safe hot path)");

        float* cur = scratchA_.data();
        float* nxt = scratchB_.data();

        // Interpolation cascade: 1 -> Factor samples (nested 2x doubling).
        cur[0] = x;
        int count = 1;
        for (int s = 0; s < kStages; ++s) {
            auto& stage = up_[static_cast<std::size_t>(s)];
            for (int i = 0; i < count; ++i) {
                float out0 = 0.0f;
                float out1 = 0.0f;
                stage.process(cur[i], out0, out1);  // out0 = even, out1 = odd
                nxt[2 * i] = out0;
                nxt[2 * i + 1] = out1;
            }
            count *= 2;
            std::swap(cur, nxt);
        }

        // Caller's nonlinearity, exactly Factor times, in oversampled-time order.
        for (int i = 0; i < Factor; ++i) {
            cur[i] = evalAtHighRate(cur[i]);
        }

        // Decimation cascade: Factor -> 1 sample, mirroring the up cascade.
        for (int s = kStages - 1; s >= 0; --s) {
            auto& stage = down_[static_cast<std::size_t>(s)];
            const int half = count / 2;
            for (int k = 0; k < half; ++k) {
                // in0 = even phase, in1 = odd phase (pairing from upstream).
                nxt[k] = stage.process(cur[2 * k], cur[2 * k + 1]);
            }
            count = half;
            std::swap(cur, nxt);
        }

        return cur[0];
    }

private:
    static constexpr int kStages =
        (Factor == 2) ? 1 : (Factor == 4) ? 2 : 3;  // log2(Factor)

    std::array<HalfbandUpsampler, static_cast<std::size_t>(kStages)> up_{};
    std::array<HalfbandDownsampler, static_cast<std::size_t>(kStages)> down_{};

    // Ping-pong scratch for the oversampled sample run (fixed-size, no heap).
    std::array<float, static_cast<std::size_t>(Factor)> scratchA_{};
    std::array<float, static_cast<std::size_t>(Factor)> scratchB_{};

    float sampleRate_ = 0.0f;
};

}  // namespace acfx

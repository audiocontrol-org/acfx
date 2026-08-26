#pragma once

// ============================================================================
// SPIKE / THROWAWAY — reverse-reverb feasibility probe, NOT a shipping effect.
// ----------------------------------------------------------------------------
// Answers "does a reverse reverb fit in the F446 SRAM using decimation for the
// capture window?" A reverse reverb must buffer a window before it can reverse
// it, so it is inherently latent (latency == window length) and double-
// buffered (capture window A while emitting the reversed+reverberated B).
//
// DECIMATION DONE PROPERLY (revised after the first probe aliased audibly):
//   - capture rate is 16 kHz (D=3 from 48k), stored int16, double-buffered:
//     16000 * int16 * 2 buffers = 64 KB (vs 375 KB full-rate stereo).
//   - an anti-alias BIQUAD lowpass runs at the full rate BEFORE the input is
//     decimated, so content above the 8 kHz decimated-Nyquist can no longer
//     fold back down as aliasing (the first /6 sample-and-hold probe's main
//     artifact source).
//   - the reversed window is streamed through a mono 16 kHz Freeverb tank, and
//     the wet output is LINEARLY INTERPOLATED across each decimation interval
//     (not zero-order held) to suppress the stair-step imaging.
//
// int16 is retained deliberately: its ~-96 dB floor is inaudible next to the
// reverb, and float capture would double the window to 128 KB. If this probe
// passes it gets replaced by a specced effect (window/rate as live params,
// crossfaded swaps, tests) — do not build on it.
//
// Platform independence (Constitution IV): standard library only, no heap.
// Single-context setParameter (D26): params applied directly.
// ============================================================================

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "primitives/lofi/int16-quant.h"

namespace acfx {

class SpikeReverseReverb {
public:
    static constexpr int kInternalDivisor = 3;       // 48k -> 16k capture + tank rate
    static constexpr int kWindow          = 16000;   // 1.0 s @ 16 kHz (fixed for the spike)

    // Mono Freeverb tunings scaled from 44.1 kHz to 16 kHz (round(len*16000/44100)).
    static constexpr int kComb[8] = {405, 431, 463, 492, 516, 541, 565, 587};
    static constexpr int kAp[4]   = {202, 160, 124, 82};
    static constexpr float kGain  = 0.015f;
    static constexpr float kApFb  = 0.5f;

    enum : std::uint8_t { kDecay = 0, kDamping = 1, kMix = 2 };
    static constexpr std::size_t kNumParams = 3;

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        const float fs = static_cast<float>(ctx.sampleRate);
        // Anti-alias cutoff at 85% of the decimated Nyquist (16 kHz / 2).
        const float decimatedRate = fs / static_cast<float>(kInternalDivisor);
        designLowpass(aa_, fs, decimatedRate * 0.5f * 0.85f);
        setParameter(ParamId{kDecay},   normalize(kParams[kDecay],   kParams[kDecay].defaultValue));
        setParameter(ParamId{kDamping}, normalize(kParams[kDamping], kParams[kDamping].defaultValue));
        setParameter(ParamId{kMix},     normalize(kParams[kMix],     kParams[kMix].defaultValue));
        reset();
    }

    void reset() noexcept {
        bufs_[0].fill(0);
        bufs_[1].fill(0);
        pool_.fill(0.0f);
        for (int i = 0; i < 8; ++i) { combPos_[i] = 0; combStore_[i] = 0.0f; }
        for (int i = 0; i < 4; ++i) { apPos_[i] = 0; }
        aa_.reset();
        sel_ = 0; writeIdx_ = 0; playIdx_ = 0; playActive_ = false;
        decimPhase_ = 0; wetPrev_ = 0.0f; wetCur_ = 0.0f;
    }

    void setParameter(ParamId id, float normalized) noexcept {
        if (id.value >= kNumParams) return;
        const float v = denormalize(kParams[id.value], normalized);
        switch (id.value) {
            case kDecay:   combFb_ = v * 0.28f + 0.7f;                 break;
            case kDamping: damp1_  = v * 0.4f; damp2_ = 1.0f - damp1_; break;
            case kMix:     mix_    = v;                                break;
            default: break;
        }
    }

    void process(AudioBlock& io) noexcept {
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples  = io.numSamples();
        float* xL = io.channel(0);
        float* xR = channels > 1 ? io.channel(1) : xL;
        const float invD = 1.0f / static_cast<float>(kInternalDivisor);

        for (int n = 0; n < samples; ++n) {
            const float dryL = xL[n];
            const float dryR = xR[n];

            // Anti-alias filter the mono input at the FULL rate, every sample,
            // so the decimated capture below cannot alias.
            const float monoAA = aa_.process((dryL + dryR) * 0.5f);

            if (decimPhase_ == 0) {
                // Store one anti-aliased, decimated sample into the write buffer.
                bufs_[sel_][static_cast<std::size_t>(writeIdx_)] = quantizeInt16(monoAA);
                ++writeIdx_;

                // Emit one reversed sample through the tank; interpolate between
                // the previous and current wet values across the D-sample gap.
                float rev = 0.0f;
                if (playActive_ && playIdx_ >= 0) {
                    rev = dequantizeInt16(bufs_[sel_ ^ 1][static_cast<std::size_t>(playIdx_)]);
                    --playIdx_;
                }
                wetPrev_ = wetCur_;
                wetCur_  = runTank(rev * kGain);

                if (writeIdx_ >= kWindow) {
                    sel_ ^= 1;
                    writeIdx_  = 0;
                    playIdx_   = kWindow - 1;
                    playActive_ = true;
                }
            }

            // Linear reconstruction: ramp from wetPrev_ to wetCur_ over the
            // interval (removes the zero-order-hold stair-step imaging).
            const float frac = static_cast<float>(decimPhase_) * invD;
            const float wet  = wetPrev_ + (wetCur_ - wetPrev_) * frac;
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            xL[n] = dryL * (1.0f - mix_) + wet * mix_;
            if (channels > 1) xR[n] = dryR * (1.0f - mix_) + wet * mix_;
        }
    }

private:
    // --- minimal RBJ biquad (Direct Form I), for the anti-alias lowpass ---
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        float process(float x) noexcept {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
    };
    static void designLowpass(Biquad& bq, float fs, float fc) noexcept {
        const float w0    = 2.0f * 3.14159265358979f * fc / fs;
        const float cosw  = std::cos(w0);
        const float sinw  = std::sin(w0);
        const float alpha = sinw / (2.0f * 0.70710678f);   // Q = 1/sqrt(2), Butterworth
        const float a0    = 1.0f + alpha;
        bq.b0 = (1.0f - cosw) * 0.5f / a0;
        bq.b1 = (1.0f - cosw)        / a0;
        bq.b2 = (1.0f - cosw) * 0.5f / a0;
        bq.a1 = (-2.0f * cosw)       / a0;
        bq.a2 = (1.0f - alpha)       / a0;
    }

    static constexpr int kPoolSize = [] {
        int t = 0;
        for (int i = 0; i < 8; ++i) t += kComb[i];
        for (int i = 0; i < 4; ++i) t += kAp[i];
        return t;
    }();

    int combOff(int i) const noexcept {
        int off = 0;
        for (int k = 0; k < i; ++k) off += kComb[k];
        return off;
    }
    int apOff(int i) const noexcept {
        int off = 0;
        for (int k = 0; k < 8; ++k) off += kComb[k];
        for (int k = 0; k < i; ++k) off += kAp[k];
        return off;
    }

    float runTank(float fed) noexcept {
        float acc = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const int off = combOff(i);
            int&   pos = combPos_[i];
            float& st  = combStore_[i];
            const float y = pool_[static_cast<std::size_t>(off + pos)];
            st = y * damp2_ + st * damp1_;
            pool_[static_cast<std::size_t>(off + pos)] = fed + st * combFb_;
            if (++pos >= kComb[i]) pos = 0;
            acc += y;
        }
        float x = acc;
        for (int i = 0; i < 4; ++i) {
            const int off = apOff(i);
            int& pos = apPos_[i];
            const float bufout = pool_[static_cast<std::size_t>(off + pos)];
            const float out    = -x + bufout;
            pool_[static_cast<std::size_t>(off + pos)] = x + bufout * kApFb;
            if (++pos >= kAp[i]) pos = 0;
            x = out;
        }
        return x;
    }

    static constexpr std::array<ParameterDescriptor, kNumParams> kParams = {{
        {ParamId{kDecay}, "decay", ParamUnit::none, 0.0f, 1.0f, 0.8f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDamping}, "damping", ParamUnit::none, 0.0f, 1.0f, 0.4f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "mix", ParamUnit::none, 0.0f, 1.0f, 0.7f,
         ParamSkew::linear, ParamKind::continuous, 0},
    }};

    // Double-buffered decimated int16 capture (the memory-critical store).
    std::array<std::array<std::int16_t, kWindow>, 2> bufs_{};
    int  sel_        = 0;
    int  writeIdx_   = 0;
    int  playIdx_    = 0;
    bool playActive_ = false;

    Biquad aa_{};                              // anti-alias lowpass (full-rate)

    // Mono 16 kHz Freeverb tank.
    std::array<float, kPoolSize> pool_{};
    int   combPos_[8]   = {};
    float combStore_[8] = {};
    int   apPos_[4]     = {};

    int   decimPhase_ = 0;
    float wetPrev_    = 0.0f;
    float wetCur_     = 0.0f;

    int   numChannels_ = 2;
    float combFb_ = 0.924f;
    float damp1_  = 0.16f;
    float damp2_  = 0.84f;
    float mix_    = 0.7f;
};

} // namespace acfx

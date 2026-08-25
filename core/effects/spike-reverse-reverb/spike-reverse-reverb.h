#pragma once

// ============================================================================
// SPIKE / THROWAWAY — reverse-reverb feasibility probe, NOT a shipping effect.
// ----------------------------------------------------------------------------
// Answers "does a reverse reverb fit in the F446 SRAM using decimation for the
// capture window?" The capture buffer is the whole memory story: a reverse
// reverb must buffer a window before it can reverse it, so it is inherently
// latent (latency == window length) and double-buffered (capture window A
// while emitting the reversed+reverberated window B).
//
// Technique under test: DECIMATE the capture to 8 kHz (D=6) and store it as
// int16 — a 1.0 s stereo-summed mono window is then 8000 * int16 * 2 buffers
// = 32 KB (vs 375 KB full-rate stereo double-buffered). The reversed window is
// streamed through a compact mono Freeverb tank (also at 8 kHz, ~9 KB) so the
// swell is diffuse/reverberant, not a bare reverse-tape echo.
//
// The 8 kHz bandwidth (telephone-ish) is acceptable for a diffuse reverse
// swell. If this probe passes it gets replaced by a specced effect (window as
// a live param, thread-safe setParameter, tests) — do not build on it.
//
// Platform independence (Constitution IV): standard library only, no heap.
// Single-context setParameter (D26): params applied directly.
// ============================================================================

#include <array>
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
    static constexpr int kInternalDivisor = 6;      // 48k -> 8k capture + tank rate
    static constexpr int kWindow          = 8000;   // 1.0 s @ 8 kHz (fixed for the spike)

    // Mono Freeverb tunings scaled from 44.1 kHz to 8 kHz (round(len*8000/44100)).
    static constexpr int kComb[8] = {202, 216, 232, 246, 258, 270, 283, 293};
    static constexpr int kAp[4]   = {101, 80, 62, 41};
    static constexpr float kGain  = 0.015f;
    static constexpr float kApFb  = 0.5f;

    enum : std::uint8_t { kDecay = 0, kDamping = 1, kMix = 2 };
    static constexpr std::size_t kNumParams = 3;

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
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
        sel_ = 0; writeIdx_ = 0; playIdx_ = 0; playActive_ = false;
        decimPhase_ = 0; heldWet_ = 0.0f;
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

        for (int n = 0; n < samples; ++n) {
            const float dryL = xL[n];
            const float dryR = xR[n];

            if (decimPhase_ == 0) {
                // Capture one decimated mono sample into the write buffer.
                const float mono = (dryL + dryR) * 0.5f;
                bufs_[sel_][static_cast<std::size_t>(writeIdx_)] = quantizeInt16(mono);
                ++writeIdx_;

                // Emit one reversed+reverberated sample from the play buffer.
                float rev = 0.0f;
                if (playActive_ && playIdx_ >= 0) {
                    rev = dequantizeInt16(bufs_[sel_ ^ 1][static_cast<std::size_t>(playIdx_)]);
                    --playIdx_;
                }
                heldWet_ = runTank(rev * kGain);

                // Window full: swap. Capture (writeIdx_->N) and playback
                // (playIdx_->-1) complete on the SAME tick by construction.
                if (writeIdx_ >= kWindow) {
                    sel_ ^= 1;
                    writeIdx_  = 0;
                    playIdx_   = kWindow - 1;
                    playActive_ = true;
                }
            }
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            xL[n] = dryL * (1.0f - mix_) + heldWet_ * mix_;
            if (channels > 1) xR[n] = dryR * (1.0f - mix_) + heldWet_ * mix_;
        }
    }

private:
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
    int  sel_        = 0;      // which buffer is currently capturing
    int  writeIdx_   = 0;
    int  playIdx_    = 0;
    bool playActive_ = false;

    // Mono 8 kHz Freeverb tank.
    std::array<float, kPoolSize> pool_{};
    int   combPos_[8]   = {};
    float combStore_[8] = {};
    int   apPos_[4]     = {};

    int   decimPhase_ = 0;
    float heldWet_    = 0.0f;

    int   numChannels_ = 2;
    float combFb_ = 0.924f;   // decay 0.8 default
    float damp1_  = 0.16f;    // damping 0.4 default
    float damp2_  = 0.84f;
    float mix_    = 0.7f;
};

} // namespace acfx

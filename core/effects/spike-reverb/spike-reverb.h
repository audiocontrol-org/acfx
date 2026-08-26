#pragma once

// ============================================================================
// SPIKE / THROWAWAY — reverb feasibility probe, NOT a shipping effect.
// ----------------------------------------------------------------------------
// Does a reverb fit in the F446 SRAM and run, using the lo-fi delay's internal-
// rate decimation? Freeverb-style tank (8 comb + 4 allpass/ch) run at a /2-
// decimated 24 kHz internal rate in float (~55 KB stereo). Decimation buys back
// exactly the memory float costs; the ~12 kHz internal Nyquist is inaudible in
// a reverb tail.
//
// v2 adds a MODULATED DELAY stage in front of the tank (per channel, full-rate
// int16, up to 120 ms). It is a chorus at short times (LFO-swept tap blended
// with dry) and an echo at long times (with feedback) — one flexible stage
// feeding the reverb. Params 3..6 map onto the existing CC bindings
// (CC70/73/75/78). ~+23 KB, still fits with headroom.
//
// If this probe passes it gets replaced by a properly-specced effect (own
// params file, thread-safe setParameter, tests) — do not build on it.
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
#include "primitives/delays/bounded-delay-line.h"

namespace acfx {

class SpikeReverb {
public:
    static constexpr int kInternalDivisor = 2;              // 48k -> 24k tank
    static constexpr int kSpread          = 13;
    static constexpr int kCombL[8] = {607, 646, 695, 738, 774, 811, 847, 880};
    static constexpr int kApL[4]   = {303, 240, 186, 122};
    static constexpr float kGain   = 0.015f;
    static constexpr float kApFb   = 0.5f;

    static constexpr int   kPreMax   = 5760;    // 120 ms @ 48 kHz front delay line
    static constexpr float kModMaxMs = 5.0f;    // full-scale chorus excursion (+/- ms)

    enum : std::uint8_t {
        kRoomSize = 0, kDamping = 1, kMix = 2,
        kDelayTime = 3, kModDepth = 4, kModRate = 5, kFeedback = 6,
    };
    static constexpr std::size_t kNumParams = 7;

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        modMaxSamp_  = kModMaxMs * 0.001f * sampleRate_;
        const int cap = static_cast<int>(0.120f * sampleRate_) + 1;
        for (int ch = 0; ch < 2; ++ch) preLine_[ch].prepare(cap, sampleRate_);
        for (std::size_t i = 0; i < kNumParams; ++i)
            setParameter(ParamId{static_cast<std::uint8_t>(i)},
                         normalize(kParams[i], kParams[i].defaultValue));
        reset();
    }

    void reset() noexcept {
        pool_.fill(0.0f);
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 8; ++i) { combPos_[ch][i] = 0; combStore_[ch][i] = 0.0f; }
            for (int i = 0; i < 4; ++i) { apPos_[ch][i] = 0; }
            wet_[ch] = 0.0f;
            preLine_[ch].reset();
        }
        decimPhase_ = 0;
        lfoPhase_   = 0.0f;
    }

    void setParameter(ParamId id, float normalized) noexcept {
        if (id.value >= kNumParams) return;
        const float v = denormalize(kParams[id.value], normalized);
        switch (id.value) {
            case kRoomSize:  combFb_    = v * 0.28f + 0.7f;                 break;
            case kDamping:   damp1_     = v * 0.4f; damp2_ = 1.0f - damp1_; break;
            case kMix:       mix_       = v;                                break;
            case kDelayTime: baseDelay_ = v * sampleRate_;                  break; // seconds -> samples
            case kModDepth:  modDepth_  = v;                                break;
            case kModRate:   lfoInc_    = v / sampleRate_;                  break; // Hz -> cycles/sample
            case kFeedback:  feedback_  = v;                                break;
            default: break;
        }
    }

    void process(AudioBlock& io) noexcept {
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples  = io.numSamples();
        float* xL = io.channel(0);
        float* xR = channels > 1 ? io.channel(1) : xL;
        constexpr float kTwoPi = 6.28318530718f;

        for (int n = 0; n < samples; ++n) {
            const float dryL = xL[n];
            const float dryR = xR[n];

            // Front modulated-delay stage (full rate). LFO offset by 1/4 cycle
            // between channels for a wider stereo chorus image.
            const float lfoL = std::sin(kTwoPi * lfoPhase_);
            const float lfoR = std::sin(kTwoPi * (lfoPhase_ + 0.25f));
            lfoPhase_ += lfoInc_;
            if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;

            const float chorL = chorusSample(0, dryL, lfoL);
            const float chorR = channels > 1 ? chorusSample(1, dryR, lfoR) : chorL;

            // Reverb tank at the decimated rate; sample-and-hold the wet output.
            if (decimPhase_ == 0) {
                const float mono = (chorL + chorR) * 0.5f;
                const float fed  = mono * kGain;
                wet_[0] = runTank(0, fed);
                wet_[1] = channels > 1 ? runTank(1, fed) : wet_[0];
            }
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            xL[n] = chorL * (1.0f - mix_) + wet_[0] * mix_;
            if (channels > 1) xR[n] = chorR * (1.0f - mix_) + wet_[1] * mix_;
        }
    }

private:
    // One full-rate modulated-delay sample: read a fractional, LFO-swept tap,
    // write input + feedback*tap, return the chorus blend (dry + half the tap).
    float chorusSample(int ch, float x, float lfo) noexcept {
        auto& line = preLine_[static_cast<std::size_t>(ch)];
        const float d   = baseDelay_ + lfo * modDepth_ * modMaxSamp_;
        const float tap = line.readFractional(d);
        line.write(x + feedback_ * tap);
        return x + 0.5f * tap;
    }

    static constexpr int combLen(int ch, int i) noexcept { return kCombL[i] + (ch ? kSpread : 0); }
    static constexpr int apLen(int ch, int i)   noexcept { return kApL[i]   + (ch ? kSpread : 0); }

    static constexpr int kPoolSize = [] {
        int total = 0;
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 8; ++i) total += kCombL[i] + (ch ? kSpread : 0);
            for (int i = 0; i < 4; ++i) total += kApL[i]   + (ch ? kSpread : 0);
        }
        return total;
    }();

    int combOff(int ch, int i) const noexcept {
        int off = 0;
        for (int c = 0; c < ch; ++c) {
            for (int k = 0; k < 8; ++k) off += combLen(c, k);
            for (int k = 0; k < 4; ++k) off += apLen(c, k);
        }
        for (int k = 0; k < i; ++k) off += combLen(ch, k);
        return off;
    }
    int apOff(int ch, int i) const noexcept {
        int off = 0;
        for (int c = 0; c < ch; ++c) {
            for (int k = 0; k < 8; ++k) off += combLen(c, k);
            for (int k = 0; k < 4; ++k) off += apLen(c, k);
        }
        for (int k = 0; k < 8; ++k) off += combLen(ch, k);
        for (int k = 0; k < i; ++k) off += apLen(ch, k);
        return off;
    }

    float runTank(int ch, float fed) noexcept {
        float acc = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const int off = combOff(ch, i);
            const int len = combLen(ch, i);
            int&      pos = combPos_[ch][i];
            float&    st  = combStore_[ch][i];
            const float y = pool_[static_cast<std::size_t>(off + pos)];
            st = y * damp2_ + st * damp1_;
            pool_[static_cast<std::size_t>(off + pos)] = fed + st * combFb_;
            if (++pos >= len) pos = 0;
            acc += y;
        }
        float x = acc;
        for (int i = 0; i < 4; ++i) {
            const int off = apOff(ch, i);
            const int len = apLen(ch, i);
            int&      pos = apPos_[ch][i];
            const float bufout = pool_[static_cast<std::size_t>(off + pos)];
            const float out    = -x + bufout;
            pool_[static_cast<std::size_t>(off + pos)] = x + bufout * kApFb;
            if (++pos >= len) pos = 0;
            x = out;
        }
        return x;
    }

    static constexpr std::array<ParameterDescriptor, kNumParams> kParams = {{
        {ParamId{kRoomSize}, "room_size", ParamUnit::none, 0.0f, 1.0f, 0.7f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDamping}, "damping", ParamUnit::none, 0.0f, 1.0f, 0.5f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "mix", ParamUnit::none, 0.0f, 1.0f, 0.3f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDelayTime}, "delay_time", ParamUnit::seconds, 0.001f, 0.120f, 0.012f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kModDepth}, "mod_depth", ParamUnit::none, 0.0f, 1.0f, 0.35f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kModRate}, "mod_rate", ParamUnit::hz, 0.05f, 8.0f, 0.6f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kFeedback}, "delay_feedback", ParamUnit::none, 0.0f, 0.85f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
    }};

    // Front modulated-delay stage.
    std::array<BoundedDelayLine<std::int16_t, kPreMax>, 2> preLine_{};
    float baseDelay_  = 576.0f;   // 12 ms @ 48 kHz default
    float modDepth_   = 0.35f;
    float modMaxSamp_ = 240.0f;   // 5 ms @ 48 kHz
    float lfoInc_     = 0.0000125f;
    float lfoPhase_   = 0.0f;
    float feedback_   = 0.0f;

    // Reverb tank.
    std::array<float, kPoolSize> pool_{};
    int   combPos_[2][8]   = {};
    float combStore_[2][8] = {};
    int   apPos_[2][4]     = {};
    float wet_[2]          = {0.0f, 0.0f};
    int   decimPhase_      = 0;

    int   numChannels_ = 2;
    float sampleRate_  = 48000.0f;
    float combFb_ = 0.896f;
    float damp1_  = 0.2f;
    float damp2_  = 0.8f;
    float mix_    = 0.3f;
};

} // namespace acfx

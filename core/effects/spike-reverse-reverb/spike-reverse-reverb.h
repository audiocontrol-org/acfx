#pragma once

// ============================================================================
// SPIKE / THROWAWAY — reverse-reverb feasibility probe, NOT a shipping effect.
// ----------------------------------------------------------------------------
// SWITCHABLE reverse engine — a `mode` param (CC80) A/Bs two reverse schemes
// that share ONE 64 KB capture buffer (so both fit; only one runs at a time):
//
//   BLOCK (mode 0): the classic double-buffer reverse. Capture a window
//     forward into one half, play the other half backward. Discrete "commit a
//     phrase, hear it swell back" gesture; window length (CC79) changes at the
//     swap boundary; ~1 s latency == window.
//
//   GRANULAR (mode 1): overlap-add reverse. Continuous circular capture +
//     overlapping Hann-enveloped backward grains (50% overlap). Window (grain)
//     length changes smoothly in real time; near-zero latency; click-free.
//
// Shared buffer: cap_[2*kWindow] (64 KB). Granular writes circularly across the
// whole thing; block uses cap_[0..kWindow) and cap_[kWindow..2*kWindow) as its
// two halves. Switching mode resets the capture runtime (a brief re-fill).
//
// Both feed the same 16 kHz Freeverb tank -> stereo modulated chorus. Capture
// is 16 kHz (D=3) int16, anti-aliased before decimation. If this probe passes
// it gets replaced by a specced effect — do not build on it.
//
// Platform independence (Constitution IV): standard library only, no heap.
// Single-context setParameter (D26): params applied directly.
// ============================================================================

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "primitives/delays/bounded-delay-line.h"
#include "primitives/lofi/int16-quant.h"

namespace acfx {

class SpikeReverseReverb {
public:
    static constexpr int kInternalDivisor = 3;       // 48k -> 16k capture + tank rate
    static constexpr int kWindow   = 16000;          // 1.0 s @ 16 kHz — max window / block half
    static constexpr int kMinWindow = 1600;          // 0.1 s @ 16 kHz shortest window
    static constexpr int kBufSize  = 2 * kWindow;    // shared: granular circular / block two halves
    static constexpr int kGrains   = 3;              // overlapping grain slots (2 active + margin)

    static constexpr int kComb[8] = {405, 431, 463, 492, 516, 541, 565, 587};
    static constexpr int kAp[4]   = {202, 160, 124, 82};
    static constexpr float kGain  = 0.015f;
    static constexpr float kApFb  = 0.5f;

    static constexpr int   kPreMax   = 2880;   // 60 ms @ 48 kHz wet-side mod delay
    static constexpr float kModMaxMs = 5.0f;

    enum : std::uint8_t {
        kDecay = 0, kDamping = 1, kMix = 2,
        kDelayTime = 3, kModDepth = 4, kModRate = 5, kFeedback = 6,
        kWindowTime = 7, kMode = 8,
    };
    static constexpr std::size_t kNumParams = 9;
    static constexpr std::array<std::string_view, 2> kModeLabels = {{"block", "granular"}};
    enum { kModeBlock = 0, kModeGranular = 1 };

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        modMaxSamp_  = kModMaxMs * 0.001f * sampleRate_;
        const float decimatedRate = sampleRate_ / static_cast<float>(kInternalDivisor);
        designLowpass(aa_, sampleRate_, decimatedRate * 0.5f * 0.85f);
        const int cap = static_cast<int>(0.060f * sampleRate_) + 1;
        for (int ch = 0; ch < 2; ++ch) preLine_[ch].prepare(cap, sampleRate_);
        for (std::size_t i = 0; i < kNumParams; ++i)
            setParameter(ParamId{static_cast<std::uint8_t>(i)},
                         normalize(kParams[i], kParams[i].defaultValue));
        reset();
    }

    void reset() noexcept {
        cap_.fill(0);
        pool_.fill(0.0f);
        for (int i = 0; i < 8; ++i) { combPos_[i] = 0; combStore_[i] = 0.0f; }
        for (int i = 0; i < 4; ++i) { apPos_[i] = 0; }
        for (int ch = 0; ch < 2; ++ch) preLine_[ch].reset();
        aa_.reset();
        resetCapture();
        decimPhase_ = 0; wetPrev_ = 0.0f; wetCur_ = 0.0f; lfoPhase_ = 0.0f;
    }

    void setParameter(ParamId id, float normalized) noexcept {
        if (id.value >= kNumParams) return;
        const float v = denormalize(kParams[id.value], normalized);
        switch (id.value) {
            case kDecay:     combFb_    = v * 0.28f + 0.7f;                 break;
            case kDamping:   damp1_     = v * 0.4f; damp2_ = 1.0f - damp1_; break;
            case kMix:       mix_       = v;                                break;
            case kDelayTime: baseDelay_ = v * sampleRate_;                  break;
            case kModDepth:  modDepth_  = v;                                break;
            case kModRate:   lfoInc_    = v / sampleRate_;                  break;
            case kFeedback:  feedback_  = v;                                break;
            case kWindowTime: {
                // Window/grain length: seconds -> decimated samples. Drives both
                // engines. Granular applies it to the next grain (smooth);
                // block latches it at the next swap.
                int g = static_cast<int>(v * sampleRate_ / static_cast<float>(kInternalDivisor));
                if (g < kMinWindow) g = kMinWindow;
                if (g > kWindow)    g = kWindow;
                grainLen_ = g; pendingWindow_ = g;
                break;
            }
            case kMode: {
                const int m = v >= 0.5f ? kModeGranular : kModeBlock;
                if (m != mode_) { mode_ = m; resetCapture(); }  // clean re-fill on switch
                break;
            }
            default: break;
        }
    }

    void process(AudioBlock& io) noexcept {
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples  = io.numSamples();
        float* xL = io.channel(0);
        float* xR = channels > 1 ? io.channel(1) : xL;
        const float invD = 1.0f / static_cast<float>(kInternalDivisor);
        constexpr float kTwoPi = 6.28318530718f;

        for (int n = 0; n < samples; ++n) {
            const float dryL = xL[n];
            const float dryR = xR[n];

            const float monoAA = aa_.process((dryL + dryR) * 0.5f);

            if (decimPhase_ == 0) {
                const float rev = (mode_ == kModeGranular) ? granularTick(monoAA)
                                                           : blockTick(monoAA);
                wetPrev_ = wetCur_;
                wetCur_  = runTank(rev * kGain);
            }

            const float frac = static_cast<float>(decimPhase_) * invD;
            const float wet  = wetPrev_ + (wetCur_ - wetPrev_) * frac;
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            const float lfoL = std::sin(kTwoPi * lfoPhase_);
            const float lfoR = std::sin(kTwoPi * (lfoPhase_ + 0.25f));
            lfoPhase_ += lfoInc_;
            if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
            const float wetL = chorusSample(0, wet, lfoL);
            const float wetR = channels > 1 ? chorusSample(1, wet, lfoR) : wetL;

            xL[n] = dryL * (1.0f - mix_) + wetL * mix_;
            if (channels > 1) xR[n] = dryR * (1.0f - mix_) + wetR * mix_;
        }
    }

private:
    void resetCapture() noexcept {
        // Block state.
        sel_ = 0; writeIdx_ = 0; playIdx_ = 0; playActive_ = false;
        fillCount_[0] = 0; fillCount_[1] = 0; windowLen_ = pendingWindow_;
        // Granular state.
        for (int k = 0; k < kGrains; ++k) { grActive_[k] = 0; grI_[k] = 0; grLen_[k] = grainLen_; grStart_[k] = 0; }
        w_ = 0; ticksSinceLaunch_ = 0; hop_ = grainLen_ / 2;
    }

    // --- BLOCK reverse: two halves of the shared buffer (offset 0 and kWindow) ---
    static constexpr int bufOffset(int s) noexcept { return s * kWindow; }
    float blockTick(float monoAA) noexcept {
        cap_[static_cast<std::size_t>(bufOffset(sel_) + writeIdx_)] = quantizeInt16(monoAA);
        ++writeIdx_;
        float rev = 0.0f;
        if (playActive_ && playIdx_ >= 0) {
            rev = dequantizeInt16(cap_[static_cast<std::size_t>(bufOffset(sel_ ^ 1) + playIdx_)]);
            --playIdx_;
        }
        if (writeIdx_ >= windowLen_) {
            fillCount_[sel_] = writeIdx_;
            sel_ ^= 1;
            writeIdx_  = 0;
            playIdx_   = fillCount_[sel_ ^ 1] - 1;
            playActive_ = true;
            windowLen_ = pendingWindow_;
        }
        return rev;
    }

    // --- GRANULAR reverse: circular capture + overlapping backward grains ---
    float granularTick(float monoAA) noexcept {
        cap_[static_cast<std::size_t>(w_)] = quantizeInt16(monoAA);
        if (ticksSinceLaunch_ >= hop_) {
            for (int k = 0; k < kGrains; ++k) {
                if (!grActive_[k]) {
                    grActive_[k] = 1; grI_[k] = 0;
                    grLen_[k] = grainLen_; grStart_[k] = w_;
                    break;
                }
            }
            hop_ = grainLen_ / 2; if (hop_ < 1) hop_ = 1;
            ticksSinceLaunch_ = 0;
        }
        ++ticksSinceLaunch_;
        w_ = (w_ + 1) % kBufSize;

        constexpr float kTwoPi = 6.28318530718f;
        float rev = 0.0f;
        for (int k = 0; k < kGrains; ++k) {
            if (!grActive_[k]) continue;
            const int L = grLen_[k];
            const int i = grI_[k];
            int idx = (grStart_[k] - i) % kBufSize;
            if (idx < 0) idx += kBufSize;
            const float env = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(i)
                                                      / static_cast<float>(L - 1)));
            rev += dequantizeInt16(cap_[static_cast<std::size_t>(idx)]) * env;
            if (++grI_[k] >= L) grActive_[k] = 0;
        }
        return rev;
    }

    float chorusSample(int ch, float x, float lfo) noexcept {
        auto& line = preLine_[static_cast<std::size_t>(ch)];
        const float d   = baseDelay_ + lfo * modDepth_ * modMaxSamp_;
        const float tap = line.readFractional(d);
        line.write(x + feedback_ * tap);
        return x + 0.5f * tap;
    }

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
        const float alpha = sinw / (2.0f * 0.70710678f);
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
        {ParamId{kDelayTime}, "delay_time", ParamUnit::seconds, 0.001f, 0.060f, 0.014f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kModDepth}, "mod_depth", ParamUnit::none, 0.0f, 1.0f, 0.45f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kModRate}, "mod_rate", ParamUnit::hz, 0.05f, 8.0f, 0.5f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kFeedback}, "delay_feedback", ParamUnit::none, 0.0f, 0.85f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kWindowTime}, "window_time", ParamUnit::seconds, 0.1f, 1.0f, 1.0f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kMode}, "mode", ParamUnit::none, 0.0f, 1.0f, 1.0f,
         ParamSkew::linear, ParamKind::discrete, 2, kModeLabels},
    }};

    // Shared capture buffer (granular circular / block two halves).
    std::array<std::int16_t, kBufSize> cap_{};
    int mode_ = kModeGranular;

    // Block reverse state.
    int  sel_        = 0;
    int  writeIdx_   = 0;
    int  playIdx_    = 0;
    bool playActive_ = false;
    int  fillCount_[2]  = {0, 0};
    int  windowLen_     = kWindow;
    int  pendingWindow_ = kWindow;

    // Granular reverse state.
    int grActive_[kGrains] = {};
    int grI_[kGrains]      = {};
    int grLen_[kGrains]    = {};
    int grStart_[kGrains]  = {};
    int grainLen_          = kWindow;
    int ticksSinceLaunch_  = 0;
    int hop_               = kWindow / 2;
    int w_                 = 0;

    Biquad aa_{};

    // Wet-side modulated chorus stage.
    std::array<BoundedDelayLine<std::int16_t, kPreMax>, 2> preLine_{};
    float baseDelay_  = 672.0f;
    float modDepth_   = 0.45f;
    float modMaxSamp_ = 240.0f;
    float lfoInc_     = 0.0000104f;
    float lfoPhase_   = 0.0f;
    float feedback_   = 0.0f;

    // Mono 16 kHz Freeverb tank.
    std::array<float, kPoolSize> pool_{};
    int   combPos_[8]   = {};
    float combStore_[8] = {};
    int   apPos_[4]     = {};

    int   decimPhase_ = 0;
    float wetPrev_    = 0.0f;
    float wetCur_     = 0.0f;

    int   numChannels_ = 2;
    float sampleRate_  = 48000.0f;
    float combFb_ = 0.924f;
    float damp1_  = 0.16f;
    float damp2_  = 0.84f;
    float mix_    = 0.7f;
};

} // namespace acfx

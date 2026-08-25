#pragma once

// ============================================================================
// SPIKE / THROWAWAY — feasibility probe, NOT a shipping effect.
// ----------------------------------------------------------------------------
// Answers "does a reverb fit in the F446's SRAM and run, using the same
// internal-rate decimation the lo-fi delay uses?" It is a Freeverb-style tank
// (8 comb + 4 allpass per channel, Jezar's public-domain topology) run at a
// DECIMATED internal rate (D=2 -> 24 kHz) in float. Decimation buys back
// exactly the memory float costs (~55 KB stereo) and the ~12 kHz internal
// Nyquist is inaudible in a reverb tail. If this probe passes it gets replaced
// by a properly-specced effect (own params file, thread-safe setParameter,
// tests) — do not build on it.
//
// Platform independence (Constitution IV): standard library only. No heap: all
// buffers are in-object std::array. Single-context setParameter (D26, nucleo
// poll loop) so params are applied directly, not double-buffered.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"

namespace acfx {

class SpikeReverb {
public:
    // --- Freeverb tunings scaled from 44.1 kHz to the 24 kHz internal rate ---
    // (round(len * 24000/44100)). Right channel adds the stereo spread (13).
    static constexpr int kInternalDivisor = 2;              // 48k -> 24k tank
    static constexpr int kSpread          = 13;
    static constexpr int kCombL[8] = {607, 646, 695, 738, 774, 811, 847, 880};
    static constexpr int kApL[4]   = {303, 240, 186, 122};

    // Freeverb fixed constants.
    static constexpr float kGain   = 0.015f;   // input scaling into the combs
    static constexpr float kApFb   = 0.5f;     // allpass feedback

    // Parameter indices (dense). Mirrors the delay's low CC map:
    // CC74->0 (roomsize), CC71->1 (damping), CC72->2 (mix).
    enum : std::uint8_t { kRoomSize = 0, kDamping = 1, kMix = 2 };
    static constexpr std::size_t kNumParams = 3;

    static constexpr span<const ParameterDescriptor> parameters() noexcept {
        return kParams;
    }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        // Seed with defaults so an un-driven device already reverberates.
        setParameter(ParamId{kRoomSize}, normalize(kParams[kRoomSize], kParams[kRoomSize].defaultValue));
        setParameter(ParamId{kDamping},  normalize(kParams[kDamping],  kParams[kDamping].defaultValue));
        setParameter(ParamId{kMix},      normalize(kParams[kMix],      kParams[kMix].defaultValue));
        reset();
    }

    void reset() noexcept {
        pool_.fill(0.0f);
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 8; ++i) { combPos_[ch][i] = 0; combStore_[ch][i] = 0.0f; }
            for (int i = 0; i < 4; ++i) { apPos_[ch][i] = 0; }
            wet_[ch] = 0.0f;
        }
        decimPhase_ = 0;
    }

    // Single execution context (D26): apply immediately, no double-buffer.
    void setParameter(ParamId id, float normalized) noexcept {
        if (id.value >= kNumParams) return;
        const float v = denormalize(kParams[id.value], normalized);
        switch (id.value) {
            case kRoomSize: combFb_ = v * 0.28f + 0.7f;                 break; // 0.7..0.98
            case kDamping:  damp1_  = v * 0.4f; damp2_ = 1.0f - damp1_; break;
            case kMix:      mix_    = v;                                break;
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

            // Run the tank once every D samples; sample-and-hold the wet output
            // (the decimation — same shape as the lo-fi delay's global tick).
            if (decimPhase_ == 0) {
                const float mono = (dryL + dryR) * 0.5f;
                const float fed  = mono * kGain;
                wet_[0] = runTank(0, fed);
                wet_[1] = channels > 1 ? runTank(1, fed) : wet_[0];
            }
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            xL[n] = dryL * (1.0f - mix_) + wet_[0] * mix_;
            if (channels > 1) xR[n] = dryR * (1.0f - mix_) + wet_[1] * mix_;
        }
    }

private:
    // Per-channel comb length (right channel offset by the stereo spread).
    static constexpr int combLen(int ch, int i) noexcept { return kCombL[i] + (ch ? kSpread : 0); }
    static constexpr int apLen(int ch, int i)   noexcept { return kApL[i]   + (ch ? kSpread : 0); }

    // Compile-time flat-pool offsets: [ch][combs 0..7][allpass 0..3].
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
        // Parallel comb bank.
        float acc = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const int off = combOff(ch, i);
            const int len = combLen(ch, i);
            int&      pos = combPos_[ch][i];
            float&    st  = combStore_[ch][i];
            const float y = pool_[static_cast<std::size_t>(off + pos)];
            st = y * damp2_ + st * damp1_;                       // one-pole LP in feedback
            pool_[static_cast<std::size_t>(off + pos)] = fed + st * combFb_;
            if (++pos >= len) pos = 0;
            acc += y;
        }
        // Series allpass diffusers.
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

    // --- parameter table (throwaway inline) ---
    static constexpr std::array<ParameterDescriptor, kNumParams> kParams = {{
        {ParamId{kRoomSize}, "room_size", ParamUnit::none, 0.0f, 1.0f, 0.7f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDamping}, "damping", ParamUnit::none, 0.0f, 1.0f, 0.5f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "mix", ParamUnit::none, 0.0f, 1.0f, 0.3f,
         ParamSkew::linear, ParamKind::continuous, 0},
    }};

    // --- state ---
    std::array<float, kPoolSize> pool_{};
    int   combPos_[2][8]   = {};
    float combStore_[2][8] = {};
    int   apPos_[2][4]     = {};
    float wet_[2]          = {0.0f, 0.0f};
    int   decimPhase_      = 0;

    int   numChannels_ = 2;
    float combFb_ = 0.896f;   // roomsize 0.7 default -> 0.7*0.28+0.7
    float damp1_  = 0.2f;     // damping 0.5 default -> 0.5*0.4
    float damp2_  = 0.8f;
    float mix_    = 0.3f;
};

} // namespace acfx

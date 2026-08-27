#pragma once

// ============================================================================
// SPIKE / THROWAWAY — "Breathing Canyon", NOT a shipping effect.
// ----------------------------------------------------------------------------
// Modelled on the Eventide H3000 Algorithm 106 "SWEPT REVERB" (the algorithm
// behind the "Breathing Canyon" preset), plus a pitch-shifted shimmer feedback
// loop added on top per the operator's request. From the H3000 manual:
//
//   Swept Reverb = a bank of delay lines with feedback into a reverb network,
//   where EACH delay line's time is continuously modulated by its own sweep
//   generator using a RANDOM depth ("creates a thicker texture than
//   conventional sweep generators"). Master controls: delay size, sweep rate,
//   sweep depth, feedback (recirculation/decay), mix. The moving/swooshing
//   "breathing" IS the randomly-swept delay lines; a slow LFO adds the swell.
//
// Our take (16 kHz-class, mono, heap-free): pre-delay -> input diffusion ->
// 4-line modulated FDN cathedral (per-line smoothed-random sweep on fractional
// reads = the breathing) -> reverb output. A GLOBAL feedback loop (HPF ->
// damping LPF -> pitch shifter -> soft-clip -> gain) recirculates the output
// for the shimmer/endless quality. A slow breath LFO modulates decay. Runs at
// 8 kHz internal (D=6) so the canyon is long.
//
// Stability: two feedback structures. The FDN Hadamard mix is orthonormal so
// its loop gain == the decay coefficient (<1). The global shimmer loop gain is
// a product (damping x pitch x soft-clip x shimmer); the in-loop soft-clip
// bounds it. Levels/decay set by offline measurement, not guessed.
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

class SpikeBreathingCanyon {
public:
    static constexpr int kInternalDivisor = 6;      // 48k -> 8 kHz internal
    static constexpr int kPreMax   = 4200;          // ~0.52 s pre-delay @ 8 kHz (int16)
    static constexpr int kLineCap  = 2600;          // FDN line capacity (base + sweep margin)
    static constexpr int kPitchBuf = 2048;
    static constexpr int kPitchWin = 1024;
    // Base delay-line lengths @ 8 kHz (mutually spread; ~130-280 ms).
    static constexpr int kBase[4]  = {1039, 1361, 1693, 2237};
    static constexpr float kSweepMaxSamp = 60.0f;   // max fractional sweep excursion

    enum : std::uint8_t {
        kPredelay = 0, kSize = 1, kDecay = 2, kDamping = 3,
        kSweepRate = 4, kSweepDepth = 5, kPitch = 6, kShimmer = 7,
        kBreath = 8, kMix = 9,
    };
    static constexpr std::size_t kNumParams = 10;

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        const float internalRate = sampleRate_ / static_cast<float>(kInternalDivisor);
        preLine_.prepare(kPreMax, internalRate);
        for (int i = 0; i < 4; ++i) lines_[i].prepare(kLineCap, internalRate);
        for (int i = 0; i < 2; ++i) diff_[i].prepare(i == 0 ? 283 : 421, internalRate);
        designAntiAlias();
        for (std::size_t i = 0; i < kNumParams; ++i)
            setParameter(ParamId{static_cast<std::uint8_t>(i)},
                         normalize(kParams[i], kParams[i].defaultValue));
        reset();
    }

    void reset() noexcept {
        preLine_.reset();
        for (int i = 0; i < 4; ++i) { lines_[i].reset(); fdnDamp_[i] = 0.0f;
                                      sweepVal_[i] = 0.0f; sweepTarget_[i] = 0.0f; sweepCnt_[i] = i * 97; }
        for (int i = 0; i < 2; ++i) diff_[i].reset();
        pitchBuf_.fill(0);
        pitchW_ = 0; pitchO0_ = 0.0f; pitchO1_ = static_cast<float>(kPitchWin) * 0.5f;
        aa_.reset(); hpf_.reset();
        fbSample_ = 0.0f; decimPhase_ = 0; wetPrev_ = 0.0f; wetCur_ = 0.0f; loopDamp_ = 0.0f;
        breathPhase_ = 0.0f; rng_ = 0x2545f491u;
    }

    void setParameter(ParamId id, float normalized) noexcept {
        if (id.value >= kNumParams) return;
        const float v = denormalize(kParams[id.value], normalized);
        const float internalRate = sampleRate_ / static_cast<float>(kInternalDivisor);
        switch (id.value) {
            case kPredelay:  predelaySamp_ = v * internalRate;                 break; // seconds
            case kSize:      sizeScale_    = 0.35f + v * 0.65f;                 break; // 0.35..1.0 of base
            case kDecay:     decay_        = v * 0.35f + 0.6f;                  break; // 0.6..0.95 recirc
            case kDamping: {
                dampCut_ = 6000.0f * std::pow(0.03f, v);                        // 6k..~180 Hz
                const float a = std::exp(-2.0f * 3.14159265f * dampCut_ / internalRate);
                dampA1_ = a; dampA2_ = 1.0f - a;                               // one-pole LP coeffs
                break;
            }
            case kSweepRate: sweepInc_     = (0.05f + v * v * 5.0f);           break; // Hz-ish (per-line jitter)
            case kSweepDepth:sweepDepth_   = v;                                break;
            case kPitch:     pitchRate_    = std::pow(2.0f, v / 12.0f);         break; // semitones
            case kShimmer:   shimmer_      = v * 0.9f;                          break; // 0..0.9 loop gain
            case kBreath:    breathDepth_  = v;                                break;
            case kMix:       mix_          = v;                                break;
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
                // pre-delay: input arrives at the canyon `predelaySamp_` late.
                const float pd = preLine_.readFractional(predelaySamp_);
                preLine_.write(monoAA);

                float in = pd + fbSample_;             // inject shimmer feedback
                in = diff_[0].process(in, 0.7f);       // input diffusion
                in = diff_[1].process(in, 0.6f);

                // ---- modulated 4-line FDN (the swept cathedral) ----
                const float breath = 0.5f + 0.5f * std::sin(kTwoPi * breathPhase_);
                breathPhase_ += 0.08f / 8000.0f;       // ~0.08 Hz breath (per 8 kHz tick)
                if (breathPhase_ >= 1.0f) breathPhase_ -= 1.0f;
                const float decayEff = decay_ * (1.0f - breathDepth_ * 0.3f * breath);

                float d[4];
                for (int i = 0; i < 4; ++i) {
                    advanceSweep(i);
                    const float baseLen = static_cast<float>(kBase[i]) * sizeScale_;
                    float rd = baseLen - sweepDepth_ * kSweepMaxSamp * sweepVal_[i];
                    if (rd < 1.0f) rd = 1.0f;
                    float v = lines_[i].readFractional(rd);
                    fdnDamp_[i] = v * dampA2_ + fdnDamp_[i] * dampA1_;   // per-line damping LP
                    d[i] = fdnDamp_[i];
                }
                const float f0 = (d[0] + d[1] + d[2] + d[3]) * 0.5f;    // orthonormal Hadamard
                const float f1 = (d[0] - d[1] + d[2] - d[3]) * 0.5f;
                const float f2 = (d[0] + d[1] - d[2] - d[3]) * 0.5f;
                const float f3 = (d[0] - d[1] - d[2] + d[3]) * 0.5f;
                const float fv[4] = {f0, f1, f2, f3};
                for (int i = 0; i < 4; ++i) lines_[i].write(in + decayEff * fv[i]);
                const float wet = (d[0] + d[1] + d[2] + d[3]) * 0.5f;

                // ---- global shimmer feedback loop ----
                float fb = hpf_.process(wet);          // DC/rumble block (down-shift safety)
                loopDamp_ = fb * dampA2_ + loopDamp_ * dampA1_;   // damping tone in the loop
                fb = loopDamp_;
                fb = pitchShift(fb);                   // pitch the recirculated signal
                fb = softClip(fb);                     // bounds the loop -> no runaway
                fbSample_ = fb * shimmer_;

                wetPrev_ = wetCur_;
                wetCur_  = wet;
            }
            const float frac = static_cast<float>(decimPhase_) * invD;
            const float wetOut = wetPrev_ + (wetCur_ - wetPrev_) * frac;
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            xL[n] = dryL * (1.0f - mix_) + wetOut * mix_;
            if (channels > 1) xR[n] = dryR * (1.0f - mix_) + wetOut * mix_;
        }
    }

private:
    // smoothed-random per-line sweep (H3000's "random depth" sweep generator).
    void advanceSweep(int i) noexcept {
        if (--sweepCnt_[i] <= 0) {
            rng_ = rng_ * 1664525u + 1013904223u;
            sweepTarget_[i] = (static_cast<float>((rng_ >> 9) & 0xFFFFu) / 32768.0f - 1.0f);
            const int period = static_cast<int>(2000.0f / (sweepInc_ + 0.05f)) + 8;
            sweepCnt_[i] = period + (static_cast<int>(rng_ >> 3) & 63);
        }
        sweepVal_[i] += 0.01f * (sweepTarget_[i] - sweepVal_[i]);   // one-pole smoothing
    }

    static float softClip(float x) noexcept {
        if (x < -1.2f) return -0.8f;
        if (x >  1.2f) return  0.8f;
        return x - (x * x * x) * (1.0f / 4.32f);   // cubic soft clip
    }

    // two-tap crossfade pitch shifter over pitchBuf_ (int16).
    float pitchShift(float x) noexcept {
        constexpr float kTwoPi = 6.28318530718f;
        pitchBuf_[static_cast<std::size_t>(pitchW_)] = quantizeInt16(x);
        auto rd = [&](float o) {
            float p = static_cast<float>(pitchW_) - o;
            while (p < 0.0f) p += kPitchBuf;
            const int i0 = static_cast<int>(p); const float fr = p - static_cast<float>(i0);
            int i1 = i0 + 1; if (i1 >= kPitchBuf) i1 = 0;
            return dequantizeInt16(pitchBuf_[static_cast<std::size_t>(i0)]) * (1.0f - fr)
                 + dequantizeInt16(pitchBuf_[static_cast<std::size_t>(i1)]) * fr;
        };
        const float t0 = rd(pitchO0_), t1 = rd(pitchO1_);
        const float e0 = 0.5f * (1.0f - std::cos(kTwoPi * pitchO0_ / kPitchWin));
        const float e1 = 0.5f * (1.0f - std::cos(kTwoPi * pitchO1_ / kPitchWin));
        pitchW_ = (pitchW_ + 1) % kPitchBuf;
        const float step = 1.0f - pitchRate_;
        pitchO0_ += step; if (pitchO0_ < 0) pitchO0_ += kPitchWin; else if (pitchO0_ >= kPitchWin) pitchO0_ -= kPitchWin;
        pitchO1_ += step; if (pitchO1_ < 0) pitchO1_ += kPitchWin; else if (pitchO1_ >= kPitchWin) pitchO1_ -= kPitchWin;
        return t0 * e0 + t1 * e1;
    }

    // A small allpass diffuser over its own int16 delay line.
    struct Diffuser {
        BoundedDelayLine<std::int16_t, 512> line;
        void prepare(int cap, float sr) noexcept { line.prepare(cap, sr); len_ = cap; }
        void reset() noexcept { line.reset(); }
        float process(float x, float g) noexcept {
            const float d = line.readFractional(static_cast<float>(len_ - 1));
            const float v = x - g * d;
            line.write(v);
            return g * v + d;
        }
        int len_ = 1;
    };

    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        float process(float x) noexcept {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
        }
    };
    void designAntiAlias() noexcept {
        // anti-alias LP before the D=6 decimation (~0.85 * 4 kHz), plus fixed loop HPF ~80 Hz.
        const float rate = sampleRate_ / static_cast<float>(kInternalDivisor);
        lowpass(aa_, sampleRate_, rate * 0.5f * 0.85f);
        highpass(hpf_, rate, 80.0f);
        // loop damping LPF coefficients are the one-pole below, not this biquad.
    }
    static void lowpass(Biquad& bq, float fs, float fc) noexcept {
        const float w0 = 2.0f * 3.14159265f * fc / fs, c = std::cos(w0), s = std::sin(w0);
        const float al = s / (2.0f * 0.70710678f), a0 = 1 + al;
        bq.b0 = (1 - c) * 0.5f / a0; bq.b1 = (1 - c) / a0; bq.b2 = (1 - c) * 0.5f / a0;
        bq.a1 = -2 * c / a0; bq.a2 = (1 - al) / a0;
    }
    static void highpass(Biquad& bq, float fs, float fc) noexcept {
        const float w0 = 2.0f * 3.14159265f * fc / fs, c = std::cos(w0), s = std::sin(w0);
        const float al = s / (2.0f * 0.70710678f), a0 = 1 + al;
        bq.b0 = (1 + c) * 0.5f / a0; bq.b1 = -(1 + c) / a0; bq.b2 = (1 + c) * 0.5f / a0;
        bq.a1 = -2 * c / a0; bq.a2 = (1 - al) / a0;
    }

    static constexpr std::array<ParameterDescriptor, kNumParams> kParams = {{
        {ParamId{kPredelay}, "Canyon/Predelay", ParamUnit::seconds, 0.0f, 0.5f, 0.08f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kSize}, "Canyon/Size", ParamUnit::none, 0.0f, 1.0f, 0.7f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDecay}, "Canyon/Decay", ParamUnit::none, 0.0f, 1.0f, 0.8f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDamping}, "Canyon/Damping", ParamUnit::none, 0.0f, 1.0f, 0.5f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kSweepRate}, "Sweep/Rate", ParamUnit::none, 0.0f, 1.0f, 0.35f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kSweepDepth}, "Sweep/Depth", ParamUnit::none, 0.0f, 1.0f, 0.45f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kPitch}, "Shimmer/Pitch", ParamUnit::none, -12.0f, 12.0f, 12.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kShimmer}, "Shimmer/Amount", ParamUnit::none, 0.0f, 1.0f, 0.3f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kBreath}, "Breath/Depth", ParamUnit::none, 0.0f, 1.0f, 0.35f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "Output/Mix", ParamUnit::none, 0.0f, 1.0f, 1.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
    }};

    // --- state ---
    BoundedDelayLine<std::int16_t, kPreMax> preLine_;             // pre-delay
    BoundedDelayLine<float, kLineCap>       lines_[4];            // modulated FDN
    float fdnDamp_[4]   = {};
    float sweepVal_[4]  = {};
    float sweepTarget_[4] = {};
    int   sweepCnt_[4]  = {};
    Diffuser diff_[2];                                           // input diffusion
    std::array<std::int16_t, kPitchBuf> pitchBuf_{};             // shimmer pitch shifter
    int   pitchW_ = 0;
    float pitchO0_ = 0.0f, pitchO1_ = static_cast<float>(kPitchWin) * 0.5f;
    float pitchRate_ = 2.0f;
    Biquad aa_{}, hpf_{};                                        // anti-alias + loop HPF
    float dampA1_ = 0.2f, dampA2_ = 0.8f, loopDamp_ = 0.0f;      // loop/FDN one-pole damping

    float fbSample_ = 0.0f;
    int   decimPhase_ = 0;
    float wetPrev_ = 0.0f, wetCur_ = 0.0f;
    float breathPhase_ = 0.0f;
    std::uint32_t rng_ = 0x2545f491u;

    int   numChannels_ = 2;
    float sampleRate_  = 48000.0f;
    float predelaySamp_ = 640.0f;
    float sizeScale_ = 0.8f, decay_ = 0.8f, dampCut_ = 3000.0f;
    float sweepInc_ = 0.6f, sweepDepth_ = 0.45f;
    float shimmer_ = 0.3f, breathDepth_ = 0.35f, mix_ = 0.35f;
};

} // namespace acfx

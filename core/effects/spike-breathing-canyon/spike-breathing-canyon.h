#pragma once

// ============================================================================
// SPIKE / THROWAWAY — "Breathing Canyon", NOT a shipping effect.
// ----------------------------------------------------------------------------
// Modelled on the Eventide H3000 Algorithm 106 "SWEPT REVERB" (the engine
// behind the Breathing Canyon preset), exposed with MAXIMUM tweakability for
// the spike phase, plus a pitch-shifted shimmer feedback loop.
//
// Six delay lines feed a recirculating (Householder) reverb network; each line
// has its own delay time, sweep rate, sweep depth (smoothed-RANDOM modulation =
// the "breathing"), pan and level. Master m-Delay/Rate/Depth scale the per-line
// values; Fdback sets recirculation/decay. Glide smooths delay-time changes
// (and with modulation gives doppler pitch); Freeze holds the tank (Repeat). A
// global pitch-shifted feedback loop (HPF -> damping -> pitch -> soft-clip ->
// gain) adds the shimmer; a slow Breath LFO swells the decay. 8 kHz internal.
//
// Stability: the Householder mix is orthonormal so the tank loop gain == Fdback
// (<1); the shimmer loop is bounded by the in-loop soft-clip. Verified STABLE
// offline (tools/measure-breathing-canyon.cpp), not guessed.
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

namespace bc_detail {
inline constexpr ParameterDescriptor cont(std::uint8_t id, std::string_view name,
                                          float lo, float hi, float def) {
    return {ParamId{id}, name, ParamUnit::none, lo, hi, def,
            ParamSkew::linear, ParamKind::continuous, 0};
}
} // namespace bc_detail

class SpikeBreathingCanyon {
public:
    static constexpr int kLines    = 6;
    static constexpr int kInternalDivisor = 4;      // 48k -> 12 kHz internal
    static constexpr int kPreMax   = 4200;          // ~0.52 s pre-delay @ 8 kHz
    static constexpr int kLineCap  = 2600;          // per-line capacity (~217 ms @ 12 kHz)
    static constexpr int kPitchBuf = 2048;
    static constexpr int kPitchWin = 1024;
    static constexpr float kSweepMaxSamp = 90.0f;
    static constexpr int kChorusMax = 1800;   // full-rate (48 kHz) wet-chorus line: up to ~30 ms + mod
    static constexpr float kChorusModMs = 5.0f;
    // Base per-line delay lengths @ 8 kHz (the Delays 1-6 defaults, ~90-260 ms).
    static constexpr int kBase[kLines] = {727, 1013, 1279, 1523, 1789, 2069};

    enum : std::uint8_t {
        kSize = 0, kMRate = 1, kMDepth = 2, kFeedback = 3, kMix = 4,
        kPredelay = 5, kDamping = 6, kGlide = 7, kFreeze = 8,
        kPitch = 9, kShimmer = 10, kBreath = 11,
        kDelay1 = 12, kRate1 = 18, kDepth1 = 24, kPan1 = 30, kLevel1 = 36,
        kAlgo = 42, kShimLfoRate = 43, kShimLfoDepth = 44,
        kChorusTime = 45, kChorusDepth = 46, kChorusRate = 47, kChorusFeedback = 48,
        kNumParams = 49,
    };
    static constexpr std::array<std::string_view, 2> kOffOn = {{"off", "on"}};
    // Selectable FDN recirculation topologies (all orthonormal -> loop gain == Feedback).
    static constexpr std::array<std::string_view, 5> kAlgoNames =
        {{"Cathedral", "Chamber", "Swirl", "Plate", "Cascade"}};

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        internalRate_ = sampleRate_ / static_cast<float>(kInternalDivisor);
        const float rate = internalRate_;
        preLine_.prepare(kPreMax, rate);
        for (int i = 0; i < kLines; ++i) lines_[i].prepare(kLineCap, rate);
        { constexpr int c[kInDiff] = {113, 167, 251, 347};
          for (int i = 0; i < kInDiff; ++i) diff_[i].prepare(c[i], rate); }
        { constexpr int c[kLines] = {131, 193, 239, 281, 337, 397};
          for (int i = 0; i < kLines; ++i) lineAP_[i].prepare(c[i], rate); }
        // Wet-side chorus runs at the FULL output rate (not the 12 kHz tank rate).
        for (int ch = 0; ch < 2; ++ch) chorusLine_[ch].prepare(kChorusMax, sampleRate_);
        chorusModMax_ = kChorusModMs * 0.001f * sampleRate_;
        designFilters();
        for (std::size_t i = 0; i < kNumParams; ++i)
            setParameter(ParamId{static_cast<std::uint8_t>(i)},
                         normalize(kParams[i], kParams[i].defaultValue));
        reset();
    }

    void reset() noexcept {
        preLine_.reset();
        for (int i = 0; i < kLines; ++i) {
            lines_[i].reset(); fdnDamp_[i] = 0.0f; sweepVal_[i] = 0.0f; sweepTarget_[i] = 0.0f;
            sweepCnt_[i] = i * 89 + 7; glideLen_[i] = lineDelay_[i] > 1.0f ? lineDelay_[i] : static_cast<float>(kBase[i]);
        }
        for (int i = 0; i < kInDiff; ++i) diff_[i].reset();
        for (int i = 0; i < kLines; ++i) lineAP_[i].reset();
        pitchBuf_.fill(0); pitchW_ = 0; pitchO0_ = 0.0f; pitchO1_ = static_cast<float>(kPitchWin) * 0.5f;
        aa_.reset(); hpf_.reset();
        fbSample_ = 0.0f; decimPhase_ = 0; wetPrevL_ = wetCurL_ = wetPrevR_ = wetCurR_ = 0.0f;
        loopDamp_ = 0.0f; breathPhase_ = 0.0f; shimLfoPhase_ = 0.0f; rng_ = 0x2545f491u;
        for (int ch = 0; ch < 2; ++ch) chorusLine_[ch].reset();
        chorusLfoPhase_ = 0.0f;
    }

    void setParameter(ParamId id, float normalized) noexcept {
        if (id.value >= kNumParams) return;
        const float v = denormalize(kParams[id.value], normalized);
        const float rate = sampleRate_ / static_cast<float>(kInternalDivisor);
        const int idv = id.value;
        if (idv >= kDelay1 && idv < kDelay1 + kLines)   { lineDelay_[idv - kDelay1] = v * rate; return; } // sec
        if (idv >= kRate1  && idv < kRate1  + kLines)   { lineRate_[idv - kRate1]   = v;         return; }
        if (idv >= kDepth1 && idv < kDepth1 + kLines)   { lineDepth_[idv - kDepth1] = v;         return; }
        if (idv >= kPan1   && idv < kPan1   + kLines)   { linePan_[idv - kPan1]     = v;         return; } // -1..1
        if (idv >= kLevel1 && idv < kLevel1 + kLines)   { lineLevel_[idv - kLevel1] = v;         return; }
        switch (idv) {
            case kSize:     sizeScale_ = 0.35f + v * 0.9f;                            break;
            case kMRate:    mRate_     = v;                                           break;
            case kMDepth:   mDepth_    = v;                                           break;
            case kFeedback: feedback_  = v * 0.42f + 0.55f;                           break; // 0.55..0.97
            case kMix:      mix_       = v;                                           break;
            case kPredelay: predelaySamp_ = v * rate;                                 break;
            case kDamping: { dampCut_ = 6000.0f * std::pow(0.03f, v);
                             const float a = std::exp(-2.0f * 3.14159265f * dampCut_ / rate);
                             dampA1_ = a; dampA2_ = 1.0f - a;                          break; }
            case kGlide:    glideCoef_ = 1.0f - std::pow(0.5f, 0.02f + v * 0.5f);     break; // 0=fast..slow
            case kFreeze:   freeze_    = v >= 0.5f;                                   break;
            case kPitch:    pitchRate_ = std::pow(2.0f, v / 12.0f);                   break;
            case kShimmer:  shimmer_   = v * 0.9f;                                    break;
            case kBreath:   breathDepth_ = v;                                         break;
            case kAlgo: { int a = static_cast<int>(v + 0.5f); algo_ = a < 0 ? 0 : (a > 4 ? 4 : a); break; }
            case kShimLfoRate: { const float hz = 0.02f * std::pow(100.0f, v); // 0.02..2 Hz, log
                                 shimLfoInc_ = hz / internalRate_;               break; }
            case kShimLfoDepth: shimLfoDepth_ = v;                               break; // semitones (0..12)
            case kChorusTime:     chorusBase_ = v * sampleRate_;                 break; // seconds -> samples @ full rate
            case kChorusDepth:    chorusDepth_ = v;                              break;
            case kChorusRate:     chorusLfoInc_ = v / sampleRate_;               break; // Hz -> phase/sample
            case kChorusFeedback: chorusFb_ = v;                                 break;
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
            const float dryL = xL[n], dryR = xR[n];
            const float monoAA = aa_.process((dryL + dryR) * 0.5f);

            if (decimPhase_ == 0) {
                const float pd = preLine_.readFractional(predelaySamp_);
                preLine_.write(monoAA);
                float in = (freeze_ ? 0.0f : pd) + fbSample_;   // Freeze holds the tank (no new input)
                in = diff_[0].process(in, 0.75f);
                in = diff_[1].process(in, 0.75f);
                in = diff_[2].process(in, 0.625f);
                in = diff_[3].process(in, 0.625f);

                const float breath = 0.5f + 0.5f * std::sin(kTwoPi * breathPhase_);
                breathPhase_ += 0.08f / internalRate_;
                if (breathPhase_ >= 1.0f) breathPhase_ -= 1.0f;
                const float decayEff = feedback_ * (1.0f - breathDepth_ * 0.3f * breath);

                float d[kLines], wetL = 0.0f, wetR = 0.0f;
                for (int i = 0; i < kLines; ++i) {
                    advanceSweep(i);
                    const float target = lineDelay_[i] * sizeScale_ * (0.25f + 1.5f * mDelayFromSize());
                    glideLen_[i] += glideCoef_ * (target - glideLen_[i]);   // smoothed delay time
                    float rd = glideLen_[i] - lineDepth_[i] * mDepth_ * kSweepMaxSamp * sweepVal_[i];
                    if (rd < 1.0f) rd = 1.0f;
                    if (rd > kLineCap - 1) rd = kLineCap - 1;
                    float s = lines_[i].readFractional(rd);
                    fdnDamp_[i] = s * dampA2_ + fdnDamp_[i] * dampA1_;
                    d[i] = fdnDamp_[i];
                    const float pan = linePan_[i];                          // -1..1
                    const float gl = 0.5f * (1.0f - pan), gr = 0.5f * (1.0f + pan);
                    wetL += d[i] * lineLevel_[i] * gl;
                    wetR += d[i] * lineLevel_[i] * gr;
                }
                // Selectable orthonormal recirculation topology (loop gain == decayEff).
                float f[kLines];
                switch (algo_) {
                    default: case 0: {                         // Cathedral: Householder (dense)
                        float sum = 0.0f; for (int i = 0; i < kLines; ++i) sum += d[i];
                        const float hf = (2.0f / kLines) * sum;
                        for (int i = 0; i < kLines; ++i) f[i] = d[i] - hf; break; }
                    case 1:                                     // Chamber: identity (parallel combs)
                        for (int i = 0; i < kLines; ++i) f[i] = d[i]; break;
                    case 2:                                     // Swirl: circular permutation
                        for (int i = 0; i < kLines; ++i) f[i] = d[(i + 1) % kLines]; break;
                    case 3: {                                   // Plate: butterfly pairs (bright)
                        constexpr float r = 0.70710678f;
                        for (int p = 0; p < kLines; p += 2) { f[p] = (d[p] + d[p+1]) * r; f[p+1] = (d[p] - d[p+1]) * r; }
                        break; }
                    case 4: {                                   // Cascade: butterfly + rotate pairs (densest)
                        constexpr float r = 0.70710678f; float t[kLines];
                        for (int p = 0; p < kLines; p += 2) { t[p] = (d[p] + d[p+1]) * r; t[p+1] = (d[p] - d[p+1]) * r; }
                        for (int i = 0; i < kLines; ++i) f[i] = t[(i + 2) % kLines]; break; }
                }
                for (int i = 0; i < kLines; ++i)
                    lines_[i].write(in + decayEff * lineAP_[i].process(f[i], 0.55f));

                // global shimmer loop off the mono sum
                float fb = hpf_.process((wetL + wetR) * 0.5f);
                loopDamp_ = fb * dampA2_ + loopDamp_ * dampA1_; fb = loopDamp_;
                shimLfoPhase_ += shimLfoInc_; if (shimLfoPhase_ >= 1.0f) shimLfoPhase_ -= 1.0f;
                const float shimSemis = shimLfoDepth_ * std::sin(kTwoPi * shimLfoPhase_);
                fb = pitchShift(fb, pitchRate_ * std::pow(2.0f, shimSemis / 12.0f));
                fb = softClip(fb);
                fbSample_ = fb * shimmer_;

                wetPrevL_ = wetCurL_; wetCurL_ = wetL * kOutGain;
                wetPrevR_ = wetCurR_; wetCurR_ = wetR * kOutGain;
            }
            const float frac = static_cast<float>(decimPhase_) * invD;
            float woL = wetPrevL_ + (wetCurL_ - wetPrevL_) * frac;
            float woR = wetPrevR_ + (wetCurR_ - wetPrevR_) * frac;
            decimPhase_ = (decimPhase_ + 1) % kInternalDivisor;

            // Wet-side stereo chorus (full rate): quadrature LFO, per-channel line.
            const float clfoL = std::sin(kTwoPi * chorusLfoPhase_);
            const float clfoR = std::sin(kTwoPi * (chorusLfoPhase_ + 0.25f));
            chorusLfoPhase_ += chorusLfoInc_;
            if (chorusLfoPhase_ >= 1.0f) chorusLfoPhase_ -= 1.0f;
            woL = chorusSample(0, woL, clfoL);
            woR = chorusSample(1, woR, clfoR);

            xL[n] = dryL * (1.0f - mix_) + woL * mix_;
            if (channels > 1) xR[n] = dryR * (1.0f - mix_) + woR * mix_;
        }
    }

private:
    static constexpr float kOutGain = 0.88f;  // measured make-up; keeps worst-case peak <~1.0 across all topologies (see harness)
    float mDelayFromSize() const noexcept { return sizeScale_ * (0.5f); }  // (folded into per-line via Size)

    void advanceSweep(int i) noexcept {
        if (--sweepCnt_[i] <= 0) {
            rng_ = rng_ * 1664525u + 1013904223u;
            sweepTarget_[i] = static_cast<float>((rng_ >> 9) & 0xFFFFu) / 32768.0f - 1.0f;
            const float r = mRate_ * (0.3f + 1.4f * lineRate_[i]);   // master x per-line rate
            const int period = static_cast<int>(internalRate_ * 0.25f / (r * 5.0f + 0.05f)) + 6;
            sweepCnt_[i] = period + (static_cast<int>(rng_ >> 3) & 63);
        }
        sweepVal_[i] += 0.01f * (sweepTarget_[i] - sweepVal_[i]);
    }

    // Wet-side modulated chorus (one per channel), same shape as the reverse
    // reverb: a modulated delay tap mixed 50/50 with the input, with feedback.
    float chorusSample(int ch, float x, float lfo) noexcept {
        auto& line = chorusLine_[static_cast<std::size_t>(ch)];
        float d = chorusBase_ + lfo * chorusDepth_ * chorusModMax_;
        if (d < 1.0f) d = 1.0f;
        const float tap = line.readFractional(d);
        line.write(x + chorusFb_ * tap);
        return x + 0.5f * tap;
    }

    static float softClip(float x) noexcept {
        if (x < -1.2f) return -0.8f; if (x > 1.2f) return 0.8f;
        return x - (x * x * x) * (1.0f / 4.32f);
    }
    float pitchShift(float x, float rate) noexcept {
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
        const float step = 1.0f - rate;
        pitchO0_ += step; if (pitchO0_ < 0) pitchO0_ += kPitchWin; else if (pitchO0_ >= kPitchWin) pitchO0_ -= kPitchWin;
        pitchO1_ += step; if (pitchO1_ < 0) pitchO1_ += kPitchWin; else if (pitchO1_ >= kPitchWin) pitchO1_ -= kPitchWin;
        return t0 * e0 + t1 * e1;
    }

    struct Diffuser {   // true (unity-gain) allpass -> safe inside feedback loops
        BoundedDelayLine<float, 512> line; int len_ = 1;
        void prepare(int cap, float sr) noexcept { line.prepare(cap, sr); len_ = cap; }
        void reset() noexcept { line.reset(); }
        float process(float x, float g) noexcept {
            const float d = line.readFractional(static_cast<float>(len_ - 1));
            const float v = x - g * d; line.write(v); return g * v + d;
        }
    };
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void reset() noexcept { x1 = x2 = y1 = y2 = 0; }
        float process(float x) noexcept { const float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
    };
    void designFilters() noexcept {
        const float rate = sampleRate_ / static_cast<float>(kInternalDivisor);
        // anti-alias LP before decimation
        { const float fc = rate * 0.5f * 0.85f, w0 = 2*3.14159265f*fc/sampleRate_, c = std::cos(w0), s = std::sin(w0);
          const float al = s/(2*0.70710678f), a0 = 1+al;
          aa_.b0=(1-c)*0.5f/a0; aa_.b1=(1-c)/a0; aa_.b2=(1-c)*0.5f/a0; aa_.a1=-2*c/a0; aa_.a2=(1-al)/a0; }
        // loop HPF ~80 Hz (down-shift safety)
        { const float fc = 80.0f, w0 = 2*3.14159265f*fc/rate, c = std::cos(w0), s = std::sin(w0);
          const float al = s/(2*0.70710678f), a0 = 1+al;
          hpf_.b0=(1+c)*0.5f/a0; hpf_.b1=-(1+c)/a0; hpf_.b2=(1+c)*0.5f/a0; hpf_.a1=-2*c/a0; hpf_.a2=(1-al)/a0; }
    }

    // ---- parameter table (built with a constexpr helper for the 30 per-line rows) ----
    static constexpr std::array<ParameterDescriptor, kNumParams> kParams = {{
        // Defaults are the operator's tuned preset (2026-08-26): a smooth, wide
        // canyon with a touch of shimmer. Delays stay well-spread/coprime to
        // avoid tail flutter (lines 3/4 sit at 0.079/0.081 -- both read "0.08"
        // but are not exactly equal, which would re-introduce a degenerate line).
        bc_detail::cont(kSize, "Master/Size", 0.0f, 1.0f, 1.0f),
        bc_detail::cont(kMRate, "Master/Sweep Rate", 0.0f, 1.0f, 0.35f),
        bc_detail::cont(kMDepth, "Master/Sweep Depth", 0.0f, 1.0f, 0.30f),
        bc_detail::cont(kFeedback, "Master/Feedback", 0.0f, 1.0f, 0.76f),
        bc_detail::cont(kMix, "Master/Mix", 0.0f, 1.0f, 1.0f),
        {ParamId{kPredelay}, "Global/Predelay", ParamUnit::seconds, 0.0f, 0.34f, 0.05f, ParamSkew::linear, ParamKind::continuous, 0},
        bc_detail::cont(kDamping, "Global/Damping", 0.0f, 1.0f, 0.16f),
        bc_detail::cont(kGlide, "Global/Glide", 0.0f, 1.0f, 0.0f),
        {ParamId{kFreeze}, "Global/Freeze", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 2, kOffOn},
        {ParamId{kPitch}, "Shimmer/Pitch", ParamUnit::none, -12.0f, 12.0f, 12.0f, ParamSkew::linear, ParamKind::continuous, 0},
        bc_detail::cont(kShimmer, "Shimmer/Amount", 0.0f, 1.0f, 0.18f),
        bc_detail::cont(kShimLfoRate, "Shimmer/LFO Rate", 0.0f, 1.0f, 0.08f),
        {ParamId{kShimLfoDepth}, "Shimmer/LFO Depth", ParamUnit::none, 0.0f, 12.0f, 12.0f, ParamSkew::linear, ParamKind::continuous, 0},
        bc_detail::cont(kBreath, "Breath/Depth", 0.0f, 1.0f, 0.12f),
        {ParamId{kChorusTime}, "Chorus/Time", ParamUnit::seconds, 0.001f, 0.030f, 0.014f, ParamSkew::linear, ParamKind::continuous, 0},
        bc_detail::cont(kChorusDepth, "Chorus/Depth", 0.0f, 1.0f, 0.45f),
        {ParamId{kChorusRate}, "Chorus/Rate", ParamUnit::hz, 0.05f, 8.0f, 0.5f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kChorusFeedback}, "Chorus/Feedback", ParamUnit::none, 0.0f, 0.85f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
        bc_detail::cont(kDelay1+0, "Delays/1", 0.0f, 0.21f, 0.030f) , bc_detail::cont(kDelay1+1, "Delays/2", 0.0f, 0.21f, 0.050f),
        bc_detail::cont(kDelay1+2, "Delays/3", 0.0f, 0.21f, 0.079f) , bc_detail::cont(kDelay1+3, "Delays/4", 0.0f, 0.21f, 0.081f),
        bc_detail::cont(kDelay1+4, "Delays/5", 0.0f, 0.21f, 0.100f) , bc_detail::cont(kDelay1+5, "Delays/6", 0.0f, 0.21f, 0.150f),
        bc_detail::cont(kRate1+0, "Rates/1", 0.0f, 1.0f, 0.21f) , bc_detail::cont(kRate1+1, "Rates/2", 0.0f, 1.0f, 0.18f),
        bc_detail::cont(kRate1+2, "Rates/3", 0.0f, 1.0f, 0.10f) , bc_detail::cont(kRate1+3, "Rates/4", 0.0f, 1.0f, 0.07f),
        bc_detail::cont(kRate1+4, "Rates/5", 0.0f, 1.0f, 0.13f) , bc_detail::cont(kRate1+5, "Rates/6", 0.0f, 1.0f, 0.21f),
        bc_detail::cont(kDepth1+0, "Depths/1", 0.0f, 1.0f, 0.00f) , bc_detail::cont(kDepth1+1, "Depths/2", 0.0f, 1.0f, 0.14f),
        bc_detail::cont(kDepth1+2, "Depths/3", 0.0f, 1.0f, 0.29f) , bc_detail::cont(kDepth1+3, "Depths/4", 0.0f, 1.0f, 0.22f),
        bc_detail::cont(kDepth1+4, "Depths/5", 0.0f, 1.0f, 0.50f) , bc_detail::cont(kDepth1+5, "Depths/6", 0.0f, 1.0f, 0.50f),
        bc_detail::cont(kPan1+0, "Pans/1", -1.0f, 1.0f, -1.0f) , bc_detail::cont(kPan1+1, "Pans/2", -1.0f, 1.0f, -1.0f),
        bc_detail::cont(kPan1+2, "Pans/3", -1.0f, 1.0f, 1.0f) , bc_detail::cont(kPan1+3, "Pans/4", -1.0f, 1.0f, 1.0f),
        bc_detail::cont(kPan1+4, "Pans/5", -1.0f, 1.0f, -1.0f) , bc_detail::cont(kPan1+5, "Pans/6", -1.0f, 1.0f, 0.56f),
        bc_detail::cont(kLevel1+0, "Levels/1", 0.0f, 1.0f, 0.69f) , bc_detail::cont(kLevel1+1, "Levels/2", 0.0f, 1.0f, 0.72f),
        bc_detail::cont(kLevel1+2, "Levels/3", 0.0f, 1.0f, 0.17f) , bc_detail::cont(kLevel1+3, "Levels/4", 0.0f, 1.0f, 0.14f),
        bc_detail::cont(kLevel1+4, "Levels/5", 0.0f, 1.0f, 0.33f) , bc_detail::cont(kLevel1+5, "Levels/6", 0.0f, 1.0f, 0.37f),
        {ParamId{kAlgo}, "Master/Algorithm", ParamUnit::none, 0.0f, 4.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 5, kAlgoNames},
    }};

    // ---- state ----
    BoundedDelayLine<std::int16_t, kPreMax> preLine_;
    BoundedDelayLine<float, kLineCap>       lines_[kLines];
    float fdnDamp_[kLines] = {}, sweepVal_[kLines] = {}, sweepTarget_[kLines] = {}, glideLen_[kLines] = {};
    int   sweepCnt_[kLines] = {};
    float lineDelay_[kLines] = {}, lineRate_[kLines] = {}, lineDepth_[kLines] = {};
    float linePan_[kLines] = {}, lineLevel_[kLines] = {};
    static constexpr int kInDiff = 4;
    Diffuser diff_[kInDiff];       // input diffusion chain (densifies early reflections)
    Diffuser lineAP_[kLines];      // per-line feedback diffusion (smooths the tail)
    std::array<std::int16_t, kPitchBuf> pitchBuf_{};
    int   pitchW_ = 0; float pitchO0_ = 0.0f, pitchO1_ = static_cast<float>(kPitchWin) * 0.5f, pitchRate_ = 2.0f;
    Biquad aa_{}, hpf_{};
    float dampA1_ = 0.2f, dampA2_ = 0.8f, loopDamp_ = 0.0f;
    float fbSample_ = 0.0f; int decimPhase_ = 0;
    float wetPrevL_ = 0, wetCurL_ = 0, wetPrevR_ = 0, wetCurR_ = 0;
    float breathPhase_ = 0.0f; std::uint32_t rng_ = 0x2545f491u;
    int   numChannels_ = 2; float sampleRate_ = 48000.0f, internalRate_ = 12000.0f;
    float predelaySamp_ = 640.0f, sizeScale_ = 0.9f, mRate_ = 0.35f, mDepth_ = 0.45f;
    float feedback_ = 0.8f, dampCut_ = 3000.0f, glideCoef_ = 0.1f;
    bool  freeze_ = false; int algo_ = 0;
    float shimmer_ = 0.3f, breathDepth_ = 0.35f, mix_ = 1.0f;
    float shimLfoPhase_ = 0.0f, shimLfoInc_ = 0.0f, shimLfoDepth_ = 0.0f;
    // Wet-side stereo chorus (full-rate int16 delay lines).
    std::array<BoundedDelayLine<std::int16_t, kChorusMax>, 2> chorusLine_{};
    float chorusBase_ = 672.0f, chorusDepth_ = 0.45f, chorusModMax_ = 240.0f;
    float chorusFb_ = 0.0f, chorusLfoPhase_ = 0.0f, chorusLfoInc_ = 0.0f;
};

} // namespace acfx

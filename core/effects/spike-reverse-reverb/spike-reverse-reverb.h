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

    static constexpr int kComb[8] = {405, 431, 463, 492, 516, 541, 565, 587}; // Room: combs
    static constexpr int kAp[4]   = {202, 160, 124, 82};                      // Room: allpasses
    static constexpr int kFdn[4]         = {887, 1129, 1381, 1607}; // Hall: FDN delay lines
    static constexpr int kPlateDiff[4]   = {113, 162, 241, 399};    // Plate: input diffusers
    static constexpr int kPlateLoopAp    = 683;                     // Plate: in-loop allpass
    static constexpr int kPlateLoopDelay = 2411;                    // Plate: loop delay
    static constexpr float kGain  = 0.015f;
    static constexpr float kApFb  = 0.5f;
    enum { kRoom = 0, kHall = 1, kPlate = 2 };   // reverb algorithms (share the pool)

    static constexpr int   kPreMax   = 2880;   // 60 ms @ 48 kHz wet-side mod delay
    static constexpr float kModMaxMs = 5.0f;
    static constexpr float kFilterLfoMaxOct = 2.5f;  // cutoff LFO full-depth swing (+/- octaves)
    static constexpr float kFilterLfoMaxQ   = 5.0f;  // resonance LFO full-depth swing (+/- Q)

    enum : std::uint8_t {
        kDecay = 0, kDamping = 1, kMix = 2,
        kDelayTime = 3, kModDepth = 4, kModRate = 5, kFeedback = 6,
        kWindowTime = 7, kMode = 8, kPitch = 9, kPitchBlend = 10,
        kPitchLfoRate = 11, kPitchLfoDepth = 12,
        kPitchLpCutoff = 13, kPitchLpReso = 14,
        kFilterLfoRate = 15, kFilterLfoCutoffDepth = 16, kFilterLfoResoDepth = 17,
        kEqFreq = 18, kEqGain = 19, kEqQ = 20, kReverbAlgo = 21,
    };
    static constexpr std::size_t kNumParams = 22;
    static constexpr std::array<std::string_view, 3> kAlgoLabels = {{"room", "hall", "plate"}};
    static constexpr std::array<std::string_view, 2> kModeLabels = {{"block", "granular"}};
    enum { kModeBlock = 0, kModeGranular = 1 };

    // Display order for hosts/plugins groups related controls (reverse -> pitch
    // -> pitch filter -> pitch LFO -> reverb -> wet chorus -> mix). The stable
    // ParamId in each descriptor is preserved, so setParameter, the shared CC
    // map, and automation ids are all unaffected by this regrouping.
    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParamsDisplay; }

    void prepare(const ProcessContext& ctx) noexcept {
        numChannels_ = ctx.numChannels < 2 ? ctx.numChannels : 2;
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        modMaxSamp_  = kModMaxMs * 0.001f * sampleRate_;
        const float decimatedRate = sampleRate_ / static_cast<float>(kInternalDivisor);
        designLowpass(aa_, sampleRate_, decimatedRate * 0.5f * 0.85f, 0.70710678f);
        const int cap = static_cast<int>(0.060f * sampleRate_) + 1;
        for (int ch = 0; ch < 2; ++ch) preLine_[ch].prepare(cap, sampleRate_);
        for (std::size_t i = 0; i < kNumParams; ++i)
            setParameter(ParamId{static_cast<std::uint8_t>(i)},
                         normalize(kParams[i], kParams[i].defaultValue));
        reset();
    }

    void reset() noexcept {
        cap_.fill(0);
        resetTank();
        for (int ch = 0; ch < 2; ++ch) preLine_[ch].reset();
        aa_.reset();
        pitch_.reset();
        pitchLp_.reset();
        eqL_.reset();
        eqR_.reset();
        resetCapture();
        decimPhase_ = 0; wetPrev_ = 0.0f; wetCur_ = 0.0f; lfoPhase_ = 0.0f;
        pitchLfoPhase_ = 0.0f; filterLfoPhase_ = 0.0f;
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
            case kPitch:
                pitchBaseSemi_ = v;                              // base transpose in semitones
                pitch_.rate    = std::pow(2.0f, v / 12.0f);      // static base rate (LFO depth 0)
                pitchActive_   = (v > 0.05f || v < -0.05f);      // exact 0 => transparent
                break;
            case kPitchBlend: pitchBlend_ = v; break;            // 0=dry reverse .. 1=fully pitched
            case kPitchLfoRate:
                pitchLfoInc_ = v / (sampleRate_ / static_cast<float>(kInternalDivisor));
                break;
            case kPitchLfoDepth: pitchLfoDepth_ = v; break;      // vibrato depth in semitones
            case kPitchLpCutoff:
                pitchLpCutoff_ = v;
                designLowpass(pitchLp_, sampleRate_ / static_cast<float>(kInternalDivisor),
                              pitchLpCutoff_, pitchLpQ_);
                break;
            case kPitchLpReso:
                pitchLpQ_ = 0.5f + v * 5.5f;                     // 0..1 -> Q 0.5..6.0
                designLowpass(pitchLp_, sampleRate_ / static_cast<float>(kInternalDivisor),
                              pitchLpCutoff_, pitchLpQ_);
                break;
            case kFilterLfoRate:
                filterLfoInc_ = v / (sampleRate_ / static_cast<float>(kInternalDivisor));
                break;
            case kFilterLfoCutoffDepth:
                filterLfoCutoffDepth_ = v;
                restoreFilterBaseIfIdle();
                break;
            case kFilterLfoResoDepth:
                filterLfoResoDepth_ = v;
                restoreFilterBaseIfIdle();
                break;
            case kEqFreq: eqFreq_   = v; redesignEq(); break;
            case kEqGain: eqGainDb_ = v; redesignEq(); break;
            case kEqQ:    eqQ_      = v; redesignEq(); break;
            case kReverbAlgo: {
                const int a = v < 0.5f ? kRoom : (v < 1.5f ? kHall : kPlate);
                if (a != algo_) { algo_ = a; resetTank(); }   // clear tail on switch
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
                float rev = (mode_ == kModeGranular) ? granularTick(monoAA)
                                                     : blockTick(monoAA);
                // Optional pitch LFO (vibrato): recompute the shifter rate per
                // tick only when depth is engaged (keeps powf off the hot path
                // otherwise). Always advance the shifter to keep its buffer
                // current; blend the pitched copy in. Transparent at 0.
                const bool lfoOn = pitchLfoDepth_ > 0.001f;
                if (lfoOn) {
                    const float lfo = std::sin(kTwoPi * pitchLfoPhase_);
                    pitchLfoPhase_ += pitchLfoInc_;
                    if (pitchLfoPhase_ >= 1.0f) pitchLfoPhase_ -= 1.0f;
                    pitch_.rate = std::pow(2.0f, (pitchBaseSemi_ + lfo * pitchLfoDepth_) / 12.0f);
                }
                float pitched = pitch_.process(rev);
                if (!(pitchActive_ || lfoOn)) pitched = rev;   // transparent when not pitching
                rev += (pitched - rev) * pitchBlend_;          // blend pitched into the reverse
                // Optional filter LFO: sweep cutoff (in octaves) and/or Q, redesign
                // the biquad per tick only while engaged (keeps the transcendentals
                // off the hot path otherwise).
                if (filterLfoCutoffDepth_ > 0.001f || filterLfoResoDepth_ > 0.001f) {
                    const float flfo = std::sin(kTwoPi * filterLfoPhase_);
                    filterLfoPhase_ += filterLfoInc_;
                    if (filterLfoPhase_ >= 1.0f) filterLfoPhase_ -= 1.0f;
                    float fc = pitchLpCutoff_ * std::pow(2.0f, flfo * filterLfoCutoffDepth_ * kFilterLfoMaxOct);
                    if (fc < 100.0f)  fc = 100.0f;
                    if (fc > 7000.0f) fc = 7000.0f;
                    float q = pitchLpQ_ + flfo * filterLfoResoDepth_ * kFilterLfoMaxQ;
                    if (q < 0.4f)  q = 0.4f;
                    if (q > 10.0f) q = 10.0f;
                    designLowpass(pitchLp_, sampleRate_ / static_cast<float>(kInternalDivisor), fc, q);
                }
                rev = pitchLp_.process(rev);                   // lowpass the blended stream
                                                               // (pitched + dry reverse both)
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

            // Final mix, then the output parametric EQ (full rate, per channel).
            xL[n] = eqL_.process(dryL * (1.0f - mix_) + wetL * mix_);
            if (channels > 1) xR[n] = eqR_.process(dryR * (1.0f - mix_) + wetR * mix_);
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

    // When both filter-LFO depths are off, restore the biquad to its base
    // cutoff/Q (the per-tick modulation path is skipped, so it would otherwise
    // stay stuck at the last swept coefficients).
    void restoreFilterBaseIfIdle() noexcept {
        if (filterLfoCutoffDepth_ < 0.001f && filterLfoResoDepth_ < 0.001f)
            designLowpass(pitchLp_, sampleRate_ / static_cast<float>(kInternalDivisor),
                          pitchLpCutoff_, pitchLpQ_);
    }

    // Redesign both output-EQ channels from the current freq/gain/Q (stereo-linked
    // coefficients, independent state). Full-rate fs.
    void redesignEq() noexcept {
        designPeaking(eqL_, sampleRate_, eqFreq_, eqGainDb_, eqQ_);
        designPeaking(eqR_, sampleRate_, eqFreq_, eqGainDb_, eqQ_);
    }

    // Two-tap crossfade granular pitch shifter (classic real-time method) on the
    // decimated reversed stream. Two read pointers half a window apart, each
    // stepping at (1 - ratio); Hann-crossfaded so the wrap is inaudible.
    struct PitchShifter {
        static constexpr int kBuf = 2048;    // ~128 ms @ 16 kHz
        static constexpr int kWin = 1024;    // crossfade window (samples)
        std::array<std::int16_t, kBuf> buf{};
        int   w    = 0;
        float o0   = 0.0f;
        float o1   = static_cast<float>(kWin) * 0.5f;
        float rate = 1.0f;
        void reset() noexcept { buf.fill(0); w = 0; o0 = 0.0f; o1 = static_cast<float>(kWin) * 0.5f; }
        float readFrac(float pos) const noexcept {
            while (pos < 0.0f)                            pos += static_cast<float>(kBuf);
            while (pos >= static_cast<float>(kBuf))       pos -= static_cast<float>(kBuf);
            const int   i0 = static_cast<int>(pos);
            const float fr = pos - static_cast<float>(i0);
            int i1 = i0 + 1; if (i1 >= kBuf) i1 = 0;
            return dequantizeInt16(buf[static_cast<std::size_t>(i0)]) * (1.0f - fr)
                 + dequantizeInt16(buf[static_cast<std::size_t>(i1)]) * fr;
        }
        float process(float x) noexcept {
            constexpr float kTwoPi = 6.28318530718f;
            buf[static_cast<std::size_t>(w)] = quantizeInt16(x);
            const float t0 = readFrac(static_cast<float>(w) - o0);
            const float t1 = readFrac(static_cast<float>(w) - o1);
            const float e0 = 0.5f * (1.0f - std::cos(kTwoPi * o0 / static_cast<float>(kWin)));
            const float e1 = 0.5f * (1.0f - std::cos(kTwoPi * o1 / static_cast<float>(kWin)));
            const float out = t0 * e0 + t1 * e1;
            w = (w + 1) % kBuf;
            const float step = 1.0f - rate;
            o0 += step; if (o0 < 0.0f) o0 += kWin; else if (o0 >= kWin) o0 -= kWin;
            o1 += step; if (o1 < 0.0f) o1 += kWin; else if (o1 >= kWin) o1 -= kWin;
            return out;
        }
    };

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
    static void designLowpass(Biquad& bq, float fs, float fc, float q) noexcept {
        const float w0    = 2.0f * 3.14159265358979f * fc / fs;
        const float cosw  = std::cos(w0);
        const float sinw  = std::sin(w0);
        const float alpha = sinw / (2.0f * q);
        const float a0    = 1.0f + alpha;
        bq.b0 = (1.0f - cosw) * 0.5f / a0;
        bq.b1 = (1.0f - cosw)        / a0;
        bq.b2 = (1.0f - cosw) * 0.5f / a0;
        bq.a1 = (-2.0f * cosw)       / a0;
        bq.a2 = (1.0f - alpha)       / a0;
    }
    // RBJ peaking (bell) EQ. Identity at gainDb == 0, so the band is transparent
    // when flat. Used for the output parametric EQ.
    static void designPeaking(Biquad& bq, float fs, float f0, float gainDb, float q) noexcept {
        const float A     = std::pow(10.0f, gainDb / 40.0f);
        const float w0    = 2.0f * 3.14159265358979f * f0 / fs;
        const float cosw  = std::cos(w0);
        const float sinw  = std::sin(w0);
        const float alpha = sinw / (2.0f * q);
        const float a0    = 1.0f + alpha / A;
        bq.b0 = (1.0f + alpha * A) / a0;
        bq.b1 = (-2.0f * cosw)     / a0;
        bq.b2 = (1.0f - alpha * A) / a0;
        bq.a1 = (-2.0f * cosw)     / a0;
        bq.a2 = (1.0f - alpha / A) / a0;
    }

    // The three algorithms overlap in one shared pool (only one is active; a
    // switch clears it). Pool is sized to the largest algorithm's footprint.
    static constexpr int kRoomSize  = [] { int t = 0; for (int i = 0; i < 8; ++i) t += kComb[i];
                                                       for (int i = 0; i < 4; ++i) t += kAp[i]; return t; }();
    static constexpr int kHallSize  = [] { int t = 0; for (int i = 0; i < 4; ++i) t += kFdn[i]; return t; }();
    static constexpr int kPlateSize = [] { int t = 0; for (int i = 0; i < 4; ++i) t += kPlateDiff[i];
                                                       return t + kPlateLoopAp + kPlateLoopDelay; }();
    static constexpr int kTankPool  = (kRoomSize > kHallSize ? kRoomSize : kHallSize) > kPlateSize
                                    ? (kRoomSize > kHallSize ? kRoomSize : kHallSize) : kPlateSize;

    // Clear the tank buffer + all algorithm state (on reset / algorithm switch).
    void resetTank() noexcept {
        pool_.fill(0.0f);
        for (int i = 0; i < 8; ++i) { combPos_[i] = 0; combStore_[i] = 0.0f; }
        for (int i = 0; i < 4; ++i) { apPos_[i] = 0; }
        for (int i = 0; i < 4; ++i) { fdnPos_[i] = 0; fdnDamp_[i] = 0.0f; platePos_[i] = 0; }
        plateLoopApPos_ = 0; plateLoopDelayPos_ = 0; plateDamp_ = 0.0f;
    }

    float runTank(float fed) noexcept {
        switch (algo_) {
            case kHall:  return runHall(fed);
            case kPlate: return runPlate(fed);
            default:     return runRoom(fed);
        }
    }

    // --- Room: Freeverb (8 comb -> 4 allpass) ---
    int combOff(int i) const noexcept { int off = 0; for (int k = 0; k < i; ++k) off += kComb[k]; return off; }
    int apOff(int i) const noexcept {
        int off = 0;
        for (int k = 0; k < 8; ++k) off += kComb[k];
        for (int k = 0; k < i; ++k) off += kAp[k];
        return off;
    }
    float runRoom(float fed) noexcept {
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

    // --- Hall: 4-line feedback delay network with a normalised Hadamard matrix ---
    int fdnOff(int i) const noexcept { int off = 0; for (int k = 0; k < i; ++k) off += kFdn[k]; return off; }
    float runHall(float fed) noexcept {
        float d[4];
        for (int i = 0; i < 4; ++i) {
            const int idx = fdnOff(i) + fdnPos_[i];
            float v = pool_[static_cast<std::size_t>(idx)];
            fdnDamp_[i] = v * damp2_ + fdnDamp_[i] * damp1_;   // per-line damping
            d[i] = fdnDamp_[i];
        }
        // Orthonormal 4x4 Hadamard mix (rows scaled by 0.5).
        const float f0 = (d[0] + d[1] + d[2] + d[3]) * 0.5f;
        const float f1 = (d[0] - d[1] + d[2] - d[3]) * 0.5f;
        const float f2 = (d[0] + d[1] - d[2] - d[3]) * 0.5f;
        const float f3 = (d[0] - d[1] - d[2] + d[3]) * 0.5f;
        const float f[4] = {f0, f1, f2, f3};
        for (int i = 0; i < 4; ++i) {
            const int idx = fdnOff(i) + fdnPos_[i];
            pool_[static_cast<std::size_t>(idx)] = fed + combFb_ * f[i];
            if (++fdnPos_[i] >= kFdn[i]) fdnPos_[i] = 0;
        }
        return (d[0] + d[1] + d[2] + d[3]) * 0.5f;
    }

    // --- Plate: input diffusion -> a single damped allpass+delay feedback loop ---
    int plateDiffOff(int i) const noexcept { int off = 0; for (int k = 0; k < i; ++k) off += kPlateDiff[k]; return off; }
    int plateLoopApOff() const noexcept { int off = 0; for (int k = 0; k < 4; ++k) off += kPlateDiff[k]; return off; }
    int plateLoopDelayOff() const noexcept { return plateLoopApOff() + kPlateLoopAp; }
    float runPlate(float fed) noexcept {
        float x = fed;
        for (int i = 0; i < 4; ++i) {                 // input diffusion
            const int idx = plateDiffOff(i) + platePos_[i];
            const float bufout = pool_[static_cast<std::size_t>(idx)];
            const float out    = -x + bufout;
            pool_[static_cast<std::size_t>(idx)] = x + bufout * 0.7f;
            if (++platePos_[i] >= kPlateDiff[i]) platePos_[i] = 0;
            x = out;
        }
        const int dIdx = plateLoopDelayOff() + plateLoopDelayPos_;
        const float d  = pool_[static_cast<std::size_t>(dIdx)];
        plateDamp_ = d * damp2_ + plateDamp_ * damp1_;          // loop damping
        const float loopIn = x + combFb_ * plateDamp_;          // decay feedback
        const int aIdx = plateLoopApOff() + plateLoopApPos_;    // in-loop allpass
        const float aBuf = pool_[static_cast<std::size_t>(aIdx)];
        const float aOut = -loopIn + aBuf;
        pool_[static_cast<std::size_t>(aIdx)] = loopIn + aBuf * 0.6f;
        if (++plateLoopApPos_ >= kPlateLoopAp) plateLoopApPos_ = 0;
        pool_[static_cast<std::size_t>(dIdx)] = aOut;           // write loop delay
        if (++plateLoopDelayPos_ >= kPlateLoopDelay) plateLoopDelayPos_ = 0;
        return plateDamp_;
    }

    static constexpr std::array<ParameterDescriptor, kNumParams> kParams = {{
        {ParamId{kDecay}, "Reverb/Decay", ParamUnit::none, 0.0f, 1.0f, 1.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDamping}, "Reverb/Damping", ParamUnit::none, 0.0f, 1.0f, 0.7f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "Output/Mix", ParamUnit::none, 0.0f, 1.0f, 1.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDelayTime}, "Chorus/Time", ParamUnit::seconds, 0.001f, 0.060f, 0.014f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kModDepth}, "Chorus/Depth", ParamUnit::none, 0.0f, 1.0f, 0.45f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kModRate}, "Chorus/Rate", ParamUnit::hz, 0.05f, 8.0f, 0.5f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kFeedback}, "Chorus/Feedback", ParamUnit::none, 0.0f, 0.85f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kWindowTime}, "Reverse/Window", ParamUnit::seconds, 0.1f, 1.0f, 0.5f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kMode}, "Reverse/Mode", ParamUnit::none, 0.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::discrete, 2, kModeLabels},
        {ParamId{kPitch}, "Pitch/Amount", ParamUnit::none, -12.0f, 12.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kPitchBlend}, "Pitch/Blend", ParamUnit::none, 0.0f, 1.0f, 0.11f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kPitchLfoRate}, "Pitch/LFO Rate", ParamUnit::hz, 0.02f, 1.0f, 0.02f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kPitchLfoDepth}, "Pitch/LFO Depth", ParamUnit::none, 0.0f, 12.0f, 12.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kPitchLpCutoff}, "Filter/Cutoff", ParamUnit::hz, 200.0f, 7000.0f, 4000.0f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kPitchLpReso}, "Filter/Resonance", ParamUnit::none, 0.0f, 1.0f, 0.1f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kFilterLfoRate}, "Filter/LFO Rate", ParamUnit::hz, 0.02f, 8.0f, 0.3f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kFilterLfoCutoffDepth}, "Filter/LFO Cutoff", ParamUnit::none, 0.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kFilterLfoResoDepth}, "Filter/LFO Reso", ParamUnit::none, 0.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kEqFreq}, "Output/EQ Freq", ParamUnit::hz, 40.0f, 12000.0f, 300.0f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kEqGain}, "Output/EQ Gain", ParamUnit::decibels, -18.0f, 18.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kEqQ}, "Output/EQ Q", ParamUnit::none, 0.3f, 8.0f, 1.0f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kReverbAlgo}, "Reverb/Algorithm", ParamUnit::none, 0.0f, 2.0f, 0.0f,
         ParamSkew::linear, ParamKind::discrete, 3, kAlgoLabels},
    }};

    // kParams stays in ParamId order (internal lookups + the shared CC map key
    // off position == id). This is the grouped view hosts show — a reordering of
    // the SAME descriptors (each keeps its ParamId), derived so there is one
    // source of truth.
    static constexpr std::array<ParameterDescriptor, kNumParams> kParamsDisplay = [] {
        constexpr std::uint8_t order[kNumParams] = {
            kMode, kWindowTime,                                            // Reverse
            kPitch, kPitchBlend, kPitchLfoRate, kPitchLfoDepth,           // Pitch
            kPitchLpCutoff, kPitchLpReso,                                 // Filter
            kFilterLfoRate, kFilterLfoCutoffDepth, kFilterLfoResoDepth,   // Filter LFO
            kReverbAlgo, kDecay, kDamping,                                // Reverb
            kDelayTime, kModDepth, kModRate, kFeedback,                   // Chorus
            kMix, kEqFreq, kEqGain, kEqQ,                                 // Output
        };
        std::array<ParameterDescriptor, kNumParams> out = kParams;
        for (std::size_t i = 0; i < kNumParams; ++i) out[i] = kParams[order[i]];
        return out;
    }();

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
    PitchShifter pitch_{};
    Biquad pitchLp_{};                  // lowpass on the pitch-shifter output (tames clang)
    bool  pitchActive_   = false;
    float pitchBlend_    = 1.0f;
    float pitchBaseSemi_ = 0.0f;
    float pitchLfoInc_   = 0.0f;
    float pitchLfoPhase_ = 0.0f;
    float pitchLfoDepth_ = 0.0f;
    float pitchLpCutoff_ = 4000.0f;
    float pitchLpQ_      = 0.707f;
    float filterLfoInc_        = 0.0f;
    float filterLfoPhase_      = 0.0f;
    float filterLfoCutoffDepth_ = 0.0f;
    float filterLfoResoDepth_   = 0.0f;

    // Output parametric EQ (one peaking band, full rate, stereo-linked).
    Biquad eqL_{};
    Biquad eqR_{};
    float  eqFreq_   = 300.0f;
    float  eqGainDb_ = 0.0f;
    float  eqQ_      = 1.0f;

    // Wet-side modulated chorus stage.
    std::array<BoundedDelayLine<std::int16_t, kPreMax>, 2> preLine_{};
    float baseDelay_  = 672.0f;
    float modDepth_   = 0.45f;
    float modMaxSamp_ = 240.0f;
    float lfoInc_     = 0.0000104f;
    float lfoPhase_   = 0.0f;
    float feedback_   = 0.0f;

    // Mono 16 kHz Freeverb tank.
    // Shared tank buffer (one algorithm active at a time; switching clears it).
    std::array<float, kTankPool> pool_{};
    int   algo_ = kRoom;
    int   combPos_[8]   = {};     // Room
    float combStore_[8] = {};
    int   apPos_[4]     = {};
    int   fdnPos_[4]    = {};     // Hall
    float fdnDamp_[4]   = {};
    int   platePos_[4]  = {};     // Plate
    int   plateLoopApPos_    = 0;
    int   plateLoopDelayPos_ = 0;
    float plateDamp_         = 0.0f;

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

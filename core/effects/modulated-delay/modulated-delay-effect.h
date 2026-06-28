#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "primitives/delay-line.h"
#include "primitives/lfo.h"
#include "primitives/svf-primitive.h"

// A delay effect with a State-Variable Filter in the feedback loop plus three
// independent modulation LFOs (US2): delay-time mod, feedback-filter cutoff mod,
// and feedback-filter resonance mod.  Satisfies the Effect contract (no base
// class, no vtable in the audio path).
//
// Signal flow per channel, per sample (research Decision 2; post-filter wet tap):
//   smoothedDelaySecs_ glides toward targetDelaySecs_ via a one-pole smoother
//   effectiveDelaySecs = smoothedDelaySecs_ + delayLfo.tick() * delayDepth * kDelayModRangeSecs
//   dsamp = effectiveDelaySecs * sampleRate                    (clamped by DelayLine, FR-014)
//   effCutoff = baseCutoff * pow(2, cutoffLfo.tick() * cutoffDepth * kCutoffModOctaves)
//   effRes    = clamp(baseRes + resLfo.tick() * resDepth * kResModRange, 0, 1)
//   d = delay[ch].readFractional(dsamp)
//   f = svf[ch].process(d)          (effCutoff / effRes applied per-sample)
//   delay[ch].write(x[n] + feedback * f)
//   x[n] = (1-mix)*x[n] + mix*f
//
// Depth-zero invariant (FR-013): with all three mod depths at 0, process() output
// is bit-for-bit identical to US1.  depth=0 multiplies every LFO output to zero
// before it can influence the signal path; pow(2,0)=1 exactly; clamp(base+0)=base.
//
// Thread-ownership boundary: setParameter() may be called from ANY thread; the
// audio thread consumes pending values at the top of process().
// prepare()/reset() mutate state directly and MUST be called while stopped.

namespace acfx {

class ModulatedDelayEffect {
public:
    // Stable parameter ids — dense index into kParams.  US1 ids 0..5 are frozen.
    enum Param : std::uint8_t {
        kDelayTime      = 0,
        kFeedback       = 1,
        kMix            = 2,
        kCutoff         = 3,
        kResonance      = 4,
        kMode           = 5,
        // US2: three independent modulation LFOs (appended; indices 6..14 stable).
        kDelayModRate   = 6,
        kDelayModDepth  = 7,
        kDelayModShape  = 8,
        kCutoffModRate  = 9,
        kCutoffModDepth = 10,
        kCutoffModShape = 11,
        kResModRate     = 12,
        kResModDepth    = 13,
        kResModShape    = 14,
    };

    // Single source of parameter truth (SC-006, FR-022).  All values in plain units.
    static constexpr std::array<ParameterDescriptor, 15> kParams = {{
        // US1 parameters (indices 0..5 — frozen)
        {ParamId{kDelayTime}, "delay_time", ParamUnit::seconds,
         0.001f, 2.0f, 0.3f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kFeedback}, "feedback", ParamUnit::none,
         0.0f, 0.98f, 0.4f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "mix", ParamUnit::none,
         0.0f, 1.0f, 0.35f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kCutoff}, "fb_cutoff", ParamUnit::hz,
         20.0f, 20000.0f, 2000.0f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kResonance}, "fb_resonance", ParamUnit::none,
         0.0f, 1.0f, 0.2f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMode}, "fb_mode", ParamUnit::none,
         0.0f, 2.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 3},
        // US2 delay-time modulation (indices 6..8)
        {ParamId{kDelayModRate}, "delay_mod_rate", ParamUnit::hz,
         0.01f, 20.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kDelayModDepth}, "delay_mod_depth", ParamUnit::none,
         0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kDelayModShape}, "delay_mod_shape", ParamUnit::none,
         0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4},
        // US2 cutoff modulation (indices 9..11)
        {ParamId{kCutoffModRate}, "cutoff_mod_rate", ParamUnit::hz,
         0.01f, 20.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kCutoffModDepth}, "cutoff_mod_depth", ParamUnit::none,
         0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kCutoffModShape}, "cutoff_mod_shape", ParamUnit::none,
         0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4},
        // US2 resonance modulation (indices 12..14)
        {ParamId{kResModRate}, "res_mod_rate", ParamUnit::hz,
         0.01f, 20.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kResModDepth}, "res_mod_depth", ParamUnit::none,
         0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kResModShape}, "res_mod_shape", ParamUnit::none,
         0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4},
    }};

    // Build-time guard: every descriptor in the table is valid.
    static_assert(
        [] {
            for (const ParameterDescriptor& d : kParams)
                if (!isValidDescriptor(d))
                    return false;
            return true;
        }(),
        "ModulatedDelayEffect parameter table violates a descriptor invariant "
        "(max>min; logarithmic => min>0; discrete => count>=2)");

    ModulatedDelayEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Audio stream must be stopped.  Allocates 2.0-second delay buffers per channel.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;

        const int capacity = static_cast<int>(sampleRate_ * 2.0f) + 2;
        for (int ch = 0; ch < numChannels_; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(ch);
            buffers_[idx].assign(static_cast<std::size_t>(capacity), 0.0f);
            delays_[idx].prepare(buffers_[idx].data(), capacity, sampleRate_);
            filters_[idx].init(sampleRate_);
        }

        smoothCoeff_       = 1.0f - std::exp(-1.0f / (sampleRate_ * 0.020f));
        smoothedDelaySecs_ = targetDelaySecs_;

        // Prepare LFOs with sample rate and seed them at their default rates.
        delayLfo_.prepare(sampleRate_);
        delayLfo_.setRate(kParams[kDelayModRate].defaultValue);
        cutoffLfo_.prepare(sampleRate_);
        cutoffLfo_.setRate(kParams[kCutoffModRate].defaultValue);
        resLfo_.prepare(sampleRate_);
        resLfo_.setRate(kParams[kResModRate].defaultValue);

        applyAll();
    }

    // Audio stream must be stopped.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(ch);
            delays_[idx].reset();
            filters_[idx].reset();
        }
        smoothedDelaySecs_ = targetDelaySecs_;
        delayLfo_.reset();
        cutoffLfo_.reset();
        resLfo_.reset();
        applyAll();
    }

    void process(AudioBlock& io) noexcept {
        applyPending();

        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples  = io.numSamples();

        for (int n = 0; n < samples; ++n) {
            // Advance the one-pole delay-time smoother (shared across channels).
            smoothedDelaySecs_ +=
                smoothCoeff_ * (targetDelaySecs_ - smoothedDelaySecs_);

            // Tick all three LFOs exactly once per sample.  Must tick even when
            // depth=0 so phase advances correctly; depth=0 multiplies the value
            // to zero, leaving the signal path unchanged (FR-013).
            const float delayLfoOut  = delayLfo_.tick();
            const float cutoffLfoOut = cutoffLfo_.tick();
            const float resLfoOut    = resLfo_.tick();

            // Effective delay time — DelayLine clamps to valid range (FR-014).
            // depth=0: + 0.0f*range = no change from smoothed base (FR-013).
            const float effectiveDelaySecs =
                smoothedDelaySecs_ + delayLfoOut * delayModDepth_ * kDelayModRangeSecs;
            const float dsamp = effectiveDelaySecs * sampleRate_;

            // Effective cutoff in the log domain.
            // pow(2, lfoVal * 0.0f * octaves) = pow(2, 0.0f) = 1.0f exactly.
            // So effCutoff = cutoffHz_ * 1.0f = cutoffHz_ when depth=0 (FR-013).
            float effCutoff = cutoffHz_ *
                std::pow(2.0f, cutoffLfoOut * cutoffModDepth_ * kCutoffModOctaves);
            {
                const float maxFreq = sampleRate_ * 0.32f;
                if (effCutoff > maxFreq) effCutoff = maxFreq;
                if (effCutoff < 20.0f)  effCutoff = 20.0f;
            }

            // Effective resonance, clamped to [0,1].
            // depth=0: baseRes + 0.0f = baseRes; clamp(baseRes)=baseRes (FR-013).
            float effRes = resonance_ + resLfoOut * resModDepth_ * kResModRange;
            if (effRes < 0.0f) effRes = 0.0f;
            if (effRes > 1.0f) effRes = 1.0f;

            for (int ch = 0; ch < channels; ++ch) {
                const std::size_t idx = static_cast<std::size_t>(ch);
                float* const      x   = io.channel(ch);
                DelayLine&        dl  = delays_[idx];
                SvfPrimitive&     sv  = filters_[idx];

                // Apply modulated filter params per-sample.  When depth=0 these
                // equal the block-level values already set by applyPending().
                sv.setFreq(effCutoff);
                sv.setRes(effRes);

                const float d = dl.readFractional(dsamp);
                const float f = sv.process(d);
                dl.write(x[n] + feedback_ * f);
                x[n] = (1.0f - mix_) * x[n] + mix_ * f;
            }
        }
    }

    // Publish a normalized 0..1 value for a parameter.  Callable from any thread.
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return;
        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

private:
    static constexpr int         kMaxChannels = 8;
    static constexpr std::size_t kNumParams   = 15;

    // Physical modulation ranges (in plain units).
    // kDelayModRangeSecs: ±30 ms peak modulation; musical vibrato at depth=1.
    // kCutoffModOctaves:  ±2 octaves at depth=1 (quarter to four times base freq).
    // kResModRange:       ±0.5 resonance range at depth=1.
    static constexpr float kDelayModRangeSecs = 0.030f;
    static constexpr float kCutoffModOctaves  = 2.0f;
    static constexpr float kResModRange       = 0.5f;

    static std::uint32_t floatBits(float f) noexcept {
        std::uint32_t u = 0;
        std::memcpy(&u, &f, sizeof(u));
        return u;
    }
    static float bitsFloat(std::uint32_t u) noexcept {
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    }

    static SvfMode toMode(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1:  return SvfMode::highpass;
        case 2:  return SvfMode::bandpass;
        case 0:
        default: return SvfMode::lowpass;
        }
    }

    static LfoShape toShape(int index) noexcept {
        switch (index) {
        case 1:  return LfoShape::triangle;
        case 2:  return LfoShape::saw;
        case 3:  return LfoShape::random;
        case 0:
        default: return LfoShape::sine;
        }
    }

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[static_cast<std::size_t>(p)].load(
            std::memory_order_relaxed));
    }

    // Consume any parameter edits published since the last block.  Audio thread only.
    void applyPending() noexcept {
        // US1 base params
        if (pendingDirty_[kDelayTime].exchange(0u, std::memory_order_acquire)) {
            targetDelaySecs_ =
                denormalize(kParams[kDelayTime], pendingValue(kDelayTime));
        }
        if (pendingDirty_[kFeedback].exchange(0u, std::memory_order_acquire)) {
            float fb = denormalize(kParams[kFeedback], pendingValue(kFeedback));
            if (fb >= 1.0f) fb = 0.999f;
            feedback_ = fb;
        }
        if (pendingDirty_[kMix].exchange(0u, std::memory_order_acquire)) {
            mix_ = denormalize(kParams[kMix], pendingValue(kMix));
        }
        if (pendingDirty_[kCutoff].exchange(0u, std::memory_order_acquire)) {
            cutoffHz_ = denormalize(kParams[kCutoff], pendingValue(kCutoff));
            applyCutoff();
        }
        if (pendingDirty_[kResonance].exchange(0u, std::memory_order_acquire)) {
            resonance_ = denormalize(kParams[kResonance], pendingValue(kResonance));
            applyResonance();
        }
        if (pendingDirty_[kMode].exchange(0u, std::memory_order_acquire)) {
            mode_ = toMode(denormalize(kParams[kMode], pendingValue(kMode)));
            applyMode();
        }
        // US2: delay-time modulation
        if (pendingDirty_[kDelayModRate].exchange(0u, std::memory_order_acquire)) {
            delayLfo_.setRate(
                denormalize(kParams[kDelayModRate], pendingValue(kDelayModRate)));
        }
        if (pendingDirty_[kDelayModDepth].exchange(0u, std::memory_order_acquire)) {
            delayModDepth_ =
                denormalize(kParams[kDelayModDepth], pendingValue(kDelayModDepth));
        }
        if (pendingDirty_[kDelayModShape].exchange(0u, std::memory_order_acquire)) {
            delayLfo_.setShape(toShape(static_cast<int>(
                denormalize(kParams[kDelayModShape], pendingValue(kDelayModShape)))));
        }
        // US2: cutoff modulation
        if (pendingDirty_[kCutoffModRate].exchange(0u, std::memory_order_acquire)) {
            cutoffLfo_.setRate(
                denormalize(kParams[kCutoffModRate], pendingValue(kCutoffModRate)));
        }
        if (pendingDirty_[kCutoffModDepth].exchange(0u, std::memory_order_acquire)) {
            cutoffModDepth_ =
                denormalize(kParams[kCutoffModDepth], pendingValue(kCutoffModDepth));
        }
        if (pendingDirty_[kCutoffModShape].exchange(0u, std::memory_order_acquire)) {
            cutoffLfo_.setShape(toShape(static_cast<int>(
                denormalize(kParams[kCutoffModShape], pendingValue(kCutoffModShape)))));
        }
        // US2: resonance modulation
        if (pendingDirty_[kResModRate].exchange(0u, std::memory_order_acquire)) {
            resLfo_.setRate(
                denormalize(kParams[kResModRate], pendingValue(kResModRate)));
        }
        if (pendingDirty_[kResModDepth].exchange(0u, std::memory_order_acquire)) {
            resModDepth_ =
                denormalize(kParams[kResModDepth], pendingValue(kResModDepth));
        }
        if (pendingDirty_[kResModShape].exchange(0u, std::memory_order_acquire)) {
            resLfo_.setShape(toShape(static_cast<int>(
                denormalize(kParams[kResModShape], pendingValue(kResModShape)))));
        }
    }

    float clampedCutoff() const noexcept {
        const float maxFreq = sampleRate_ * 0.32f;
        float       f       = cutoffHz_;
        if (f > maxFreq) f = maxFreq;
        if (f < 20.0f)   f = 20.0f;
        return f;
    }

    void applyCutoff() noexcept {
        const float f = clampedCutoff();
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].setFreq(f);
    }
    void applyResonance() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].setRes(resonance_);
    }
    void applyMode() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].setMode(mode_);
    }
    void applyAll() noexcept {
        applyCutoff();
        applyResonance();
        applyMode();
    }

    std::array<std::vector<float>, kMaxChannels> buffers_{};
    std::array<DelayLine,          kMaxChannels> delays_{};
    std::array<SvfPrimitive,       kMaxChannels> filters_{};

    float sampleRate_  = 48000.0f;
    int   numChannels_ = 0;

    float targetDelaySecs_   = kParams[kDelayTime].defaultValue;
    float smoothedDelaySecs_ = kParams[kDelayTime].defaultValue;
    float smoothCoeff_       = 0.0f;

    float   feedback_  = kParams[kFeedback].defaultValue;
    float   mix_       = kParams[kMix].defaultValue;
    float   cutoffHz_  = kParams[kCutoff].defaultValue;
    float   resonance_ = kParams[kResonance].defaultValue;
    SvfMode mode_      = SvfMode::lowpass;

    // US2: three independent modulation LFOs and their depth scalars.
    Lfo   delayLfo_{};
    Lfo   cutoffLfo_{};
    Lfo   resLfo_{};
    float delayModDepth_  = kParams[kDelayModDepth].defaultValue;   // 0 = off
    float cutoffModDepth_ = kParams[kCutoffModDepth].defaultValue;  // 0 = off
    float resModDepth_    = kParams[kResModDepth].defaultValue;     // 0 = off

    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

} // namespace acfx

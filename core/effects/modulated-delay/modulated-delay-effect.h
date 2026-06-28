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
#include "primitives/svf-primitive.h"

// A delay effect with a State-Variable Filter in the feedback loop.
// Satisfies the Effect contract (no base class, no vtable in the audio path).
// Parameters: delay time, feedback, dry/wet mix, feedback filter cutoff/resonance/mode.
// US1: no LFO modulation or wow/flutter — those are added in later units.
//
// Signal flow per channel, per sample (research Decision 2; post-filter wet tap):
//   smoothedDelaySecs_ glides toward targetDelaySecs_ via a one-pole smoother
//   (research Decision 5; click-free on stepped control edits)
//   dsamp = smoothedDelaySecs_ * sampleRate
//   d = delay[ch].readFractional(dsamp)
//   f = svf[ch].process(d)                  (cutoff clamped < sr/3, FR-003)
//   delay[ch].write(x[n] + feedback * f)    (feedback clamped strictly < 1, FR-010)
//   x[n] = (1-mix)*x[n] + mix*f             (dry/wet blend, FR-002)
//
// Thread-ownership boundary (identical to SvfEffect):
//   setParameter() may be called from ANY thread — it only publishes a lock-free
//   atomic; the audio thread consumes pending values at the top of process().
//   prepare()/reset() mutate state directly and MUST be called while the audio
//   stream is stopped.

namespace acfx {

class ModulatedDelayEffect {
public:
    // Stable parameter ids — dense index into kParams. Append-only in later units.
    enum Param : std::uint8_t {
        kDelayTime = 0,
        kFeedback  = 1,
        kMix       = 2,
        kCutoff    = 3,
        kResonance = 4,
        kMode      = 5
    };

    // Single source of parameter truth (SC-006, FR-022). All values in plain units.
    static constexpr std::array<ParameterDescriptor, 6> kParams = {{
        // delay_time: log Hz over [0.001, 2.0] s; min must be > 0 for log skew.
        {ParamId{kDelayTime}, "delay_time", ParamUnit::seconds,
         0.001f, 2.0f, 0.3f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        // feedback: linear [0, 0.98] — hard maximum strictly below 1.0 (FR-010).
        {ParamId{kFeedback}, "feedback", ParamUnit::none,
         0.0f, 0.98f, 0.4f, ParamSkew::linear, ParamKind::continuous, 0},
        // mix: linear dry/wet [0=dry, 1=wet].
        {ParamId{kMix}, "mix", ParamUnit::none,
         0.0f, 1.0f, 0.35f, ParamSkew::linear, ParamKind::continuous, 0},
        // fb_cutoff: log Hz over [20, 20000]; min > 0 required for log.
        {ParamId{kCutoff}, "fb_cutoff", ParamUnit::hz,
         20.0f, 20000.0f, 2000.0f, ParamSkew::logarithmic, ParamKind::continuous, 0},
        // fb_resonance: linear [0, 1].
        {ParamId{kResonance}, "fb_resonance", ParamUnit::none,
         0.0f, 1.0f, 0.2f, ParamSkew::linear, ParamKind::continuous, 0},
        // fb_mode: discrete {0=LP, 1=HP, 2=BP} — same mapping as SvfEffect.
        {ParamId{kMode}, "fb_mode", ParamUnit::none,
         0.0f, 2.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 3},
    }};

    // Build-time guard: every descriptor in the table is valid. A malformed entry
    // (e.g. log param with min<=0) becomes a compile error, not a runtime NaN.
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

    // Audio stream must be stopped. Allocates 2.0-second delay buffers per channel.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;

        // 2.0-second buffer capacity + 1 guard sample (DelayLine contract).
        const int capacity = static_cast<int>(sampleRate_ * 2.0f) + 2;

        for (int ch = 0; ch < numChannels_; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(ch);
            // Heap allocation here is intentional (prepare is called with stream stopped).
            buffers_[idx].assign(static_cast<std::size_t>(capacity), 0.0f);
            delays_[idx].prepare(buffers_[idx].data(), capacity, sampleRate_);
            filters_[idx].init(sampleRate_);
        }

        // One-pole smoother for delay time: ~20 ms time constant.
        // Formula: smoothed += coeff * (target - smoothed) each audio sample.
        // coeff = 1 - exp(-1 / (sr * T)) with T = 0.020 s.
        smoothCoeff_       = 1.0f - std::exp(-1.0f / (sampleRate_ * 0.020f));
        smoothedDelaySecs_ = targetDelaySecs_;  // start at current target; no pending glide.

        applyAll();
    }

    // Audio stream must be stopped.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(ch);
            delays_[idx].reset();
            filters_[idx].reset();
        }
        smoothedDelaySecs_ = targetDelaySecs_;  // cancel any in-flight glide.
        applyAll();
    }

    void process(AudioBlock& io) noexcept {
        applyPending();  // consume cross-thread parameter edits at block top.

        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples  = io.numSamples();

        for (int n = 0; n < samples; ++n) {
            // Advance the one-pole smoother once per sample (shared across channels).
            smoothedDelaySecs_ +=
                smoothCoeff_ * (targetDelaySecs_ - smoothedDelaySecs_);
            const float dsamp = smoothedDelaySecs_ * sampleRate_;

            for (int ch = 0; ch < channels; ++ch) {
                const std::size_t idx = static_cast<std::size_t>(ch);
                float* const      x   = io.channel(ch);
                DelayLine&        dl  = delays_[idx];
                SvfPrimitive&     sv  = filters_[idx];

                const float d = dl.readFractional(dsamp);         // fractional tap
                const float f = sv.process(d);                    // post-filter wet tap
                dl.write(x[n] + feedback_ * f);                   // write into feedback loop
                x[n] = (1.0f - mix_) * x[n] + mix_ * f;          // dry/wet blend
            }
        }
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the top of the next process() call.
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return;
        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

private:
    static constexpr int         kMaxChannels = 8;
    static constexpr std::size_t kNumParams   = 6;

    // float <-> uint32 bit reinterpretation: a 4-byte memcpy is a register move.
    // Lets the cross-thread atomics be provably lock-free on every target.
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

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[static_cast<std::size_t>(p)].load(
            std::memory_order_relaxed));
    }

    // Consume any parameter edits published since the last block. Audio thread only.
    void applyPending() noexcept {
        if (pendingDirty_[kDelayTime].exchange(0u, std::memory_order_acquire)) {
            targetDelaySecs_ =
                denormalize(kParams[kDelayTime], pendingValue(kDelayTime));
        }
        if (pendingDirty_[kFeedback].exchange(0u, std::memory_order_acquire)) {
            float fb = denormalize(kParams[kFeedback], pendingValue(kFeedback));
            // Clamp strictly below 1.0 to prevent runaway even if a future parameter
            // table change widens the range (FR-010; the descriptor max is 0.98).
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
    }

    // DaisySP's Svf requires cutoff strictly below sampleRate/3; clamp into a safe
    // band just under that bound (and never below the descriptor minimum).
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

    // Per-channel storage: each vector is sized once in prepare(); DelayLine holds
    // a non-owning pointer into it. No resize occurs in process().
    std::array<std::vector<float>, kMaxChannels> buffers_{};
    std::array<DelayLine,          kMaxChannels> delays_{};
    std::array<SvfPrimitive,       kMaxChannels> filters_{};

    float sampleRate_  = 48000.0f;
    int   numChannels_ = 0;

    // One-pole delay-time smoother state (research Decision 5).
    float targetDelaySecs_   = kParams[kDelayTime].defaultValue;
    float smoothedDelaySecs_ = kParams[kDelayTime].defaultValue;
    float smoothCoeff_       = 0.0f;  // computed in prepare()

    // Applied parameter state — mutated only in prepare/reset/applyPending.
    float   feedback_  = kParams[kFeedback].defaultValue;
    float   mix_       = kParams[kMix].defaultValue;
    float   cutoffHz_  = kParams[kCutoff].defaultValue;
    float   resonance_ = kParams[kResonance].defaultValue;
    SvfMode mode_      = SvfMode::lowpass;

    // Cross-thread pending edits: any thread publishes, the audio thread consumes.
    // Stored as float bit-patterns so the atomic is provably lock-free on every target.
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

} // namespace acfx

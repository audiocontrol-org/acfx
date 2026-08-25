#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/modulated-delay/modulated-delay-params.h"
#include "effects/modulated-delay/wow-flutter.h"
#include "primitives/delays/bounded-delay-line.h"
#include "primitives/lofi/bit-crush.h"
#include "primitives/lofi/decimator.h"
#include "primitives/modulation/lfo.h"
#include "primitives/filters/svf-primitive.h"

// A delay effect with a State-Variable Filter in the feedback loop plus three
// independent modulation LFOs (US2) and a wow & flutter stage on the input
// path (US3).  Satisfies the Effect contract (no base class, no vtable in the
// audio path).
//
// Signal flow per channel, per sample (research Decision 2; post-filter wet tap):
//   x' = wowFlutter_[ch].processSample(x[n])   (US3 tape instability, FR-017/020)
//   smoothedDelaySecs_ glides toward targetDelaySecs_ via a one-pole smoother
//   effectiveDelaySecs = smoothedDelaySecs_ + delayLfo.tick() * delayDepth * kDelayModRangeSecs
//   dsamp = effectiveDelaySecs * sampleRate                    (clamped by DelayLine, FR-014)
//   effCutoff = baseCutoff * pow(2, cutoffLfo.tick() * cutoffDepth * kCutoffModOctaves)
//   effRes    = clamp(baseRes + resLfo.tick() * resDepth * kResModRange, 0, 1)
//   d = delay[ch].readFractional(dsamp)
//   f = svf[ch].process(d)          (effCutoff / effRes applied per-sample)
//   delay[ch].write(x' + feedback * f)
//   x[n] = (1-mix)*x' + mix*f
//
// Depth-zero invariants:
//   FR-013: US2 mod depths at 0 → signal path unchanged from US1 (exact, not approx).
//   FR-019: US3 wow+flutter depths both 0 → wowFlutter passthrough, x' == x.
//
// Thread-ownership boundary: setParameter() may be called from ANY thread; the
// audio thread consumes pending values at the top of process().
// prepare()/reset() mutate state directly and MUST be called while stopped.

namespace acfx {

// Templated on three compile-time policy parameters (design "Bounding
// foundation"): per-channel delay-line capacity, per-sample storage type, and
// maximum channel count. All storage is in-object std::array (via
// BoundedDelayLine), so the effect is heap-free and can boot on the STM32F446RE
// (TASK-34). Defaulting all three preserves today's behavior:
// ModulatedDelayEffect<> == the 2 s @ 48 kHz, float, 8-channel effect; for
// Sample = float the delay line is bit-identical to the old heap DelayLine, so
// the clean float path is numerically unchanged (asserted bit-exact against a
// captured golden vector, design Decisions 12a). NOTE: the ~3 MB default object
// must be heap-allocated (desktop/plugin/Daisy do); a stack local overflows.
template <std::size_t MaxDelaySamples = 96000,
          typename    Sample          = float,
          std::size_t MaxChannels      = 8>
class ModulatedDelayEffect {
public:
    // Param identities + descriptor table live in modulated-delay-params.h
    // (ModDelayParam is a SCOPED enum there, so no bare enumerator leaks into
    // acfx::). Re-expose the dense-index names here as integer-typed constexpr
    // constants — not C++20 `using enum` (core must also build as C++17/Teensy),
    // integer-typed so kParams[kX] / ParamId{p} / std::array<Param,N> keep
    // compiling without a per-site cast. Fx::Param / Fx::kX stay source-compatible.
    using Param = std::uint8_t;
    static constexpr Param kDelayTime      = modDelayId(ModDelayParam::kDelayTime);
    static constexpr Param kFeedback       = modDelayId(ModDelayParam::kFeedback);
    static constexpr Param kMix            = modDelayId(ModDelayParam::kMix);
    static constexpr Param kCutoff         = modDelayId(ModDelayParam::kCutoff);
    static constexpr Param kResonance      = modDelayId(ModDelayParam::kResonance);
    static constexpr Param kMode           = modDelayId(ModDelayParam::kMode);
    static constexpr Param kDelayModRate   = modDelayId(ModDelayParam::kDelayModRate);
    static constexpr Param kDelayModDepth  = modDelayId(ModDelayParam::kDelayModDepth);
    static constexpr Param kDelayModShape  = modDelayId(ModDelayParam::kDelayModShape);
    static constexpr Param kCutoffModRate  = modDelayId(ModDelayParam::kCutoffModRate);
    static constexpr Param kCutoffModDepth = modDelayId(ModDelayParam::kCutoffModDepth);
    static constexpr Param kCutoffModShape = modDelayId(ModDelayParam::kCutoffModShape);
    static constexpr Param kResModRate     = modDelayId(ModDelayParam::kResModRate);
    static constexpr Param kResModDepth    = modDelayId(ModDelayParam::kResModDepth);
    static constexpr Param kResModShape    = modDelayId(ModDelayParam::kResModShape);
    static constexpr Param kWowRate        = modDelayId(ModDelayParam::kWowRate);
    static constexpr Param kWowDepth       = modDelayId(ModDelayParam::kWowDepth);
    static constexpr Param kFlutterRate    = modDelayId(ModDelayParam::kFlutterRate);
    static constexpr Param kFlutterDepth   = modDelayId(ModDelayParam::kFlutterDepth);
    static constexpr Param kLofiRate       = modDelayId(ModDelayParam::kLofiRate);
    static constexpr Param kLofiBits       = modDelayId(ModDelayParam::kLofiBits);

    // Option labels for discrete parameters (single source of truth for adapters).
    static constexpr auto& kModeLabels  = acfx::kModDelayModeLabels;
    static constexpr auto& kShapeLabels = acfx::kModDelayShapeLabels;
    static constexpr auto& kRateLabels  = acfx::kModDelayRateLabels;
    static constexpr auto& kBitsLabels  = acfx::kModDelayBitsLabels;

    // Single source of parameter truth (SC-006, FR-022). All values in plain
    // units. The descriptor-validity static_assert lives beside the table.
    static constexpr auto& kParams = kModulatedDelayParams;

    ModulatedDelayEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Longest realizable delay in physical seconds at the current rate D: the
    // buffer holds decimated-rate samples, so its span in seconds scales with D
    // (design Approach A / OQ1). The effective delay time is clamped to this.
    float maxDelaySeconds() const noexcept {
        return static_cast<float>(delays_[0].capacity())
             * static_cast<float>(decimator_.divisor()) / sampleRate_;
    }

    // Audio stream must be stopped.  Prepares the in-object (heap-free) delay
    // lines: capacity is the 2.0 s @ sampleRate request, clamped to MaxDelaySamples.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_  = static_cast<float>(ctx.sampleRate);
        const int maxCh = static_cast<int>(MaxChannels);
        numChannels_ = ctx.numChannels < maxCh ? ctx.numChannels : maxCh;

        const int requested = static_cast<int>(sampleRate_ * 2.0f) + 2;
        const int capacity  =
            requested < static_cast<int>(MaxDelaySamples)
                ? requested
                : static_cast<int>(MaxDelaySamples);
        for (int ch = 0; ch < numChannels_; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(ch);
            delays_[idx].prepare(capacity, sampleRate_);
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

        // US3: prepare wow & flutter stage (allocates short per-channel buffers).
        wowFlutter_.prepare(sampleRate_, numChannels_);
        wowFlutter_.setWowRate(kParams[kWowRate].defaultValue);
        wowFlutter_.setFlutterRate(kParams[kFlutterRate].defaultValue);
        wowFlutter_.setWowDepth(kParams[kWowDepth].defaultValue);       // 0.0 = bypass
        wowFlutter_.setFlutterDepth(kParams[kFlutterDepth].defaultValue); // 0.0 = bypass

        heldWet_.fill(0.0f);  // no held wet sample yet (sample-and-hold state)

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
        wowFlutter_.reset();
        heldWet_.fill(0.0f);
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

            // Tick all three US2 LFOs exactly once per sample.  Must tick even
            // when depth=0 so phase advances correctly; depth=0 multiplies the
            // value to zero, leaving the signal path unchanged (FR-013).
            const float delayLfoOut  = delayLfo_.tick();
            const float cutoffLfoOut = cutoffLfo_.tick();
            const float resLfoOut    = resLfo_.tick();

            // US3: tick wow & flutter LFOs once per sample (shared across
            // channels — modulation is correlated/identical per the spec
            // Assumptions).  Returns zero when both depths are 0 (FR-019).
            const float wfDisplacement = wowFlutter_.tickModulation();

            // Effective delay time in physical seconds (FR-013: depth=0 => no
            // change from the smoothed base). Clamped to the current rate's
            // realizable maximum (OQ1: physical meaning; range coupled to D). At
            // the clean settings the effective time is far below the clamp, so
            // this is a no-op there.
            float effectiveDelaySecs =
                smoothedDelaySecs_ + delayLfoOut * delayModDepth_ * kDelayModRangeSecs;
            const float maxSecs = maxDelaySeconds();
            if (effectiveDelaySecs > maxSecs) effectiveDelaySecs = maxSecs;

            // Convert physical seconds to INTERNAL samples: the delay line stores
            // decimated-rate samples at 48 kHz / D. At D=1, sampleRate_/1.0f ==
            // sampleRate_ exactly, so dsampInternal == effectiveDelaySecs *
            // sampleRate_ bit-for-bit — the pre-lo-fi arithmetic is preserved.
            // DelayLine still clamps to [0, capacity-1] (FR-014).
            const float divisorF      = static_cast<float>(decimator_.divisor());
            const float dsampInternal = effectiveDelaySecs * (sampleRate_ / divisorF);

            // Guard: skip the per-sample transcendental cost (std::pow, setFreq
            // sinf+powf, setRes powf) when depth==0.  applyPending() (and the
            // depth->0 restore path) keep the base coefficient current so no
            // per-sample work is needed in the common case.  (I-1 fix)
            const bool modCutoff = (cutoffModDepth_ != 0.0f);
            const bool modRes    = (resModDepth_    != 0.0f);

            float effCutoff = 0.0f;
            if (modCutoff) {
                effCutoff = cutoffHz_ *
                    std::pow(2.0f, cutoffLfoOut * cutoffModDepth_ * kCutoffModOctaves);
                const float maxFreq = sampleRate_ * 0.32f;
                if (effCutoff > maxFreq) effCutoff = maxFreq;
                if (effCutoff < 20.0f)  effCutoff = 20.0f;
            }

            float effRes = 0.0f;
            if (modRes) {
                effRes = resonance_ + resLfoOut * resModDepth_ * kResModRange;
                if (effRes < 0.0f) effRes = 0.0f;
                if (effRes > 1.0f) effRes = 1.0f;
            }

            // The decimation phase is GLOBAL (shared across channels): decide the
            // internal tick ONCE per output sample. At D=1 isTick() is true every
            // sample, so the wet loop runs every sample and the per-channel hold
            // is a no-op — the pre-lo-fi behaviour exactly.
            const bool tick = decimator_.isTick();

            for (int ch = 0; ch < channels; ++ch) {
                const std::size_t idx = static_cast<std::size_t>(ch);
                float* const      x   = io.channel(ch);

                // US3: apply wow & flutter to the input before the main delay
                // (FR-020). Both depths=0 => exact passthrough (FR-019). This is
                // the full-rate (48 kHz) dry path and the decimation IN sample.
                const float xPrime =
                    wowFlutter_.processSample(x[n], ch, wfDisplacement);

                if (tick) {
                    auto&         dl = delays_[idx];
                    SvfPrimitive& sv = filters_[idx];

                    if (modCutoff) sv.setFreq(effCutoff);
                    if (modRes)    sv.setRes(effRes);

                    const float d = dl.readFractional(dsampInternal);
                    const float f = sv.process(d);
                    // Bit-crush INSIDE the feedback loop (design "Bit-crush +
                    // storage"): recursive/grittier the longer a repeat rings. At
                    // B=16 crushToGrid is identity, so the float path stays
                    // bit-exact and the int16 policy shows only its storage floor.
                    dl.write(crushToGrid(xPrime + feedback_ * f, crushBits_));
                    heldWet_[idx] = f;   // sample-and-hold reconstruction OUT
                }

                // Sample-and-hold wet reconstruction + dry/wet MIX at 48 kHz.
                x[n] = (1.0f - mix_) * xPrime + mix_ * heldWet_[idx];
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
    static constexpr std::size_t kNumParams   = kModDelayNumParams;  // 21

    // Wow/flutter scratch buffer size (compile-time): nominal(10 ms) +
    // 2*range(2*5 ms) + guard, sized for the highest prepared sample rate so the
    // wow range is never truncated. 0.020 s * 96 kHz + guard ≈ 1928 samples.
    static constexpr float       kWowSpanSecs      = 0.020f;
    static constexpr float       kWowMaxSampleRate = 96000.0f;
    static constexpr std::size_t kWowSamples =
        static_cast<std::size_t>(kWowSpanSecs * kWowMaxSampleRate) + 8;
    static_assert(kWowSamples >= static_cast<std::size_t>(kWowSpanSecs * 48000.0f) + 4,
                  "wow/flutter buffer too small for the 48 kHz device rate");

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
            applyCutoff();  // depth->0: immediately restore base coefficient (I-1)
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
            applyResonance();  // depth->0: immediately restore base coefficient (I-1)
        }
        if (pendingDirty_[kResModShape].exchange(0u, std::memory_order_acquire)) {
            resLfo_.setShape(toShape(static_cast<int>(
                denormalize(kParams[kResModShape], pendingValue(kResModShape)))));
        }
        // US3: wow & flutter parameters
        if (pendingDirty_[kWowRate].exchange(0u, std::memory_order_acquire)) {
            wowFlutter_.setWowRate(
                denormalize(kParams[kWowRate], pendingValue(kWowRate)));
        }
        if (pendingDirty_[kWowDepth].exchange(0u, std::memory_order_acquire)) {
            wowFlutter_.setWowDepth(
                denormalize(kParams[kWowDepth], pendingValue(kWowDepth)));
        }
        if (pendingDirty_[kFlutterRate].exchange(0u, std::memory_order_acquire)) {
            wowFlutter_.setFlutterRate(
                denormalize(kParams[kFlutterRate], pendingValue(kFlutterRate)));
        }
        if (pendingDirty_[kFlutterDepth].exchange(0u, std::memory_order_acquire)) {
            wowFlutter_.setFlutterDepth(
                denormalize(kParams[kFlutterDepth], pendingValue(kFlutterDepth)));
        }
        // Lo-fi: rate divisor. index 0..3 -> D = 1<<index. setDivisor() also
        // RESETS the decimator phase — this IS the tape-speed reinterpret trigger
        // (design "Live rate-change semantics"): the buffer is retained and the
        // new D takes effect on an internal tick.
        if (pendingDirty_[kLofiRate].exchange(0u, std::memory_order_acquire)) {
            const int index = static_cast<int>(
                denormalize(kParams[kLofiRate], pendingValue(kLofiRate)));
            decimator_.setDivisor(1 << index);
        }
        // Lo-fi: effective bit depth. index 0..4 -> B in {16,12,8,6,4}; B=16 is
        // the crushToGrid bypass, so the clean end stays full-resolution.
        if (pendingDirty_[kLofiBits].exchange(0u, std::memory_order_acquire)) {
            const int index = static_cast<int>(
                denormalize(kParams[kLofiBits], pendingValue(kLofiBits)));
            crushBits_ = kModDelayBitsTable[static_cast<std::size_t>(index)];
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

    std::array<BoundedDelayLine<Sample, MaxDelaySamples>, MaxChannels> delays_{};
    std::array<SvfPrimitive,                              MaxChannels> filters_{};

    // Lo-fi layer: SHARED decimator (decimation phase is global) + per-channel
    // held wet (sample-and-hold between ticks). Defaults D=1/B=16 are the clean
    // settings — tick every sample, crush bypassed, wet path full-resolution.
    SampleHoldDecimator          decimator_{};
    int                          crushBits_ = 16;
    std::array<float, MaxChannels> heldWet_{};

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

    // US3: wow & flutter stage on the input path (FR-017..FR-021).
    WowFlutterStage<MaxChannels, kWowSamples> wowFlutter_{};

    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

} // namespace acfx

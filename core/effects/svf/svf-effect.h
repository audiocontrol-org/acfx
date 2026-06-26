#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "primitives/svf-primitive.h"

// The State-Variable Filter effect — the one effect proving the cross-platform
// spine. Satisfies the Effect contract (no base class, no vtable in the hot
// path). One constexpr parameter table (cutoff/resonance/mode) drives every
// adapter (FR-003). Per-channel DaisySP Svf state, allocation-free process.
//
// Thread-ownership boundary: setParameter() may be called from ANY thread (a UI
// loop, a MIDI callback, an MCU main loop). It only publishes an atomic pending
// normalized value; the audio thread consumes pending values at the top of
// process(). So the actual filter coefficients are mutated on exactly one thread,
// with no torn-coefficient data race against process() — an invariant the core
// encodes itself rather than asking every adapter to honor.

namespace acfx {

class SvfEffect {
public:
    // Stable parameter ids — the dense index into kParams.
    enum Param : std::uint8_t { kCutoff = 0, kResonance = 1, kMode = 2 };

    // The single source of parameter truth (SC-006). cutoff: log Hz; resonance:
    // linear 0..1; mode: discrete {lowpass, highpass, bandpass}.
    static constexpr std::array<ParameterDescriptor, 3> kParams = {{
        {ParamId{kCutoff}, "cutoff", ParamUnit::hz, 20.0f, 20000.0f, 1000.0f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kResonance}, "resonance", ParamUnit::none, 0.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMode}, "mode", ParamUnit::none, 0.0f, 2.0f, 0.0f,
         ParamSkew::linear, ParamKind::discrete, 3},
    }};

    SvfEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingNorm_[i].store(0.0f, std::memory_order_relaxed);
            pendingDirty_[i].store(false, std::memory_order_relaxed);
        }
    }

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].init(sampleRate_);
        applyAll();
    }

    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].reset();
        applyAll();
    }

    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            SvfPrimitive& f = filters_[static_cast<std::size_t>(ch)];
            for (int n = 0; n < samples; ++n)
                x[n] = f.process(x[n]);
        }
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the next process() (no immediate filter
    // mutation here — that keeps coefficient updates single-threaded).
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return; // out-of-range id: a programming error; no silent state change
        pendingNorm_[i].store(normalized, std::memory_order_relaxed);
        pendingDirty_[i].store(true, std::memory_order_release);
    }

private:
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kNumParams = 3;

    static SvfMode toMode(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1:
            return SvfMode::highpass;
        case 2:
            return SvfMode::bandpass;
        case 0:
        default:
            return SvfMode::lowpass;
        }
    }

    // Apply any parameter values published since the last block (audio thread).
    void applyPending() noexcept {
        if (pendingDirty_[kCutoff].exchange(false, std::memory_order_acquire)) {
            cutoffHz_ = denormalize(kParams[kCutoff],
                                    pendingNorm_[kCutoff].load(std::memory_order_relaxed));
            applyCutoff();
        }
        if (pendingDirty_[kResonance].exchange(false, std::memory_order_acquire)) {
            resonance_ = denormalize(kParams[kResonance],
                                     pendingNorm_[kResonance].load(std::memory_order_relaxed));
            applyResonance();
        }
        if (pendingDirty_[kMode].exchange(false, std::memory_order_acquire)) {
            mode_ = toMode(denormalize(kParams[kMode],
                                       pendingNorm_[kMode].load(std::memory_order_relaxed)));
            applyMode();
        }
    }

    // DaisySP's Svf requires cutoff strictly below sampleRate/3; clamp into a safe
    // band just under that bound (and never below the descriptor minimum).
    float clampedCutoff() const noexcept {
        const float maxFreq = sampleRate_ * 0.32f;
        float f = cutoffHz_;
        if (f > maxFreq)
            f = maxFreq;
        if (f < 20.0f)
            f = 20.0f;
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

    std::array<SvfPrimitive, kMaxChannels> filters_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;

    // Applied parameter state — owned by the audio thread (read/written only in
    // prepare/reset/applyPending).
    float cutoffHz_ = kParams[kCutoff].defaultValue;
    float resonance_ = kParams[kResonance].defaultValue;
    SvfMode mode_ = SvfMode::lowpass;

    // Cross-thread pending edits: any thread publishes, the audio thread consumes.
    std::array<std::atomic<float>, kNumParams> pendingNorm_;
    std::array<std::atomic<bool>, kNumParams> pendingDirty_;
};

} // namespace acfx

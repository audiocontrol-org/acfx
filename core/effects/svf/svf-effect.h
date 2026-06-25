#pragma once

#include <array>
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

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[ch].init(sampleRate_);
        applyAll();
    }

    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[ch].reset();
        applyAll();
    }

    void process(AudioBlock& io) noexcept {
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            SvfPrimitive& f = filters_[ch];
            for (int n = 0; n < samples; ++n)
                x[n] = f.process(x[n]);
        }
    }

    // Normalized 0..1 in; mapped to plain units via the matching descriptor.
    void setParameter(ParamId id, float normalized) noexcept {
        switch (id.value) {
        case kCutoff:
            cutoffHz_ = denormalize(kParams[kCutoff], normalized);
            applyCutoff();
            break;
        case kResonance:
            resonance_ = denormalize(kParams[kResonance], normalized);
            applyResonance();
            break;
        case kMode:
            mode_ = toMode(denormalize(kParams[kMode], normalized));
            applyMode();
            break;
        default:
            break; // out-of-range id: a programming error; no silent state change
        }
    }

private:
    static constexpr int kMaxChannels = 8;

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
            filters_[ch].setFreq(f);
    }
    void applyResonance() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[ch].setRes(resonance_);
    }
    void applyMode() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[ch].setMode(mode_);
    }
    void applyAll() noexcept {
        applyCutoff();
        applyResonance();
        applyMode();
    }

    std::array<SvfPrimitive, kMaxChannels> filters_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;

    // Current plain-unit parameter state (seeded from the descriptor defaults).
    float cutoffHz_ = kParams[kCutoff].defaultValue;
    float resonance_ = kParams[kResonance].defaultValue;
    SvfMode mode_ = SvfMode::lowpass;
};

} // namespace acfx

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "primitives/nonlinear/hysteresis.h"
#include "primitives/oversampling/oversampler.h"

// TapeDynamicsCore — the RT-safe per-channel composition kernel (FR-008/009).
//
// Composes the shipped primitives (Hysteresis, Oversampler) into the full
// per-sample tape dynamics signal chain with optional trim: input gain →
// Oversampler<Factor> (JA/hysteresis step) → optional trim → dry/wet mix →
// output trim. Platform-independent (Constitution IV): no host-framework or
// embedded-vendor headers. RT-safe by construction (Constitution VI): every
// member is a value (no heap), all coefficient work lives in prepare()/setters,
// and process() is allocation-free, lock-free, and bounded.
//
// Composition (data-model.md "Entity — TapeDynamicsCore"):
//   oversampler   Oversampler<Factor>  runs Hysteresis at the oversampled rate
//   hysteresis    Hysteresis           the Jiles-Atherton magnetics core
//   (optional)    EnvelopeFollower     trim ballistics (if trim.enabled)
//   (optional)    GainComputer         trim gain mapping (if trim.enabled)
//
// The template Factor parameter (2/4/8/16) is chosen at effect level based on
// the oversampling parameter; TapeDynamicsEffect instantiates all four cores
// and dispatches at process() time (data-model.md).

namespace acfx {

template <int Factor>
class TapeDynamicsCore {
public:
    // Prepare for a sample rate and channel count; sizes/configures all
    // composed primitives and caches macro→physics coefficients (drive gain,
    // Ms, k, mix, output). No audio-path work (Constitution VI).
    void prepare(double sampleRate, int channels) noexcept {
        sampleRate_ = sampleRate;
        numChannels_ = channels;
        if (numChannels_ < 1)
            numChannels_ = 1;
        if (numChannels_ > kMaxChannels)
            numChannels_ = kMaxChannels;

        for (int ch = 0; ch < numChannels_; ++ch) {
            oversampler_[ch].init(sampleRate_);
            hysteresis_[ch].prepare(sampleRate_);
        }

        // Cache current parameter state pushed into composed units
        // (later tasks fill in the actual coefficient conversions).
    }

    // Advance one audio sample on a channel; signal flow:
    // x · drive → Oversampler<Factor>::process(·, JA step) → [trim?] →
    // mix(dry, wet) · output. Returns the processed output sample.
    [[nodiscard]] float processSample(float x, int ch) noexcept {
        if (ch < 0 || ch >= numChannels_)
            return x;

        // Stub: just return the input for now (unity passthrough).
        // Later tasks implement: drive scaling, oversampler dispatch,
        // hysteresis processing, trim, mix, output gain.
        return x;
    }

    // Clear all composed state (filter memories, magnetization, etc.)
    // without discarding applied parameter state.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            oversampler_[ch].reset();
            hysteresis_[ch].reset();
        }
    }

private:
    static constexpr int kMaxChannels = 32;

    double sampleRate_ = 48000.0;
    int numChannels_ = 1;

    std::array<Oversampler<Factor>, kMaxChannels> oversampler_;
    std::array<Hysteresis, kMaxChannels> hysteresis_;

    // Cached macro parameters (to be filled in later tasks).
    float drive_ = 1.0f;
    float driveLinear_ = 1.0f;
    float Ms_ = 1.0f;
    float k_ = 1.0f;
    float mix_ = 1.0f;
    float output_ = 1.0f;
    bool trimEnabled_ = false;
};

} // namespace acfx

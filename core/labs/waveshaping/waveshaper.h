#pragma once

#include <cstdint>
#include "labs/waveshaping/waveshaper-shapes.h"

// Stateful waveshaper wrapper — applies drive/bias, a selected memoryless shape,
// a wrapper-owned DC-blocker, and optional gain compensation.
//
// Signal chain (research.md Decision 2):
//   u = drive * x + bias
//   y = shape(u)          ← pure, memoryless; dispatched in T010
//   y = dcBlock(y)        ← one-pole HP; state lives here (FR-008)
//   y = gainCompFactor * y  (if gainComp_ is true)
//
// Pre-graduation location: core/labs/waveshaping/
// Post-graduation target:  core/primitives/nonlinear/   (T024)

namespace acfx {

class Waveshaper {
public:
    // Prepare for a sample rate. Builds LUT if evaluation_ == lut (T017/US3).
    // Clears DC-block state (xPrev, yPrev → 0).
    void init(float sampleRate) noexcept;

    // Select which transfer function to apply.
    void setShape(Shape shape) noexcept;

    // Select closed-form or LUT evaluation backend.
    // A backend change takes effect on the next init() call.
    void setEvaluation(Evaluation evaluation) noexcept;

    // Pre-gain applied to the input before bias: u = drive * x + bias.
    void setDrive(float drive) noexcept;

    // Fixed DC offset applied after drive (FR-007).
    // Bias-induced DC is removed by the wrapper's DC-blocker.
    void setBias(float bias) noexcept;

    // Enable/disable auto-makeup gain toward unity output level (FR-010).
    // The factor is computed in T011; scaffolded here.
    void setGainCompensation(bool on) noexcept;

    // Clear DC-block state (xPrev, yPrev → 0); keep LUT and parameter state (FR-009).
    void reset() noexcept;

    // Process one sample through the full signal chain.
    // RT-safe: noexcept, no heap allocation, bounded work (FR-020).
    // TODO(T010): wire in shape dispatch and full staging skeleton.
    float process(float x) noexcept;

private:
    // -----------------------------------------------------------------------
    // DC-blocker — wrapper-owned (FR-008); NEVER in acfx::shape::*
    //
    // One-pole high-pass (research.md Decision 3):
    //   y[n] = x[n] - x[n-1] + R * y[n-1]
    //
    // R near 1 → very low cutoff; removes bias-induced DC without coloring
    // the audio band.  "Fixed vs parameter" is an Open Question (research.md
    // Decision 3); fixed at construction for now.
    //
    // Typical cutoff with R = 0.995 at fs = 48 kHz:
    //   f_c ≈ (1 - R) * fs / (2π) ≈ 38 Hz
    // -----------------------------------------------------------------------
    static constexpr float kDcR = 0.995f;
    float dcXPrev_ = 0.0f;
    float dcYPrev_ = 0.0f;

    // Apply the one-pole DC-blocker to a single sample.
    // All state (dcXPrev_, dcYPrev_) is owned by this wrapper — no free
    // function in acfx::shape::* holds or sees this state (FR-008).
    float dcBlock(float x) noexcept {
        const float y = x - dcXPrev_ + kDcR * dcYPrev_;
        dcXPrev_      = x;
        dcYPrev_      = y;
        return y;
    }

    // -----------------------------------------------------------------------
    // Gain compensation — wrapper-owned (FR-010)
    //
    // gainCompFactor_: makeup factor applied as the last stage of the chain.
    // Computation law (derived from shape + drive) is finalized in T011.
    // Scaffolded here; factor defaults to unity.
    // -----------------------------------------------------------------------
    bool  gainComp_       = false;
    float gainCompFactor_ = 1.0f;  // TODO(T011): compute from shape + drive

    // -----------------------------------------------------------------------
    // Signal-chain parameters
    // -----------------------------------------------------------------------
    Shape      shape_      = Shape::tanh;
    Evaluation evaluation_ = Evaluation::closedForm;
    float      drive_      = 1.0f;
    float      bias_       = 0.0f;
    float      sampleRate_ = 48000.0f;

    // -----------------------------------------------------------------------
    // LUT — optional, built in init() (FR-011)
    // TODO(T017/US3): declare fixed-size table storage once the LUT design
    //   (table size, domain, interpolation scheme) is finalised in US3.
    // -----------------------------------------------------------------------
};

} // namespace acfx

#pragma once

#include <algorithm>
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
    // Clears DC-block state (xPrev, yPrev → 0).  Recomputes the gain-comp factor.
    // RT-note: not on the audio path; LUT build (US3) will be the only heavy
    // work and lives here, never in process().
    void init(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        dcXPrev_    = 0.0f;
        dcYPrev_    = 0.0f;
        updateGainCompFactor();
        // TODO(T017/US3): if evaluation_ == Evaluation::lut, build the table
        // here. closedForm is the only backend implemented in US1.
    }

    // Select which transfer function to apply.
    void setShape(Shape shape) noexcept { shape_ = shape; }

    // Select closed-form or LUT evaluation backend.
    // A backend change takes effect on the next init() call.
    void setEvaluation(Evaluation evaluation) noexcept { evaluation_ = evaluation; }

    // Pre-gain applied to the input before bias: u = drive * x + bias.
    // Recomputes gainCompFactor_ (off the audio path).
    void setDrive(float drive) noexcept {
        drive_ = drive;
        updateGainCompFactor();
    }

    // Fixed DC offset applied after drive (FR-007).
    // Bias-induced DC is removed by the wrapper's DC-blocker.
    void setBias(float bias) noexcept { bias_ = bias; }

    // Enable/disable auto-makeup gain toward unity output level (FR-010).
    // The factor is computed in T011; scaffolded here.
    void setGainCompensation(bool on) noexcept { gainComp_ = on; }

    // Clear DC-block state (xPrev, yPrev → 0); keep LUT and parameter state (FR-009).
    void reset() noexcept {
        dcXPrev_ = 0.0f;
        dcYPrev_ = 0.0f;
    }

    // Process one sample through the full signal chain (research.md Decision 2):
    //   u = drive * x + bias
    //   y = shape(u)            ← memoryless dispatch (shapeDispatch)
    //   y = dcBlock(y)          ← wrapper-owned one-pole HP (FR-008)
    //   y = gainCompFactor * y  ← only when gainComp_ is on (FR-010)
    //
    // RT-safe: noexcept, no heap allocation, no locks, bounded work — the shape
    // dispatch is a bounded switch (FR-020).
    float process(float x) noexcept {
        const float u = drive_ * x + bias_;
        float y = shapeDispatch(u);
        y = dcBlock(y);
        if (gainComp_) {
            y *= gainCompFactor_;  // factor finalized in T011 (unity for now)
        }
        return y;
    }

private:
    // -----------------------------------------------------------------------
    // Memoryless shape dispatch — selects a pure acfx::shape::* function.
    //
    // US1 (T009) implements only tanh, cubicSoft, and hardClip; the remaining
    // catalog entries (arctan, algebraic, softKnee, chebyshev, biasedAsym,
    // diodeCurve, sineFold, triangleFold) are declared but not yet defined —
    // CALLING them would fail to link. The full catalog dispatch is wired in
    // T014 (US2). Until then the switch CALLS only the three implemented shapes
    // and routes the not-yet-wired enum members to tanhShape via the default.
    // This is a temporary stub for unimplemented branches (each carries an
    // explicit TODO(T014)), not a runtime data fallback: every shape a US1
    // caller can exercise (tanh, hardClip; cubicSoft) is dispatched exactly.
    // -----------------------------------------------------------------------
    float shapeDispatch(float u) const noexcept {
        switch (shape_) {
            case Shape::tanh:      return shape::tanhShape(u);
            case Shape::cubicSoft: return shape::cubicSoftClip(u);
            case Shape::hardClip:  return shape::hardClip(u);
            // TODO(T014): wire arctan, algebraic, softKnee, chebyshev,
            //   biasedAsym, diodeCurve, sineFold, triangleFold once their
            //   acfx::shape::* bodies (T009 remainder) are defined.
            default:               return shape::tanhShape(u);
        }
    }

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
    // Law (T011, research.md Decision 2):  DRIVE-COMPENSATION
    //   gainCompFactor = 1 / max(drive, kGainCompEps)
    //
    // Rationale: the common waveshaping curves have small-signal slope ≈ 1
    // at the origin (tanh'(0) = 1, hardClip'(0) = 1, etc.).  For a
    // low-level input x the signal chain computes:
    //   u = drive * x,  y = shape(u) ≈ u = drive * x
    // Multiplying by 1/drive restores y ≈ x ("unity makeup").
    //
    // With drive = 4 and kAmplitude = 0.05 (the direction test values):
    //   gainComp OFF:  output RMS ≈ drive * inputRms  (amplified 4×)
    //   gainComp ON:   output RMS ≈ inputRms          (returned to unity)
    //
    // Divide-by-zero guard: kGainCompEps (1e-6f) clamps the divisor.  A
    // drive of 0 silences the input and has no meaningful makeup; this
    // guard is documented behaviour, not a silent data fallback.
    //
    // updateGainCompFactor() is called from setDrive() and init() — never
    // from process() — so the audio path sees only a cached float multiply.
    // -----------------------------------------------------------------------
    static constexpr float kGainCompEps = 1.0e-6f;

    bool  gainComp_       = false;
    float gainCompFactor_ = 1.0f;

    void updateGainCompFactor() noexcept {
        const float d   = (drive_ > kGainCompEps) ? drive_ : kGainCompEps;
        gainCompFactor_ = 1.0f / d;
    }

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

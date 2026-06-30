#pragma once

#include <algorithm>
#include <cstdint>
#include "labs/waveshaping/waveshaper-shapes.h"
#include "labs/waveshaping/waveshaper-lut.h"

// Stateful waveshaper wrapper — applies drive/bias, a selected memoryless shape,
// a wrapper-owned DC-blocker, and optional gain compensation.
//
// Signal chain (research.md Decision 2):
//   u = drive * x + bias
//   y = shape(u)          ← memoryless; shapeValue() selects the backend
//   y = dcBlock(y)        ← one-pole HP; state lives here (FR-008)
//   y = gainCompFactor * y  (if gainComp_ is true)
//
// Evaluation backends (Evaluation enum, FR-011):
//   closedForm — calls shapeValue(u) directly (exact, always available)
//   lut        — looks up from WaveshaperLut built during init()
//
// Rebuild-on-init semantics (FR-011, waveshaper-api.md):
//   The LUT is built inside init() — never inside process().  If setShape() or
//   setEvaluation() is called after init(), the change takes effect on the NEXT
//   init() call.  This is the CONTRACT: switching to Evaluation::lut or changing
//   the Shape requires a re-init() to refresh the table.  process() never rebuilds
//   the table; this ensures process() remains RT-safe (no heavy work on audio path).
//
// Pre-graduation location: core/labs/waveshaping/
// Post-graduation target:  core/primitives/nonlinear/   (T024)

namespace acfx {

class Waveshaper {
public:
    // Prepare for a sample rate. Clears DC-block state (xPrev, yPrev → 0).
    // Recomputes the gain-comp factor.  Builds the LUT if evaluation_ == lut
    // (FR-011).  This is the ONLY place where the LUT is built — never inside
    // process().  Shape or evaluation changes after init() take effect on the
    // next init() call (rebuild-on-init semantics; see file header comment).
    //
    // RT-note: not on the audio path.  LUT build (WaveshaperLut::build) is the
    // only non-trivial work and lives here, never in process().
    void init(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        dcXPrev_    = 0.0f;
        dcYPrev_    = 0.0f;
        updateGainCompFactor();
        if (evaluation_ == Evaluation::lut) {
            // Capture *this by reference so shapeValue sees the current shape_.
            // build() samples shapeValue at kTableSize points; safe to call here
            // because init() is not on the real-time audio path.
            lut_.build([this](float u) noexcept { return shapeValue(u); });
        }
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
    //   y = shapeValue(u)       ← closedForm path, OR
    //     = lut_.evaluate(u)    ← lut path (built in init())
    //   y = dcBlock(y)          ← wrapper-owned one-pole HP (FR-008)
    //   y = gainCompFactor * y  ← only when gainComp_ is on (FR-010)
    //
    // Backend selection: a single predictable branch on evaluation_ at the top
    // of process().  This is the lowest-overhead correct implementation; the
    // branch target is constant for the lifetime of a stream and will be
    // predicted accurately by hardware branch predictors.  Do NOT rebuild the
    // LUT here — LUT build belongs in init() (rebuild-on-init semantics).
    //
    // RT-safe: noexcept, no heap allocation, no locks, bounded work (FR-020).
    float process(float x) noexcept {
        const float u = drive_ * x + bias_;
        float y = (evaluation_ == Evaluation::lut)
                      ? lut_.evaluate(u)
                      : shapeValue(u);
        y = dcBlock(y);
        if (gainComp_) {
            y *= gainCompFactor_;
        }
        return y;
    }

private:
    // -----------------------------------------------------------------------
    // Default parameters for parameterized shapes.
    //
    // The wrapper API (waveshaper-api.md) does not expose per-shape setters
    // (e.g. setChebyshevOrder, setFoldGain); those are documented future
    // extensions.  Until then, each parameterized shape uses a named constant
    // baked into the dispatch.
    // -----------------------------------------------------------------------

    // chebyshev default order: T_2(u) = 2u²−1.
    // T_2 is the first order that genuinely "targets" a harmonic (T_1 is
    // trivially linear).  Bounded to [−1,1] for |u| ≤ 1.
    static constexpr int kDefaultChebyshevOrder = 2;

    // sineFold / triangleFold default fold depth.
    // At foldGain=1, sineFold maps u=1 → sin(π/2) = 1 (first fold at |u|=1);
    // triangleFold is a full-range triangle wave on [−1,1].
    static constexpr float kDefaultFoldGain = 1.0f;

    // biasedAsym: wrapper-local asymmetric form.  NOT a pure acfx::shape::*
    // function (research.md Decision 1); the pure namespace contains only
    // parameterless or gain-parameterized shapes.
    //
    // Closed form:  tanh(u + kBiasedAsymOffset) − kBiasedAsymDcCorr
    //   kBiasedAsymDcCorr = tanh(kBiasedAsymOffset) ensures f(0) = 0.
    //
    // Asymmetry: the positive and negative halves saturate at different rates
    // (slope ≈ sech²(0.5) ≈ 0.786 at origin), producing even+odd harmonics on
    // a symmetric input without requiring the user to set an external bias.
    //
    // Range: (−1 − tanh(0.5), 1 − tanh(0.5)) ≈ (−1.462, 0.538); bounded but
    // NOT symmetrically ±1.  kBiasedAsymDcCorr is the exact float literal for
    // tanh(0.5), baked here for RT-safety (no std:: call on the audio path).
    static constexpr float kBiasedAsymOffset = 0.5f;
    static constexpr float kBiasedAsymDcCorr = 0.46211715726f; // = tanh(0.5f)

    // -----------------------------------------------------------------------
    // shapeValue() — evaluate the current shape at u (closed-form, no LUT).
    //
    // Reused both as the closedForm backend in process() and as the function
    // sampled by WaveshaperLut::build() during init() to fill the LUT table.
    //
    // Complete switch over all 11 Shape enum members; every value gets a
    // deliberate, correct mapping.  No silent fallback: if a new Shape member
    // is added without a matching case, -Wswitch surfaces it.
    //
    // RT-safe: noexcept, no heap allocation, no locks, bounded switch.
    // -----------------------------------------------------------------------
    float shapeValue(float u) const noexcept {
        switch (shape_) {
            case Shape::tanh:
                return shape::tanhShape(u);
            case Shape::arctan:
                return shape::arctanShape(u);
            case Shape::cubicSoft:
                return shape::cubicSoftClip(u);
            case Shape::algebraic:
                return shape::algebraic(u);
            case Shape::hardClip:
                return shape::hardClip(u);
            case Shape::softKnee:
                return shape::softKnee(u);
            case Shape::chebyshev:
                // kDefaultChebyshevOrder baked in; per-shape order setter is
                // a documented future extension (waveshaper-api.md).
                return shape::chebyshev(u, kDefaultChebyshevOrder);
            case Shape::biasedAsym:
                // Operating-point-shifted tanh, DC-corrected so f(0)=0.
                // Wrapper-local; no acfx::shape::biasedAsym exists.
                // See kBiasedAsymOffset / kBiasedAsymDcCorr above.
                return shape::tanhShape(u + kBiasedAsymOffset) - kBiasedAsymDcCorr;
            case Shape::diodeCurve:
                // MEMORYLESS transfer curve (research.md Decision 6 / FR-004).
                // Distinct from the stateful, circuit-solved diode clipper that
                // phase-circuit-modeling's diode-clippers item owns.
                // See core/labs/waveshaping/README.md for the altitude boundary.
                return shape::diodeCurve(u);
            case Shape::sineFold:
                // kDefaultFoldGain baked in; per-shape foldGain setter is
                // a documented future extension (waveshaper-api.md).
                return shape::sineFold(u, kDefaultFoldGain);
            case Shape::triangleFold:
                // kDefaultFoldGain baked in; per-shape foldGain setter is
                // a documented future extension (waveshaper-api.md).
                return shape::triangleFold(u, kDefaultFoldGain);
        }
        // Unreachable for valid (in-range) enum values; the switch above is
        // exhaustive.  If execution reaches here the enum was out-of-range
        // (e.g. corrupted memory).  Route to tanhShape: deterministic, bounded,
        // and explicitly documented — not a silent data fallback.
        return shape::tanhShape(u);
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
    // LUT — fixed-size, built in init() when evaluation_ == Evaluation::lut.
    //
    // WaveshaperLut is a value member (std::array<float, 512> internally),
    // so no heap allocation results from its presence here.  The table is
    // populated by lut_.build([this](float u){ return shapeValue(u); }) in
    // init(); process() reads it via lut_.evaluate(u) on the lut path.
    //
    // The lut_ member is default-constructed to zeroed table_ entries.
    // An un-init()'d lut_ returns 0 from evaluate(), which is a silent
    // misfire — callers MUST call init() before process() (standard contract).
    // -----------------------------------------------------------------------
    WaveshaperLut lut_;
};

} // namespace acfx

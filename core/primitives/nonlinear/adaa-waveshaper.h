#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "primitives/nonlinear/waveshaper-shapes.h"

// First-order antiderivative anti-aliasing (ADAA) waveshaper — research.md
// Decision 5.  A SEPARATE, opt-in, strictly-layered type: it does NOT modify
// acfx::shape::* or acfx::Waveshaper (FR-013/014).  It mirrors the Waveshaper
// staging (u = drive*x + bias; shape; DC-block; gain-comp) but replaces the
// naive memoryless shaping with the first-order ADAA difference quotient
//
//   y[n] = (F(u[n]) − F(u[n−1])) / (u[n] − u[n−1])
//
// where F is the analytic antiderivative of the selected shape (F' = shape).
// When |u[n] − u[n−1]| is tiny the quotient degenerates to 0/0; we fall back to
// the direct shape evaluated at the midpoint (u[n]+u[n−1])/2 (the well-known
// stable limit, research.md Decision 5).  This is a DEFINED numerical guard,
// not a silent data fallback.
//
// Coverage: only shapes with an analytic antiderivative are admissible.
// setShape() refuses an uncovered shape (e.g. Shape::biasedAsym, which is
// wrapper-realized and has no pure acfx::shape antiderivative) with a
// descriptive std::invalid_argument (Constitution V) rather than mis-shaping.
//
// OPEN QUESTION (research.md Decision 5): second-order ADAA is intentionally
// NOT implemented here; AdaaOrder currently exposes only `first`.
//
// Graduated from: core/labs/waveshaping/ (T024)
// Current location: core/primitives/nonlinear/

namespace acfx {

// ADAA order selector.  Only first-order is implemented; second-order is a
// documented Open Question (research.md Decision 5).
enum class AdaaOrder : std::uint8_t { first /*, second (open question) */ };

class ADAAWaveshaper {
public:
    // Prepare for a sample rate.  Clears ADAA history (uPrev/FPrev) and
    // DC-block state, and recomputes the gain-comp factor.  Not on the audio
    // path; no LUT is required for the ADAA variant.
    void init(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        updateGainCompFactor();
        reset();
    }

    // Select the transfer function.  THROWS std::invalid_argument for a shape
    // with no analytic antiderivative (hasAntiderivative(s) == false), naming
    // the shape — ADAA cannot anti-alias a shape it cannot integrate, and a
    // silent degrade-to-naive would violate Constitution V.  This is a
    // control-thread configuration call, so throwing is acceptable; process()
    // remains noexcept regardless.
    void setShape(Shape shape) {
        if (!shape::hasAntiderivative(shape)) {
            throw std::invalid_argument(
                "ADAAWaveshaper: shape has no antiderivative (naive-only): "
                + shapeName(shape));
        }
        shape_ = shape;
        // Re-pair the cached antiderivative with the NEW shape at the existing
        // history point. Without this, FPrev_ would still hold the OLD shape's
        // F(uPrev_), and the next process() would compute
        // (F_new(u) − F_old(uPrev_))/du — mixing two distinct antiderivatives
        // (wrong whenever F_new(uPrev_) ≠ F_old(uPrev_), e.g. algebraic with
        // F(0)=1). We re-evaluate at the CURRENT uPrev_ (rather than resetting
        // history to 0) so the ADAA averaging window still tracks the real input
        // trajectory — consistent with how Waveshaper preserves running state
        // across setShape.
        FPrev_ = antiderivAt(uPrev_);
    }

    // Select the ADAA order.  Only AdaaOrder::first is implemented (Decision 5).
    void setAdaaOrder(AdaaOrder order) noexcept { order_ = order; }

    // Pre-gain applied to the input before bias: u = drive * x + bias.
    void setDrive(float drive) noexcept {
        drive_ = drive;
        updateGainCompFactor();
    }

    // Fixed DC offset applied after drive; bias-induced DC is removed by the
    // wrapper's DC-blocker (matches Waveshaper).
    void setBias(float bias) noexcept { bias_ = bias; }

    // Enable/disable auto-makeup gain toward unity (1/drive law, matches
    // Waveshaper).
    void setGainCompensation(bool on) noexcept { gainComp_ = on; }

    // Clear ADAA history (uPrev_/FPrev_) AND DC-block state; keep parameters.
    // uPrev_ is set to 0 and FPrev_ to F(0) for the current shape so the very
    // first difference quotient is exact (the additive constant cancels, but
    // F(uPrev) must be the TRUE antiderivative at uPrev=0).
    void reset() noexcept {
        uPrev_   = 0.0f;
        FPrev_   = antiderivAt(0.0f);
        dcXPrev_ = 0.0f;
        dcYPrev_ = 0.0f;
    }

    // Process one sample through the staged ADAA signal chain (Decision 5):
    //   u  = drive * x + bias
    //   du = u − uPrev
    //   y  = shape((u+uPrev)/2)          if |du| < kEps   (midpoint fallback)
    //      = (F(u) − F(uPrev)) / du      otherwise
    //   y  = dcBlock(y)
    //   y *= gainCompFactor              if gainComp on
    // F(u) is evaluated once per sample and cached as FPrev_ for the next call
    // (avoids a redundant antiderivative eval); shape() is evaluated only on the
    // rare midpoint-fallback branch.
    //
    // RT-safe: noexcept, no heap allocation, no locks, bounded work.
    float process(float x) noexcept {
        const float u  = drive_ * x + bias_;
        const float Fu = antiderivAt(u);
        const float du = u - uPrev_;

        float y;
        if (std::abs(du) < kEps) {
            // Difference quotient degenerates to 0/0; use the exact limit
            // (the shape at the midpoint).  Defined numerical guard, not a
            // silent data fallback (research.md Decision 5).
            y = shapeAt(0.5f * (u + uPrev_));
        } else {
            y = (Fu - FPrev_) / du;
        }

        uPrev_ = u;
        FPrev_ = Fu;

        y = dcBlock(y);
        if (gainComp_) {
            y *= gainCompFactor_;
        }
        return y;
    }

private:
    // -----------------------------------------------------------------------
    // Default parameters for parameterized shapes — MUST match Waveshaper so
    // the ADAA and naive paths realize the SAME transfer (otherwise the
    // aliasing comparison would not be apples-to-apples).
    // -----------------------------------------------------------------------
    static constexpr int   kDefaultChebyshevOrder = 2;     // matches Waveshaper
    static constexpr float kDefaultFoldGain        = 1.0f;  // matches Waveshaper

    // biasedAsym wrapper-local constants (matches Waveshaper).  Only used in the
    // unreachable shapeAt() case below (setShape refuses biasedAsym), kept for a
    // consistent transfer should that guard ever be reached.
    static constexpr float kBiasedAsymOffset = 0.5f;
    static constexpr float kBiasedAsymDcCorr = 0.46211715726f; // = tanh(0.5f)

    // Midpoint-fallback threshold for |u − uPrev|.  A small documented
    // numerical guard against the 0/0 singularity of the difference quotient
    // (research.md Decision 5) — NOT a silent data fallback.
    static constexpr float kEps = 1.0e-5f;

    // -----------------------------------------------------------------------
    // shapeAt() — the memoryless shape at u (used only on the midpoint
    // fallback).  Mirrors Waveshaper::shapeValue exactly so the realized
    // transfer is identical.
    // -----------------------------------------------------------------------
    float shapeAt(float u) const noexcept {
        switch (shape_) {
            case Shape::tanh:        return shape::tanhShape(u);
            case Shape::arctan:      return shape::arctanShape(u);
            case Shape::cubicSoft:   return shape::cubicSoftClip(u);
            case Shape::algebraic:   return shape::algebraic(u);
            case Shape::hardClip:    return shape::hardClip(u);
            case Shape::softKnee:    return shape::softKnee(u);
            case Shape::chebyshev:   return shape::chebyshev(u, kDefaultChebyshevOrder);
            case Shape::biasedAsym:
                // Unreachable: setShape() refuses biasedAsym (uncovered).
                return shape::tanhShape(u + kBiasedAsymOffset) - kBiasedAsymDcCorr;
            case Shape::diodeCurve:  return shape::diodeCurve(u);
            case Shape::sineFold:    return shape::sineFold(u, kDefaultFoldGain);
            case Shape::triangleFold:return shape::triangleFold(u, kDefaultFoldGain);
        }
        return shape::tanhShape(u);  // unreachable for the closed enum
    }

    // -----------------------------------------------------------------------
    // antiderivAt() — F(u) for the current shape, paired one-to-one with
    // shapeAt() (F' = shape).  Uses the SAME default order/fold-gain constants.
    // biasedAsym has no antiderivative; setShape() refuses it, so that case is
    // unreachable.
    // -----------------------------------------------------------------------
    float antiderivAt(float u) const noexcept {
        switch (shape_) {
            case Shape::tanh:        return shape::tanhAntideriv(u);
            case Shape::arctan:      return shape::arctanAntideriv(u);
            case Shape::cubicSoft:   return shape::cubicSoftClipAntideriv(u);
            case Shape::algebraic:   return shape::algebraicAntideriv(u);
            case Shape::hardClip:    return shape::hardClipAntideriv(u);
            case Shape::softKnee:    return shape::softKneeAntideriv(u);
            case Shape::chebyshev:   return shape::chebyshevAntideriv(u, kDefaultChebyshevOrder);
            case Shape::biasedAsym:
                // Unreachable: setShape() refuses biasedAsym (no antiderivative).
                break;
            case Shape::diodeCurve:  return shape::diodeCurveAntideriv(u);
            case Shape::sineFold:    return shape::sineFoldAntideriv(u, kDefaultFoldGain);
            case Shape::triangleFold:return shape::triangleFoldAntideriv(u, kDefaultFoldGain);
        }
        // Unreachable: only antiderivative-covered shapes can be stored.
        return shape::tanhAntideriv(u);
    }

    // Human-readable shape name for the descriptive setShape() error.
    static std::string shapeName(Shape s) {
        switch (s) {
            case Shape::tanh:        return "tanh";
            case Shape::arctan:      return "arctan";
            case Shape::cubicSoft:   return "cubicSoft";
            case Shape::algebraic:   return "algebraic";
            case Shape::hardClip:    return "hardClip";
            case Shape::softKnee:    return "softKnee";
            case Shape::chebyshev:   return "chebyshev";
            case Shape::biasedAsym:  return "biasedAsym";
            case Shape::diodeCurve:  return "diodeCurve";
            case Shape::sineFold:    return "sineFold";
            case Shape::triangleFold:return "triangleFold";
        }
        return "unknown";
    }

    // -----------------------------------------------------------------------
    // DC-blocker — wrapper-owned (FR-008); identical one-pole HP design to
    // Waveshaper (research.md Decision 3).  y[n] = x[n] − x[n−1] + R·y[n−1].
    // -----------------------------------------------------------------------
    static constexpr float kDcR = 0.995f;  // matches Waveshaper
    float dcXPrev_ = 0.0f;
    float dcYPrev_ = 0.0f;

    float dcBlock(float x) noexcept {
        const float y = x - dcXPrev_ + kDcR * dcYPrev_;
        dcXPrev_      = x;
        dcYPrev_      = y;
        return y;
    }

    // -----------------------------------------------------------------------
    // Gain compensation — wrapper-owned (FR-010); identical 1/drive law to
    // Waveshaper.  Computed off the audio path (init/setDrive).
    // -----------------------------------------------------------------------
    static constexpr float kGainCompEps = 1.0e-6f;  // matches Waveshaper

    bool  gainComp_       = false;
    float gainCompFactor_ = 1.0f;

    void updateGainCompFactor() noexcept {
        const float d   = (drive_ > kGainCompEps) ? drive_ : kGainCompEps;
        gainCompFactor_ = 1.0f / d;
    }

    // -----------------------------------------------------------------------
    // Signal-chain parameters + ADAA history.
    // -----------------------------------------------------------------------
    Shape     shape_      = Shape::tanh;          // covered by default
    AdaaOrder order_      = AdaaOrder::first;
    float     drive_      = 1.0f;
    float     bias_       = 0.0f;
    float     sampleRate_ = 48000.0f;

    float     uPrev_ = 0.0f;   // u[n−1]
    float     FPrev_ = 0.0f;   // F(u[n−1]), cached to avoid a redundant eval
};

} // namespace acfx

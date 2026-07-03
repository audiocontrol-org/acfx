#pragma once

#include <cmath>
#include <cstdint>

// GainComputer — RT-safe stateless static-curve gain-reduction primitive.
//
// Maps an input level (dB) to a gain change (dB, <= 0 = attenuation) via a
// selectable curve mode (compress / limit / expand / gate) with a single
// unified quadratic C1 knee straddling the threshold (hard corner at
// knee = 0). This is the pure static-curve kernel that dynamics processors
// (compressors, limiters, expanders, gates, …) drive with an externally
// supplied level (see EnvelopeFollower); it holds NO runtime state of its
// own — it does not detect level or apply ballistics (see
// contracts/gain-computer-api.md).
//
// Lives in core/primitives/dynamics/ as the second inhabitant of the
// `dynamics/` category, graduated — unchanged in its public contract — via
// `git mv` from core/labs/compressor/ per Constitution IX. The originating
// lab persists as README + host-only harness.
//
// The public API from contracts/gain-computer-api.md; the setters store their
// (guarded) fields and computeGainDb() evaluates the piecewise map fresh —
// unified quadratic C1 knee, per-mode branches, range bound, ratio guard —
// with pure branch-and-multiply arithmetic (task T008; see
// specs/compressors/data-model.md, "Entity — GainComputer").
//
// The curve, per mode (u = levelDb - thresholdDb; W = knee width; R = ratio):
//   All modes share ONE knee helper kneeGain(u, W, mLow, mHigh) blending a
//   low-side slope mLow into a high-side slope mHigh — both lines passing
//   through (threshold, 0) — over the window u in [-W/2, +W/2] with a C1
//   quadratic (standard Reiss soft-knee), degenerating to a hard corner at
//   W = 0. Only (mLow, mHigh) and an optional range floor differ per mode:
//     compress : mLow = 0,          mHigh = 1/R - 1   (<= 0; unity below thr)
//     limit    : mLow = 0,          mHigh = -1        (1/R -> 0; out held at thr)
//     expand   : mLow = R - 1,      mHigh = 0         (downward below thr), gain >= rangeDb
//     gate     : mLow = -2*range/W, mHigh = 0         (drop to rangeDb below thr), gain >= rangeDb
//   (gate's mLow is chosen so the knee reaches the rangeDb floor exactly at
//   the lower knee edge u = -W/2; at W = 0 it is the hard step 0/rangeDb.)
//
// Constitution refs:
//   IV   — platform-independent core: no JUCE / Daisy SDK / Teensy / effects /
//          harness includes here, ever.
//   VI   — real-time safety: no heap allocation, no locks, bounded work in
//          computeGainDb() (branch-only arithmetic, no transcendental).
//   VII  — strict typing & small modules: no `any`-equivalents, file stays
//          well under the 300-500 line guideline.
//
// See also: specs/compressors/spec.md,
//           specs/compressors/data-model.md,
//           specs/compressors/contracts/gain-computer-api.md

namespace acfx {

enum class GainMode : std::uint8_t { compress, limit, expand, gate };

class GainComputer {
public:
    // Configuration setters — store parameters; guarded against degenerate
    // input (FR-024). No cached per-sample coefficients are needed: the map
    // is direct arithmetic, computed fresh in computeGainDb().
    void setMode(GainMode mode) noexcept { mode_ = mode; }
    // Non-finite writes are ignored (the prior valid value stands) so no
    // NaN/Inf parameter can ever leak into computeGainDb() (FR-024).
    void setThreshold(float dB) noexcept {
        if (std::isfinite(dB)) thresholdDb_ = dB;
    }
    // ratio < 1 guarded to 1; limit mode treats ratio as infinite regardless.
    void setRatio(float ratio) noexcept {
        if (!std::isfinite(ratio)) return;
        ratio_ = ratio < 1.0f ? 1.0f : ratio;
    }
    // 0 = hard corner; > 0 = unified quadratic C1 knee straddling threshold.
    void setKnee(float dB) noexcept {
        if (!std::isfinite(dB)) return;
        kneeDb_ = dB < 0.0f ? 0.0f : dB;
    }
    // Expander/gate max attenuation (floor); clamped <= 0 (a floor never boosts).
    void setRange(float dB) noexcept {
        if (!std::isfinite(dB)) return;
        rangeDb_ = dB > 0.0f ? 0.0f : dB;
    }

    // Pure static curve: input level in dB -> gain change in dB (<= 0 =
    // attenuation). No runtime state; identical inputs -> identical outputs,
    // call-order independent. Branch-only arithmetic; no transcendental.
    float computeGainDb(float levelDb) const noexcept {
        // Defensive: a non-finite level maps to unity, never NaN/Inf (FR-024).
        if (!std::isfinite(levelDb)) return 0.0f;

        const float u = levelDb - thresholdDb_; // signed distance from threshold
        const float W = kneeDb_;                 // knee width, >= 0 (guarded)

        switch (mode_) {
        case GainMode::compress: {
            // Above thr: slope 1/R - 1 (<= 0). Unity below. No range floor.
            const float mHigh = 1.0f / ratio_ - 1.0f; // ratio_ >= 1 guaranteed
            return kneeGain(u, W, 0.0f, mHigh);
        }
        case GainMode::limit: {
            // compress with ratio -> inf: 1/R -> 0, so mHigh = -1 (out held at thr).
            return kneeGain(u, W, 0.0f, -1.0f);
        }
        case GainMode::expand: {
            // Below thr: downward by ratio (slope R - 1). Unity above. Range floor.
            const float g = kneeGain(u, W, ratio_ - 1.0f, 0.0f);
            return g < rangeDb_ ? rangeDb_ : g;
        }
        case GainMode::gate: {
            // Extreme expand: drop toward the rangeDb floor below thr; unity above.
            if (W <= 0.0f) return u < 0.0f ? rangeDb_ : 0.0f; // hard step
            const float mLow = -2.0f * rangeDb_ / W;          // >= 0 (rangeDb <= 0)
            const float g = kneeGain(u, W, mLow, 0.0f);
            return g < rangeDb_ ? rangeDb_ : g;
        }
        }
        return 0.0f; // unreachable: all GainMode values handled above
    }

private:
    // Unified quadratic C1 soft knee (standard Reiss form), shared by every
    // mode. Blends a low-side linear segment (slope mLow) into a high-side one
    // (slope mHigh) over u in [-W/2, +W/2]; both segments pass through
    // (threshold, 0), so the knee value and slope match at both edges. At
    // W = 0 the knee window collapses and this is the exact hard corner.
    static float kneeGain(float u, float W, float mLow, float mHigh) noexcept {
        const float halfW = 0.5f * W;
        if (u <= -halfW) return mLow * u;  // low linear segment (W==0: u <= 0)
        if (u >= halfW) return mHigh * u;  // high linear segment (W==0: u >= 0)
        // Knee (W > 0 here): q(u) = mLow*u + (mHigh-mLow)*(u+W/2)^2 / (2W).
        const float t = u + halfW;
        return mLow * u + (mHigh - mLow) * (t * t) / (2.0f * W);
    }

    // -----------------------------------------------------------------------
    // Configuration — all fields; GainComputer holds no other state (it is
    // stateless per FR-001).
    // -----------------------------------------------------------------------
    GainMode mode_       = GainMode::compress;
    float    thresholdDb_ = -18.0f; // dBFS (tuning-pass placeholder)
    float    ratio_       = 4.0f;   // >= 1; limit mode ignores it (infinite)
    float    kneeDb_      = 6.0f;   // dB, >= 0; 0 = hard corner
    float    rangeDb_     = -40.0f; // dB, <= 0; expand/gate attenuation floor
};

} // namespace acfx

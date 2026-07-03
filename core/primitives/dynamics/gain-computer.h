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
//   compress / limit / expand share ONE knee helper kneeGain(u, W, mLow, mHigh)
//   blending a low-side slope mLow into a high-side slope mHigh — both lines
//   passing through (threshold, 0) — over the window u in [-W/2, +W/2] with a
//   C1 quadratic (standard Reiss soft-knee), degenerating to a hard corner at
//   W = 0. Only (mLow, mHigh) and an optional range floor differ per mode:
//     compress : mLow = 0,     mHigh = 1/R - 1   (<= 0; unity below thr)
//     limit    : mLow = 0,     mHigh = -1        (1/R -> 0; out held at thr)
//     expand   : mLow = R - 1, mHigh = 0         (downward below thr), gain >= rangeDb
//   gate is DIFFERENT: a gate interpolates between two LEVELS (0 dB above thr,
//   the rangeDb floor below), not between two slopes. Its knee is therefore a
//   C1 cubic SMOOTHSTEP over the window u in [-W/2, +W/2], NOT the quadratic
//   slope-blend helper (see the gate branch for the exact math):
//     gate     : gainDb = rangeDb*(1 - smoothstep(tau)), tau = (u + W/2)/W,
//                smoothstep(tau) = 3*tau^2 - 2*tau^3 (0 at the lower edge,
//                rangeDb; 1 at the upper edge, 0), with ZERO slope at both
//                edges so it joins the flat unity/floor regions C1, and
//                reduces EXACTLY to the hard step 0/rangeDb at W = 0.
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
            // A gate interpolates between two LEVELS — unity (0 dB) above the
            // threshold and the rangeDb floor below — via a C1 cubic smoothstep
            // across the knee window u in [-W/2, +W/2]. This is NOT the quadratic
            // slope-blend used by the other modes: those blend two SLOPES, which
            // for a gate neither degenerates to the hard corner nor makes the
            // near-threshold shape depend on knee width (FR-007/SC-003).
            //
            // Let tau = (u + W/2)/W map the window to [0, 1] and
            //     s(tau) = 3*tau^2 - 2*tau^3          (cubic Hermite smoothstep).
            // s(0) = 0, s(1) = 1, and s'(tau) = 6*tau*(1 - tau) so s'(0) = s'(1)
            // = 0. With gainDb = rangeDb*(1 - s):
            //   * upper edge u = +W/2 (tau = 1): s = 1 -> gainDb = 0, joining the
            //     flat unity region above with zero slope (C1).
            //   * lower edge u = -W/2 (tau = 0): s = 0 -> gainDb = rangeDb,
            //     joining the flat floor below with zero slope (C1).
            //   * threshold u = 0 (tau = 1/2): s = 1/2 -> gainDb = rangeDb/2.
            // As W -> 0 the window collapses and the map is EXACTLY the hard step
            // (0 above threshold, rangeDb below). gainDb is bounded to [rangeDb, 0]
            // for every tau in [0, 1].
            if (W <= 0.0f) return u < 0.0f ? rangeDb_ : 0.0f; // hard step (knee -> 0)
            const float halfW = 0.5f * W;
            if (u >= halfW) return 0.0f;      // flat unity region above the knee
            if (u <= -halfW) return rangeDb_; // flat rangeDb floor below the knee
            const float tau = (u + halfW) / W;              // in [0, 1]
            const float s   = tau * tau * (3.0f - 2.0f * tau); // 3*tau^2 - 2*tau^3
            const float g   = rangeDb_ * (1.0f - s);
            // F3: guard the smoothstep term against an absurd-but-finite knee;
            // a non-finite result falls back to the hard step. Branch-only.
            if (!std::isfinite(g)) return u < 0.0f ? rangeDb_ : 0.0f;
            if (g > 0.0f) return 0.0f;             // gate gain is always <= 0
            return g < rangeDb_ ? rangeDb_ : g;    // and always >= rangeDb floor
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
        const float g = mLow * u + (mHigh - mLow) * (t * t) / (2.0f * W);
        // F3: an absurd-but-finite knee width can overflow the quadratic term to
        // Inf/NaN; fall back to the hard corner (the linear segment through the
        // threshold on the relevant side) so the map stays finite. Branch-only.
        if (!std::isfinite(g)) return u < 0.0f ? mLow * u : mHigh * u;
        return g;
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

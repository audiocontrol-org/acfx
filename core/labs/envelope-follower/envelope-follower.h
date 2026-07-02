#pragma once

#include <cmath>
#include <cstdint>

// EnvelopeFollower — RT-safe dynamics level-detector primitive.
//
// Tracks the amplitude envelope of an input signal via a selectable detector
// (peak / rms / peakHold), an optional dB-domain conversion, and a selectable
// attack/release smoother topology (branching / decoupled). This is the
// shared level-detection kernel that dynamics processors (compressors,
// limiters, gates, …) build their gain computer on top of; it does not itself
// compute gain reduction (see contracts/envelope-follower-api.md, "Out of
// scope").
//
// Authored in the LAB (core/labs/envelope-follower/) per Constitution IX; a
// later task graduates this file — unchanged in its public contract — via
// `git mv` into core/primitives/dynamics/envelope-follower.h.
//
// This is a SKELETON (task T002): all state is declared and every method
// compiles, but the per-sample math is stubbed. `process()` returns 0.0f
// until a later task fills in the detect/domain/smooth pipeline described in
// specs/envelope-followers/data-model.md.
//
// Constitution refs:
//   IV   — platform-independent core: no JUCE / Daisy SDK / Teensy / effects /
//          harness includes here, ever.
//   VI   — real-time safety: no heap allocation, no locks, bounded work in
//          process().
//   VII  — strict typing & small modules: no `any`-equivalents, file stays
//          well under the 300-500 line guideline.
//
// See also: specs/envelope-followers/spec.md,
//           specs/envelope-followers/data-model.md,
//           specs/envelope-followers/contracts/envelope-follower-api.md

namespace acfx {

enum class DetectMode   : std::uint8_t { peak, rms, peakHold };
enum class Ballistics   : std::uint8_t { branching, decoupled };
enum class DetectDomain : std::uint8_t { linear, decibel };

class EnvelopeFollower {
public:
    // Prepare for a sample rate; caches fs, recomputes coefficients, clears
    // runtime state. Guards sampleRate <= 0 to a safe 48000.0f so nothing
    // downstream divides by zero (FR-016/FR-018).
    void init(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 48000.0f;
        aAtk_        = coeffFor(attackSeconds_);
        aRel_        = coeffFor(releaseSeconds_);
        aRms_        = coeffFor(rmsWindowSeconds_);
        holdSamples_ = holdSamplesFor(holdSeconds_);
        clearRuntimeState();
    }

    // Configuration setters — store the parameter and recompute the cached
    // coefficient it feeds (never in process(), FR-013). Do NOT reset runtime
    // state here.
    void setMode(DetectMode mode) noexcept { mode_ = mode; }
    void setBallistics(Ballistics ballistics) noexcept { ballistics_ = ballistics; }
    void setSmooth(bool smooth) noexcept { smooth_ = smooth; }
    void setDomain(DetectDomain domain) noexcept { domain_ = domain; }
    void setAttack(float seconds) noexcept {
        attackSeconds_ = seconds;
        aAtk_          = coeffFor(seconds);
    }
    void setRelease(float seconds) noexcept {
        releaseSeconds_ = seconds;
        aRel_           = coeffFor(seconds);
    }
    void setHold(float seconds) noexcept {
        holdSeconds_ = seconds;
        holdSamples_ = holdSamplesFor(seconds);
    }
    void setRmsWindow(float seconds) noexcept {
        rmsWindowSeconds_ = seconds;
        aRms_             = coeffFor(seconds);
    }

    // Clear all runtime state to the defined initial condition (env = 0).
    void reset() noexcept { clearRuntimeState(); }

    // Process one input sample; return the current envelope. Stubbed for
    // this skeleton task — always returns 0.0f. A later task implements the
    // detect / domain / smooth pipeline (data-model.md "State transitions").
    float process(float /*x*/) noexcept { return 0.0f; }

private:
    // Time-constant -> one-pole coefficient. For the update
    //   y[n] = a*y[n-1] + (1-a)*x[n]
    // the envelope reaches 1 - 1/e (~63%) of a step in `seconds`.
    //   seconds <= 0        -> 0.0f (instantaneous: a=0 makes y=x)
    //   non-finite result   -> 0.0f (never emit NaN/Inf, FR-018)
    // Result is clamped to [0.0f, 1.0f) so it is never >= 1 (FR-018).
    float coeffFor(float seconds) const noexcept {
        if (seconds <= 0.0f) {
            return 0.0f;
        }
        const float a = std::exp(-1.0f / (seconds * sampleRate_));
        if (!std::isfinite(a) || a < 0.0f) {
            return 0.0f;
        }
        // Strictly below 1.0f: nudge down the largest representable float < 1.
        constexpr float kMaxCoeff = 0.99999994f; // nextafter(1.0f, 0.0f)
        return (a > kMaxCoeff) ? kMaxCoeff : a;
    }

    // Hold time -> non-negative sample count (integer-safe rounding).
    std::int32_t holdSamplesFor(float seconds) const noexcept {
        if (seconds <= 0.0f) {
            return 0;
        }
        const std::int32_t n =
            static_cast<std::int32_t>(std::lround(seconds * sampleRate_));
        return (n >= 0) ? n : 0;
    }

    void clearRuntimeState() noexcept {
        env_         = 0.0f;
        meanSquare_  = 0.0f;
        y1_          = 0.0f;
        heldPeak_    = 0.0f;
        holdCounter_ = 0;
    }

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------
    float        sampleRate_       = 48000.0f;
    DetectMode   mode_             = DetectMode::peak;
    Ballistics   ballistics_       = Ballistics::branching;
    bool         smooth_           = false;
    DetectDomain domain_           = DetectDomain::linear;
    float        attackSeconds_    = 0.0f;
    float        releaseSeconds_   = 0.0f;
    float        holdSeconds_      = 0.0f;
    float        rmsWindowSeconds_ = 0.0f;

    // -----------------------------------------------------------------------
    // Derived / cached coefficients — recomputed in setters, never in
    // process() (a later task fills in the derivations from data-model.md).
    // -----------------------------------------------------------------------
    float       aAtk_        = 0.0f;
    float       aRel_        = 0.0f;
    float       aRms_        = 0.0f;
    std::int32_t holdSamples_ = 0;

    // -----------------------------------------------------------------------
    // Runtime state — cleared by reset()/init(), RT-mutated in process().
    // -----------------------------------------------------------------------
    float        env_         = 0.0f;
    float        meanSquare_  = 0.0f;
    float        y1_          = 0.0f;
    float        heldPeak_    = 0.0f;
    std::int32_t holdCounter_ = 0;
};

} // namespace acfx

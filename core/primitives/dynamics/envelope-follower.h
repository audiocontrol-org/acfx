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
// Lives in core/primitives/dynamics/ as the first inhabitant of the
// `dynamics/` category, graduated — unchanged in its public contract — via
// `git mv` from core/labs/envelope-follower/ per Constitution IX. The
// originating lab persists as README + host-only harness.
//
// This is a SKELETON (task T002, dispatch wired up in T007): all state is
// declared and `process()` now dispatches through detect/applyDomain/
// applySmoothing, but those seam methods are still stubs (peak-only
// detection, linear-only domain, passthrough smoothing) until later tasks
// fill in the real math described in specs/envelope-followers/data-model.md.
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

    // Process one input sample; return the current envelope. This is the
    // dispatch SKELETON (task T007): the detect -> domain -> smooth pipeline
    // structure is wired up, but the private seam methods below are stubs
    // filled in by later tasks (see data-model.md "State transitions").
    float process(float x) noexcept {
        const float detected = detect(x);                 // mode-dependent level
        const float domained = applyDomain(detected);     // linear passthrough or dB (stub: passthrough)
        const float smoothed = applySmoothing(domained);   // topology-dependent smoother (stub: passthrough)
        env_ = smoothed;
        return env_;
    }

private:
    // Mode-dependent level detector (data-model.md "Detection modes").
    // Stub: every branch currently returns peak (|x|) behavior.
    float detect(float x) noexcept {
        switch (mode_) {
            case DetectMode::peak:
                return std::fabs(x);
            case DetectMode::rms: {
                // One-pole mean-square (leaky integrator), window = setRmsWindow (aRms_).
                // meanSquare_ is runtime state (cleared by reset()); update then sqrt.
                meanSquare_ = aRms_ * meanSquare_ + (1.0f - aRms_) * (x * x);
                return std::sqrt(meanSquare_);
            }
            case DetectMode::peakHold: {
                const float rect = std::fabs(x);
                if (rect >= heldPeak_) {
                    heldPeak_    = rect;          // new/equal peak: latch it
                    holdCounter_ = holdSamples_;   // (re)start the hold window
                } else if (holdCounter_ > 0) {
                    --holdCounter_;                // still holding: heldPeak_ unchanged
                } else {
                    heldPeak_ = rect;               // hold expired: track down to input (ballistics then releases)
                }
                return heldPeak_;
            }
        }
        return std::fabs(x);
    }

    // Linear-vs-decibel domain conversion (data-model.md "Domain").
    float applyDomain(float level) noexcept {
        if (domain_ == DetectDomain::decibel) {
            // Clamp to the -120 dBFS floor, then convert to dB BEFORE smoothing.
            // A level at/below the floor returns -120 dB (never -inf/NaN). FR-012.
            if (level <= kFloorLin) {
                return kFloorDb;
            }
            return 20.0f * std::log10(level);
        }
        return level; // linear passthrough (base contract)
    }

    // Ballistics topology-dependent smoother (data-model.md "Smoothing").
    float applySmoothing(float value) noexcept {
        switch (ballistics_) {
            case Ballistics::branching: {
                // One-pole: attack coeff when the input rises above the
                // current envelope, release coeff when it falls. Coeffs are
                // cached (aAtk_/aRel_) by the setters (FR-013).
                // y[n] = a*y[n-1] + (1-a)*value  reaches 1-1/e of a step in tau.
                const float a = (value > env_) ? aAtk_ : aRel_;
                return a * env_ + (1.0f - a) * value;
            }
            case Ballistics::decoupled: {
                // Decoupled peak detector (Reiss). Release stage y1_ then
                // attack stage. Coeffs are cached (aAtk_/aRel_) by the setters.
                // Base (smooth_==false): hard max-with-decay release, decaying
                //   toward the domain FLOOR (0 linear, -120 dB) — NOT toward 0,
                //   which in the dB domain is unity (loud). In the linear domain
                //   floor==0 so this reduces to the classic max(level, aRel*y1).
                // Smooth (smooth_==true): one-pole smooth blend release at the
                //   RELEASE rate toward the current level (which is the floor on
                //   silence), correct in both domains.
                const float level = value;
                const float floor = (domain_ == DetectDomain::decibel) ? kFloorDb : 0.0f;
                y1_ = smooth_ ? std::fmax(level, aRel_ * y1_ + (1.0f - aRel_) * level)
                              : std::fmax(level, floor + aRel_ * (y1_ - floor));
                return aAtk_ * env_ + (1.0f - aAtk_) * y1_;
            }
        }
        return value;
    }

    // Time-constant -> one-pole coefficient. For the update
    //   y[n] = a*y[n-1] + (1-a)*x[n]
    // the envelope reaches 1 - 1/e (~63%) of a step in `seconds`.
    //   seconds <= 0        -> 0.0f (instantaneous: a=0 makes y=x)
    //   non-finite result   -> 0.0f (never emit NaN/Inf, FR-018)
    // Result is clamped to [0.0f, 1.0f) so it is never >= 1 (FR-018).
    // NOTE (research Decision 7, CLOSED): exp() is the mathematically exact
    // one-pole pole for ANY fs, so no higher-order low-sample-rate correction is
    // needed — characterized/demonstrated in
    // tests/core/envelope-follower-ballistics-test.cpp ("coefficient accuracy
    // holds at low sample rates").
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
        const float floor = (domain_ == DetectDomain::decibel) ? kFloorDb : 0.0f;
        env_         = floor;
        y1_          = floor;
        meanSquare_  = 0.0f;
        heldPeak_    = 0.0f;
        holdCounter_ = 0;
    }

    // dB floor (FR-012): a level at/below this linear threshold is treated as
    // silence and reported as kFloorDb rather than -inf/NaN.
    static constexpr float kFloorDb  = -120.0f;   // dB floor (FR-012)
    static constexpr float kFloorLin = 1e-6f;     // 10^(kFloorDb/20) = -120 dBFS in linear

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------
    float        sampleRate_       = 48000.0f;
    DetectMode   mode_             = DetectMode::peak;
    Ballistics   ballistics_       = Ballistics::branching;
    bool         smooth_           = false;
    DetectDomain domain_           = DetectDomain::linear;
    // Sensible non-zero defaults so a consumer that only calls init() + a mode
    // gets useful ballistics — and, critically, RMS mode actually averages (a
    // zero window would collapse the mean-square to x^2, i.e. |x|, silently NOT
    // doing RMS — Constitution V forbids that silent degeneracy). Callers
    // override via the setters.
    float        attackSeconds_    = 0.010f;   // 10 ms
    float        releaseSeconds_   = 0.100f;   // 100 ms
    float        holdSeconds_      = 0.0f;     // peak-hold opt-in (0 = plain peak)
    float        rmsWindowSeconds_ = 0.050f;   // 50 ms RMS averaging window

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

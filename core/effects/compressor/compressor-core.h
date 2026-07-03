#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "primitives/dynamics/gain-computer.h"
#include "primitives/delays/delay-line.h"
#include "primitives/dynamics/envelope-follower.h"
#include "primitives/filters/svf-primitive.h"

// CompressorCore — the RT-safe per-channel composition kernel (T010).
//
// Composes the shipped primitives (research Decision 6) into the full
// per-sample dynamics chain: sidechain HPF -> level detection -> static curve
// -> ballistics -> makeup -> lookahead -> VCA multiply -> feedback tap ->
// dry/wet mix -> output trim. Platform-independent (Constitution IV): no
// host-framework or embedded-vendor headers. RT-safe by construction
// (Constitution VI): every member is a value (no heap), all coefficient work
// lives in prepare()/setters, and process() is allocation-free, lock-free, and
// bounded. The only buffer (lookahead) is a fixed member std::array bound to
// the composed DelayLine in prepare().
//
// Composition (data-model.md "Entity — CompressorCore"):
//   detector      EnvelopeFollower  level detection + level-site ballistics,
//                                    decibel domain (time constants stay
//                                    level-independent).
//   gain          GainComputer      the static curve (instantaneous map).
//   gainSmoother  EnvelopeFollower  gain-site ballistics on the gain-reduction
//                                    signal (see BALLISTICS SITE below).
//   scFilter      SvfPrimitive      sidechain highpass (bypassed at 0 Hz).
//   lookahead     DelayLine         main-path pre-delay (bypassed at 0 samples).
//
// BALLISTICS SITE (research Decision 4) — attack/release are routed by
// applyBallistics() to whichever smoother the current site uses; the inactive
// one is held instantaneous so a live site switch is clean:
//   level site: `detector` carries attack/release (smooths the detected dB
//               level); the curve is instantaneous; `gainSmoother` is unused.
//   gain  site: `detector` runs ~instantaneous (attack=release=0, so it tracks
//               the level with no smoothing); `gainSmoother` carries
//               attack/release applied to the gain-reduction signal.
//
// gainSmoother DOMAIN + SIGN (documented choice): the gain-reduction value
// grDb is already in dB, so gainSmoother runs in the LINEAR domain — its
// applyDomain() is a passthrough, so it simply one-poles the dB number (no
// second dB conversion). It is fed the gain-reduction MAGNITUDE (-grDb, >= 0)
// and the smoothed result is negated back. This is deliberate: the shipped
// branching ballistics select the ATTACK coefficient when the tracked value
// RISES. Gain reduction is a downward (more-negative) excursion, so feeding
// grDb directly would swap attack and release (engagement would follow the
// release time — breaking SC-002). Smoothing the magnitude makes "more gain
// reduction" a rising value, so engagement correctly uses the attack time
// constant (matches the Reiss smoothed-gain formulation).
//
// See also: specs/compressors/spec.md,
//           specs/compressors/data-model.md,
//           specs/compressors/contracts/compressor-effect-api.md,
//           specs/compressors/research.md (Decisions 3-6).

namespace acfx {

enum class Detection      : std::uint8_t { feedForward, feedBack };
enum class BallisticsSite : std::uint8_t { level, gain };

class CompressorCore {
public:
    // Fixed lookahead budget: ~20 ms at a 192 kHz max supported sample rate
    // (0.020 * 192000 = 3840 samples). The backing std::array is sized to this
    // once, in the class body, so prepare() only BINDS it (no allocation); the
    // extra +1 is the DelayLine's guard sample (its prepare() wants
    // capacity >= maxDelay + 1).
    static constexpr int kMaxLookaheadSamples = 3840;
    static constexpr int kLookaheadCapacity   = kMaxLookaheadSamples + 1;

    // Prepare for a sample rate; sizes/binds the lookahead buffer and pushes
    // every cached coefficient into the composed sub-units. `maxLookaheadSamples`
    // is the caller's requested ceiling, clamped to the fixed buffer budget and
    // used to guard setLookahead(). No audio-path work here (Constitution VI).
    void prepare(float sampleRate, int maxLookaheadSamples) noexcept {
        sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 48000.0f;

        maxLookaheadSamples_ =
            maxLookaheadSamples < 0 ? 0
          : (maxLookaheadSamples > kMaxLookaheadSamples ? kMaxLookaheadSamples
                                                        : maxLookaheadSamples);
        if (lookaheadSamples_ > maxLookaheadSamples_)
            lookaheadSamples_ = maxLookaheadSamples_;

        // Detector: decibel domain so attack/release are level-independent.
        // init() clears state to the current domain's floor; setDomain(decibel)
        // re-baselines to the -120 dB floor on the first prepare (and no-ops
        // thereafter, when init() has already cleared to the dB floor).
        detector_.init(sampleRate_);
        detector_.setDomain(DetectDomain::decibel);

        // gainSmoother stays in the LINEAR domain (default) — it one-poles the
        // already-dB gain-reduction magnitude (see file header).
        gainSmoother_.init(sampleRate_);

        scFilter_.init(sampleRate_);
        applyScHpf();

        lookahead_.prepare(lookaheadBuffer_.data(), kLookaheadCapacity, sampleRate_);

        applyBallistics();
        updateMakeup();
        outputGainLin_ = dbToLin(outputDb_);

        reset();
    }

    // Clear runtime state; prevOutput cold-starts at the floor (silence, 0.0f —
    // the feedback tap is a linear audio sample, not a dB level). Also clears
    // the composed sub-units. SvfPrimitive::reset() re-Init()s the DaisySP
    // filter and discards its cutoff, so re-apply the sidechain HPF coefficient.
    void reset() noexcept {
        prevOutput_ = 0.0f;
        detector_.reset();
        gainSmoother_.reset();
        scFilter_.reset();
        applyScHpf();
        lookahead_.reset();
    }

    // ------------------------------------------------------------------
    // Configuration (recompute cached coefficients; do NOT reset runtime state).
    // ------------------------------------------------------------------

    // Mode/threshold/ratio/knee/range are owned by the composed GainComputer;
    // forward them and recompute the cached auto-makeup (which depends on the
    // curve). `mode_` is mirrored locally solely so auto-makeup can be forced to
    // 0 for the downward (expand/gate) modes (research Decision 5).
    void setMode(GainMode mode) noexcept {
        mode_ = mode;
        gain_.setMode(mode);
        updateMakeup();
    }
    void setThreshold(float dB) noexcept { gain_.setThreshold(dB); updateMakeup(); }
    void setRatio(float ratio) noexcept  { gain_.setRatio(ratio);  updateMakeup(); }
    void setKnee(float dB) noexcept       { gain_.setKnee(dB);       updateMakeup(); }
    void setRange(float dB) noexcept      { gain_.setRange(dB);      updateMakeup(); }
    void setDetector(DetectMode mode) noexcept { detector_.setMode(mode); }

    // Attack/release are routed to the active smoother by applyBallistics().
    void setAttack(float seconds) noexcept  { attackSeconds_  = seconds; applyBallistics(); }
    void setRelease(float seconds) noexcept { releaseSeconds_ = seconds; applyBallistics(); }
    void setDetection(Detection detection) noexcept { detection_ = detection; }
    void setBallisticsSite(BallisticsSite site) noexcept {
        ballisticsSite_ = site;
        applyBallistics();
    }
    void setSidechainHpf(float hz) noexcept { scHpfHz_ = hz; applyScHpf(); }

    // Guard the lookahead length to the fixed buffer budget (RT-safety: the
    // read is always in range). Clamped to [0, maxLookaheadSamples_].
    void setLookahead(int samples) noexcept {
        if (samples < 0) samples = 0;
        if (samples > maxLookaheadSamples_) samples = maxLookaheadSamples_;
        lookaheadSamples_ = samples;
    }
    void setMakeup(float dB) noexcept     { makeupDb_ = dB; updateMakeup(); }
    void setAutoMakeup(bool enabled) noexcept { autoMakeup_ = enabled; updateMakeup(); }
    void setMix(float wet) noexcept        { mix_ = wet; }
    void setOutput(float dB) noexcept {
        outputDb_      = dB;
        outputGainLin_ = dbToLin(dB);
    }

    // Process one sample. `key` is the external sidechain (or the main input
    // when keyless — the caller passes x as key). Returns the wet-mixed,
    // output-trimmed sample (data-model.md "Per-sample chain").
    //
    // RT-safe: noexcept, no heap allocation, no locks, bounded work.
    float process(float x, float key) noexcept {
        // Detection input. Feedback taps the previous POST-makeup, PRE-mix
        // output (research Decision 3); feedforward taps the (optionally
        // HPF-filtered) key. In feedback mode the filtered key would be
        // discarded, so the HPF is skipped there — behaviorally identical to
        // the data-model pseudocode (which filters then discards), but without
        // pointlessly advancing the filter state.
        float detIn;
        if (detection_ == Detection::feedBack) {
            detIn = prevOutput_;
        } else {
            detIn = (scHpfHz_ > 0.0f) ? scFilter_.process(key) : key;
        }

        // Level detection in the decibel domain.
        const float level = detector_.process(detIn);

        // Static curve -> gain reduction (dB, <= 0).
        float grDb = gain_.computeGainDb(level);

        // Gain-site ballistics: smooth the gain-reduction magnitude so
        // engagement uses the attack time constant (see file header).
        if (ballisticsSite_ == BallisticsSite::gain) {
            grDb = -gainSmoother_.process(-grDb);
        }

        // Fold in makeup (auto or manual, cached) and linearize.
        const float gLin = dbToLin(grDb + makeupEffectiveDb_);

        // Main path pre-delay. The delay line is kept current every sample;
        // readFractional(lookaheadSamples_) is an exact integer-sample delay,
        // and readFractional(0) returns the just-written x (bypass at 0).
        lookahead_.write(x);
        const float main =
            lookahead_.readFractional(static_cast<float>(lookaheadSamples_));

        // VCA multiply.
        const float comp = main * gLin;

        // Feedback tap: POST-makeup, PRE-mix.
        prevOutput_ = comp;

        // Parallel dry/wet mix, then output trim.
        const float y = mix_ * comp + (1.0f - mix_) * x;
        return y * outputGainLin_;
    }

private:
    // dB -> linear amplitude. A per-sample transcendental for the gain multiply
    // is acceptable and confined here (the detector's dB conversion lives inside
    // EnvelopeFollower).
    static float dbToLin(float db) noexcept {
        return std::pow(10.0f, db * 0.05f);
    }

    // Route attack/release to the active smoother; hold the inactive smoother
    // instantaneous (attack=release=0) so a site switch never lands on a
    // smoother carrying stale ballistics. In gain-site mode the detector runs
    // ~instantaneous (no level smoothing) and gainSmoother carries the timing.
    void applyBallistics() noexcept {
        if (ballisticsSite_ == BallisticsSite::level) {
            detector_.setAttack(attackSeconds_);
            detector_.setRelease(releaseSeconds_);
            gainSmoother_.setAttack(0.0f);
            gainSmoother_.setRelease(0.0f);
        } else { // gain
            detector_.setAttack(0.0f);
            detector_.setRelease(0.0f);
            gainSmoother_.setAttack(attackSeconds_);
            gainSmoother_.setRelease(releaseSeconds_);
        }
    }

    // (Re)apply the sidechain highpass coefficient. Highpass mode always; the
    // cutoff is only pushed when enabled (> 0). Clamp into DaisySP's valid band
    // (0 < f < fs/3) — the placeholder range is 0..500 Hz, well inside it, but
    // clamp defensively for low sample rates / degenerate inputs (FR-024).
    void applyScHpf() noexcept {
        scFilter_.setMode(SvfMode::highpass);
        if (scHpfHz_ > 0.0f) {
            float f = scHpfHz_;
            const float maxF = sampleRate_ * 0.32f;
            if (f > maxF) f = maxF;
            if (f < 1.0f) f = 1.0f;
            scFilter_.setFreq(f);
        }
    }

    // Recompute the cached effective makeup once per parameter change (research
    // Decision 5), never per sample. Auto-makeup is the closed form
    // -computeGainDb(0 dBFS); it is forced to 0 for the downward (expand/gate)
    // modes so an upward-direction curve never inflates level.
    void updateMakeup() noexcept {
        if (!autoMakeup_) {
            makeupEffectiveDb_ = makeupDb_;
            return;
        }
        if (mode_ == GainMode::expand || mode_ == GainMode::gate) {
            makeupEffectiveDb_ = 0.0f;
        } else {
            makeupEffectiveDb_ = -gain_.computeGainDb(0.0f);
        }
    }

    // -------------------------------------------------------------------
    // Composed sub-units (data-model.md), one set per channel.
    // -------------------------------------------------------------------
    EnvelopeFollower detector_;
    EnvelopeFollower gainSmoother_;
    GainComputer     gain_;
    SvfPrimitive     scFilter_;
    DelayLine        lookahead_;

    // Fixed lookahead backing storage (Constitution VI: no heap in process()).
    // Bound to lookahead_ in prepare(); zero-initialized.
    std::array<float, static_cast<std::size_t>(kLookaheadCapacity)> lookaheadBuffer_{};

    // -------------------------------------------------------------------
    // Configuration (data-model.md "Configuration").
    // -------------------------------------------------------------------
    Detection      detection_        = Detection::feedForward;
    BallisticsSite ballisticsSite_   = BallisticsSite::level;
    GainMode       mode_             = GainMode::compress; // mirror for auto-makeup gating
    float          attackSeconds_    = 0.010f;
    float          releaseSeconds_   = 0.100f;
    float          scHpfHz_          = 0.0f;   // 0 = bypass
    int            lookaheadSamples_ = 0;      // 0 = bypass
    float          makeupDb_         = 0.0f;
    bool           autoMakeup_       = false;
    float          mix_              = 1.0f;
    float          outputDb_         = 0.0f;

    // -------------------------------------------------------------------
    // Cached coefficients (recomputed in setters/prepare, never per-sample).
    // -------------------------------------------------------------------
    float makeupEffectiveDb_ = 0.0f;   // auto or manual, folded to one value
    float outputGainLin_     = 1.0f;   // dbToLin(outputDb_)

    // -------------------------------------------------------------------
    // Runtime state (per channel; cleared by reset(), RT-mutated in process()).
    // -------------------------------------------------------------------
    float prevOutput_ = 0.0f;

    // -------------------------------------------------------------------
    // Prepare-time cache.
    // -------------------------------------------------------------------
    float sampleRate_          = 48000.0f;
    int   maxLookaheadSamples_ = 0;
};

} // namespace acfx

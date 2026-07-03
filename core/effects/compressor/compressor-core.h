#pragma once

#include <cmath>
#include <cstdint>

#include "labs/compressor/gain-computer.h"
#include "primitives/delays/delay-line.h"
#include "primitives/dynamics/envelope-follower.h"
#include "primitives/filters/svf-primitive.h"

// CompressorCore — the RT-safe composition kernel (T003): the SURFACE only.
// This header declares the class's composed members and method signatures;
// method BODIES are deliberately deferred to later tasks (T010:
// prepare/reset/setters + the real per-sample signal chain; T034: wiring the
// composed DelayLine into the main path with a sized lookahead buffer). This
// lets downstream user stories build against a stable, normative shape before
// the chain is implemented (specs/compressors/contracts/compressor-effect-api.md
// "CompressorCore").
//
// NOTE (Setup-phase include path): gain-computer.h currently lives in the
// authoring lab (core/labs/compressor/gain-computer.h, task T002) and is
// git-mv'd to core/primitives/dynamics/gain-computer.h in T009 (graduation,
// same public contract). This include is updated to the graduated path in
// that same commit.
//
// Composition (data-model.md "Entity — CompressorCore"): an EnvelopeFollower
// detector (level detection + level-site ballistics, dB domain), a second
// EnvelopeFollower gainSmoother (gain-site ballistics on the gain-reduction
// signal), a GainComputer gain (the static curve), an SvfPrimitive scFilter
// (sidechain highpass, bypassed at 0 Hz), and a DelayLine lookahead
// (main-path pre-delay, bypassed at 0 samples). Platform-independent: no
// host-framework or embedded-vendor headers (Constitution IV). RT-safe by
// construction — every member is a value (no heap allocation), and
// coefficient work is destined for setters/prepare(), never process()
// (Constitution VI).
//
// Setter convention at THIS stage (skeleton, T003): each setter stores its
// parameter. The five params owned outright by the composed GainComputer
// (mode/threshold/ratio/knee/range) and the detector's peak/rms mode are
// forwarded directly to their composed member's own (already-stubbed)
// setter — trivial 1:1 plumbing, not chain logic, so no state is duplicated
// here (data-model.md deliberately does not list those as CompressorCore's
// own config fields). Every other parameter (detection topology, ballistics
// site, attack/release, sidechain HPF cutoff, lookahead length, makeup,
// auto-makeup, mix, output trim) is CompressorCore's own config field
// (data-model.md "Configuration") and is stored as-is here — routing it into
// the composed detector/gainSmoother/scFilter/lookahead depends on the
// runtime topology (feedForward vs feedBack, level- vs gain-site ballistics)
// and is real chain logic left to T010/T034.
//
// See also: specs/compressors/spec.md,
//           specs/compressors/data-model.md,
//           specs/compressors/contracts/compressor-effect-api.md

namespace acfx {

enum class Detection      : std::uint8_t { feedForward, feedBack };
enum class BallisticsSite : std::uint8_t { level, gain };

class CompressorCore {
public:
    // Prepare for a sample rate; caches fs and the caller's requested max
    // lookahead length. Real coefficient/state setup for the composed
    // sub-units and the lookahead buffer sizing are T010/T034 work.
    void prepare(float sampleRate, int maxLookaheadSamples) noexcept {
        sampleRate_          = sampleRate;
        maxLookaheadSamples_ = maxLookaheadSamples;
    }

    // Clear runtime state; prevOutput cold-starts at the floor (silence,
    // 0.0f — the feedback tap is a linear audio sample, not a dB level).
    // Clearing the composed sub-units' own internal state is T010 work.
    void reset() noexcept { prevOutput_ = 0.0f; }

    // Configuration (recompute cached coefficients; do NOT reset runtime
    // state). Trivial 1:1 forwards to the composed GainComputer/detector —
    // see the file-level comment for why these are not duplicated locally.
    void setMode(GainMode mode) noexcept { gain_.setMode(mode); }
    void setThreshold(float dB) noexcept { gain_.setThreshold(dB); }
    void setRatio(float ratio) noexcept { gain_.setRatio(ratio); }
    void setKnee(float dB) noexcept { gain_.setKnee(dB); }
    void setRange(float dB) noexcept { gain_.setRange(dB); }
    void setDetector(DetectMode mode) noexcept { detector_.setMode(mode); }

    // CompressorCore's own config fields (data-model.md "Configuration");
    // routing into the composed sub-units is real chain logic (T010/T034).
    void setAttack(float seconds) noexcept { attackSeconds_ = seconds; }
    void setRelease(float seconds) noexcept { releaseSeconds_ = seconds; }
    void setDetection(Detection detection) noexcept { detection_ = detection; }
    void setBallisticsSite(BallisticsSite site) noexcept { ballisticsSite_ = site; }
    void setSidechainHpf(float hz) noexcept { scHpfHz_ = hz; }
    void setLookahead(int samples) noexcept { lookaheadSamples_ = samples; }
    void setMakeup(float dB) noexcept { makeupDb_ = dB; }
    void setAutoMakeup(bool enabled) noexcept { autoMakeup_ = enabled; }
    void setMix(float wet) noexcept { mix_ = wet; }
    void setOutput(float dB) noexcept { outputDb_ = dB; }

    // Process one sample. `key` is the external sidechain (or the main input
    // when keyless). Stub (task T003): unity passthrough of the main input;
    // the real chain (data-model.md "Per-sample chain") lands in T010.
    float process(float x, float key) noexcept {
        (void)key;
        return x;
    }

private:
    // -------------------------------------------------------------------
    // Composed sub-units (data-model.md "Entity — CompressorCore — Composed
    // primitives"), one set per channel.
    // -------------------------------------------------------------------
    EnvelopeFollower detector_;
    EnvelopeFollower gainSmoother_;
    GainComputer     gain_;
    SvfPrimitive     scFilter_;
    DelayLine        lookahead_;

    // -------------------------------------------------------------------
    // Configuration (data-model.md "Entity — CompressorCore — Configuration").
    // -------------------------------------------------------------------
    Detection      detection_      = Detection::feedForward;
    BallisticsSite ballisticsSite_ = BallisticsSite::level;
    float          attackSeconds_  = 0.010f;
    float          releaseSeconds_ = 0.100f;
    float          scHpfHz_        = 0.0f;   // 0 = bypass
    int            lookaheadSamples_ = 0;    // 0 = bypass
    float          makeupDb_       = 0.0f;
    bool           autoMakeup_     = false;
    float          mix_            = 1.0f;
    float          outputDb_       = 0.0f;

    // -------------------------------------------------------------------
    // Runtime state (per channel; cleared by reset(), RT-mutated in
    // process()).
    // -------------------------------------------------------------------
    float prevOutput_ = 0.0f;

    // -------------------------------------------------------------------
    // Prepare-time cache.
    // -------------------------------------------------------------------
    float sampleRate_          = 48000.0f;
    int   maxLookaheadSamples_ = 0;
};

} // namespace acfx

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "effects/saturation/saturation-core.h"
#include "effects/saturation/saturation-voicings.h"
#include "primitives/dynamics/dynamics-modulator.h"
#include "primitives/dynamics/envelope-follower.h"
#include "primitives/filters/svf-primitive.h"

// ProgramDependentSaturationCore — the RT-safe per-channel composition
// kernel. It composes the shipped SaturationCore, EnvelopeFollower, and
// SvfPrimitive (all UNCHANGED) with four DynamicsModulator instances to run
// the per-sample chain: source-select -> sidechain HPF -> topology fork ->
// dB detect -> normalize over the ref window -> per-target base+offset ->
// SaturationCore::process (per data-model.md "Per-sample chain" and
// contracts/program-dependent-saturation-effect-api.md
// "ProgramDependentSaturationCore"). Tone is modulated per-block (newBlock);
// drive/bias/mix per-sample.
//
// Composition (data-model.md "Entity — ProgramDependentSaturationCore"),
// one set per channel:
//   saturation  SaturationCore     the nonlinearity (voicings, drive/bias/
//                                  tone/mix/output) — composed UNCHANGED
//                                  (FR-004/026); no nonlinearity kernel is
//                                  added here.
//   detector    EnvelopeFollower   shared level detection + ballistics,
//                                  decibel domain; feeds all four modulators.
//   scFilter    SvfPrimitive       sidechain highpass (bypassed at 0 Hz).
//   driveMod/biasMod/toneMod/mixMod  DynamicsModulator x4  per-target signed-
//                                  offset mappers (envelope -> offset), each
//                                  with its own depth + curve, no cross-talk.
//
// DynamicsModulator is composed from its GRADUATED primitive path
// (core/primitives/dynamics/dynamics-modulator.h — the dynamics/ category's
// third inhabitant, graduated via `git mv` in T009).
//
// Platform-independent (Constitution IV): no host-framework or embedded-
// vendor headers. RT-safe by construction (Constitution VI): every member is
// a value (no heap allocation), and all coefficient/config work lives in
// prepare()/setters, never process().
//
// See also: specs/program-dependent-saturation/spec.md,
//           specs/program-dependent-saturation/data-model.md,
//           specs/program-dependent-saturation/contracts/program-dependent-saturation-effect-api.md

namespace acfx {

enum class ModTarget : std::uint8_t { drive, bias, tone, mix };
enum class Detection : std::uint8_t { feedForward, feedBack };

class ProgramDependentSaturationCore {
public:
    // Prepare/init all composed units. The detector runs in the DECIBEL domain
    // (Decision 1: the dB envelope is normalized over the ref window), so it is
    // init()'d first (which baselines at the current — linear — floor) and then
    // switched to decibel, which re-baselines env_/y1_ to the −120 dB floor so
    // silence reads correctly on the first sample (envelope-follower.h
    // setDomain). Caches the sample rate (for the SC-HPF cutoff clamp) and the
    // ref window; clears the feedback tap and per-block tone offset. No audio
    // work (Constitution VI): all coefficient/config work lives here + in the
    // setters, never in process().
    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0f) ? sampleRate : 48000.0f;
        saturation_.prepare(sampleRate_);
        detector_.init(sampleRate_);
        detector_.setDomain(DetectDomain::decibel);
        scFilter_.init(sampleRate_);
        // Re-assert the SC-HPF configuration for the (possibly new) sample rate.
        applyScHpf();
        prevOutput_ = 0.0f;
        toneBlockOffset_ = 0.0f;
        lastNorm_ = 0.0f;
    }

    // Clear the composed units' running state + the feedback tap. detector_ /
    // scFilter_ / saturation_ re-baseline their own state; the detector keeps
    // the decibel domain established in prepare() (reset() does not change the
    // domain), so env_/y1_ baseline to the −120 dB floor.
    void reset() noexcept {
        saturation_.reset();
        detector_.reset();
        scFilter_.reset();
        prevOutput_ = 0.0f;
        toneBlockOffset_ = 0.0f;
        lastNorm_ = 0.0f;
    }

    // ------------------------------------------------------------------
    // Static base parameters. drive/bias/tone/mix are the four modulation
    // TARGETS: their base is stored in the target's NATIVE modulation unit
    // (drive dB, bias/tone −1..+1, mix 0..1) and the modulated value is pushed
    // to saturation_ in process()/newBlock(). voicing/output/quality are NOT
    // modulation targets — they forward directly.
    // ------------------------------------------------------------------

    // REFINEMENT (documented, vs. the T003 skeleton `setStaticDrive(gainLinear)`):
    // the drive base is held in dB (0..48), NOT as a linear gain. The clarify
    // decision modulates drive in its native dB range (Decision 1: `offset =
    // depth·span·curve(norm)` in dB), and orthogonality (FR-007) requires the
    // dB→linear conversion to be dbToGain() applied to the FINAL modulated dB —
    // exactly as SaturationEffect converts its drive descriptor (saturation-
    // effect.h dbToGain). So the effect wrapper (T011) must pass drive in dB
    // here (unlike setOutput, which stays a linear gain like SaturationCore).
    void setStaticDrive(float driveDb) noexcept { staticDriveDb_ = driveDb; }
    void setVoicing(SaturationVoicing voicing) noexcept { saturation_.setVoicing(voicing); }
    // tone is per-BLOCK (Decision 4: setTone recomputes SVF coefficients). The
    // static tilt is pushed to saturation_ ONCE here so that when tone depth is
    // 0, newBlock() can skip setTone entirely (no redundant SVF recompute) and
    // saturation_ already carries the correct static tilt — the tone half of the
    // zero-depth orthogonality identity (FR-007).
    void setStaticTone(float tilt) noexcept {
        staticTone_ = tilt;
        saturation_.setTone(tilt);
    }
    void setStaticMix(float wet) noexcept { staticMix_ = wet; }
    // Linear makeup gain (like SaturationCore::setOutput) — not a modulation
    // target, forwarded directly.
    void setOutput(float gainLinear) noexcept { saturation_.setOutput(gainLinear); }
    void setStaticBias(float bias) noexcept { staticBias_ = bias; }
    void setQuality(SaturationQuality quality) noexcept { saturation_.setQuality(quality); }

    // ------------------------------------------------------------------
    // Detector (shared EnvelopeFollower) configuration — forwarded.
    // ------------------------------------------------------------------
    void setDetectorMode(DetectMode mode) noexcept { detector_.setMode(mode); }
    void setBallistics(Ballistics ballistics) noexcept { detector_.setBallistics(ballistics); }
    void setAttack(float seconds) noexcept { detector_.setAttack(seconds); }
    void setRelease(float seconds) noexcept { detector_.setRelease(seconds); }
    void setDetection(Detection detection) noexcept { detection_ = detection; }
    // Normalization window (default −60..0 dBFS). Degenerate (lo==hi) windows are
    // tolerated: process() guards the zero-width divide to a bounded norm of 0.
    void setRefWindow(float loDb, float hiDb) noexcept {
        refLo_ = loDb;
        refHi_ = hiDb;
    }

    // ------------------------------------------------------------------
    // Modulation matrix — per target depth + curve. No cross-talk: each target
    // owns its own DynamicsModulator. toneDepth_ is cached so newBlock() can
    // apply the zero-depth skip guard (orthogonality) without a depth getter.
    // ------------------------------------------------------------------
    void setDepth(ModTarget target, float signedDepth) noexcept {
        switch (target) {
        case ModTarget::drive: driveMod_.setDepth(signedDepth); break;
        case ModTarget::bias:  biasMod_.setDepth(signedDepth); break;
        case ModTarget::tone:
            toneMod_.setDepth(signedDepth);
            toneDepth_ = signedDepth;
            // Disabling tone modulation must REVERT saturation_ to the static
            // tilt (FR-007): newBlock() no-ops at depth 0, so without this the
            // tone would freeze at its last modulated value. Control-thread only.
            if (signedDepth == 0.0f)
                saturation_.setTone(staticTone_);
            break;
        case ModTarget::mix:   mixMod_.setDepth(signedDepth); break;
        }
    }
    void setCurve(ModTarget target, ModCurve curve) noexcept {
        switch (target) {
        case ModTarget::drive: driveMod_.setCurve(curve); break;
        case ModTarget::bias:  biasMod_.setCurve(curve); break;
        case ModTarget::tone:  toneMod_.setCurve(curve); break;
        case ModTarget::mix:   mixMod_.setCurve(curve); break;
        }
    }

    // ------------------------------------------------------------------
    // Sidechain. externalKey routes detection to the key input; scHpf configures
    // (or bypasses at hz<=0) the sidechain highpass.
    // ------------------------------------------------------------------
    void setExternalKey(bool enabled) noexcept { externalKey_ = enabled; }
    void setScHpf(float hz) noexcept {
        scHpfHz_ = hz;
        applyScHpf();
    }

    // ------------------------------------------------------------------
    // Audio path. `x` is the main sample; `key` the optional external sidechain
    // sample. drive/bias/mix modulate per-sample; tone per-block.
    // ------------------------------------------------------------------

    // Apply the per-BLOCK tone modulation (Decision 4). ORTHOGONALITY GUARD
    // (FR-007): when tone depth is 0 this does NOTHING — it must not call
    // saturation_.setTone (which would re-run the SVF tilt recompute and, more
    // importantly, could push a value differing from the static tilt); the
    // static tilt set by setStaticTone() stays in effect. When depth != 0, the
    // block-representative normalized envelope `blockEnvNorm` (0..1) drives the
    // tone offset over its native half-span.
    // Most-recent per-sample normalized-window envelope (0..1), carried from the
    // previous block. The effect wrapper feeds this back into newBlock() so the
    // per-block tone tilt tracks the block's representative level with a
    // one-block (control-rate) lag — fine for a slow spectral tilt (Decision 4).
    // Pure read of a value store; it does NOT participate in process()'s output
    // (byte-for-byte orthogonality with the standalone saturator is preserved).
    float lastNorm() const noexcept { return lastNorm_; }

    void newBlock(float blockEnvNorm) noexcept {
        if (toneDepth_ == 0.0f)
            return;
        const float offset = toneMod_.modulate(blockEnvNorm) * kToneSpan;
        const float tone = clampf(staticTone_ + offset, -1.0f, 1.0f);
        toneBlockOffset_ = tone - staticTone_;
        saturation_.setTone(tone);
    }

    // Per-sample chain (data-model.md "Per-sample chain"):
    //   src  = externalKey ? key : x
    //   src  = scHpfActive ? scFilter(src) : src
    //   det  = feedBack ? prevOutput : src
    //   env  = detector(det)                     // dB-domain envelope
    //   norm = clamp((env − refLo)/(refHi − refLo), 0, 1)
    //   drive'= clamp(staticDriveDb + driveMod(norm)*span, 0, 48) → setDrive(dbToGain(drive'))
    //   bias' = clamp(staticBias  + biasMod(norm)*span,  −1, 1) → setBias(bias')
    //   mix'  = clamp(staticMix   + mixMod(norm)*span,    0, 1) → setMix(mix')
    //   y = saturation.process(x); prevOutput = y; return y
    //
    // ZERO-DEPTH ORTHOGONALITY (FR-007, branchless): DynamicsModulator::modulate
    // returns EXACTLY 0 when depth==0, so `base + 0*span == base`; the clamp is a
    // no-op for an in-range base, and dbToGain(staticDriveDb_) is byte-identical
    // to the scalar SaturationEffect would push once — so pushing every sample is
    // the same result. drive/bias/mix are therefore pushed unconditionally (the
    // cleaner branchless path); only tone is skipped when its depth is 0 (see
    // newBlock — tone is the one whose setter recomputes SVF coefficients).
    //
    // The chain is SPLIT into two composable steps (T036) so the effect's linked
    // stereo mode can drive a SHARED normalized envelope across all channels:
    //   detectNorm(x, key)     — the detection half (source-select → SC-HPF →
    //                            topology fork → detect → normalize) returning the
    //                            0..1 window envelope; the ONLY step that advances
    //                            detector_/scFilter_ state and reads prevOutput_.
    //   processWithNorm(x,norm)— the modulation half (per-target base+offset push
    //                            → saturation_.process → feedback-tap store).
    // process(x, key) composes them so perChannel + every existing direct-core
    // test (orthogonality, topology, THD, matrix) stays byte-for-byte identical.
    //
    // SIGNATURE NOTE (deviation from the T036 sketch's `detectNorm(float key)`):
    // detectNorm keeps BOTH x and key because the feedforward source is
    // `externalKey_ ? key : x` (data-model.md "Per-sample chain"). Dropping x
    // would break the direct-core topology test, which calls `process(x, 0.0f)`
    // with externalKey off and expects feedforward to detect on x — not the
    // sentinel 0.0f key. The effect's linked path sidesteps this by passing the
    // already-resolved cross-channel-max source as BOTH args (see the effect).
    float detectNorm(float x, float key) noexcept {
        float src = externalKey_ ? key : x;
        if (scHpfActive_)
            src = scFilter_.process(src);
        const float det = (detection_ == Detection::feedBack) ? prevOutput_ : src;
        return normalizedFrom(det);
    }

    // Linked stereo shared-detection path (FR-013/SC-011). The routing supplies
    // the cross-channel detection source it chose — in feedforward the largest-
    // magnitude input, in feedBack the largest-magnitude previous OUTPUT across
    // the linked channels — and this runs the detector + window normalize on it.
    // The per-channel key/HPF/topology fork is intentionally NOT re-applied: the
    // routing already resolved the source, and (matching per-channel feedback,
    // which taps the un-HPF'd previous output) the SC-HPF does not apply to a
    // fed-back output. One shared norm still drives every channel, so the stereo
    // image stays stable — the max is over channels, not a per-channel detector.
    float detectNormShared(float sharedSrc) noexcept { return normalizedFrom(sharedSrc); }

    // Cross-thread-free accessors used by the linked routing (control/audio
    // thread; pure reads). prevOutput() is the feedback tap value; feedback
    // topology selects the output-domain cross-channel max in linked mode.
    float prevOutput() const noexcept { return prevOutput_; }
    bool feedbackDetection() const noexcept { return detection_ == Detection::feedBack; }

    // Apply the modulation half for a pre-computed normalized envelope `norm`.
    // perChannel passes each channel's OWN detectNorm result; linked passes ONE
    // shared norm to every channel (image-stable common modulation). ZERO-DEPTH
    // ORTHOGONALITY holds for ANY norm: with every depth 0 the modulate() terms
    // are exactly 0, so drive'/bias'/mix' collapse to the static bases regardless
    // of the norm the linked path feeds in.
    float processWithNorm(float x, float norm) noexcept {
        lastNorm_ = norm; // carry the applied norm on EVERY channel (linked too)

        const float driveDb = clampf(staticDriveDb_ + driveMod_.modulate(norm) * kDriveSpanDb,
                                     kDriveMinDb, kDriveMaxDb);
        saturation_.setDrive(dbToGain(driveDb));
        const float bias = clampf(staticBias_ + biasMod_.modulate(norm) * kBiasSpan, -1.0f, 1.0f);
        saturation_.setBias(bias);
        const float mix = clampf(staticMix_ + mixMod_.modulate(norm) * kMixSpan, 0.0f, 1.0f);
        saturation_.setMix(mix);

        const float y = saturation_.process(x);
        prevOutput_ = y; // feedback tap = the FINAL output (Decision 3)
        return y;
    }

    float process(float x, float key) noexcept { return processWithNorm(x, detectNorm(x, key)); }

private:
    // dB → linear gain. MIRRORS SaturationEffect::dbToGain EXACTLY (saturation-
    // effect.h) so the modulated-drive orthogonality identity is byte-for-byte:
    // at zero depth the modulated dB == staticDriveDb_ and this reproduces the
    // scalar SaturationEffect pushes into SaturationCore::setDrive.
    static float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

    static float clampf(float v, float lo, float hi) noexcept {
        return std::min(std::max(v, lo), hi);
    }

    // Run the detector on a resolved detection source and normalize its dB
    // envelope over the reference window into [0,1]; carry lastNorm_ for the
    // effect's per-block newBlock(). Shared by detectNorm (per-channel) and
    // detectNormShared (linked). A degenerate (zero-width or inverted) window is
    // guarded to a bounded 0 — never a divide-by-zero / NaN (Constitution V).
    float normalizedFrom(float det) noexcept {
        const float envDb = detector_.process(det);
        const float denom = refHi_ - refLo_;
        float norm = (denom > 0.0f) ? (envDb - refLo_) / denom : 0.0f;
        norm = clampf(norm, 0.0f, 1.0f);
        lastNorm_ = norm;
        return norm;
    }

    // (Re)configure the sidechain highpass from scHpfHz_. hz<=0 bypasses (the
    // flag short-circuits scFilter_ in process()). A positive cutoff is clamped
    // just under DaisySP's sampleRate/3 stability bound (mirrors SaturationCore's
    // emphasis clamp) and to a low floor, so a degenerate cutoff never destabilizes
    // the SVF.
    void applyScHpf() noexcept {
        if (scHpfHz_ <= 0.0f) {
            scHpfActive_ = false;
            return;
        }
        scHpfActive_ = true;
        const float maxFreq = sampleRate_ * 0.32f;
        float hz = scHpfHz_;
        if (hz > maxFreq) hz = maxFreq;
        if (hz < 20.0f)   hz = 20.0f;
        scFilter_.setMode(SvfMode::highpass);
        scFilter_.setFreq(hz);
        scFilter_.setRes(0.0f);
    }

    // -------------------------------------------------------------------
    // Native modulation spans — the maximum absolute offset a full-depth
    // modulation (depth=±1) at full envelope (norm=1) applies to the target
    // base, before the target-range clamp (offset = depth·span·curve(norm)).
    // Documented tuning-pass choices: each span lets depth=±1 traverse the
    // target's meaningful range from its base (drive across the whole 0..48 dB
    // descriptor; bias/tone the ±1 half-span from center; mix across the whole
    // 0..1 range). All get clamped to the native range afterward.
    // -------------------------------------------------------------------
    // Modulation spans (tuning-pass placeholders). NEGATIVE-DEPTH NOTE (TASK-5):
    // with a negative drive depth the drive offset falls with level at slope
    // kDriveSpanDb / windowWidthDb dB per input-dB. At the default −60..0 dBFS
    // window that is 48/60 = 0.8 < 1, so rising input level (1 dB/dB) outpaces the
    // drive reduction — negative depth SLOWS the THD rise rather than reversing
    // it. Genuine "less saturation as it gets louder" softening requires a window
    // NARROWER than the drive span (e.g. −30..0 via setRefWindow). This is a
    // tuning characteristic of the window/span relationship, not a correctness
    // guarantee; reconciling the default is a captured tuning-pass decision.
    static constexpr float kDriveSpanDb = 48.0f; // full drive descriptor range
    static constexpr float kBiasSpan    = 1.0f;  // ±1 half-span from center
    static constexpr float kToneSpan    = 1.0f;  // ±1 half-span from center
    static constexpr float kMixSpan     = 1.0f;  // full 0..1 dry/wet range

    static constexpr float kDriveMinDb = 0.0f;
    static constexpr float kDriveMaxDb = 48.0f;

    // -------------------------------------------------------------------
    // Composed sub-units (data-model.md "Composed units"), one set per channel.
    // -------------------------------------------------------------------
    SaturationCore   saturation_;
    EnvelopeFollower detector_;
    SvfPrimitive     scFilter_;
    DynamicsModulator driveMod_;
    DynamicsModulator biasMod_;
    DynamicsModulator toneMod_;
    DynamicsModulator mixMod_;

    // -------------------------------------------------------------------
    // Static base parameters (native units) the modulation offsets add to.
    // Defaults mirror SaturationCore's own defaults (0 dB drive, symmetric bias,
    // flat tone, fully wet mix) so a freshly-prepared core with all depths 0 is
    // the static saturator.
    // -------------------------------------------------------------------
    float staticDriveDb_ = 0.0f; // dB (0..48); dbToGain(0) == 1.0 == SaturationCore default drive
    float staticBias_    = 0.0f; // −1..+1
    float staticTone_    = 0.0f; // −1..+1
    float staticMix_     = 1.0f; // 0..1

    // -------------------------------------------------------------------
    // Detector / sidechain / topology configuration.
    // -------------------------------------------------------------------
    Detection detection_   = Detection::feedForward;
    float     refLo_       = -60.0f; // normalization window low (dBFS)
    float     refHi_       = 0.0f;   // normalization window high (dBFS)
    bool      externalKey_ = false;
    float     scHpfHz_     = 0.0f;   // 0 = bypass
    bool      scHpfActive_ = false;
    float     toneDepth_   = 0.0f;   // cached tone depth for the newBlock skip guard
    float     sampleRate_  = 48000.0f;

    // -------------------------------------------------------------------
    // Runtime state (per channel; cleared by reset(), RT-mutated in process()).
    // -------------------------------------------------------------------
    float prevOutput_ = 0.0f;      // Feedback tap (final output y); cold-start = 0.
    float toneBlockOffset_ = 0.0f; // The per-block tone offset in effect.
    float lastNorm_ = 0.0f;        // Most-recent normalized-window envelope (effect's newBlock carry).
};

} // namespace acfx

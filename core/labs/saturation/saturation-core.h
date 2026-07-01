#pragma once

#include <cmath>
#include <cstdint>

#include "labs/saturation/saturation-voicings.h"
#include "primitives/filters/svf-primitive.h"
#include "primitives/nonlinear/adaa-waveshaper.h"
#include "primitives/nonlinear/waveshaper.h"

// SaturationCore — the RT-safe composition kernel (T004): the SURFACE only.
// This header declares the class's composed members and method signatures;
// method BODIES are deliberately deferred to later tasks (T009: prepare/reset/
// setters; T010: the naive-quality signal chain in process(); T018: the ADAA
// path selection in process()). This lets US1 (SaturationEffect) build against
// a stable, normative shape before the chain is implemented
// (specs/saturation/contracts/saturation-api.md "SaturationCore").
//
// Composition (no new DSP primitive — FR-001): three SvfPrimitive instances
// (pre-emphasis, post-de-emphasis, tone-tilt), plus a Waveshaper (naive) and
// an ADAAWaveshaper (adaa); `quality_` selects which one drives the nonlinear
// stage at process() time (data-model.md "SaturationCore — Composed
// sub-units"). Platform-independent: no JUCE / libDaisy / Teensy headers
// (Constitution IV). RT-safe by construction — every member is a value (no
// heap allocation), and coefficient/table work is destined for prepare(),
// never process() (Constitution VI).

namespace acfx {

class SaturationCore {
public:
    // Build per-voicing SVF coefficients and prepare the composed sub-units
    // for sampleRate. No audio-path work (all coefficient/table work lives here
    // and in the setters, never in process() — Constitution VI).
    void prepare(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        preEmphasis_.init(sampleRate);
        postDeEmphasis_.init(sampleRate);
        toneTilt_.init(sampleRate);
        naiveShaper_.init(sampleRate);
        adaaShaper_.init(sampleRate);
        // Bake the current voicing (shapes + gain-comp + emphasis curves) and
        // push the current scalar parameters into the composed sub-units.
        configureShapers();
        applyEmphasisCoeffs();
        applyToneTilt();
        applyDrive();
        applyBias();
    }

    // Clear filter + DC-blocker state across all composed sub-units, without
    // discarding applied parameter state (mirrors Waveshaper::reset). Because
    // SvfPrimitive::reset() re-Init()s the DaisySP filter — which discards the
    // configured cutoff/resonance back to library defaults — the filter
    // coefficients MUST be re-applied afterward (same shape as
    // SvfEffect::reset()'s applyAll()).
    void reset() noexcept {
        preEmphasis_.reset();
        postDeEmphasis_.reset();
        toneTilt_.reset();
        naiveShaper_.reset();
        adaaShaper_.reset();
        applyEmphasisCoeffs();
        applyToneTilt();
    }

    // Select the named voicing: bakes in shape[voicing] plus the pre-emphasis
    // and post-de-emphasis filter curves (voicingConfig() in
    // saturation-voicings.h). Reconfiguring filter coefficients here is safe
    // under the SvfEffect thread model (control thread, stream stopped).
    void setVoicing(SaturationVoicing voicing) noexcept {
        voicing_ = voicing;
        configureShapers();
        applyEmphasisCoeffs();
    }

    // Select the naive/adaa nonlinear evaluation path (SaturationQuality::
    // oversampled is a reserved, unwired seam — FR-015; it falls back to the
    // adaa path here, a defined bounded behavior, never a partial/aliased one).
    void setQuality(SaturationQuality quality) noexcept { quality_ = quality; }

    // Pre-gain applied ahead of the nonlinearity (linear gain, not dB). Pushed
    // to BOTH shapers so switching quality keeps the realized drive consistent.
    void setDrive(float gainLinear) noexcept {
        driveGain_ = gainLinear;
        applyDrive();
    }

    // USER asymmetry dial, applied alongside drive ahead of the nonlinearity.
    // Not baked into the voicing (data-model.md Decision 5). Pushed to BOTH
    // shapers so the naive/adaa paths stay consistent across a quality switch.
    void setBias(float bias) noexcept {
        bias_ = bias;
        applyBias();
    }

    // Post-de-emphasis tone tilt amount, -1..+1 (see applyToneTilt() for the
    // realization). Control thread; recomputes toneTilt_ coefficients.
    void setTone(float tilt) noexcept {
        toneAmount_ = tilt;
        applyToneTilt();
    }

    // Dry/wet parallel blend, 0..1.
    void setMix(float wet) noexcept { mix_ = wet; }

    // Makeup trim applied after the mix stage (linear gain, not dB).
    void setOutput(float gainLinear) noexcept { outputGain_ = gainLinear; }

    // The normative signal chain (saturation-api.md "Signal chain"):
    //   wet = preEmphasis[voicing](x)
    //   wet = shaper(wet)          // shaper applies drive*wet + bias, its own
    //                              // DC-blocker, and gain-comp internally —
    //                              // drive/bias are NOT pre-multiplied here
    //                              // (research.md Decision 3): the contract's
    //                              // shaper(drive*x + bias) is realized BY the
    //                              // shaper's setDrive/setBias.
    //   wet = postDeEmphasis[voicing](wet)
    //   wet = toneTilt(wet)
    //   y   = mix*wet + (1 - mix)*x
    //   y   = outputGain * y
    //   return y
    // RT-safe: noexcept, no heap allocation, no locks, bounded work.
    float process(float x) noexcept {
        float wet = preEmphasis_.process(x);
        wet = (quality_ == SaturationQuality::naive)
                  ? naiveShaper_.process(wet)
                  : adaaShaper_.process(wet);
        wet = postDeEmphasis_.process(wet);
        wet = toneTilt_.process(wet);
        float y = mix_ * wet + (1.0f - mix_) * x;
        y *= outputGain_;
        return y;
    }

private:
    // -------------------------------------------------------------------
    // Configuration helpers — all coefficient/shape work, off the audio path.
    // Centralized so prepare()/reset()/setters realize IDENTICAL state (the
    // reset-determinism contract in saturation-core-test.cpp TEST 6 depends on
    // this: reset() must reproduce exactly what prepare()+setters established).
    // -------------------------------------------------------------------

    // Bake the current voicing's nonlinear shape into BOTH shapers and enable
    // their internal gain-compensation (FR-004): SaturationCore exposes no
    // gain-comp setter — it is ON by construction so composed loudness stays
    // bounded as drive rises (saturation-harmonics-test.cpp TEST 2).
    void configureShapers() noexcept {
        const VoicingConfig cfg = voicingConfig(voicing_);
        naiveShaper_.setShape(cfg.shape);
        // ADAAWaveshaper::setShape throws only for a shape with no analytic
        // antiderivative; every voicing shape has one, so this never throws in
        // practice (keeping this noexcept path well-defined).
        //
        // T016 INVARIANT (do not weaken without re-checking the caller):
        // SaturationEffect::applyPending() calls setVoicing() (hence this
        // function) INSIDE process() on the audio thread (saturation-effect.h),
        // so this call must never actually throw. That safety rests entirely on
        // every SaturationVoicing's shape being ADAA-safe
        // (shape::hasAntiderivative(cfg.shape) == true for all four voicings,
        // saturation-voicings.h). That invariant is asserted for every voicing
        // in tests/core/saturation-voicings-test.cpp ("every voicing's shape has
        // an antiderivative...") -- a future voicing added with an
        // antiderivative-less shape (e.g. Shape::biasedAsym) will fail THAT
        // test, not throw out of process() at runtime.
        adaaShaper_.setShape(cfg.shape);
        naiveShaper_.setGainCompensation(true);
        adaaShaper_.setGainCompensation(true);
    }

    // (Re)apply the pre-/post-emphasis SVF coefficients from the current
    // voicing. Mode/freq/res are pushed in a fixed order so the DaisySP
    // damp/freq recomputation is deterministic across prepare()/reset().
    void applyEmphasisCoeffs() noexcept {
        const VoicingConfig cfg = voicingConfig(voicing_);
        applyEmphasis(preEmphasis_, cfg.pre);
        applyEmphasis(postDeEmphasis_, cfg.post);
    }

    static void applyEmphasis(SvfPrimitive& svf, const EmphasisConfig& cfg) noexcept {
        svf.setMode(cfg.mode);
        svf.setFreq(cfg.cutoffHz);
        svf.setRes(cfg.resonance);
    }

    // Tone-tilt realization (the exact tone LAW is a carried open question —
    // data-model.md; this is a simple, defensible tilt that is TRANSPARENT at
    // tone=0, which is all the US1 tests exercise):
    //   tone <  0 (darker):   lowpass whose cutoff falls from ~open toward
    //                         kToneDarkHz as tone -> -1 (rolls off highs).
    //   tone >= 0 (brighter): highpass whose cutoff rises from ~sub-audio
    //                         (transparent) toward kToneBrightHz as tone -> +1
    //                         (thins the low end).
    // Cutoffs are interpolated on a log scale; resonance is held flat.
    void applyToneTilt() noexcept {
        const float openHz = sampleRate_ * kToneOpenFrac; // ~just under sr/3
        if (toneAmount_ < 0.0f) {
            const float frac = toneAmount_ + 1.0f; // 0 at -1, ->1 at 0
            const float hz = kToneDarkHz * std::pow(openHz / kToneDarkHz, frac);
            toneTilt_.setMode(SvfMode::lowpass);
            toneTilt_.setFreq(hz);
        } else {
            const float hz =
                kToneFlatHpHz * std::pow(kToneBrightHz / kToneFlatHpHz, toneAmount_);
            toneTilt_.setMode(SvfMode::highpass);
            toneTilt_.setFreq(hz);
        }
        toneTilt_.setRes(kToneRes);
    }

    void applyDrive() noexcept {
        naiveShaper_.setDrive(driveGain_);
        adaaShaper_.setDrive(driveGain_);
    }

    void applyBias() noexcept {
        naiveShaper_.setBias(bias_);
        adaaShaper_.setBias(bias_);
    }

    // Tone-tilt tuning constants (open question — simple defensible values).
    static constexpr float kToneOpenFrac = 0.32f;   // open LP cutoff ~= sr*0.32
    static constexpr float kToneDarkHz   = 1500.0f; // darkest LP cutoff (tone=-1)
    static constexpr float kToneFlatHpHz = 5.0f;    // transparent HP cutoff (tone=0)
    static constexpr float kToneBrightHz = 1000.0f; // brightest HP cutoff (tone=+1)
    static constexpr float kToneRes      = 0.0f;    // flat, no resonance peak

    // -------------------------------------------------------------------
    // Composed sub-units (data-model.md "SaturationCore — Composed
    // sub-units"). Three SvfPrimitive stages plus both nonlinear-stage
    // backends; quality_ (below) selects naiveShaper_ vs adaaShaper_ at
    // process() time.
    // -------------------------------------------------------------------
    SvfPrimitive preEmphasis_;
    SvfPrimitive postDeEmphasis_;
    SvfPrimitive toneTilt_;
    Waveshaper naiveShaper_;
    ADAAWaveshaper adaaShaper_;

    // -------------------------------------------------------------------
    // Applied parameter state (data-model.md "SaturationCore — Applied
    // parameter state"), denormalized and set from the effect wrapper.
    // Defaults mirror the effect's planning defaults (data-model.md
    // "SaturationEffect — Parameters"): drive/output unity gain, no bias,
    // softClip voicing, adaa quality, flat tone, fully wet mix.
    // -------------------------------------------------------------------
    float driveGain_ = 1.0f;
    float bias_ = 0.0f;
    SaturationVoicing voicing_ = SaturationVoicing::softClip;
    SaturationQuality quality_ = SaturationQuality::adaa;
    float toneAmount_ = 0.0f;
    float mix_ = 1.0f;
    float outputGain_ = 1.0f;
    float sampleRate_ = 48000.0f;
};

} // namespace acfx

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "effects/saturation/saturation-voicings.h"
#include "primitives/filters/svf-primitive.h"
#include "primitives/nonlinear/adaa-waveshaper.h"
#include "primitives/nonlinear/waveshaper.h"
#include "primitives/oversampling/oversampler.h"

// SaturationCore — the RT-safe composition kernel (T004): the SURFACE only.
// This header declares the class's composed members and method signatures;
// method BODIES are deliberately deferred to later tasks (T009: prepare/reset/
// setters; T010: the naive-quality signal chain in process(); T018: the ADAA
// path selection in process()). This lets US1 (SaturationEffect) build against
// a stable, normative shape before the chain is implemented
// (specs/saturation/contracts/saturation-api.md "SaturationCore").
//
// Composition (FR-001, plus the oversampling sibling wired in for FR-015):
// three SvfPrimitive instances (pre-emphasis, post-de-emphasis, tone-tilt),
// plus a Waveshaper (naive), an ADAAWaveshaper (adaa), and an
// Oversampler<4> driving a second Waveshaper (oversampled — a naive shaper
// run at 4x, since oversampling itself supplies the anti-aliasing);
// `quality_` selects which backend drives the nonlinear stage at process()
// time (data-model.md "SaturationCore — Composed sub-units"). Platform-
// independent: no host-framework or embedded-vendor headers (Constitution
// IV). RT-safe by construction — every member is a value (no heap
// allocation), and coefficient/table work is destined for prepare(), never
// process() (Constitution VI).

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
        oversampler_.init(sampleRate);
        // oversampledShaper_ runs INSIDE the oversampler at the oversampled
        // rate (Factor x sampleRate), so its rate-dependent state (the
        // DC-blocker) must be prepared at oversampler_.oversampledRate(),
        // not the base sampleRate used by naiveShaper_/adaaShaper_.
        oversampledShaper_.init(oversampler_.oversampledRate());
        // Clear the dry-path mix-alignment ring (base rate).
        dryMixRing_.fill(0.0f);
        dryMixWrite_ = 0;
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
        oversampler_.reset();
        oversampledShaper_.reset();
        dryMixRing_.fill(0.0f);
        dryMixWrite_ = 0;
        applyEmphasisCoeffs();
        applyToneTilt();
    }

    // Select the named voicing: bakes in shape[voicing] plus the pre-emphasis
    // and post-de-emphasis filter curves (voicingConfig() in
    // saturation-voicings.h). NOTE: this reconfigures filter coefficients + shapes,
    // and SaturationEffect::applyPending() invokes it ON THE AUDIO THREAD inside
    // process() (see configureShapers() below, and saturation-effect.h) — so the
    // work here MUST stay RT-safe (allocation-free, lock-free, bounded). Do not
    // add control-thread-only work (heap, locks, blocking) to this path.
    void setVoicing(SaturationVoicing voicing) noexcept {
        voicing_ = voicing;
        configureShapers();
        applyEmphasisCoeffs();
    }

    // Select the naive/adaa/oversampled nonlinear evaluation path.
    // SaturationQuality::oversampled is now WIRED to the real Oversampler<4>
    // primitive (FR-015 closed by design:primitive/oversampling); see
    // process() for the realized oversampled chain and its documented
    // latency characteristic.
    //
    // No-stale-state note (T018): this setter ONLY swaps which composed
    // shaper process() reads from — it never touches naiveShaper_'s,
    // adaaShaper_'s, or oversampledShaper_'s internal state. That is safe
    // because:
    //   1. All three shapers are ALWAYS kept parameter-identical:
    //      setDrive()/setBias()/configureShapers() (voicing changes) push to
    //      naiveShaper_, adaaShaper_, AND oversampledShaper_ unconditionally
    //      (applyDrive(), applyBias(), configureShapers() below), regardless
    //      of which one is currently selected. So a switch never lands on a
    //      shaper still configured with an old drive/bias/shape.
    //   2. ADAAWaveshaper::setShape() re-pairs its cached antiderivative
    //      (FPrev_) with the NEW shape at the existing history point
    //      (adaa-waveshaper.h) even while adaaShaper_ is the INACTIVE path,
    //      so a later switch back to adaa never mixes F(uPrev_) from a
    //      stale shape with the new shape's difference quotient.
    //   3. The INACTIVE shaper's own running history (Waveshaper's DC-
    //      blocker xPrev_/yPrev_, for both naiveShaper_ and
    //      oversampledShaper_; ADAAWaveshaper's uPrev_/FPrev_ and DC-
    //      blocker) intentionally free-runs at its last value while
    //      inactive — this is the expected parallel-dual-path design (only
    //      the active shaper's process() runs per sample), not corruption:
    //      it cannot produce NaN/Inf (all shapers' process() are noexcept
    //      over finite input), and at worst it costs one transient sample
    //      on switch-back, same as any other bypass-style toggle. Verified
    //      by inspection; no fix needed.
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
        switch (quality_) {
        case SaturationQuality::naive:
            wet = naiveShaper_.process(wet);
            break;
        case SaturationQuality::adaa:
            wet = adaaShaper_.process(wet);
            break;
        case SaturationQuality::oversampled:
            // WIRED (FR-015, closed by design:primitive/oversampling): run
            // the nonlinearity at Factor=4 through the real Oversampler
            // primitive rather than mapping to the ADAA path. The chain is
            // pre-emphasis(base) -> Oversampler<4>{ oversampledShaper_ @ 4x
            // } -> post-de-emphasis(base), matching the design's naive-
            // shaper-under-oversampling approach (a cheap naive shaper is
            // correct here because the Oversampler's half-band cascade -- not
            // the shaper -- is what suppresses aliasing). The lambda is
            // noexcept (Oversampler<Factor>::process() static_asserts this)
            // and introduces no heap allocation or locks, so this stays
            // RT-safe. Effect-layer user-selectability of this quality
            // (SaturationEffect::kQualityLabels) is tracked in
            // saturation-effect.h, not here; this branch is reachable via
            // any SaturationCore::setQuality(oversampled) call regardless.
            //
            // Wet-path latency + mix alignment: unlike the naive/adaa tiers,
            // this path introduces wet-path processing latency equal to
            // oversampler_.groupDelaySamples() (67.5 samples at Factor=4). The
            // dry/wet `mix` stage below IS delay-compensated for this -- the dry
            // term is delayed by the SAME exact fractional amount (see the mix
            // stage), so mix < 1 blends time-aligned signals (no comb filtering).
            // What remains deliberately OUT OF SCOPE here (captured-deferred per
            // spec FR-025 / plugin PDC) is reporting the effect's oversampled-mode
            // latency OUTWARD to a host for cross-plugin delay compensation; the
            // internal wet/dry alignment is handled.
            wet = oversampler_.process(
                wet, [&](float s) noexcept { return oversampledShaper_.process(s); });
            break;
        }
        // No `default:` -- the switch is exhaustive over the closed
        // SaturationQuality enum; -Wswitch catches a future enumerator added
        // without a matching case here (mirrors shapeValue()'s convention in
        // waveshaper.h).
        wet = postDeEmphasis_.process(wet);
        // Tone-tilt is a GENUINE passthrough at tone=0 (the default): the tilt
        // filter is a true no-op there, so it is bypassed rather than run as a
        // ~5 Hz highpass. Cheap branch, RT-safe (no alloc/lock), and it composes
        // correctly with the mix/output stages below (bypass just leaves `wet`
        // as the post-de-emphasis output). Non-zero tone runs the tilt filter as
        // documented in applyToneTilt().
        if (toneAmount_ != 0.0f)
            wet = toneTilt_.process(wet);
        // Dry/wet mix, time-aligned. The oversampled tier delays the wet path by
        // oversampler_.groupDelaySamples(); delay the dry term by the SAME exact
        // (fractional) amount so mix<1 blends aligned signals rather than comb-
        // filtering (PR#10 review). naive/adaa have zero wet-path latency, so their
        // dry delay is 0 and readFractional(0) returns the just-written x unchanged
        // (their mix behavior is byte-for-byte what it was before this line).
        dryMixWrite(x);
        const float dryDelaySamples = (quality_ == SaturationQuality::oversampled)
                                          ? oversampler_.groupDelaySamples()
                                          : 0.0f;
        const float dry = dryMixRead(dryDelaySamples);
        float y = mix_ * wet + (1.0f - mix_) * dry;
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

    // Bake the current voicing's nonlinear shape into ALL THREE shapers
    // (naive, adaa, oversampled) and enable their internal gain-compensation
    // (FR-004): SaturationCore exposes no gain-comp setter — it is ON by
    // construction so composed loudness stays bounded as drive rises
    // (saturation-harmonics-test.cpp TEST 2).
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
        // saturation-voicings.h). This is a RUNTIME-enforced invariant, NOT a
        // compile-time one: shape::hasAntiderivative (waveshaper-shapes.h) is a
        // plain `inline bool`, not `constexpr`, so it cannot be checked in a
        // static_assert. The invariant is instead asserted for every voicing in
        // tests/core/saturation-voicings-test.cpp ("every voicing's shape has an
        // antiderivative...") -- a future voicing added with an
        // antiderivative-less shape (e.g. Shape::biasedAsym) will fail THAT
        // test, not throw out of process() at runtime.
        adaaShaper_.setShape(cfg.shape);
        // oversampledShaper_ is a plain Waveshaper (same type as
        // naiveShaper_), so it takes the same setShape()/
        // setGainCompensation() calls — kept parameter-identical to the
        // other two shapers per the no-stale-state discipline (setQuality()
        // above).
        oversampledShaper_.setShape(cfg.shape);
        naiveShaper_.setGainCompensation(true);
        adaaShaper_.setGainCompensation(true);
        oversampledShaper_.setGainCompensation(true);
    }

    // (Re)apply the pre-/post-emphasis SVF coefficients from the current
    // voicing. Mode/freq/res are pushed in a fixed order so the DaisySP
    // damp/freq recomputation is deterministic across prepare()/reset().
    void applyEmphasisCoeffs() noexcept {
        const VoicingConfig cfg = voicingConfig(voicing_);
        applyEmphasis(preEmphasis_, cfg.pre);
        applyEmphasis(postDeEmphasis_, cfg.post);
    }

    // DaisySP's Svf requires cutoff strictly below sampleRate/3; clamp into a
    // safe band just under that bound (and never below a low floor), mirroring
    // SvfEffect::clampedCutoff() (svf-effect.h). This makes the realized
    // emphasis corner EXPLICIT and consistent across sample rates rather than
    // relying on DaisySP's silent internal clamp — voicing cutoffs above the
    // bound (e.g. softClip's 18 kHz pre/post, console's 15 kHz post) would
    // otherwise collapse sample-rate-dependently at or below 48/54 kHz.
    float clampedEmphasisFreq(float hz) const noexcept {
        const float maxFreq = sampleRate_ * 0.32f;
        if (hz > maxFreq)
            hz = maxFreq;
        if (hz < 20.0f)
            hz = 20.0f;
        return hz;
    }

    // Non-static (mirrors SvfEffect's per-instance clamp) so it can read
    // sampleRate_ and clamp the emphasis cutoff before setFreq.
    void applyEmphasis(SvfPrimitive& svf, const EmphasisConfig& cfg) noexcept {
        svf.setMode(cfg.mode);
        svf.setFreq(clampedEmphasisFreq(cfg.cutoffHz));
        svf.setRes(cfg.resonance);
    }

    // Tone-tilt realization (the exact tone LAW is a carried open question —
    // data-model.md; this is a simple, defensible tilt):
    //   tone == 0:            GENUINE passthrough — process() bypasses this
    //                         filter entirely (see process()), so tone=0 is a
    //                         true unity no-op, not a ~5 Hz highpass. These
    //                         coefficients are still computed here for the
    //                         tone>0 side and only take effect once tone != 0.
    //   tone <  0 (darker):   lowpass whose cutoff falls from ~open toward
    //                         kToneDarkHz as tone -> -1 (rolls off highs).
    //   tone >  0 (brighter): highpass whose cutoff rises from just-above
    //                         kToneFlatHpHz toward kToneBrightHz as tone -> +1
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
        oversampledShaper_.setDrive(driveGain_);
    }

    void applyBias() noexcept {
        naiveShaper_.setBias(bias_);
        adaaShaper_.setBias(bias_);
        oversampledShaper_.setBias(bias_);
    }

    // Tone-tilt tuning constants (open question — simple defensible values).
    static constexpr float kToneOpenFrac = 0.32f;   // open LP cutoff ~= sr*0.32
    static constexpr float kToneDarkHz   = 1500.0f; // darkest LP cutoff (tone=-1)
    static constexpr float kToneFlatHpHz = 5.0f;    // transparent HP cutoff (tone=0)
    static constexpr float kToneBrightHz = 1000.0f; // brightest HP cutoff (tone=+1)
    static constexpr float kToneRes      = 0.0f;    // flat, no resonance peak

    // -------------------------------------------------------------------
    // Composed sub-units (data-model.md "SaturationCore — Composed
    // sub-units"). Three SvfPrimitive stages plus THREE nonlinear-stage
    // backends; quality_ (below) selects naiveShaper_ vs adaaShaper_ vs
    // (oversampler_ + oversampledShaper_) at process() time. oversampledShaper_
    // is a plain Waveshaper (same type as naiveShaper_, not ADAAWaveshaper) —
    // oversampling supplies the anti-aliasing, so a cheap naive shaper run
    // inside the Oversampler is the correct, non-redundant choice (FR-015).
    // -------------------------------------------------------------------
    SvfPrimitive preEmphasis_;
    SvfPrimitive postDeEmphasis_;
    SvfPrimitive toneTilt_;
    Waveshaper naiveShaper_;
    ADAAWaveshaper adaaShaper_;
    Oversampler<4> oversampler_;
    Waveshaper oversampledShaper_;

    // Dry-path delay for dry/wet mix alignment (PR#10 review): the oversampled
    // tier delays the wet path by oversampler_.groupDelaySamples() (67.5 @ Factor
    // 4); the dry term of the mix is delayed by the SAME exact (fractional) amount
    // so mix<1 blends time-aligned signals instead of comb-filtering. naive/adaa
    // are zero-latency (dry delay 0 -> the just-written x, unchanged).
    //
    // Implemented as a SELF-CONTAINED ring buffer (a plain std::array + index, no
    // pointers) rather than the pointer-binding DelayLine primitive: SaturationCore
    // must stay trivially copyable (tests capture a configured core BY VALUE into a
    // closure), and a member DelayLine bound to a sibling storage member would
    // dangle its pointer on copy. Fixed-size => no heap (Constitution VI). Capacity
    // exceeds the max group delay (67.5) + the fractional read's 2-sample reach.
    static constexpr int kDryMixDelayCap = 128;
    std::array<float, static_cast<std::size_t>(kDryMixDelayCap)> dryMixRing_{};
    int dryMixWrite_ = 0;

    // Push x into the dry-delay ring (advance write head, wrap).
    void dryMixWrite(float x) noexcept {
        dryMixRing_[static_cast<std::size_t>(dryMixWrite_)] = x;
        dryMixWrite_ = (dryMixWrite_ + 1) % kDryMixDelayCap;
    }

    // Read the dry ring delayed by `d` samples (fractional, linear-interpolated).
    // d is clamped into [0, cap-1]; d==0 returns the most-recently-written sample.
    float dryMixRead(float d) const noexcept {
        const float clamped =
            std::min(std::max(d, 0.0f), static_cast<float>(kDryMixDelayCap - 1));
        const int i = static_cast<int>(clamped);
        const float f = clamped - static_cast<float>(i);
        const int newer = (dryMixWrite_ - 1 - i + 2 * kDryMixDelayCap) % kDryMixDelayCap;
        const int older = (dryMixWrite_ - 2 - i + 2 * kDryMixDelayCap) % kDryMixDelayCap;
        return (1.0f - f) * dryMixRing_[static_cast<std::size_t>(newer)]
             + f * dryMixRing_[static_cast<std::size_t>(older)];
    }

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

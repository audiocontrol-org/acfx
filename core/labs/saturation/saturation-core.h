#pragma once

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
    // for sampleRate. No audio-path work. Body: T009.
    void prepare(float sampleRate) noexcept;

    // Clear filter + DC-blocker state across all composed sub-units, without
    // discarding applied parameter state (mirrors Waveshaper::reset). Body: T009.
    void reset() noexcept;

    // Select the named voicing: bakes in shape[voicing] plus the pre-emphasis
    // and post-de-emphasis filter curves (voicingConfig() in
    // saturation-voicings.h). Body: T009.
    void setVoicing(SaturationVoicing voicing) noexcept;

    // Select the naive/adaa nonlinear evaluation path (SaturationQuality::
    // oversampled is a reserved, unwired seam — FR-015). Body: T009.
    void setQuality(SaturationQuality quality) noexcept;

    // Pre-gain applied ahead of the nonlinearity (linear gain, not dB).
    // Body: T009.
    void setDrive(float gainLinear) noexcept;

    // USER asymmetry dial, applied alongside drive ahead of the nonlinearity.
    // Not baked into the voicing (data-model.md Decision 5). Body: T009.
    void setBias(float bias) noexcept;

    // Post-de-emphasis tone tilt amount, -1..+1. Body: T009.
    void setTone(float tilt) noexcept;

    // Dry/wet parallel blend, 0..1. Body: T009.
    void setMix(float wet) noexcept;

    // Makeup trim applied after the mix stage (linear gain, not dB).
    // Body: T009.
    void setOutput(float gainLinear) noexcept;

    // The normative signal chain (saturation-api.md "Signal chain"):
    //   wet = preEmphasis[voicing](x)
    //   wet = shaper(driveGain*wet + bias)     // naive or adaa, per quality_
    //   wet = postDeEmphasis[voicing](wet)
    //   wet = toneTilt(wet)
    //   y   = mix*wet + (1 - mix)*x
    //   y   = outputGain * y
    //   return y
    // Bodies: T010 (naive path), T018 (adaa path selection).
    // TODO(T009/T010/T018): real signal chain. Stubbed to pass its input
    // through unchanged so a single-TU include that calls process() still
    // links before those tasks land.
    float process(float x) noexcept { return x; }

private:
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

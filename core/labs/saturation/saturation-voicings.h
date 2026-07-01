#pragma once

#include <cstdint>

#include "primitives/filters/svf-primitive.h"
#include "primitives/nonlinear/waveshaper-shapes.h"

// The voicing table (T005): the SURFACE mapping each named voicing to its fixed
// nonlinear shape and pre-/post-emphasis filter curves (data-model.md "Voicing").
// This header declares the enums, the plain aggregate config, and the constexpr
// selector; it carries no state and performs no allocation (Constitution VI).
//
// Only `softClip` is tuned here (real-ish, still open-question numbers — the
// tuning pass is deferred). `tape`, `console`, and `tubePreamp` are documented
// PLACEHOLDERs so the switch below stays exhaustive and the header compiles;
// T012 replaces them with real numbers.

namespace acfx {

// ---------------------------------------------------------------------------
// SaturationVoicing — the four named characters (saturation-api.md "Enums").
// ---------------------------------------------------------------------------
enum class SaturationVoicing : std::uint8_t {
    softClip = 0,
    tape = 1,
    console = 2,
    tubePreamp = 3
};

// ---------------------------------------------------------------------------
// SaturationQuality — anti-aliasing selection (saturation-api.md "Enums").
// `oversampled` is RESERVED: documented but UNWIRED (FR-015). It carries no
// dependency on the oversampling sibling; selecting it must fall back to a
// defined, bounded behavior in the core (never a partial/aliased path). It is
// deliberately absent from SaturationEffect::kQualityLabels (not yet
// user-selectable).
// ---------------------------------------------------------------------------
enum class SaturationQuality : std::uint8_t {
    naive = 0,
    adaa = 1,
    oversampled = 2  // RESERVED — unwired seam (FR-015); do not implement here.
};

// ---------------------------------------------------------------------------
// EmphasisConfig — the SVF parameters needed to configure one SvfPrimitive
// stage (mode + cutoff + resonance). Plain aggregate; no behavior.
// ---------------------------------------------------------------------------
struct EmphasisConfig {
    SvfMode mode;
    float cutoffHz;
    float resonance;
};

// ---------------------------------------------------------------------------
// VoicingConfig — the fixed identity of one voicing (data-model.md "Voicing"):
// the nonlinear shape plus the pre-emphasis and post-de-emphasis filter curves.
// Deliberately excludes `bias` (a user parameter, not baked into the voicing —
// Decision 5). Plain aggregate, constexpr-constructible, no state.
// ---------------------------------------------------------------------------
struct VoicingConfig {
    Shape shape;
    EmphasisConfig pre;
    EmphasisConfig post;
};

// ---------------------------------------------------------------------------
// voicingConfig — the per-voicing selector (data-model.md "Voicing" /
// saturation-api.md kVoicingLabels order). Exhaustive switch over
// SaturationVoicing; every branch returns a constexpr VoicingConfig.
//
// softClip: tuned real-ish (open question — exact numbers are the tuning
//   pass). Shape::softKnee is a bounded, C1-continuous soft-clip curve
//   (see waveshaper-shapes.h); the emphasis pair is near-flat/gentle (wide-open
//   lowpass pre, matching post) so the voicing is dominated by the shape itself
//   rather than tonal coloration, consistent with "soft clip" being the
//   least-colored voicing.
// tape / console / tubePreamp: PLACEHOLDER — TODO(T012): tuning pass. Present
//   only so this switch is exhaustive and the header compiles standalone.
// ---------------------------------------------------------------------------
constexpr VoicingConfig voicingConfig(SaturationVoicing v) noexcept {
    switch (v) {
    case SaturationVoicing::softClip:
        // Tuning pass open question (data-model.md): numbers below are
        // real-ish placeholders, not yet measured/validated against SC-001..005.
        return VoicingConfig{
            Shape::softKnee,
            EmphasisConfig{SvfMode::lowpass, 18000.0f, 0.1f},  // pre: near-flat
            EmphasisConfig{SvfMode::lowpass, 18000.0f, 0.1f}   // post: matches pre
        };

    case SaturationVoicing::tape:
        // TODO(T012): tuning pass — placeholder. Values are not yet meaningful;
        // present only so this switch is exhaustive.
        return VoicingConfig{
            Shape::tanh,
            EmphasisConfig{SvfMode::lowpass, 12000.0f, 0.1f},
            EmphasisConfig{SvfMode::lowpass, 12000.0f, 0.1f}
        };

    case SaturationVoicing::console:
        // TODO(T012): tuning pass — placeholder. Values are not yet meaningful;
        // present only so this switch is exhaustive.
        return VoicingConfig{
            Shape::algebraic,
            EmphasisConfig{SvfMode::lowpass, 15000.0f, 0.1f},
            EmphasisConfig{SvfMode::lowpass, 15000.0f, 0.1f}
        };

    case SaturationVoicing::tubePreamp:
        // TODO(T012): tuning pass — placeholder. Values are not yet meaningful;
        // present only so this switch is exhaustive.
        return VoicingConfig{
            Shape::diodeCurve,
            EmphasisConfig{SvfMode::lowpass, 10000.0f, 0.1f},
            EmphasisConfig{SvfMode::lowpass, 10000.0f, 0.1f}
        };
    }
    // Unreachable for the closed enum above; documented defensive fallback
    // (matches waveshaper-shapes.h::hasAntiderivative's conservative-default
    // pattern) rather than undefined behavior.
    return VoicingConfig{
        Shape::softKnee,
        EmphasisConfig{SvfMode::lowpass, 18000.0f, 0.1f},
        EmphasisConfig{SvfMode::lowpass, 18000.0f, 0.1f}
    };
}

} // namespace acfx

#pragma once

#include <cstdint>

#include "primitives/filters/svf-primitive.h"
#include "primitives/nonlinear/waveshaper-shapes.h"

// The voicing table (T005): the SURFACE mapping each named voicing to its fixed
// nonlinear shape and pre-/post-emphasis filter curves (data-model.md "Voicing").
// This header declares the enums, the plain aggregate config, and the constexpr
// selector; it carries no state and performs no allocation (Constitution VI).
//
// All four voicings carry real, DOCUMENTED configs chosen so the set is
// measurably DISTINCT in harmonic + spectral character (US2 / FR-005/006): a
// distinct `Shape` per voicing plus a distinct pre-/post-emphasis pair. The
// EXACT numbers (cutoffs, resonances) remain the tuning-pass OPEN QUESTION
// (data-model.md / design open questions) — validated later by the harmonic
// harness; the qualitative character of each voicing is fixed here.

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
// Each branch pairs a DISTINCT Shape with a DISTINCT emphasis curve so the four
// voicings separate in both harmonic content (shape) and spectrum (emphasis):
//   softClip   — softKnee  + near-flat wide LP/LP     (clean, odd, uncolored)
//   tape       — tanh      + LP pre / darker LP post   (dark, low-order odd)
//   console    — arctan    + HP pre / gentle LP post   (clean/bright, lowest THD)
//   tubePreamp — diodeCurve+ low-mid LP / resonant LP  (warm, even harmonics)
// softClip's emphasis pair is deliberately near-flat/gentle (wide-open lowpass
// pre, matching post) so the voicing is dominated by the shape itself rather than
// tonal coloration, consistent with "soft clip" being the least-colored voicing.
// The exact cutoff/resonance numbers are the tuning-pass OPEN QUESTION.
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
        // TAPE — gentle saturation with tape-style HF loss. Character: a smooth,
        // symmetric tanh shape (odd harmonics, no even content; softer/darker than
        // softKnee's harder knee) driven through a lowpass PRE that trims extreme
        // highs before the nonlinearity, then a lower-cutoff lowpass POST that
        // rolls off highs (the tape HF-loss signature). Net: predominantly
        // low-order odd harmonics, the DARKEST of the four voicings.
        // NOTE: exact cutoffs/resonances are the tuning-pass OPEN QUESTION
        // (data-model.md / design open questions) — not yet measured vs SC-001.
        return VoicingConfig{
            Shape::tanh,
            EmphasisConfig{SvfMode::lowpass, 14000.0f, 0.1f},  // pre: trim extreme HF
            EmphasisConfig{SvfMode::lowpass, 8000.0f, 0.1f}    // post: tape HF loss
        };

    case SaturationVoicing::console:
        // CONSOLE — subtle broadband "glue", the least aggressive of the four.
        // Character: arctan is the mildest/gentlest-curvature shape in the catalog
        // (lowest odd-harmonic amplitudes at a given drive — see the harmonic
        // table in core/labs/waveshaping/README.md), so it has the LOWEST THD.
        // A gentle highpass PRE removes sub/rumble before the shaper (broadband
        // "glue" that cleans the low end), and a near-flat, slightly resonant
        // lowpass POST gives a subtle top-end presence without darkening — the
        // brightest/cleanest voicing.
        // NOTE: exact cutoffs/resonances are the tuning-pass OPEN QUESTION.
        return VoicingConfig{
            Shape::arctan,
            EmphasisConfig{SvfMode::highpass, 120.0f, 0.1f},   // pre: clean sub/rumble
            EmphasisConfig{SvfMode::lowpass, 15000.0f, 0.2f}   // post: gentle presence
        };

    case SaturationVoicing::tubePreamp:
        // TUBE PREAMP — warm, ASYMMETRIC even-harmonic character. Shape::diodeCurve
        // is the only sign-asymmetric shape here that still has an analytic
        // antiderivative (so it is ADAA-safe, unlike biasedAsym): it injects EVEN
        // harmonics + DC that the three odd shapes above cannot (see the harmonic
        // table — diodeCurve shows non-zero 2f0/4f0). A low-mid lowpass PRE pushes
        // the body of the signal into the asymmetry (warmth), and a resonant
        // lowpass POST creates a gentle presence bump near its cutoff while rolling
        // off the top. Net: the only voicing with a strong even-harmonic signature.
        // NOTE: exact cutoffs/resonances are the tuning-pass OPEN QUESTION.
        return VoicingConfig{
            Shape::diodeCurve,
            EmphasisConfig{SvfMode::lowpass, 5000.0f, 0.2f},   // pre: low-mid warmth
            EmphasisConfig{SvfMode::lowpass, 3500.0f, 0.5f}    // post: presence bump
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

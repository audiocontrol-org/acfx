#pragma once

#include <array>
#include <cstddef>

#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"

// Named dynamic-character preset table (T030 / US9 / FR-014, research.md
// Decision 7) — the single documented, TESTABLE source of truth for the
// opto/variMu/tapeComp characters, split out of
// program-dependent-saturation-effect.h to keep that wrapper within the
// ~300-500 line budget (FR-025), mirroring the parameter-table split in
// program-dependent-saturation-parameters.h.
//
// A DynamicPreset is a pure CONFIGURATION of the existing modulation matrix (no
// new DSP, FR-014): each entry pins the four target depth+curve pairs, the
// detection topology, and the shared detector mode / ballistics / attack /
// release. Applying a preset WRITES this config to every channel core
// (ProgramDependentSaturationEffect::writePreset). The row order matches
// ProgramDependentSaturationEffect::DynamicPreset (none, opto, variMu, tapeComp),
// so `kPdsPresetConfigs[static_cast<size_t>(preset)]` is the config for `preset`.
//
// OVERRIDE SEMANTICS (US9 Deferred): a preset is a STARTING POINT, not a lock.
// Selecting a preset writes this matrix; a subsequent manual setParameter on any
// depth/curve (or detector/timing) param overrides that individual value on the
// next process() — the preset does not re-assert itself. (`none` is the neutral
// baseline: it equals the effect's constructor defaults, so it is applied as a
// no-op that never clobbers a hand-dialed matrix — see applyDynamicPreset.)
//
// THE NUMERIC DEPTHS / CURVES / TOPOLOGY / TIMINGS BELOW ARE TUNING-PASS
// PLACEHOLDERS (research.md Decision 7 "Open (tuning-pass)"): the CHARACTER each
// preset expresses is normative (opto = downward drive softening; variMu =
// upward bias+drive push; tapeComp = drive push + mix self-compression); the
// exact magnitudes are defensible first-cut values, not yet validated against a
// reference measurement — mirroring the kParams range disclaimer.

namespace acfx {

// One modulation target's preset slot: a signed depth (−1..+1; sign = direction)
// and the response curve that shapes the envelope→offset map for that target.
struct PdsPresetTargetConfig {
    float    depth = 0.0f;
    ModCurve curve = ModCurve::linear;
};

// A full dynamic-character preset: the four target slots + the shared detector
// configuration. attack/release are in MILLISECONDS (the descriptor unit; the
// effect converts ms→s before forwarding to EnvelopeFollower).
struct PdsPresetConfig {
    PdsPresetTargetConfig drive;
    PdsPresetTargetConfig bias;
    PdsPresetTargetConfig tone;
    PdsPresetTargetConfig mix;
    Detection  detection  = Detection::feedForward;
    DetectMode detector   = DetectMode::rms;
    Ballistics ballistics = Ballistics::branching;
    float      attackMs   = 10.0f;
    float      releaseMs  = 100.0f;
};

// The documented preset matrix (row order == DynamicPreset enum order). Every
// value is a tuning-pass placeholder (see the file header).
inline constexpr std::array<PdsPresetConfig, 4> kPdsPresetConfigs = {{
    // none — neutral / orthogonality baseline (all depths 0, linear curves,
    // feedForward, default detector/ballistics/timing). Byte-identical to the
    // effect's constructor defaults, so `none` colors nothing (US3).
    PdsPresetConfig{{0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    Detection::feedForward, DetectMode::rms, Ballistics::branching,
                    10.0f, 100.0f},

    // opto — optical character: slow, level-smoothed DOWNWARD drive softening.
    // Negative drive depth (louder = cleaner) with a logarithmic (early-onset)
    // curve, an RMS averaging detector, decoupled (smooth) ballistics, slow
    // attack/release, and feedBack (self-regulating) detection.
    PdsPresetConfig{{-0.60f, ModCurve::logarithmic},
                    {0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    Detection::feedBack, DetectMode::rms, Ballistics::decoupled,
                    50.0f, 500.0f},

    // variMu — vari-mu tube character: level-dependent BIAS + DRIVE push. Both
    // depths positive (louder = more push); an exponential (late-onset) drive
    // curve for the tube "bloom", a linear bias curve, RMS detection, branching
    // ballistics, medium timing, feedBack detection.
    PdsPresetConfig{{0.50f, ModCurve::exponential},
                    {0.40f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    Detection::feedBack, DetectMode::rms, Ballistics::branching,
                    20.0f, 300.0f},

    // tapeComp — tape character: DRIVE push + MIX self-compression. Positive
    // drive depth (louder = more saturation) and positive mix depth (louder =
    // more wet blend, a self-compression feel), both linear; peak detection with
    // decoupled tape-ish ballistics, medium-fast attack, medium release,
    // feedForward detection.
    PdsPresetConfig{{0.40f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    {0.0f, ModCurve::linear},
                    {0.30f, ModCurve::linear},
                    Detection::feedForward, DetectMode::peak, Ballistics::decoupled,
                    15.0f, 250.0f},
}};

} // namespace acfx

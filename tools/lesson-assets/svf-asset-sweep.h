#pragma once

#include <string>
#include <vector>

#include "asset-entry.h"

// T009: sweeps the REAL acfx::SvfEffect (core/effects/svf/svf-effect.h) at a
// documented set of preset settings, writing WAV/response/impulse assets into
// `outDir` and returning the AssetEntry list T010's fragment writer consumes.
// No faked/mock DSP (Principle VII) -- every sample in every output file is
// produced by SvfEffect::process().

namespace lessonassets {

// One documented preset the sweep renders. cutoffHz/resonance are PLAIN units
// (converted to the effect's normalized 0..1 setParameter() domain internally
// via acfx::normalize against SvfEffect::kParams, the effect's own descriptor
// table -- never a hand-rolled mapping).
struct SvfAssetPreset {
    std::string name; // e.g. "lp-cutoff-low" -- becomes the asset filename stem
    int modeIndex;    // SvfEffect::Param mode index: 0 lowpass, 1 highpass, 2 bandpass
    float cutoffHz;
    float resonance; // 0..1 plain (SvfEffect's resonance descriptor is already 0..1 linear)
};

// The fixed, documented preset set (see svf-asset-sweep.cpp for the values
// and rationale): three lowpass cutoffs (low/mid/high) demonstrating roll-off
// moving with cutoff, plus one higher-resonance lowpass preset.
std::vector<SvfAssetPreset> defaultPresets();

// Renders every preset's audio clip + frequency-response JSON + impulse-
// response JSON via the real SvfEffect, writes them (content-addressed) under
// `outDir`, and returns the AssetEntry list describing what was written.
// `provenance` is the per-entry "producer@sourceHash" string (T010).
std::vector<AssetEntry> sweepSvfPresets(const std::string& outDir, const std::vector<SvfAssetPreset>& presets,
                                         const std::string& provenance);

} // namespace lessonassets

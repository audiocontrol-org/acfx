#pragma once

#include <array>
#include <cstddef>

#include "effects/tape-dynamics/tape-dynamics-core.h"

// Named starting-point preset table (FR-013) — the single documented, TESTABLE
// source of truth for tape character presets (e.g., gentle "glue" vs aggressive
// "saturate"), split out of tape-dynamics-effect.h to keep that wrapper within
// the ~300–500 line budget (FR-025), mirroring the parameter-table split in
// tape-dynamics-parameters.h.
//
// A TapeDynamicsPreset is a pure CONFIGURATION of the Hysteresis + trim
// parameters (no new DSP, FR-013): each entry pins the drive, saturation (Ms),
// width (k), solver, oversampling, and trim settings. Applying a preset WRITES
// this config to every channel core (TapeDynamicsEffect::applyPreset). The row
// order matches TapeDynamicsEffect::TapeDynamicsPreset (none, glue, saturate),
// so `kTapeDynamicsPresetConfigs[static_cast<size_t>(preset)]` is the config
// for `preset`.
//
// OVERRIDE SEMANTICS: a preset is a STARTING POINT, not a lock. Selecting a
// preset writes this config; a subsequent manual setParameter() on any drive/
// saturation/width/solver param overrides that individual value on the next
// process() — the preset does not re-assert itself. Selecting `none` writes
// its all-zero/default row here, RESETTING the parameters to the neutral
// baseline.
//
// THE NUMERIC DRIVE / SATURATION / WIDTH / SOLVER / OVERSAMPLING VALUES BELOW
// ARE TUNING-PASS PLACEHOLDERS (data-model.md): the CHARACTER each preset
// expresses is normative (glue = gentle compression feel; saturate = aggressive
// saturation); the exact magnitudes are defensible first-cut values, not yet
// validated against reference measurement — mirroring kParams range disclaimer.

namespace acfx {

// One tape preset configuration: static settings for drive, saturation, width,
// solver selection, and oversampling factor.
struct TapeDynamicsPresetConfig {
    float drive = 0.0f;
    float saturation = 1.0f;
    float width = 1.0f;
    std::uint8_t solver = 1; // 0=rk2, 1=rk4, 2=newtonRaphson; default rk4
    std::uint8_t oversampling = 2; // 0=2x, 1=4x, 2=8x, 3=16x; default 8x
    bool trimEnabled = false;
    float trimAttack = 0.01f;
    float trimRelease = 0.1f;
    float trimAmount = 0.5f;
};

// The documented preset matrix (row order == TapeDynamicsPreset enum order).
// Every value is a tuning-pass placeholder (see file header).
inline constexpr std::array<TapeDynamicsPresetConfig, 3> kTapeDynamicsPresetConfigs = {{
    // none — neutral baseline (all parameters at defaults, no character).
    // Byte-identical to TapeDynamicsEffect constructor defaults, so `none`
    // applies no tape coloration.
    TapeDynamicsPresetConfig{0.0f, 1.0f, 1.0f, 1, 2, false, 0.01f, 0.1f, 0.5f},

    // glue — gentle tape character: moderate drive with soft saturation,
    // feedback detection, slower ballistics (via trim), for a cohesive,
    // compressed sound.
    TapeDynamicsPresetConfig{6.0f, 1.2f, 0.8f, 1, 2, true, 0.05f, 0.2f, 0.4f},

    // saturate — aggressive tape character: high drive, steep saturation,
    // fast attack, tight loop width, for pronounced nonlinear distortion.
    TapeDynamicsPresetConfig{18.0f, 1.8f, 1.5f, 2, 2, true, 0.01f, 0.1f, 0.8f},
}};

// Build-time guard: ensure the preset array size matches the declared count.
// (Future: if an enum is introduced, static_assert that row count == enum size.)

} // namespace acfx

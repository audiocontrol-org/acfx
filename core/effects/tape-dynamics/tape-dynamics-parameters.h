#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "dsp/param-id.h"
#include "dsp/parameter.h"

// TapeDynamicsParameters — the host-facing parameter surface (FR-010).
// Each parameter is a ParameterDescriptor (id, range, default) per the
// platform model, handed off lock-free. Parameter ids correspond to the
// Param enum in TapeDynamicsEffect, and row order in kTapeDynamicsParams
// matches that enum exactly for dense id dispatch (FR-019).
//
// PARAMETER RANGES ARE A TUNING-PASS OPEN QUESTION. The descriptor shapes
// (kinds/units/skews/labels) are normative (specs/tape-dynamics/data-model.md);
// the exact numeric ranges are defensible placeholders, not yet validated
// against reference measurement — mirroring CompressorEffect/SaturationEffect
// kParams disclaimers.
//
// Macro-to-physics mapping (data-model.md "Entity: TapeDynamicsParameters"):
//   drive       -> input gain applied before the signal enters the magnetics
//                  as the applied field H (Hysteresis::process(H)).
//   saturation  -> JAParams.Ms (saturation magnetization / ceiling).
//   width       -> JAParams.k (coercivity / loop width / memory).
//   solver      -> Hysteresis::solver (Solver enum: rk2/rk4/newtonRaphson).
//   oversampling-> selects which TapeDynamicsCore<Factor> is active
//                  (Factor in {2,4,8} — Oversampler<Factor> static_asserts
//                  this; there is no 16x option).
//   trim.*      -> the optional explicit EnvelopeFollower+GainComputer trim
//                  stage; trim.enabled gates it, attack/release drive the
//                  follower, amount scales the applied gain-reduction depth.
//   mix/output  -> dry/wet blend and post makeup gain.
// JAParams.a/alpha/c are left at Hysteresis's own defaults (not exposed as
// top-level macro controls in this first cut — data-model.md marks them
// "default; advanced").
//
// NOTE on time-valued units: ParamUnit (core/dsp/param-id.h) has no
// milliseconds enumerator, only `seconds`. data-model.md lists trim.attack/
// trim.release in ms as the tuning-pass placeholder; this table stores the
// equivalent values in SECONDS and tags them ParamUnit::seconds, matching
// CompressorEffect's attack/release convention — no ms<->s conversion is
// needed at apply time.

namespace acfx {

// Option labels for the discrete parameters (single source of truth).
inline constexpr std::array<std::string_view, 3> kTapeDynamicsSolverLabels = {
    {"rk2", "rk4", "newtonRaphson"}};
inline constexpr std::array<std::string_view, 3> kTapeDynamicsOversamplingLabels = {
    {"2x", "4x", "8x"}};
inline constexpr std::array<std::string_view, 2> kTapeDynamicsTrimEnabledLabels = {
    {"off", "on"}};

// The single source of parameter truth (FR-010). Row order matches
// TapeDynamicsEffect::Param (leading ParamId index == that dense id). Shapes are
// normative (data-model.md); ranges are tuning-pass OPEN QUESTION:
//   0  drive:           dB, 0..24, default 0 (unity) -> H input gain
//   1  saturation:      tuned (OQ3), 0.1..2.0, default 1.0 -> JAParams.Ms
//   2  width:           tuned (OQ3), 0.1..2.0, default 1.0 -> JAParams.k
//   3  solver:          discrete {rk2, rk4, newtonRaphson}, default rk4
//   4  oversampling:    discrete {2x, 4x, 8x}, default 8x (Oversampler<Factor>
//                       supports Factor in {2,4,8} only — no 16x)
//   5  trim.enabled:    discrete {off, on}, default off
//   6  trim.attack:     seconds (ms tuning value), tuned (OQ3)
//   7  trim.release:    seconds (ms tuning value), tuned (OQ3)
//   8  trim.amount:     linear 0..1, tuned (OQ3)
//   9  mix:             linear 0..1, default 1
//   10 output:          dB, -24..24, default 0
inline constexpr std::array<ParameterDescriptor, 11> kTapeDynamicsParams = {{
    {ParamId{0}, "drive", ParamUnit::decibels, 0.0f, 24.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{1}, "saturation", ParamUnit::none, 0.1f, 2.0f, 1.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{2}, "width", ParamUnit::none, 0.1f, 2.0f, 1.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{3}, "solver", ParamUnit::none, 0.0f, 2.0f, 1.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kTapeDynamicsSolverLabels},
    {ParamId{4}, "oversampling", ParamUnit::none, 0.0f, 2.0f, 2.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kTapeDynamicsOversamplingLabels},
    {ParamId{5}, "trim.enabled", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kTapeDynamicsTrimEnabledLabels},
    {ParamId{6}, "trim.attack", ParamUnit::seconds, 0.001f, 0.5f, 0.01f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{7}, "trim.release", ParamUnit::seconds, 0.001f, 1.0f, 0.1f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{8}, "trim.amount", ParamUnit::none, 0.0f, 1.0f, 0.5f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{9}, "mix", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{10}, "output", ParamUnit::decibels, -24.0f, 24.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
}};

// Build-time guard (F9): the table's ids must be DENSE and IN ORDER — row i must
// carry ParamId{i}. TapeDynamicsEffect::Param is a dense index into this table and
// TapeDynamicsEffect::setParameter() dispatches on id.value, so a future row
// reorder that is not mirrored in the Param enum would silently mismap controls.
// This constexpr loop (same style as CompressorEffect's isValidDescriptor guard)
// makes that a compile error instead.
static_assert(
    [] {
        for (std::size_t i = 0; i < kTapeDynamicsParams.size(); ++i)
            if (static_cast<std::size_t>(kTapeDynamicsParams[i].id.value) != i)
                return false;
        return true;
    }(),
    "kTapeDynamicsParams ids must be dense and in-order (row i must have ParamId{i}) "
    "so the ParamId dispatch in TapeDynamicsEffect stays aligned with the Param enum");

} // namespace acfx

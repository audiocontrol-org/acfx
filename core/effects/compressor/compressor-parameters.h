#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "dsp/param-id.h"
#include "dsp/parameter.h"

// CompressorEffect parameter data — the single source of parameter truth
// (FR-019), split out of compressor-effect.h to keep that wrapper within the
// ~300-500 line budget (FR-028). This header owns three things and nothing
// else: the StereoLink enum, the discrete-option label arrays, and the
// constexpr ParameterDescriptor table. CompressorEffect keeps its own `Param`
// enum (the dense index names used throughout the wrapper) and exposes this
// table via a `kParams` alias; the row order here matches that enum exactly
// (threshold=0 .. output=16), so the leading ParamId index on each row is that
// same dense id.
//
// PARAMETER RANGES ARE A TUNING-PASS OPEN QUESTION. The descriptor shapes
// (kinds/units/skews/labels) are normative (contracts/compressor-effect-api.md,
// data-model.md); the exact numeric ranges are defensible placeholders, not yet
// validated against a reference measurement — mirroring SaturationEffect's
// kParams disclaimer.
//
// NOTE on time-valued units: ParamUnit (core/dsp/param-id.h) has no
// milliseconds enumerator, only `seconds`. data-model.md lists attack/release/
// lookahead ranges in ms as the tuning-pass placeholder; this table stores the
// equivalent values in SECONDS (the unit CompressorCore's setAttack/setRelease
// take) and tags them ParamUnit::seconds, so no ms<->s conversion is needed at
// apply time. lookahead is likewise stored/denormalized in seconds and
// converted to samples (round(seconds * sampleRate)) only when forwarded to
// CompressorCore::setLookahead / prepare()'s buffer sizing.

namespace acfx {

// StereoLink (data-model.md "StereoLink") — CompressorEffect-level only; not a
// CompressorCore concern. perChannel: independent per-channel detection.
// linked: one detector value (cross-channel max) drives a common gain (FR-017).
enum class StereoLink : std::uint8_t { perChannel, linked };

// Option labels for the discrete parameters (single source of truth).
inline constexpr std::array<std::string_view, 4> kCompressorModeLabels = {
    {"compress", "limit", "expand", "gate"}};
inline constexpr std::array<std::string_view, 2> kCompressorDetectionLabels = {
    {"feedForward", "feedBack"}};
inline constexpr std::array<std::string_view, 2> kCompressorDetectorLabels = {{"peak", "rms"}};
inline constexpr std::array<std::string_view, 2> kCompressorBallisticsSiteLabels = {
    {"level", "gain"}};
inline constexpr std::array<std::string_view, 2> kCompressorAutoMakeupLabels = {{"off", "on"}};
inline constexpr std::array<std::string_view, 2> kCompressorStereoLinkLabels = {
    {"perChannel", "linked"}};

// The single source of parameter truth (FR-019). Row order matches
// CompressorEffect::Param (leading ParamId index == that dense id). Shapes are
// normative (data-model.md); ranges are the tuning-pass OPEN QUESTION:
//   0  threshold:      dB, -60..0, default -18
//   1  ratio:          ratio, 1..20, default 4 (limit mode ignores it)
//   2  knee:           dB, 0..24, default 6
//   3  attack:         seconds, 0.0001..0.2 (0.1..200 ms), default 0.01
//   4  release:        seconds, 0.001..2.0 (1..2000 ms), default 0.1
//   5  mode:           discrete {compress, limit, expand, gate}
//   6  detection:      discrete {feedForward, feedBack}
//   7  detector:       discrete {peak, rms}
//   8  ballisticsSite: discrete {level, gain}
//   9  range:          dB, -80..0, default -40
//   10 scHpf:          Hz, 0..500, default 0 (0 = bypass)
//   11 lookahead:      seconds, 0..0.02 (0..20 ms), default 0
//   12 makeup:         dB, -24..24, default 0
//   13 autoMakeup:     discrete {off, on}
//   14 stereoLink:     discrete {perChannel, linked}, default linked
//   15 mix:            linear 0..1, default 1
//   16 output:         dB, -24..24, default 0
inline constexpr std::array<ParameterDescriptor, 17> kCompressorParams = {{
    {ParamId{0}, "threshold", ParamUnit::decibels, -60.0f, 0.0f, -18.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{1}, "ratio", ParamUnit::ratio, 1.0f, 20.0f, 4.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{2}, "knee", ParamUnit::decibels, 0.0f, 24.0f, 6.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{3}, "attack", ParamUnit::seconds, 0.0001f, 0.2f, 0.01f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{4}, "release", ParamUnit::seconds, 0.001f, 2.0f, 0.1f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{5}, "mode", ParamUnit::none, 0.0f, 3.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 4, kCompressorModeLabels},
    {ParamId{6}, "detection", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kCompressorDetectionLabels},
    {ParamId{7}, "detector", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kCompressorDetectorLabels},
    {ParamId{8}, "ballisticsSite", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kCompressorBallisticsSiteLabels},
    {ParamId{9}, "range", ParamUnit::decibels, -80.0f, 0.0f, -40.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{10}, "scHpf", ParamUnit::hz, 0.0f, 500.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{11}, "lookahead", ParamUnit::seconds, 0.0f, 0.02f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{12}, "makeup", ParamUnit::decibels, -24.0f, 24.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{13}, "autoMakeup", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kCompressorAutoMakeupLabels},
    {ParamId{14}, "stereoLink", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kCompressorStereoLinkLabels},
    {ParamId{15}, "mix", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{16}, "output", ParamUnit::decibels, -24.0f, 24.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
}};

} // namespace acfx

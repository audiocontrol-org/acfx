#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"

// ProgramDependentSaturationEffect parameter data — the single source of
// parameter truth (data-model.md "Parameter table"), split out of
// program-dependent-saturation-effect.h to keep that wrapper within the
// ~300-500 line budget (FR-025/FR-028), mirroring compressor-parameters.h.
// This header owns three things: the discrete-option label arrays, the
// constexpr ParameterDescriptor table, and the STATELESS scalar/enum-converter
// helpers the effect's applyPending()/setParameter() call (moved here to keep
// the wrapper within budget — none of them touch per-channel core state). The
// effect keeps its own `Param` enum (the dense index names used throughout the
// wrapper) and exposes this table via a `kParams` alias; the row order here
// matches that enum exactly (drive=0 .. stereoLink=23), so the leading ParamId
// index on each row is that same dense id.
//
// SCOPE NOTE: the DynamicPreset and StereoLink enums are deliberately NOT
// declared here — they stay NESTED in ProgramDependentSaturationEffect so a
// translation unit that also includes compressor-effect.h never sees two
// competing `acfx::StereoLink` definitions (an enum redefinition is a hard
// compile error). Only the LABEL arrays live here; the effect maps a bucket
// index to its nested enum itself. For the SAME reason, toDynamicPreset()/
// toStereoLink() (which return those nested enums) stay member functions of
// the effect — every OTHER index->enum converter below returns a type owned by
// an included primitive/core header (SaturationVoicing, SaturationQuality,
// DetectMode, Ballistics, PdsDetection, ModCurve), so it is free to live here as
// a plain acfx free function.
//
// PARAMETER RANGES ARE A TUNING-PASS OPEN QUESTION. The descriptor shapes
// (kinds/units/skews/labels) are normative (data-model.md, contracts/
// program-dependent-saturation-effect-api.md); the exact numeric ranges are
// defensible placeholders, not yet validated against a reference measurement —
// mirroring SaturationEffect's / CompressorEffect's kParams disclaimer.
//
// NOTE on the time-valued attack/release units: ParamUnit (core/dsp/param-id.h)
// has no milliseconds enumerator, only `seconds`. So — matching CompressorEffect
// exactly — this table stores attack/release in SECONDS and tags them
// ParamUnit::seconds, HONESTLY: a host reading the descriptor sees the real unit
// and range (0.0001..0.2 s attack, 0.001..2.0 s release), not ms magnitudes
// mislabelled as seconds. No ms<->s conversion happens on the descriptor path;
// the values forward straight to EnvelopeFollower::setAttack/setRelease (which
// take seconds). data-model.md's ms ranges (0.1..200 / 1..2000 ms) are the same
// spans expressed in ms. (Preset configs still carry ms magnitudes internally —
// PdsPresetConfig.attackMs/releaseMs — and convert via kMsToSec in writePreset;
// that is not a host-facing descriptor, so the ms form stays for readability.)

namespace acfx {

// Option labels for the discrete parameters (single source of truth). The four
// modulation-curve params (drive/bias/tone/mix Curve) share one label array.
inline constexpr std::array<std::string_view, 4> kPdsVoicingLabels = {
    {"softClip", "tape", "console", "tubePreamp"}};
inline constexpr std::array<std::string_view, 3> kPdsQualityLabels = {
    {"naive", "adaa", "oversampled"}};
inline constexpr std::array<std::string_view, 3> kPdsDetectorLabels = {
    {"peak", "rms", "peakHold"}};
inline constexpr std::array<std::string_view, 2> kPdsBallisticsLabels = {
    {"branching", "decoupled"}};
inline constexpr std::array<std::string_view, 2> kPdsDetectionLabels = {
    {"feedForward", "feedBack"}};
inline constexpr std::array<std::string_view, 3> kPdsCurveLabels = {
    {"linear", "log", "exp"}};
inline constexpr std::array<std::string_view, 4> kPdsDynamicPresetLabels = {
    {"none", "opto", "variMu", "tapeComp"}};
inline constexpr std::array<std::string_view, 2> kPdsExternalSidechainLabels = {
    {"off", "on"}};
inline constexpr std::array<std::string_view, 2> kPdsStereoLinkLabels = {
    {"perChannel", "linked"}};

// The single source of parameter truth (data-model.md "Parameter table"). Row
// order matches ProgramDependentSaturationEffect::Param (leading ParamId index
// == that dense id). The first seven rows (drive..quality) mirror
// SaturationEffect's static-character block EXACTLY so zero-depth orthogonality
// is a like-for-like comparison. Shapes are normative; ranges are the
// tuning-pass OPEN QUESTION:
//   0  drive:             dB, 0..48, default 0
//   1  voicing:           discrete {softClip, tape, console, tubePreamp}
//   2  tone:              linear, -1..1, default 0
//   3  mix:               linear, 0..1, default 1
//   4  output:            dB, -24..24, default 0
//   5  bias:              linear, -1..1, default 0
//   6  quality:           discrete {naive, adaa, oversampled}, default adaa
//   7  detector:          discrete {peak, rms, peakHold}, default rms
//   8  ballistics:        discrete {branching, decoupled}, default branching
//   9  attack:            seconds, 0.0001..0.2 (0.1..200 ms), default 0.01
//   10 release:           seconds, 0.001..2.0 (1..2000 ms), default 0.1
//   11 detection:         discrete {feedForward, feedBack}, default feedForward
//   12 driveDepth:        linear, -1..1, default 0
//   13 driveCurve:        discrete {linear, log, exp}, default linear
//   14 biasDepth:         linear, -1..1, default 0
//   15 biasCurve:         discrete {linear, log, exp}, default linear
//   16 toneDepth:         linear, -1..1, default 0
//   17 toneCurve:         discrete {linear, log, exp}, default linear
//   18 mixDepth:          linear, -1..1, default 0
//   19 mixCurve:          discrete {linear, log, exp}, default linear
//   20 dynamicPreset:     discrete {none, opto, variMu, tapeComp}, default none
//   21 externalSidechain: discrete {off, on}, default off
//   22 scHpf:             Hz, 0..500, default 0 (0 = bypass)
//   23 stereoLink:        discrete {perChannel, linked}, default perChannel
inline constexpr std::array<ParameterDescriptor, 24> kPdsParams = {{
    {ParamId{0}, "drive", ParamUnit::decibels, 0.0f, 48.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{1}, "voicing", ParamUnit::none, 0.0f, 3.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 4, kPdsVoicingLabels},
    {ParamId{2}, "tone", ParamUnit::none, -1.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{3}, "mix", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{4}, "output", ParamUnit::decibels, -24.0f, 24.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{5}, "bias", ParamUnit::none, -1.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{6}, "quality", ParamUnit::none, 0.0f, 2.0f, 1.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kPdsQualityLabels},
    {ParamId{7}, "detector", ParamUnit::none, 0.0f, 2.0f, 1.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kPdsDetectorLabels},
    {ParamId{8}, "ballistics", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kPdsBallisticsLabels},
    {ParamId{9}, "attack", ParamUnit::seconds, 0.0001f, 0.2f, 0.01f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{10}, "release", ParamUnit::seconds, 0.001f, 2.0f, 0.1f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{11}, "detection", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kPdsDetectionLabels},
    {ParamId{12}, "driveDepth", ParamUnit::none, -1.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{13}, "driveCurve", ParamUnit::none, 0.0f, 2.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kPdsCurveLabels},
    {ParamId{14}, "biasDepth", ParamUnit::none, -1.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{15}, "biasCurve", ParamUnit::none, 0.0f, 2.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kPdsCurveLabels},
    {ParamId{16}, "toneDepth", ParamUnit::none, -1.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{17}, "toneCurve", ParamUnit::none, 0.0f, 2.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kPdsCurveLabels},
    {ParamId{18}, "mixDepth", ParamUnit::none, -1.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{19}, "mixCurve", ParamUnit::none, 0.0f, 2.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 3, kPdsCurveLabels},
    {ParamId{20}, "dynamicPreset", ParamUnit::none, 0.0f, 3.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 4, kPdsDynamicPresetLabels},
    {ParamId{21}, "externalSidechain", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kPdsExternalSidechainLabels},
    {ParamId{22}, "scHpf", ParamUnit::hz, 0.0f, 500.0f, 0.0f, ParamSkew::linear,
     ParamKind::continuous, 0},
    {ParamId{23}, "stereoLink", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
     ParamKind::discrete, 2, kPdsStereoLinkLabels},
}};

// Build-time guard (mirrors compressor-parameters.h): the table's ids must be
// DENSE and IN ORDER — row i must carry ParamId{i}. Param is a dense index into
// this table and setParameter() dispatches on id.value, so a row reorder not
// mirrored in the Param enum would silently mismap controls. This makes that a
// compile error instead.
static_assert(
    [] {
        for (std::size_t i = 0; i < kPdsParams.size(); ++i)
            if (static_cast<std::size_t>(kPdsParams[i].id.value) != i)
                return false;
        return true;
    }(),
    "kPdsParams ids must be dense and in-order (row i must have ParamId{i}) so the "
    "ParamId dispatch in ProgramDependentSaturationEffect stays aligned with the Param enum");

// ---------------------------------------------------------------------------
// Stateless helpers for ProgramDependentSaturationEffect (moved out of
// program-dependent-saturation-effect.h per FR-025/FR-028 — none of these
// touch per-channel core state, so they are free functions rather than
// members). See the SCOPE NOTE above for why toDynamicPreset/toStereoLink stay
// on the effect instead of joining this list.
// ---------------------------------------------------------------------------

// float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy is a
// register move) so the effect's cross-thread pending-parameter atomics are
// provably lock-free.
inline std::uint32_t floatBits(float f) noexcept {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
inline float bitsFloat(std::uint32_t u) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// dB -> linear gain. MIRRORS SaturationEffect::dbToGain EXACTLY so the
// modulated-drive orthogonality identity is byte-for-byte (setOutput takes a
// linear gain; drive is passed to the core in dB — the core converts).
inline float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

// Discrete bucket index -> enum (label-array order).
inline SaturationVoicing toVoicing(float index) noexcept {
    switch (static_cast<int>(index)) {
    case 1: return SaturationVoicing::tape;
    case 2: return SaturationVoicing::console;
    case 3: return SaturationVoicing::tubePreamp;
    default: return SaturationVoicing::softClip;
    }
}
inline SaturationQuality toQuality(float index) noexcept {
    switch (static_cast<int>(index)) {
    case 2: return SaturationQuality::oversampled;
    case 1: return SaturationQuality::adaa;
    default: return SaturationQuality::naive;
    }
}
inline DetectMode toDetectMode(float index) noexcept {
    switch (static_cast<int>(index)) {
    case 2: return DetectMode::peakHold;
    case 1: return DetectMode::rms;
    default: return DetectMode::peak;
    }
}
inline Ballistics toBallistics(float index) noexcept {
    return static_cast<int>(index) == 1 ? Ballistics::decoupled : Ballistics::branching;
}
inline PdsDetection toDetection(float index) noexcept {
    return static_cast<int>(index) == 1 ? PdsDetection::feedBack : PdsDetection::feedForward;
}
inline ModCurve toModCurve(float index) noexcept {
    switch (static_cast<int>(index)) {
    case 2: return ModCurve::exponential;
    case 1: return ModCurve::logarithmic;
    default: return ModCurve::linear;
    }
}

} // namespace acfx

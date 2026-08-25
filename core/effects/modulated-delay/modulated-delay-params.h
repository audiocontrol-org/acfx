#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "dsp/param-id.h"
#include "dsp/parameter.h"

// Parameter identities + the single constexpr descriptor table for
// ModulatedDelayEffect, extracted from modulated-delay-effect.h to keep both
// files under the Constitution VII per-file line budget (the effect header hit
// the ceiling when the lo-fi layer appended two params + the internal-rate
// loop). Mirrors the compressor-parameters.h / program-dependent-saturation-
// parameters.h split: the sibling owns the LABEL arrays and the descriptor
// table; the effect wrapper re-exposes the dense-index names in class scope.
//
// SCOPE NOTE: ModDelayParam is a SCOPED enum (enum class) and every name here is
// specific-prefixed (kModDelay*), so nothing leaks a bare, collision-prone name
// (kMix / kMode / kCutoff / kModeLabels, ...) into acfx:: — a translation unit
// that composes this header with another effect never sees two competing
// acfx::X definitions (a hard compile error). The effect header hand-declares
// class-scope aliases (static constexpr, integer-typed) for the enumerators
// rather than C++20 `using enum`, because the core must also compile as C++17
// (Teensy); integer typing keeps kParams[kX] / ParamId{p} / std::array<Param,N>
// working without a cast at every use site.
//
// Platform independence (Constitution IV): standard library only.

namespace acfx {

// Stable parameter ids — dense index into kModulatedDelayParams. US1 ids 0..5
// are frozen; US2 6..14, US3 15..18, lo-fi 19..20 are appended (never reordered)
// so published MIDI-CC->index bindings stay stable.
enum class ModDelayParam : std::uint8_t {
    kDelayTime      = 0,
    kFeedback       = 1,
    kMix            = 2,
    kCutoff         = 3,
    kResonance      = 4,
    kMode           = 5,
    // US2: three independent modulation LFOs (indices 6..14).
    kDelayModRate   = 6,
    kDelayModDepth  = 7,
    kDelayModShape  = 8,
    kCutoffModRate  = 9,
    kCutoffModDepth = 10,
    kCutoffModShape = 11,
    kResModRate     = 12,
    kResModDepth    = 13,
    kResModShape    = 14,
    // US3: wow & flutter on the input path (indices 15..18).
    kWowRate        = 15,
    kWowDepth       = 16,
    kFlutterRate    = 17,
    kFlutterDepth   = 18,
    // Lo-fi layer: internal-rate decimation + in-loop bit-crush (indices 19..20).
    kLofiRate       = 19,
    kLofiBits       = 20,
};

// Underlying dense id of a ModDelayParam (the row/index it names).
inline constexpr std::uint8_t modDelayId(ModDelayParam p) noexcept {
    return static_cast<std::uint8_t>(p);
}

// Number of declared parameters (kParams size and the pending-edit array bound).
inline constexpr std::size_t kModDelayNumParams = 21;

// Option labels for discrete parameters (single source of truth for adapters).
// Prefixed (kModDelay*) so they never collide with another effect's labels at
// acfx:: namespace scope (mirrors kCompressorModeLabels / kPdsVoicingLabels).
inline constexpr std::array<std::string_view, 3> kModDelayModeLabels  = {{"lowpass", "highpass", "bandpass"}};
inline constexpr std::array<std::string_view, 4> kModDelayShapeLabels = {{"sine", "triangle", "saw", "random"}};
// Lo-fi rate divisor D = 1 << index, index 0..3 -> {1,2,4,8}.
inline constexpr std::array<std::string_view, 4> kModDelayRateLabels  = {{"/1", "/2", "/4", "/8"}};
// Lo-fi effective bit depth B, index 0..4 -> kModDelayBitsTable below.
inline constexpr std::array<std::string_view, 5> kModDelayBitsLabels  = {{"16", "12", "8", "6", "4"}};

// Effective bit depth per kLofiBits index (index 0 == 16 == the crush bypass).
inline constexpr std::array<int, 5> kModDelayBitsTable = {{16, 12, 8, 6, 4}};

// Single source of parameter truth (SC-006, FR-022). All values in plain units.
// The leading ParamId on each row is that row's dense id (== the ModDelayParam
// enumerator naming it), so a host reading the descriptor sees the stable index.
inline constexpr std::array<ParameterDescriptor, kModDelayNumParams> kModulatedDelayParams = {{
    // US1 parameters (indices 0..5 — frozen)
    {ParamId{modDelayId(ModDelayParam::kDelayTime)}, "delay_time", ParamUnit::seconds,
     0.001f, 2.0f, 0.3f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kFeedback)}, "feedback", ParamUnit::none,
     0.0f, 0.98f, 0.4f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kMix)}, "mix", ParamUnit::none,
     0.0f, 1.0f, 0.35f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kCutoff)}, "fb_cutoff", ParamUnit::hz,
     20.0f, 20000.0f, 2000.0f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kResonance)}, "fb_resonance", ParamUnit::none,
     0.0f, 1.0f, 0.2f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kMode)}, "fb_mode", ParamUnit::none,
     0.0f, 2.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 3, kModDelayModeLabels},
    // US2 delay-time modulation (indices 6..8)
    {ParamId{modDelayId(ModDelayParam::kDelayModRate)}, "delay_mod_rate", ParamUnit::hz,
     0.01f, 20.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kDelayModDepth)}, "delay_mod_depth", ParamUnit::none,
     0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kDelayModShape)}, "delay_mod_shape", ParamUnit::none,
     0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4, kModDelayShapeLabels},
    // US2 cutoff modulation (indices 9..11)
    {ParamId{modDelayId(ModDelayParam::kCutoffModRate)}, "cutoff_mod_rate", ParamUnit::hz,
     0.01f, 20.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kCutoffModDepth)}, "cutoff_mod_depth", ParamUnit::none,
     0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kCutoffModShape)}, "cutoff_mod_shape", ParamUnit::none,
     0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4, kModDelayShapeLabels},
    // US2 resonance modulation (indices 12..14)
    {ParamId{modDelayId(ModDelayParam::kResModRate)}, "res_mod_rate", ParamUnit::hz,
     0.01f, 20.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kResModDepth)}, "res_mod_depth", ParamUnit::none,
     0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kResModShape)}, "res_mod_shape", ParamUnit::none,
     0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4, kModDelayShapeLabels},
    // US3 wow & flutter on the input path (indices 15..18)
    {ParamId{modDelayId(ModDelayParam::kWowRate)}, "wow_rate", ParamUnit::hz,
     0.1f, 2.0f, 0.5f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kWowDepth)}, "wow_depth", ParamUnit::none,
     0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kFlutterRate)}, "flutter_rate", ParamUnit::hz,
     5.0f, 12.0f, 8.0f, ParamSkew::logarithmic, ParamKind::continuous, 0},
    {ParamId{modDelayId(ModDelayParam::kFlutterDepth)}, "flutter_depth", ParamUnit::none,
     0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0},
    // Lo-fi layer (indices 19..20): two discrete live params.
    // kLofiRate index 0..3 -> D = 1<<index (/1,/2,/4,/8); trades bandwidth for time.
    {ParamId{modDelayId(ModDelayParam::kLofiRate)}, "lofi_rate", ParamUnit::none,
     0.0f, 3.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 4, kModDelayRateLabels},
    // kLofiBits index 0..4 -> B in {16,12,8,6,4}; index 0 (B=16) is the crush bypass.
    {ParamId{modDelayId(ModDelayParam::kLofiBits)}, "lofi_bits", ParamUnit::none,
     0.0f, 4.0f, 0.0f, ParamSkew::linear, ParamKind::discrete, 5, kModDelayBitsLabels},
}};

// Build-time guard: every descriptor in the table is valid (max>min; logarithmic
// => min>0; discrete => count>=2 and choices.size()==count).
static_assert(
    [] {
        for (const ParameterDescriptor& d : kModulatedDelayParams)
            if (!isValidDescriptor(d))
                return false;
        return true;
    }(),
    "ModulatedDelayEffect parameter table violates a descriptor invariant "
    "(max>min; logarithmic => min>0; discrete => count>=2 and choices.size()==count)");

} // namespace acfx

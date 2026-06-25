#pragma once

#include <cstdint>

// Strongly-typed parameter identity + the descriptor enums. No platform headers.
// (contracts/parameter-model.md)

namespace acfx {

// A strongly-typed parameter identifier — not a bare int. Small, stable, dense
// per effect; indexes into the effect's constexpr descriptor table.
struct ParamId {
    std::uint8_t value;
};

constexpr bool operator==(ParamId a, ParamId b) noexcept { return a.value == b.value; }
constexpr bool operator!=(ParamId a, ParamId b) noexcept { return a.value != b.value; }

// Display/semantic unit of a parameter (for labels + host metadata).
enum class ParamUnit : std::uint8_t { none, hz, decibels, percent, ratio };

// How a normalized 0..1 value maps to plain units.
enum class ParamSkew : std::uint8_t { linear, logarithmic };

// Whether a parameter is a continuous range or a discrete enumeration.
enum class ParamKind : std::uint8_t { continuous, discrete };

} // namespace acfx

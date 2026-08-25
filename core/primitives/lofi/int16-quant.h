#pragma once

#include <cstdint>

// Core-local 16-bit <-> float quantization convention, standard-library only
// (Constitution IV: platform-independent core; no adapter/board headers).
//
// This deliberately re-implements the same convention as
// adapters/nucleo/support/sample-format.h (FR-038a: scale x32768, round
// ties-away-from-zero, clamp [-32768, 32767] with saturation, no wrap)
// rather than including that header — the core must not depend on any
// adapter (C-CORE-INWARD).

namespace acfx {

// Normalizing scale between a 16-bit PCM sample and its float representation
// in [-1.0, 1.0).
inline constexpr float kInt16Scale = 32768.0f;

// Convert one normalized float sample to int16, rounding to nearest with
// ties resolved away from zero, and clamping (saturating, never wrapping)
// to the representable int16 range.
inline std::int16_t quantizeInt16(float sample) noexcept {
    const float scaled = sample * kInt16Scale;

    if (scaled >= 32767.0f) {
        return 32767;
    }
    if (scaled <= -32768.0f) {
        return -32768;
    }

    const float biased = (scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f);
    return static_cast<std::int16_t>(biased);
}

// Convert an int16 code back to its normalized float representation.
inline float dequantizeInt16(std::int16_t code) noexcept {
    return static_cast<float>(code) / kInt16Scale;
}

} // namespace acfx

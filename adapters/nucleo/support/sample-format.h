#pragma once

// Platform-independent 16-bit <-> float sample-format conversion for the
// Nucleo USB audio adapter (FR-038, FR-038a, FR-028a). No TinyUSB, no CMSIS,
// no board headers, no <cstdio> — this header compiles under the `test`
// preset with no toolchain file. Anything that cannot satisfy that
// constraint belongs in nucleo-main.cpp instead.

#include <cstdint>

namespace acfx::nucleo {

// Normalizing scale between a 16-bit PCM sample and its float representation
// in [-1.0, 1.0). Applied in both directions (FR-038a).
inline constexpr float kInt16Scale = 32768.0f;

// A USB audio packet carries 0 to 49 stereo frames inclusive (D21 / FR-028).
inline constexpr int kMaxPacketFrames = 49;

// The DSP block cadence is fixed and independent of packet size (FR-030a).
inline constexpr int kBlockFrames = 48;

// Stereo.
inline constexpr int kChannels = 2;

namespace detail {

// Convert one normalized float sample to int16, rounding to nearest with
// ties resolved away from zero, and clamping to the representable int16
// range. The clamp is evaluated on the pre-rounding scaled value and is
// load-bearing, not defensive: an effect that overshoots 1.0 would
// otherwise wrap to the opposite rail on the narrowing cast below and emit
// loud broadband noise (FR-038a). Clamping before the cast also avoids the
// undefined behaviour of narrowing an out-of-range float to int16_t.
inline std::int16_t floatToInt16(float sample) noexcept {
    constexpr float kMin = -32768.0f;
    constexpr float kMax = 32767.0f;

    const float scaled = sample * kInt16Scale;

    if (scaled >= kMax) {
        return 32767;
    }
    if (scaled <= kMin) {
        return -32768;
    }

    // Round to nearest, ties away from zero. `scaled` is strictly within
    // (kMin, kMax) here, so after biasing by +-0.5 and truncating toward
    // zero (the narrowing cast's own behaviour), the result stays within
    // the representable int16 range — the cast below is always well
    // defined.
    const float biased = (scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f);
    return static_cast<std::int16_t>(biased);
}

} // namespace detail

// USB -> effect: de-interleave and convert. `dst` holds kChannels contiguous
// float buffers of at least `frames` samples each. Reads exactly
// `2 * frames` int16 values from `src`; `frames == 0` is a valid no-op.
inline void deinterleaveToFloat(const std::int16_t* src, float* const* dst, int frames) noexcept {
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            dst[channel][frame] =
                static_cast<float>(src[kChannels * frame + channel]) / kInt16Scale;
        }
    }
}

// Effect -> USB: convert, round to nearest (ties away from zero), CLAMP,
// and interleave. Writes exactly `2 * frames` int16 values to `dst`;
// `frames == 0` is a valid no-op.
inline void interleaveToInt16(const float* const* src, std::int16_t* dst, int frames) noexcept {
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            dst[kChannels * frame + channel] = detail::floatToInt16(src[channel][frame]);
        }
    }
}

// Result of validating a USB packet's byte count against the stereo-frame
// size (FR-028a). A byte count that is not a whole number of stereo frames
// truncates to the whole frames it does contain, and `wasTruncated` reports
// that the (always 1-3 byte) remainder was discarded.
struct PayloadFrameCount {
    int frames;
    bool wasTruncated;
};

// Convert a payload byte count to a whole stereo-frame count, truncating any
// partial-frame remainder rather than rejecting the packet outright — this
// preserves L/R channel alignment downstream (FR-028a).
inline constexpr PayloadFrameCount payloadToFrameCount(int byteCount) noexcept {
    constexpr int kBytesPerFrame = kChannels * static_cast<int>(sizeof(std::int16_t));
    const int frames = byteCount / kBytesPerFrame;
    const bool wasTruncated = (byteCount % kBytesPerFrame) != 0;
    return PayloadFrameCount{frames, wasTruncated};
}

} // namespace acfx::nucleo

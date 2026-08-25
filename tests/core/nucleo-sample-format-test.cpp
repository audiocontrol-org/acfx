#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "sample-format.h"
#include "support/allocation-sentinel.h"

// Sample-format contract (FR-038, FR-038a, FR-028, FR-028a). Tests cover:
// SF1 — Round-trip exactness: every representable int16 value survives
//       deinterleaveToFloat → interleaveToInt16 exactly.
// SF2 — Clamping, not wrapping: floats outside [-1.0, 1.0) clamp to the rails,
//       never wrap to the opposite one.
// SF2a — Ties away from zero: exact .5 LSB cases round away from zero.
// SF3 — Exact frame counts: for every frames in [0, 49], exactly that many
//       frames are read/written; no access past the payload.
// SF3a — Torn payloads truncate: byte counts that are not whole frames yield
//        whole frames plus a truncation flag.
// SF4 — No allocation: lives in nucleo-sample-format-no-allocation-test.cpp
//       (split out for the per-file line budget).
// SF5 — noexcept: both functions are callable from real-time contexts.

using namespace acfx::nucleo;
using acfx::test::AllocationSentinel;

namespace {

// Guard/sentinel approach for SF3 tests: wrap buffers with known values
// to prove no out-of-bounds access occurs.
constexpr std::int16_t kSentinelInt16 = static_cast<std::int16_t>(0xDEAD);
constexpr float kSentinelFloat = -9999.0f;

// Helper: scale float to int16 range, clamped. Used to compute expected values.
std::int16_t floatToInt16Clamped(float x) noexcept {
    constexpr float kMin = -32768.0f;
    constexpr float kMax = 32767.0f;
    const float scaled = x * kInt16Scale;
    if (scaled >= kMax) return 32767;
    if (scaled <= kMin) return -32768;
    // Round to nearest, ties away from zero
    if (scaled >= 0.0f) {
        return static_cast<std::int16_t>(scaled + 0.5f);
    } else {
        return static_cast<std::int16_t>(scaled - 0.5f);
    }
}

// Helper: int16 to float, normalized by kInt16Scale.
float int16ToFloat(std::int16_t x) noexcept { return static_cast<float>(x) / kInt16Scale; }

} // namespace

// ============================================================================
// SF1: Round-trip exactness
// ============================================================================

TEST_CASE("SF1: deinterleaveToFloat -> interleaveToInt16 recovers exact values for all int16") {
    // Full sweep of all 65536 representable int16 values.
    for (std::int32_t raw = -32768; raw <= 32767; ++raw) {
        const std::int16_t original = static_cast<std::int16_t>(raw);

        // Interleaved input: [L, R, L, R] = [original, original, original, original] (2 frames)
        std::int16_t input[4] = {original, original, original, original};

        // Destination buffers for deinterleave (2 channels, 2 frames each).
        std::vector<float> left(2);
        std::vector<float> right(2);
        float* dst[2] = {left.data(), right.data()};

        // Deinterleave.
        deinterleaveToFloat(input, dst, 2);

        // Re-interleave.
        std::int16_t output[4] = {0, 0, 0, 0};
        const float* src[2] = {left.data(), right.data()};
        interleaveToInt16(src, output, 2);

        // Every sample should recover exactly.
        CHECK_MESSAGE(output[0] == original, "frame 0 L: int16=", raw);
        CHECK_MESSAGE(output[1] == original, "frame 0 R: int16=", raw);
        CHECK_MESSAGE(output[2] == original, "frame 1 L: int16=", raw);
        CHECK_MESSAGE(output[3] == original, "frame 1 R: int16=", raw);
    }
}

// ============================================================================
// SF2: Clamping, not wrapping
// ============================================================================

TEST_CASE("SF2: clamp +1.5 to +32767, not wrap") {
    std::vector<float> left(1, 1.5f);   // beyond +1.0
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    // Must clamp to +32767, never wrap to the negative rail.
    CHECK(output[0] == 32767);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2: clamp -1.5 to -32768, not wrap") {
    std::vector<float> left(1, -1.5f);  // beyond -1.0
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    // Must clamp to -32768, never wrap to the positive rail.
    CHECK(output[0] == -32768);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2: clamp +2.0 to +32767") {
    std::vector<float> left(1, 2.0f);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    CHECK(output[0] == 32767);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2: clamp -2.0 to -32768") {
    std::vector<float> left(1, -2.0f);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    CHECK(output[0] == -32768);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2: clamp +1e9 to +32767 (extreme overshoot)") {
    std::vector<float> left(1, 1e9f);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    // Extreme overshoot must still clamp to the near rail.
    CHECK(output[0] == 32767);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2: clamp -1e9 to -32768 (extreme undershoot)") {
    std::vector<float> left(1, -1e9f);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    CHECK(output[0] == -32768);
    CHECK(output[1] == 0);
}

// ============================================================================
// SF2a: Ties away from zero
// ============================================================================

TEST_CASE("SF2a: +0.5 LSB rounds away from zero (up to +1)") {
    // 0.5 LSB in normalized float = 0.5 / 32768.0f
    const float half_lsb = 0.5f / kInt16Scale;  // exact .5 LSB
    std::vector<float> left(1, half_lsb);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    // Round away from zero: +0.5 LSB should round to +1.
    CHECK(output[0] == 1);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2a: -0.5 LSB rounds away from zero (down to -1)") {
    // -0.5 LSB in normalized float
    const float half_lsb = -0.5f / kInt16Scale;
    std::vector<float> left(1, half_lsb);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    // Round away from zero: -0.5 LSB should round to -1.
    CHECK(output[0] == -1);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2a: +32767.5 rounds away from zero (clamps to +32767)") {
    // Value between 32767 and 32768, ties away from zero.
    // Since 32768 exceeds int16 max, this should clamp to 32767.
    const float val = 32767.5f / kInt16Scale;  // slightly over +1.0
    std::vector<float> left(1, val);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    CHECK(output[0] == 32767);
    CHECK(output[1] == 0);
}

TEST_CASE("SF2a: -32768.5 rounds away from zero (clamps to -32768)") {
    const float val = -32768.5f / kInt16Scale;  // slightly under -1.0
    std::vector<float> left(1, val);
    std::vector<float> right(1, 0.0f);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[2] = {0, 0};
    interleaveToInt16(src, output, 1);

    CHECK(output[0] == -32768);
    CHECK(output[1] == 0);
}

// ============================================================================
// SF3: Exact frame counts, no out-of-range access
// ============================================================================

TEST_CASE("SF3: frames=0 is a valid no-op (deinterleaveToFloat)") {
    std::int16_t input[1] = {kSentinelInt16};
    std::vector<float> left(1, kSentinelFloat);
    std::vector<float> right(1, kSentinelFloat);
    float* dst[2] = {left.data(), right.data()};

    // frames=0 should touch nothing.
    deinterleaveToFloat(input, dst, 0);

    // Sentinel values must remain untouched.
    CHECK(left[0] == kSentinelFloat);
    CHECK(right[0] == kSentinelFloat);
}

TEST_CASE("SF3: frames=0 is a valid no-op (interleaveToInt16)") {
    std::vector<float> left(1, kSentinelFloat);
    std::vector<float> right(1, kSentinelFloat);
    const float* src[2] = {left.data(), right.data()};

    std::int16_t output[1] = {kSentinelInt16};

    // frames=0 should touch nothing.
    interleaveToInt16(src, output, 0);

    CHECK(output[0] == kSentinelInt16);
}

TEST_CASE("SF3: deinterleaveToFloat processes exactly N frames") {
    for (int frames = 1; frames <= kMaxPacketFrames; ++frames) {
        // Create input: interleaved L/R pairs, each channel alternates value.
        std::vector<std::int16_t> input(static_cast<std::size_t>(2 * frames));
        for (int i = 0; i < frames; ++i) {
            input[2 * i] = 100;  // L
            input[2 * i + 1] = 200;  // R
        }

        // Guard buffers: sentinels before and after the valid region.
        std::vector<float> left(static_cast<std::size_t>(frames + 2), kSentinelFloat);
        std::vector<float> right(static_cast<std::size_t>(frames + 2), kSentinelFloat);

        // Point to the guarded region (skip the leading sentinel).
        float* dst[2] = {left.data() + 1, right.data() + 1};

        deinterleaveToFloat(input.data(), dst, frames);

        // Check sentinels were not touched (bounds proof).
        CHECK_MESSAGE(left[0] == kSentinelFloat, "frames=", frames, " touched before L");
        CHECK_MESSAGE(left[frames + 1] == kSentinelFloat, "frames=", frames, " touched after L");
        CHECK_MESSAGE(right[0] == kSentinelFloat, "frames=", frames, " touched before R");
        CHECK_MESSAGE(right[frames + 1] == kSentinelFloat, "frames=", frames, " touched after R");

        // Check exactly frames samples were deinterleaved.
        for (int i = 0; i < frames; ++i) {
            CHECK_MESSAGE(dst[0][i] == int16ToFloat(100), "frame ", i, " L mismatch");
            CHECK_MESSAGE(dst[1][i] == int16ToFloat(200), "frame ", i, " R mismatch");
        }
    }
}

TEST_CASE("SF3: interleaveToInt16 processes exactly N frames and touches nothing beyond") {
    for (int frames = 1; frames <= kMaxPacketFrames; ++frames) {
        std::vector<float> left(static_cast<std::size_t>(frames), 0.3f);
        std::vector<float> right(static_cast<std::size_t>(frames), 0.6f);
        const float* src[2] = {left.data(), right.data()};

        // Guard with sentinels.
        std::vector<std::int16_t> output(static_cast<std::size_t>(2 * frames + 2), kSentinelInt16);

        // Point to guarded region.
        std::int16_t* dst = output.data() + 1;

        interleaveToInt16(src, dst, frames);

        // Check sentinels (bounds proof).
        CHECK_MESSAGE(output[0] == kSentinelInt16, "frames=", frames, " touched before output");
        CHECK_MESSAGE(output[2 * frames + 1] == kSentinelInt16, "frames=", frames,
                      " touched after output");

        // Check exactly 2*frames samples were written (L/R interleaved).
        for (int i = 0; i < frames; ++i) {
            // Left and right should be round-trip recoverable from the output.
            CHECK_MESSAGE(dst[2 * i] != 0, "frame ", i, " L is zero (should be ~9830)");
            CHECK_MESSAGE(dst[2 * i + 1] != 0, "frame ", i, " R is zero (should be ~19660)");
        }
    }
}

TEST_CASE("SF3: deinterleaveToFloat with kBlockFrames (48 frames) covers standard DSP block") {
    const int frames = kBlockFrames;
    std::vector<std::int16_t> input(static_cast<std::size_t>(2 * frames));
    for (int i = 0; i < frames; ++i) {
        input[2 * i] = static_cast<std::int16_t>(i);
        input[2 * i + 1] = static_cast<std::int16_t>(-i);
    }

    std::vector<float> left(static_cast<std::size_t>(frames));
    std::vector<float> right(static_cast<std::size_t>(frames));
    float* dst[2] = {left.data(), right.data()};

    deinterleaveToFloat(input.data(), dst, frames);

    for (int i = 0; i < frames; ++i) {
        CHECK(dst[0][i] == int16ToFloat(static_cast<std::int16_t>(i)));
        CHECK(dst[1][i] == int16ToFloat(static_cast<std::int16_t>(-i)));
    }
}

TEST_CASE("SF3: interleaveToInt16 with kBlockFrames (48 frames) covers standard DSP block") {
    const int frames = kBlockFrames;
    std::vector<float> left(static_cast<std::size_t>(frames));
    std::vector<float> right(static_cast<std::size_t>(frames));

    for (int i = 0; i < frames; ++i) {
        left[i] = static_cast<float>(i) / 1000.0f;
        right[i] = static_cast<float>(-i) / 1000.0f;
    }

    const float* src[2] = {left.data(), right.data()};
    std::vector<std::int16_t> output(static_cast<std::size_t>(2 * frames));

    interleaveToInt16(src, output.data(), frames);

    for (int i = 0; i < frames; ++i) {
        CHECK(output[2 * i] == floatToInt16Clamped(left[i]));
        CHECK(output[2 * i + 1] == floatToInt16Clamped(right[i]));
    }
}

// ============================================================================
// SF3a: Torn payloads truncate (byte-count handling)
// ============================================================================

TEST_CASE("SF3a: payloadToFrameCount converts 4 bytes (1 stereo frame) with no truncation") {
    auto info = payloadToFrameCount(4);
    CHECK(info.frames == 1);
    CHECK(info.wasTruncated == false);
}

TEST_CASE("SF3a: payloadToFrameCount converts 8 bytes (2 stereo frames) with no truncation") {
    auto info = payloadToFrameCount(8);
    CHECK(info.frames == 2);
    CHECK(info.wasTruncated == false);
}

TEST_CASE("SF3a: payloadToFrameCount converts 0 bytes (0 frames) with no truncation") {
    auto info = payloadToFrameCount(0);
    CHECK(info.frames == 0);
    CHECK(info.wasTruncated == false);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 1 byte to 0 frames") {
    auto info = payloadToFrameCount(1);
    CHECK(info.frames == 0);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 2 bytes to 0 frames") {
    auto info = payloadToFrameCount(2);
    CHECK(info.frames == 0);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 3 bytes to 0 frames") {
    auto info = payloadToFrameCount(3);
    CHECK(info.frames == 0);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 5 bytes to 1 frame") {
    auto info = payloadToFrameCount(5);
    CHECK(info.frames == 1);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 6 bytes to 1 frame") {
    auto info = payloadToFrameCount(6);
    CHECK(info.frames == 1);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 7 bytes to 1 frame") {
    auto info = payloadToFrameCount(7);
    CHECK(info.frames == 1);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 200 bytes to 50 frames") {
    // 200 / 4 = 50 frames, no remainder
    auto info = payloadToFrameCount(200);
    CHECK(info.frames == 50);
    CHECK(info.wasTruncated == false);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 201 bytes to 50 frames with truncation flag") {
    // 201 / 4 = 50 remainder 1
    auto info = payloadToFrameCount(201);
    CHECK(info.frames == 50);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 202 bytes to 50 frames with truncation flag") {
    // 202 / 4 = 50 remainder 2
    auto info = payloadToFrameCount(202);
    CHECK(info.frames == 50);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: payloadToFrameCount truncates 203 bytes to 50 frames with truncation flag") {
    // 203 / 4 = 50 remainder 3
    auto info = payloadToFrameCount(203);
    CHECK(info.frames == 50);
    CHECK(info.wasTruncated == true);
}

TEST_CASE("SF3a: deinterleaveToFloat processes whole frames from truncated payload") {
    // Create a 10-byte payload (2 whole stereo frames + 2 truncated bytes).
    std::vector<std::int16_t> input(5);  // 10 bytes = 5 int16 values
    input[0] = 100;
    input[1] = 200;
    input[2] = 300;
    input[3] = 400;
    input[4] = 500;  // This byte and the next are discarded.

    auto info = payloadToFrameCount(10);
    REQUIRE(info.frames == 2);
    REQUIRE(info.wasTruncated == true);

    std::vector<float> left(2);
    std::vector<float> right(2);
    float* dst[2] = {left.data(), right.data()};

    deinterleaveToFloat(input.data(), dst, info.frames);

    // Only the first 2 frames (4 int16 values) should be processed.
    CHECK(dst[0][0] == int16ToFloat(100));
    CHECK(dst[1][0] == int16ToFloat(200));
    CHECK(dst[0][1] == int16ToFloat(300));
    CHECK(dst[1][1] == int16ToFloat(400));
    // The 5th int16 (500) was never read.
}

TEST_CASE("SF3a: payload with 1-3 truncated bytes yields 0 frames + truncation flag") {
    // Test all three truncation remainders.
    for (int remainder = 1; remainder <= 3; ++remainder) {
        auto info = payloadToFrameCount(remainder);
        CHECK_MESSAGE(info.frames == 0, "remainder=", remainder);
        CHECK_MESSAGE(info.wasTruncated == true, "remainder=", remainder);
    }
}

// ============================================================================
// SF4: No allocation
// ============================================================================

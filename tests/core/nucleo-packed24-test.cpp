#include <doctest/doctest.h>

#include <cstdint>
#include <cmath>
#include <array>

#include "sample-format.h"

// Packed-24-bit wire<->float conversion contract (FR-010, T013-T014).
// T013 (RED): tests with deliberately-wrong stub that fails.
// T014: implements real packed-24 conversion to make tests pass.

using namespace acfx::nucleo;

namespace {

// Helper: assemble 3 little-endian bytes into a signed 24-bit integer.
// Returns the value in range [-8388608, 8388607].
std::int32_t assembleLE24(std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) noexcept {
    std::uint32_t raw = b0 | (static_cast<std::uint32_t>(b1) << 8) |
                        (static_cast<std::uint32_t>(b2) << 16);
    // Sign-extend from bit 23.
    if (raw & 0x800000) {
        raw |= 0xFF000000;  // set upper 8 bits for sign extension
    }
    return static_cast<std::int32_t>(raw);
}

// Helper: disassemble a signed 24-bit integer to 3 little-endian bytes.
// Input is clamped to [-8388608, 8388607].
std::array<std::uint8_t, 3> disassembleLE24(std::int32_t value) noexcept {
    // Clamp to 24-bit signed range.
    if (value > 8388607) value = 8388607;
    if (value < -8388608) value = -8388608;

    // Mask to 24 bits to handle negative numbers (two's complement).
    std::uint32_t u24 = static_cast<std::uint32_t>(value) & 0xFFFFFF;

    return {
        static_cast<std::uint8_t>(u24 & 0xFF),
        static_cast<std::uint8_t>((u24 >> 8) & 0xFF),
        static_cast<std::uint8_t>((u24 >> 16) & 0xFF)
    };
}

// Helper: expected float value for a 24-bit signed integer.
float expectedFloat24(std::int32_t value24) noexcept {
    return static_cast<float>(value24) / kPacked24Scale;
}

// Helper: expected 24-bit value when packing a float.
// Uses the same rounding logic as the eventual T014 implementation:
// round to nearest with ties away from zero, then clamp to 24-bit range.
std::int32_t expectedPacked24(float sample) noexcept {
    constexpr float kMin = -8388608.0f;
    constexpr float kMax = 8388607.0f;

    const float scaled = sample * kPacked24Scale;

    if (scaled >= kMax) {
        return 8388607;
    }
    if (scaled <= kMin) {
        return -8388608;
    }

    // Round to nearest, ties away from zero.
    const float biased = (scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f);
    return static_cast<std::int32_t>(biased);
}

}  // namespace

// ============================================================================
// FR-010: Packed-24 wire<->float round-trip exactness and resolution
// ============================================================================

TEST_CASE("FR-010: sampleFromWire24Packed unpacks 0x000000 to 0.0f") {
    std::uint8_t wire[3] = {0x00, 0x00, 0x00};
    float result = sampleFromWire24Packed(wire);
    CHECK(result == doctest::Approx(0.0f));
}

TEST_CASE("FR-010: wireFromSample24Packed packs 0.0f to 0x000000") {
    std::uint8_t out[3] = {0xFF, 0xFF, 0xFF};
    wireFromSample24Packed(0.0f, out);
    CHECK(out[0] == 0x00);
    CHECK(out[1] == 0x00);
    CHECK(out[2] == 0x00);
}

TEST_CASE("FR-010: round-trip 0x000000 (zero)") {
    std::uint8_t wire[3] = {0x00, 0x00, 0x00};
    float sample = sampleFromWire24Packed(wire);
    std::uint8_t packed[3] = {0xFF, 0xFF, 0xFF};
    wireFromSample24Packed(sample, packed);
    CHECK(packed[0] == wire[0]);
    CHECK(packed[1] == wire[1]);
    CHECK(packed[2] == wire[2]);
}

TEST_CASE("FR-010: round-trip 0x7FFFFF (max positive 24-bit signed)") {
    // 0x7FFFFF = 8388607 in decimal (max positive)
    std::uint8_t wire[3] = {0xFF, 0xFF, 0x7F};
    float sample = sampleFromWire24Packed(wire);
    std::uint8_t packed[3] = {0x00, 0x00, 0x00};
    wireFromSample24Packed(sample, packed);
    CHECK(packed[0] == wire[0]);
    CHECK(packed[1] == wire[1]);
    CHECK(packed[2] == wire[2]);
}

TEST_CASE("FR-010: round-trip 0x800000 (min negative 24-bit signed)") {
    // 0x800000 = -8388608 in decimal (min negative, two's complement)
    std::uint8_t wire[3] = {0x00, 0x00, 0x80};
    float sample = sampleFromWire24Packed(wire);
    std::uint8_t packed[3] = {0xFF, 0xFF, 0xFF};
    wireFromSample24Packed(sample, packed);
    CHECK(packed[0] == wire[0]);
    CHECK(packed[1] == wire[1]);
    CHECK(packed[2] == wire[2]);
}

TEST_CASE("FR-010: round-trip 0x000001 (smallest positive)") {
    // 0x000001 = 1 in decimal
    std::uint8_t wire[3] = {0x01, 0x00, 0x00};
    float sample = sampleFromWire24Packed(wire);
    std::uint8_t packed[3] = {0xFF, 0xFF, 0xFF};
    wireFromSample24Packed(sample, packed);
    CHECK(packed[0] == wire[0]);
    CHECK(packed[1] == wire[1]);
    CHECK(packed[2] == wire[2]);
}

TEST_CASE("FR-010: round-trip 0xFFFFFF (negative one, two's complement)") {
    // 0xFFFFFF = -1 in decimal (two's complement, LE)
    std::uint8_t wire[3] = {0xFF, 0xFF, 0xFF};
    float sample = sampleFromWire24Packed(wire);
    std::uint8_t packed[3] = {0x00, 0x00, 0x00};
    wireFromSample24Packed(sample, packed);
    CHECK(packed[0] == wire[0]);
    CHECK(packed[1] == wire[1]);
    CHECK(packed[2] == wire[2]);
}

TEST_CASE("FR-010: round-trip mid-range values") {
    // Test a few mid-range values to ensure the conversion is robust.
    std::int32_t test_values[] = {
        0x000000,    // zero
        0x000100,    // 256
        0x001000,    // 4096
        0x100000,    // 1048576
        0xFFFF00,    // -256 in LE 24-bit signed
        0xFFF000,    // -4096
        0xF00000,    // -1048576
    };

    for (std::int32_t val : test_values) {
        auto bytes = disassembleLE24(val);
        float sample = sampleFromWire24Packed(bytes.data());
        std::uint8_t packed[3];
        wireFromSample24Packed(sample, packed);

        CHECK_MESSAGE(packed[0] == bytes[0], "byte 0 mismatch for value ", val);
        CHECK_MESSAGE(packed[1] == bytes[1], "byte 1 mismatch for value ", val);
        CHECK_MESSAGE(packed[2] == bytes[2], "byte 2 mismatch for value ", val);
    }
}

TEST_CASE("FR-010: float round-trip within 24-bit resolution") {
    // A float should round-trip through pack->unpack within 24-bit resolution.
    // Resolution at mid-scale: 1 / 2^23 ≈ 1.19e-7
    constexpr float tolerance = 1.5f / kPacked24Scale;  // 1.5 LSB

    // Test a range of floats.
    for (float original = -0.999f; original < 0.999f; original += 0.1f) {
        std::uint8_t packed[3];
        wireFromSample24Packed(original, packed);
        float recovered = sampleFromWire24Packed(packed);

        CHECK_MESSAGE(std::abs(recovered - original) <= tolerance,
                      "original=", original, " recovered=", recovered, " diff=",
                      std::abs(recovered - original));
    }
}

TEST_CASE("FR-010: unpack correct float values for known 24-bit samples") {
    // Verify that unpacking produces the expected float values (not that conversion
    // is round-trip, but that the mapping itself is correct).
    struct TestCase {
        std::uint8_t wire[3];
        float expected;
    };

    TestCase cases[] = {
        {{0x00, 0x00, 0x00}, 0.0f},  // 0 -> 0.0
        {{0xFF, 0xFF, 0x7F}, 8388607.0f / kPacked24Scale},  // max positive
        {{0x00, 0x00, 0x80}, -8388608.0f / kPacked24Scale},  // max negative
        {{0x01, 0x00, 0x00}, 1.0f / kPacked24Scale},  // 1 -> 1 LSB
        {{0xFF, 0xFF, 0xFF}, -1.0f / kPacked24Scale},  // -1 -> -1 LSB
    };

    for (const auto& tc : cases) {
        float result = sampleFromWire24Packed(tc.wire);
        CHECK(result == doctest::Approx(tc.expected));
    }
}

TEST_CASE("FR-010: pack correct 24-bit values for known floats") {
    // Verify that packing produces the expected 24-bit wire values (clamping,
    // rounding, and the mapping itself).
    struct TestCase {
        float sample;
        std::uint8_t expected[3];
    };

    TestCase cases[] = {
        {0.0f, {0x00, 0x00, 0x00}},  // 0.0 -> 0
        {1.5f, {0xFF, 0xFF, 0x7F}},  // overshoot clamps to max
        {-1.5f, {0x00, 0x00, 0x80}},  // undershoot clamps to min
    };

    for (const auto& tc : cases) {
        std::uint8_t packed[3];
        wireFromSample24Packed(tc.sample, packed);
        CHECK(packed[0] == tc.expected[0]);
        CHECK(packed[1] == tc.expected[1]);
        CHECK(packed[2] == tc.expected[2]);
    }
}

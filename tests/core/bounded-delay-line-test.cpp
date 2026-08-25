#include <doctest/doctest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include "primitives/delays/bounded-delay-line.h"
#include "primitives/delays/delay-line.h"
#include "primitives/lofi/int16-quant.h"

TEST_CASE("quantizeInt16 matches the FR-038a convention") {
    CHECK(acfx::quantizeInt16(0.0f) == 0);
    CHECK(acfx::quantizeInt16(1.0f) == 32767);     // clamp at +full-scale
    CHECK(acfx::quantizeInt16(-1.0f) == -32768);
    CHECK(acfx::quantizeInt16(2.0f) == 32767);     // saturate, no wrap
    CHECK(acfx::quantizeInt16(-2.0f) == -32768);
    // round ties away from zero: 0.5/32768 scaled = 0.5 -> rounds to 1
    CHECK(acfx::quantizeInt16(0.5f / 32768.0f) == 1);
    CHECK(acfx::dequantizeInt16(16384) == doctest::Approx(0.5f));
}

TEST_CASE("BoundedDelayLine<float> is bit-identical to DelayLine") {
    constexpr int cap = 64;
    std::array<float, 128> backing{};
    acfx::DelayLine ref;
    ref.prepare(backing.data(), cap, 48000.0f);
    acfx::BoundedDelayLine<float, 128> bnd;
    bnd.prepare(cap, 48000.0f);
    // Drive identical write streams and compare fractional reads bit-for-bit.
    for (int n = 0; n < 200; ++n) {
        const float x = std::sin(0.1f * static_cast<float>(n));
        ref.write(x);
        bnd.write(x);
        const float dref = ref.readFractional(12.3f);
        const float dbnd = bnd.readFractional(12.3f);
        REQUIRE(std::bit_cast<std::uint32_t>(dref) == std::bit_cast<std::uint32_t>(dbnd));
    }
}

TEST_CASE("BoundedDelayLine<int16_t> stores on the 16-bit grid") {
    acfx::BoundedDelayLine<std::int16_t, 128> bnd;
    bnd.prepare(64, 48000.0f);
    bnd.write(0.5f);
    for (int i = 0; i < 10; ++i) bnd.write(0.0f);   // push the sample back by 10
    const float d = bnd.readFractional(10.0f);       // integer delay -> exact tap
    CHECK(d == doctest::Approx(0.5f).epsilon(1.0f / 32768.0f));
}

#include <doctest/doctest.h>
#include <cmath>
#include "primitives/lofi/bit-crush.h"

TEST_CASE("crushToGrid bypasses at 16 bits") {
    for (float x = -1.0f; x < 1.0f; x += 0.013f)
        CHECK(acfx::crushToGrid(x, 16) == x);      // exact identity
}
TEST_CASE("crushToGrid preserves zero (mid-tread)") {
    CHECK(acfx::crushToGrid(0.0f, 4) == 0.0f);
}
TEST_CASE("crushToGrid quantizes onto 2^bits levels without shrinking level") {
    // 4-bit grid: step = 2^(1-4) = 0.125. A value near full scale stays near full scale.
    const float q = acfx::crushToGrid(0.97f, 4);
    CHECK(q == doctest::Approx(1.0f));             // rounds to the 0.125 grid, NOT ~0.06
    CHECK(std::fabs(q) > 0.9f);                    // guards the right-shift level-drop trap
    // grid membership: result is an integer multiple of the step
    const float step = 0.125f;
    CHECK(std::fabs(q / step - std::round(q / step)) < 1e-5f);
}

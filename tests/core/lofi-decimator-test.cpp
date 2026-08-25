#include <doctest/doctest.h>
#include "primitives/lofi/decimator.h"

TEST_CASE("decimator ticks every D-th sample, first sample is a tick") {
    acfx::SampleHoldDecimator dec;
    dec.setDivisor(4);
    int ticks = 0;
    for (int n = 0; n < 16; ++n) if (dec.isTick()) ++ticks;
    CHECK(ticks == 4);                              // 16/4
}
TEST_CASE("changing divisor resets phase; the next sample is a tick") {
    acfx::SampleHoldDecimator dec;
    dec.setDivisor(8);
    CHECK(dec.isTick());                            // sample 0 is a tick
    CHECK_FALSE(dec.isTick());                      // sample 1
    dec.setDivisor(2);                              // phase reset
    CHECK(dec.isTick());                            // takes effect immediately on an internal tick
}

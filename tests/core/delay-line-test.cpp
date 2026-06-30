#include <doctest/doctest.h>
#include <array>
#include "primitives/delays/delay-line.h"

using acfx::DelayLine;

// ---------------------------------------------------------------------------
// Helper: fill a delay line with a 1-based ramp [1, 2, ..., n].
// For n == C writes into capacity-C, the closed-form readFractional(d) is:
//   n - d    for d in [0, C-1].
// Derivation: the most recently written value is n, one step back is n-1, etc.
// Linear interpolation: (1-f)*(n-i) + f*(n-1-i) = n - i - f = n - d.
// ---------------------------------------------------------------------------
static void fillRamp(DelayLine& dl, int n) {
    for (int k = 1; k <= n; ++k) {
        dl.write(static_cast<float>(k));
    }
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - prepare/reset zeros the buffer") {
    std::array<float, 8> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 8, 48000.0f);

    CHECK(dl.readFractional(0.0f)   == doctest::Approx(0.0f));
    CHECK(dl.readFractional(3.5f)   == doctest::Approx(0.0f));
    CHECK(dl.readFractional(7.0f)   == doctest::Approx(0.0f));
}

TEST_CASE("DelayLine - reset after writes clears buffer") {
    std::array<float, 8> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 8, 48000.0f);
    fillRamp(dl, 8);

    dl.reset();

    CHECK(dl.readFractional(0.0f)   == doctest::Approx(0.0f));
    CHECK(dl.readFractional(3.0f)   == doctest::Approx(0.0f));
    CHECK(dl.readFractional(7.0f)   == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - integer tap round-trips") {
    std::array<float, 8> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 8, 48000.0f);
    fillRamp(dl, 8);    // writes 1..8; closed-form: readFractional(d) = 8 - d

    CHECK(dl.readFractional(0.0f) == doctest::Approx(8.0f));  // most recent
    CHECK(dl.readFractional(1.0f) == doctest::Approx(7.0f));
    CHECK(dl.readFractional(3.0f) == doctest::Approx(5.0f));
    CHECK(dl.readFractional(7.0f) == doctest::Approx(1.0f));  // oldest (maxDelaySamples)
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - fractional reads match closed-form linear interpolation") {
    // Write ramp 1..8 into capacity-8.
    // Closed form: readFractional(d) = 8 - d  for d in [0, 7].
    // Derivation: for integer part i and fraction f of d,
    //   newer = buffer[(writePos-1-i) mod 8] = 8-i  (most recent is 8, one back is 7, ...)
    //   older = buffer[(writePos-2-i) mod 8] = 7-i
    //   result = (1-f)*(8-i) + f*(7-i) = 8 - i - f = 8 - d.
    std::array<float, 8> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 8, 48000.0f);
    fillRamp(dl, 8);

    // d=0.5 → 8 - 0.5 = 7.5
    CHECK(dl.readFractional(0.5f)  == doctest::Approx(7.5f));
    // d=1.5 → 8 - 1.5 = 6.5
    CHECK(dl.readFractional(1.5f)  == doctest::Approx(6.5f));
    // d=2.75 → 8 - 2.75 = 5.25
    CHECK(dl.readFractional(2.75f) == doctest::Approx(5.25f));
    // d=5.25 → 8 - 5.25 = 2.75
    CHECK(dl.readFractional(5.25f) == doctest::Approx(2.75f));
    // d=6.25 → 8 - 6.25 = 1.75
    CHECK(dl.readFractional(6.25f) == doctest::Approx(1.75f));
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - fractional read with impulse buffer (known value)") {
    // Write [1, 0, 0, 0]: impulse at position 0.
    // After 4 writes into cap-4: buffer = [1,0,0,0], writePos_ = 0.
    // readFractional(3) = buffer[0] = 1 (delay=3 reads the impulse).
    // readFractional(2.5) should interpolate: (1-0.5)*buffer[2]+0.5*buffer[1]
    //   = 0.5*0 + 0.5*0 = 0  (the impulse is exactly at index 0, not between 2 and 3).
    // readFractional(2.5) with cap=4: i=2,f=0.5
    //   newer = buffer[(0-1-2+8)%4] = buffer[5%4] = buffer[1] = 0
    //   older = buffer[(0-2-2+8)%4] = buffer[4%4] = buffer[0] = 1
    //   result = 0.5*0 + 0.5*1 = 0.5
    std::array<float, 4> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 4, 48000.0f);
    dl.write(1.0f);
    dl.write(0.0f);
    dl.write(0.0f);
    dl.write(0.0f);

    CHECK(dl.readFractional(3.0f)  == doctest::Approx(1.0f));
    CHECK(dl.readFractional(2.5f)  == doctest::Approx(0.5f));
    CHECK(dl.readFractional(0.0f)  == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - in-range guarantee: reads at maxDelaySamples and beyond") {
    std::array<float, 4> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 4, 48000.0f);
    fillRamp(dl, 4);    // writes 1..4; most recent=4, oldest=1

    // At maxDelaySamples() = 3: oldest sample = 1
    CHECK(dl.readFractional(dl.maxDelaySamples()) == doctest::Approx(1.0f));

    // Beyond capacity: all clamp to maxDelaySamples(), same result, no UB
    CHECK(dl.readFractional(4.0f)   == doctest::Approx(1.0f));
    CHECK(dl.readFractional(100.0f) == doctest::Approx(1.0f));
    CHECK(dl.readFractional(1e6f)   == doctest::Approx(1.0f));
}

TEST_CASE("DelayLine - in-range guarantee: delay=0 returns most recent sample") {
    std::array<float, 4> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 4, 48000.0f);
    dl.write(42.0f);

    CHECK(dl.readFractional(0.0f) == doctest::Approx(42.0f));
}

TEST_CASE("DelayLine - in-range guarantee: negative delaySamples clamped to 0") {
    std::array<float, 4> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 4, 48000.0f);
    dl.write(99.0f);

    CHECK(dl.readFractional(-1.0f)    == doctest::Approx(99.0f));
    CHECK(dl.readFractional(-1000.0f) == doctest::Approx(99.0f));
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - wrap-around continuity after more writes than capacity") {
    // Cap=4; write 1..8 (two full cycles); only most recent 4 (5,6,7,8) survive.
    const int cap = 4;
    std::array<float, cap> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), cap, 48000.0f);
    fillRamp(dl, 8);    // writes 1..8; surviving: 5,6,7,8 (delay 3..0)

    CHECK(dl.readFractional(0.0f) == doctest::Approx(8.0f));
    CHECK(dl.readFractional(1.0f) == doctest::Approx(7.0f));
    CHECK(dl.readFractional(2.0f) == doctest::Approx(6.0f));
    CHECK(dl.readFractional(3.0f) == doctest::Approx(5.0f));

    // Fractional mid-point between surviving samples
    CHECK(dl.readFractional(0.5f) == doctest::Approx(7.5f));
    CHECK(dl.readFractional(1.5f) == doctest::Approx(6.5f));
}

// ---------------------------------------------------------------------------
TEST_CASE("DelayLine - accessors return prepared values") {
    std::array<float, 16> storage{};
    DelayLine dl;
    dl.prepare(storage.data(), 16, 44100.0f);

    CHECK(dl.capacity()        == 16);
    CHECK(dl.sampleRate()      == doctest::Approx(44100.0f));
    CHECK(dl.maxDelaySamples() == doctest::Approx(15.0f));
}

#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "primitives/lfo.h"

// Helpers -----------------------------------------------------------------

// Prepare an LFO with a tiny synthetic sample rate (4 Hz) so that one tick
// advances phase by exactly 0.25 — giving us exactly four samples per cycle
// with known, closed-form output values for deterministic phase tests.
static acfx::Lfo makePhaseLfo(acfx::LfoShape shape) {
    acfx::Lfo lfo;
    lfo.prepare(4.0f);   // sampleRate = 4 Hz
    lfo.setRate(1.0f);   // inc = 1/4 = 0.25 per tick
    lfo.setShape(shape);
    return lfo;
}

// Sine --------------------------------------------------------------------

TEST_CASE("Lfo sine - known values at key phases") {
    auto lfo = makePhaseLfo(acfx::LfoShape::sine);

    // tick 1: output at phase=0 -> sin(0) = 0
    CHECK(lfo.tick() == doctest::Approx(0.0f).epsilon(1e-6f));
    // tick 2: output at phase=0.25 -> sin(pi/2) = 1
    CHECK(lfo.tick() == doctest::Approx(1.0f).epsilon(1e-6f));
    // tick 3: output at phase=0.5 -> sin(pi) ~ 0
    CHECK(std::abs(lfo.tick()) < 1e-5f);
    // tick 4: output at phase=0.75 -> sin(3*pi/2) = -1
    CHECK(lfo.tick() == doctest::Approx(-1.0f).epsilon(1e-6f));
}

// Triangle ----------------------------------------------------------------

TEST_CASE("Lfo triangle - known values at key phases") {
    auto lfo = makePhaseLfo(acfx::LfoShape::triangle);

    CHECK(lfo.tick() == doctest::Approx(-1.0f)); // phase=0:    4*0   - 1 = -1
    CHECK(lfo.tick() == doctest::Approx(0.0f));  // phase=0.25: 4*0.25 - 1 = 0
    CHECK(lfo.tick() == doctest::Approx(1.0f));  // phase=0.5:  3 - 4*0.5 = 1
    CHECK(lfo.tick() == doctest::Approx(0.0f));  // phase=0.75: 3 - 4*0.75 = 0
}

// Saw ---------------------------------------------------------------------

TEST_CASE("Lfo saw - known values at key phases") {
    auto lfo = makePhaseLfo(acfx::LfoShape::saw);

    CHECK(lfo.tick() == doctest::Approx(-1.0f));  // phase=0:    2*0    - 1 = -1
    CHECK(lfo.tick() == doctest::Approx(-0.5f));  // phase=0.25: 2*0.25 - 1 = -0.5
    CHECK(lfo.tick() == doctest::Approx(0.0f));   // phase=0.5:  2*0.5  - 1 = 0
    CHECK(lfo.tick() == doctest::Approx(0.5f));   // phase=0.75: 2*0.75 - 1 = 0.5
}

// Bounds: all shapes stay in [-1, 1] over a full cycle ------------------

TEST_CASE("Lfo - all shapes stay within [-1, 1] over a full cycle") {
    const float sr   = 1000.0f;
    const float rate = 1.0f;
    const int   N    = static_cast<int>(sr / rate); // one period

    for (auto shape : { acfx::LfoShape::sine,
                        acfx::LfoShape::triangle,
                        acfx::LfoShape::saw,
                        acfx::LfoShape::random }) {
        acfx::Lfo lfo;
        lfo.prepare(sr);
        lfo.setRate(rate);
        lfo.setShape(shape);

        for (int i = 0; i < N; ++i) {
            float v = lfo.tick();
            CHECK(v >= -1.0f);
            CHECK(v <= 1.0f);
        }
    }
}

// Sample-rate independence -----------------------------------------------

// Using SR values that are exact integer multiples of rate so that
// period_samples = SR / rate is an integer with no float residual.
// After exactly one period of ticks the phase wraps to 0 and the next tick
// (with sine) matches the first tick (both at phase=0, output=0).

TEST_CASE("Lfo - sample-rate independence (same period in seconds at multiple rates)") {
    const float rate = 1.0f;

    struct Case { float sr; };
    Case cases[] = { {441.0f}, {480.0f}, {960.0f} };

    for (auto [sr] : cases) {
        acfx::Lfo lfo;
        lfo.prepare(sr);
        lfo.setRate(rate);
        lfo.setShape(acfx::LfoShape::sine);

        const int period = static_cast<int>(sr / rate);

        float first = lfo.tick();           // tick 1: output at phase=0
        for (int i = 1; i < period; ++i)   // ticks 2 … period
            lfo.tick();
        float afterCycle = lfo.tick();      // tick period+1: phase back near 0

        // Both should be sin(2*pi*0) = 0 within float accumulation tolerance.
        CHECK(std::abs(afterCycle - first) < 0.02f);
    }
}

// Reset ------------------------------------------------------------------

TEST_CASE("Lfo - reset returns phase to known start") {
    acfx::Lfo lfo;
    lfo.prepare(44100.0f);
    lfo.setRate(1.0f);
    lfo.setShape(acfx::LfoShape::sine);

    float firstOut = lfo.tick(); // sin(2*pi*0) = 0

    // Advance the LFO an arbitrary number of samples, then reset.
    for (int i = 0; i < 12345; ++i) lfo.tick();

    lfo.reset();
    float afterReset = lfo.tick(); // should again be sin(0) = 0

    CHECK(afterReset == doctest::Approx(firstOut));
}

// Random: reproducibility ------------------------------------------------

TEST_CASE("Lfo random - two instances with same prepare produce identical output") {
    acfx::Lfo lfo1, lfo2;
    lfo1.prepare(44100.0f);
    lfo1.setRate(1.0f);
    lfo1.setShape(acfx::LfoShape::random);

    lfo2.prepare(44100.0f);
    lfo2.setRate(1.0f);
    lfo2.setShape(acfx::LfoShape::random);

    for (int i = 0; i < 200; ++i)
        CHECK(lfo1.tick() == lfo2.tick());
}

TEST_CASE("Lfo random - reset produces identical sequence") {
    acfx::Lfo lfo;
    lfo.prepare(44100.0f);
    lfo.setRate(1.0f);
    lfo.setShape(acfx::LfoShape::random);

    const int N = 200;
    std::vector<float> firstRun;
    firstRun.reserve(N);
    for (int i = 0; i < N; ++i) firstRun.push_back(lfo.tick());

    lfo.reset();
    for (int i = 0; i < N; ++i)
        CHECK(lfo.tick() == firstRun[static_cast<std::size_t>(i)]);
}

// Random: click-free (bounded sample-to-sample delta) -------------------

TEST_CASE("Lfo random - sample-to-sample delta bounded by 2 * inc") {
    const float sr   = 44100.0f;
    const float rate = 10.0f;
    const float inc  = rate / sr;

    // Theoretical max delta per sample = 2 * inc (proved in lfo.h comments).
    // Add a small epsilon for float rounding.
    const float maxDelta = 2.0f * inc + 1e-6f;

    acfx::Lfo lfo;
    lfo.prepare(sr);
    lfo.setRate(rate);
    lfo.setShape(acfx::LfoShape::random);

    // Run 3 full periods (includes wrap boundaries).
    const int cycles = 3;
    const int totalSamples = static_cast<int>(sr / rate) * cycles;

    float prev = lfo.tick();
    for (int i = 1; i < totalSamples; ++i) {
        float curr = lfo.tick();
        CHECK(std::abs(curr - prev) <= maxDelta);
        prev = curr;
    }
}

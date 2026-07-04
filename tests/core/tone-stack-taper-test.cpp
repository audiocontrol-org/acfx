#include <doctest/doctest.h>

#include <cmath>

#include "primitives/circuit/tone-stack/taper.h"

// US2 (spec.md US2, SC-002; contracts/potentiometer.md): the potentiometer as
// build-time math — taper laws, the divider/rheostat split, the fixed 10 ohm
// end-resistance floor, and fail-loud input validation. No circuit, no solver.

using acfx::kEndResistanceOhms;
using acfx::kLogTaperBase;
using acfx::rheostat;
using acfx::Taper;
using acfx::taperFraction;
using acfx::wiper;
using acfx::WiperSplit;

// ---------------------------------------------------------------------------
// Linear taper — pos=0.5 gives equal legs summing to the track (SC-002).
// ---------------------------------------------------------------------------

TEST_CASE("wiper - Linear pos=0.5 splits into equal legs summing to rTrack") {
    const double rTrack = 250000.0;
    const WiperSplit w = wiper(rTrack, 0.5, Taper::Linear);
    CHECK(w.rTop == doctest::Approx(rTrack / 2.0));
    CHECK(w.rBottom == doctest::Approx(rTrack / 2.0));
    CHECK(w.rTop + w.rBottom == doctest::Approx(rTrack));
}

TEST_CASE("wiper - Linear fraction equals pos away from the extremes") {
    const double rTrack = 100000.0;
    for (double pos : {0.25, 0.5, 0.75}) {
        const WiperSplit w = wiper(rTrack, pos, Taper::Linear);
        CHECK(w.rBottom == doctest::Approx(pos * rTrack));
        CHECK(w.rTop == doctest::Approx((1.0 - pos) * rTrack));
        CHECK(w.rTop + w.rBottom == doctest::Approx(rTrack));
    }
}

// ---------------------------------------------------------------------------
// Log ("audio") taper — matches the reference exponential fraction (SC-002).
// ---------------------------------------------------------------------------

TEST_CASE("taperFraction - Log matches the reference audio-taper curve") {
    // f = (base^pos - 1) / (base - 1); endpoints exact, interior concave.
    CHECK(taperFraction(0.0, Taper::Log) == doctest::Approx(0.0));
    CHECK(taperFraction(1.0, Taper::Log) == doctest::Approx(1.0));
    for (double pos : {0.25, 0.5, 0.75}) {
        const double ref =
            (std::pow(kLogTaperBase, pos) - 1.0) / (kLogTaperBase - 1.0);
        CHECK(taperFraction(pos, Taper::Log) == doctest::Approx(ref));
        // Audio taper is concave: fraction sits below the linear line interior.
        CHECK(taperFraction(pos, Taper::Log) < pos);
    }
}

TEST_CASE("wiper - Log split uses the reference fraction of the track") {
    const double rTrack = 500000.0;
    const double pos = 0.5;
    const double f = (std::pow(kLogTaperBase, pos) - 1.0) / (kLogTaperBase - 1.0);
    const WiperSplit w = wiper(rTrack, pos, Taper::Log);
    CHECK(w.rBottom == doctest::Approx(f * rTrack));
    CHECK(w.rTop == doctest::Approx((1.0 - f) * rTrack));
}

// ---------------------------------------------------------------------------
// End-resistance floor — legs never collapse to 0 at the extremes (SC-002).
// ---------------------------------------------------------------------------

TEST_CASE("wiper - pos=0 floors the bottom leg to 10 ohm, never 0") {
    const double rTrack = 250000.0;
    const WiperSplit w = wiper(rTrack, 0.0, Taper::Linear);
    CHECK(w.rBottom == doctest::Approx(kEndResistanceOhms));
    CHECK(w.rTop == doctest::Approx(rTrack));  // full track, above the floor
    CHECK(w.rBottom > 0.0);
}

TEST_CASE("wiper - pos=1 floors the top leg to 10 ohm, never 0") {
    const double rTrack = 250000.0;
    const WiperSplit w = wiper(rTrack, 1.0, Taper::Linear);
    CHECK(w.rTop == doctest::Approx(kEndResistanceOhms));
    CHECK(w.rBottom == doctest::Approx(rTrack));
    CHECK(w.rTop > 0.0);
}

TEST_CASE("wiper - both extremes hold for the Log taper too") {
    const double rTrack = 25000.0;
    CHECK(wiper(rTrack, 0.0, Taper::Log).rBottom == doctest::Approx(kEndResistanceOhms));
    CHECK(wiper(rTrack, 1.0, Taper::Log).rTop == doctest::Approx(kEndResistanceOhms));
}

// ---------------------------------------------------------------------------
// Rheostat — single floored leg (the FMV mid pot to ground).
// ---------------------------------------------------------------------------

TEST_CASE("rheostat - returns the single lower leg, floored at the extreme") {
    const double rTrack = 10000.0;
    CHECK(rheostat(rTrack, 0.5, Taper::Linear) == doctest::Approx(rTrack / 2.0));
    CHECK(rheostat(rTrack, 1.0, Taper::Linear) == doctest::Approx(rTrack));
    CHECK(rheostat(rTrack, 0.0, Taper::Linear) == doctest::Approx(kEndResistanceOhms));
}

// ---------------------------------------------------------------------------
// Fail-loud validation — no silent clamp, no fallback (FR-010).
// ---------------------------------------------------------------------------

TEST_CASE("wiper - pos outside [0,1] throws std::invalid_argument") {
    CHECK_THROWS_AS(wiper(1000.0, -0.01, Taper::Linear), std::invalid_argument);
    CHECK_THROWS_AS(wiper(1000.0, 1.01, Taper::Linear), std::invalid_argument);
    CHECK_THROWS_AS(rheostat(1000.0, 2.0, Taper::Log), std::invalid_argument);
}

TEST_CASE("wiper - non-positive rTrack throws std::invalid_argument") {
    CHECK_THROWS_AS(wiper(0.0, 0.5, Taper::Linear), std::invalid_argument);
    CHECK_THROWS_AS(wiper(-100.0, 0.5, Taper::Linear), std::invalid_argument);
    CHECK_THROWS_AS(rheostat(-1.0, 0.5, Taper::Linear), std::invalid_argument);
}

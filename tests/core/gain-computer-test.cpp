#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "primitives/dynamics/gain-computer.h"

// Static-curve accuracy + shape tests for acfx::GainComputer (tasks T014, T017
// curve part, T019, T027).
//
// Written against core/primitives/dynamics/gain-computer.h (the implemented
// kernel) and specs/compressors/spec.md (SC-001, SC-003, SC-005;
// FR-002..007). computeGainDb() is a PURE function of its configuration and
// the level argument — it holds no runtime state — so these cases need no
// stimulus/ballistics: they assert the analytic map directly.
//
// Expected values are derived from the header's exact arithmetic:
//   u = levelDb - threshold ; kneeGain(u, W, mLow, mHigh) blends a low-side
//   slope mLow into a high-side slope mHigh (both lines through (thr, 0)) with
//   a C1 quadratic over u in [-W/2, +W/2], degenerating to a hard corner at
//   W = 0. Per mode: compress (mLow 0, mHigh 1/R-1), limit (mLow 0, mHigh -1),
//   expand (mLow R-1, mHigh 0, floor rangeDb), gate (hard step at W=0; soft
//   mLow -2*range/W, mHigh 0, floor rangeDb).

using acfx::GainComputer;
using acfx::GainMode;

namespace {

// Named tolerances (svf-reference.h style: assert analytic truths, not
// fabricated precision).
constexpr float kCurveTolDb     = 0.1f;    // headline analytic curve values
constexpr float kHardCornerTolDb = 1e-4f;  // knee=0 must equal analytic hard corner
constexpr float kSlopeJumpTolDb  = 0.05f;  // C1: adjacent finite-diff slopes must not jump
constexpr float kNonPositiveTolDb = 1e-6f; // gain must be <= 0 (allow float -0/rounding)

// Scan computeGainDb() over [loDb, hiDb] and assert the curve is C1 there:
//   * value continuity — a true value jump would drive one finite-difference
//     slope far beyond slopeBound (a jump of O(1) over a step of O(h) blows up).
//   * slope continuity — the numerical derivative itself changes smoothly; a
//     hard corner (slope discontinuity) makes |slope_{i+1}-slope_i| large.
// slopeBound is an upper bound on |dGain/dLevel| anywhere in the window.
void checkC1(const GainComputer& gc, float loDb, float hiDb, float slopeBound) {
    constexpr int N = 2000;
    const float h   = (hiDb - loDb) / static_cast<float>(N);
    float prevVal   = gc.computeGainDb(loDb);
    float prevSlope = 0.0f;
    bool  havePrev  = false;
    for (int i = 1; i <= N; ++i) {
        const float x     = loDb + h * static_cast<float>(i);
        const float val   = gc.computeGainDb(x);
        const float slope = (val - prevVal) / h;
        // Value continuity: bounded slope <=> no finite value jump.
        REQUIRE(std::fabs(slope) <= slopeBound);
        if (havePrev) {
            // C1: no discontinuity in the (numerical) first derivative.
            CHECK(std::fabs(slope - prevSlope) <= kSlopeJumpTolDb);
        }
        prevVal  = val;
        prevSlope = slope;
        havePrev = true;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// T014 — compress curve accuracy, statelessness, non-positivity (FR-002..004;
// SC-001).
// ---------------------------------------------------------------------------
TEST_CASE("GainComputer - compress curve hits the analytic gain-reduction values (T014)") {
    GainComputer gc;
    gc.setMode(GainMode::compress);
    gc.setThreshold(-20.0f);
    gc.setRatio(4.0f);
    gc.setKnee(0.0f);

    // 10 dB over threshold at 4:1 -> output should be 2.5 dB over: gain -7.5 dB.
    CHECK(std::fabs(gc.computeGainDb(-10.0f) - (-7.5f)) < kCurveTolDb);
    // Below threshold: unity (no reduction).
    CHECK(gc.computeGainDb(-30.0f) == doctest::Approx(0.0f));
    // 20 dB over threshold at 4:1 -> gain -15 dB.
    CHECK(std::fabs(gc.computeGainDb(0.0f) - (-15.0f)) < kCurveTolDb);
}

TEST_CASE("GainComputer - compress at 1:1 is unity everywhere (T014)") {
    GainComputer gc;
    gc.setMode(GainMode::compress);
    gc.setThreshold(-20.0f);
    gc.setRatio(1.0f); // 1/R - 1 = 0 -> no reduction at any level
    gc.setKnee(0.0f);

    for (float lvl = -60.0f; lvl <= 12.0f; lvl += 3.0f) {
        CHECK(std::fabs(gc.computeGainDb(lvl)) < kCurveTolDb);
    }
}

TEST_CASE("GainComputer - computeGainDb is stateless / call-order independent (T014, FR-001)") {
    GainComputer gc;
    gc.setMode(GainMode::compress);
    gc.setThreshold(-20.0f);
    gc.setRatio(4.0f);
    gc.setKnee(6.0f);

    // Reference values, each evaluated in isolation.
    const float refA = gc.computeGainDb(-10.0f);
    const float refB = gc.computeGainDb(0.0f);
    const float refC = gc.computeGainDb(-30.0f);

    // Interleave in a different order; results must be bit-identical (pure fn).
    CHECK(gc.computeGainDb(0.0f) == refB);
    CHECK(gc.computeGainDb(-30.0f) == refC);
    CHECK(gc.computeGainDb(-10.0f) == refA);
    CHECK(gc.computeGainDb(-30.0f) == refC);
    CHECK(gc.computeGainDb(-10.0f) == refA);

    // A second, identically-configured instance agrees exactly.
    GainComputer gc2;
    gc2.setMode(GainMode::compress);
    gc2.setThreshold(-20.0f);
    gc2.setRatio(4.0f);
    gc2.setKnee(6.0f);
    CHECK(gc2.computeGainDb(-10.0f) == refA);
    CHECK(gc2.computeGainDb(0.0f) == refB);
}

TEST_CASE("GainComputer - compress gain is always <= 0 (T014, FR-002)") {
    GainComputer gc;
    gc.setMode(GainMode::compress);
    gc.setThreshold(-24.0f);
    gc.setRatio(8.0f);

    for (float knee : {0.0f, 6.0f, 18.0f}) {
        gc.setKnee(knee);
        for (float lvl = -80.0f; lvl <= 12.0f; lvl += 0.5f) {
            CHECK(gc.computeGainDb(lvl) <= kNonPositiveTolDb);
        }
    }
}

// ---------------------------------------------------------------------------
// T017 (curve part) — limit curve holds output at the threshold (FR-005;
// SC-003).
// ---------------------------------------------------------------------------
TEST_CASE("GainComputer - limit curve holds output at threshold (T017)") {
    GainComputer gc;
    gc.setMode(GainMode::limit);
    gc.setThreshold(-6.0f);
    gc.setKnee(0.0f);

    // 5 dB over threshold -> gain -5 dB, so output = -1 dB = threshold.
    CHECK(std::fabs(gc.computeGainDb(-1.0f) - (-5.0f)) < kCurveTolDb);
    CHECK(std::fabs((-1.0f + gc.computeGainDb(-1.0f)) - (-6.0f)) < kCurveTolDb);

    // At threshold: unity.
    CHECK(gc.computeGainDb(-6.0f) == doctest::Approx(0.0f));

    // Well above threshold, output stays pinned at the threshold (out = thr).
    for (float lvl : {0.0f, 6.0f, 12.0f, 24.0f}) {
        const float out = lvl + gc.computeGainDb(lvl);
        CHECK(std::fabs(out - (-6.0f)) < kCurveTolDb);
    }
}

// ---------------------------------------------------------------------------
// T019 — soft knee is C1 (value + slope continuous) in every mode, and knee=0
// reproduces the hard corner exactly (FR-006; SC-005).
// ---------------------------------------------------------------------------
TEST_CASE("GainComputer - soft knee is C1 across the knee window, all modes (T019)") {
    constexpr float kThr = -20.0f;
    constexpr float kW   = 10.0f;                 // knee width
    constexpr float kEps = 2.0f;                  // sample a little past each edge
    const float loFull   = kThr - kW * 0.5f - kEps;
    const float hiFull    = kThr + kW * 0.5f + kEps;

    SUBCASE("compress") {
        GainComputer gc;
        gc.setMode(GainMode::compress);
        gc.setThreshold(kThr);
        gc.setRatio(4.0f);
        gc.setKnee(kW);
        checkC1(gc, loFull, hiFull, /*slopeBound=*/1.0f);
    }
    SUBCASE("limit") {
        GainComputer gc;
        gc.setMode(GainMode::limit);
        gc.setThreshold(kThr);
        gc.setKnee(kW);
        checkC1(gc, loFull, hiFull, /*slopeBound=*/1.5f);
    }
    SUBCASE("expand") {
        GainComputer gc;
        gc.setMode(GainMode::expand);
        gc.setThreshold(kThr);
        gc.setRatio(2.0f);
        gc.setRange(-20.0f); // floor is far below the knee here -> knee stays C1
        gc.setKnee(kW);
        checkC1(gc, loFull, hiFull, /*slopeBound=*/1.5f);
    }
    SUBCASE("gate") {
        // The gate knee is constructed to reach the range floor exactly at the
        // lower knee edge (u = -W/2), so the floor clamp meets the knee there
        // (a deliberate slope kink). Scan only [thr-W/2, thr+W/2], off the
        // floor, where the underlying quadratic knee is C1.
        GainComputer gc;
        gc.setMode(GainMode::gate);
        gc.setThreshold(kThr);
        gc.setRange(-30.0f);
        gc.setKnee(kW);
        checkC1(gc, kThr - kW * 0.5f, kThr + kW * 0.5f + kEps, /*slopeBound=*/7.0f);
    }
}

TEST_CASE("GainComputer - knee=0 reproduces the analytic hard corner, all modes (T019)") {
    constexpr float kThr = -20.0f;
    // Offsets from threshold; skip exactly 0 (boundary tie is mode-defined).
    constexpr float offs[] = {-30.0f, -20.0f, -10.0f, -5.0f, -2.0f, -1.0f,
                              -0.5f,  0.5f,   1.0f,   2.0f,  5.0f,  10.0f, 20.0f};

    SUBCASE("compress") {
        GainComputer gc;
        gc.setMode(GainMode::compress);
        gc.setThreshold(kThr);
        gc.setRatio(4.0f);
        gc.setKnee(0.0f);
        const float mHigh = 1.0f / 4.0f - 1.0f; // -0.75
        for (float o : offs) {
            const float u        = o; // levelDb - thr
            const float expected = (u <= 0.0f) ? 0.0f : mHigh * u;
            CHECK(std::fabs(gc.computeGainDb(kThr + o) - expected) < kHardCornerTolDb);
        }
    }
    SUBCASE("limit") {
        GainComputer gc;
        gc.setMode(GainMode::limit);
        gc.setThreshold(kThr);
        gc.setKnee(0.0f);
        for (float o : offs) {
            const float u        = o;
            const float expected = (u <= 0.0f) ? 0.0f : -1.0f * u;
            CHECK(std::fabs(gc.computeGainDb(kThr + o) - expected) < kHardCornerTolDb);
        }
    }
    SUBCASE("expand") {
        GainComputer gc;
        gc.setMode(GainMode::expand);
        gc.setThreshold(kThr);
        gc.setRatio(2.0f);
        gc.setRange(-20.0f);
        gc.setKnee(0.0f);
        const float mLow  = 2.0f - 1.0f; // R-1 = 1
        const float range = -20.0f;
        for (float o : offs) {
            const float u   = o;
            float expected  = (u <= 0.0f) ? mLow * u : 0.0f;
            if (expected < range) expected = range; // floor
            CHECK(std::fabs(gc.computeGainDb(kThr + o) - expected) < kHardCornerTolDb);
        }
    }
    SUBCASE("gate") {
        GainComputer gc;
        gc.setMode(GainMode::gate);
        gc.setThreshold(kThr);
        gc.setRange(-30.0f);
        gc.setKnee(0.0f);
        const float range = -30.0f;
        for (float o : offs) {
            const float u        = o;
            const float expected = (u < 0.0f) ? range : 0.0f; // hard step
            CHECK(std::fabs(gc.computeGainDb(kThr + o) - expected) < kHardCornerTolDb);
        }
    }
}

// ---------------------------------------------------------------------------
// T027 — expand (downward expansion, range floor) and gate (FR-007).
// ---------------------------------------------------------------------------
TEST_CASE("GainComputer - expand follows the downward curve and honors the range floor (T027)") {
    GainComputer gc;
    gc.setMode(GainMode::expand);
    gc.setThreshold(-40.0f);
    gc.setRatio(2.0f);
    gc.setRange(-20.0f);
    gc.setKnee(0.0f);

    // 10 dB below threshold at 2:1 downward -> extra 10 dB down: gain -10 dB.
    CHECK(std::fabs(gc.computeGainDb(-50.0f) - (-10.0f)) < kCurveTolDb);
    // Above threshold: unity.
    CHECK(gc.computeGainDb(-30.0f) == doctest::Approx(0.0f));

    // Range floor: far below threshold the curve saturates at -20 dB, never below.
    CHECK(std::fabs(gc.computeGainDb(-80.0f) - (-20.0f)) < kCurveTolDb);
    for (float lvl = -120.0f; lvl <= 0.0f; lvl += 0.5f) {
        const float g = gc.computeGainDb(lvl);
        CHECK(g <= kNonPositiveTolDb);      // <= 0
        CHECK(g >= -20.0f - kCurveTolDb);   // >= range floor
    }
}

TEST_CASE("GainComputer - gate attenuates toward the range floor below threshold (T027)") {
    GainComputer gc;
    gc.setMode(GainMode::gate);
    gc.setThreshold(-40.0f);
    gc.setRange(-30.0f);
    gc.setKnee(0.0f);

    // Below threshold (hard gate): attenuate to the -30 dB floor.
    CHECK(std::fabs(gc.computeGainDb(-50.0f) - (-30.0f)) < kCurveTolDb);
    CHECK(std::fabs(gc.computeGainDb(-90.0f) - (-30.0f)) < kCurveTolDb);
    // Above threshold: unity.
    CHECK(gc.computeGainDb(-30.0f) == doctest::Approx(0.0f));

    for (float lvl = -120.0f; lvl <= 0.0f; lvl += 0.5f) {
        const float g = gc.computeGainDb(lvl);
        CHECK(g <= kNonPositiveTolDb);      // <= 0
        CHECK(g >= -30.0f - kCurveTolDb);   // >= range floor
    }
}

// ---------------------------------------------------------------------------
// NaN/Inf safety (FR-024): non-finite / extreme levels map to a finite gain.
// ---------------------------------------------------------------------------
TEST_CASE("GainComputer - non-finite and extreme levels yield finite gains (FR-024)") {
    GainComputer gc;
    for (GainMode mode : {GainMode::compress, GainMode::limit, GainMode::expand,
                          GainMode::gate}) {
        gc.setMode(mode);
        gc.setThreshold(-20.0f);
        gc.setRatio(4.0f);
        gc.setRange(-20.0f);
        gc.setKnee(6.0f);

        // Non-finite inputs are defensively mapped to unity (0 dB).
        CHECK(gc.computeGainDb(std::numeric_limits<float>::infinity()) == doctest::Approx(0.0f));
        CHECK(gc.computeGainDb(-std::numeric_limits<float>::infinity()) == doctest::Approx(0.0f));
        CHECK(gc.computeGainDb(std::numeric_limits<float>::quiet_NaN()) == doctest::Approx(0.0f));

        // Finite-but-extreme inputs must still produce finite output.
        CHECK(std::isfinite(gc.computeGainDb(1.0e30f)));
        CHECK(std::isfinite(gc.computeGainDb(-1.0e30f)));
        CHECK(std::isfinite(gc.computeGainDb(-1.0e9f)));
    }
}

// analysis-alias-sweep-test.cpp
// T016 -- harmonic-analysis feature, User Story 2: RED test for the
// alias-vs-frequency sweep (contracts/analysis-engine-api.md "alias-sweep.h",
// data-model.md "AliasSweepCurve", research.md Decision 6, spec.md US2 /
// FR-004).
//
// Definition under test (FR-004, research Decision 6):
//   The engine sweeps a tone across frequency; at EACH step it measures
//   inharmonic (folded) energy by reusing the SHIPPED integer-cycle inharmonic
//   measure (acfx::measure::aliasingMeasure, host/analysis/aliasing.h) -- no
//   new spectral machinery for the measure itself -- and returns an
//   inharmonic-energy-vs-frequency curve (AliasSweepCurve: frequencyHz[] +
//   inharmonicEnergy[]).
//
// ANALYTIC REFERENCE -- a KNOWN memoryless cubic nonlinearity y = x^3:
//   sin^3(theta) = (3/4)*sin(theta) - (1/4)*sin(3*theta)   (product-to-sum)
//   so a unit-amplitude tone at f0 through y = x^3 produces EXACTLY two
//   spectral components in the underlying continuous signal:
//     fundamental f0     amplitude 0.75   (always in-band for f0 < Nyquist)
//     3rd harmonic 3*f0  amplitude 0.25   -- IN-BAND (harmonic, subtracted
//                                            cleanly by aliasingMeasure) when
//                                            3*f0 < Nyquist; when 3*f0 exceeds
//                                            Nyquist the sampled sequence
//                                            folds that component back to
//                                            |sampleRate - 3*f0|, a frequency
//                                            that is (for the sweep points
//                                            below) NOT an integer multiple of
//                                            f0 -- i.e. genuinely INHARMONIC.
//   Sinusoid power = amplitude^2 / 2 (aliasingMeasure's convention), so:
//     in-band (no fold):  inharmonicPower ~= 0        (only round-off)
//     folded (3*f0 > Nyquist): inharmonicPower ~= 0.25^2/2 = 0.03125
//   This is the SAME closed-form cubic identity used to reason about product
//   amplitudes in analysis-imd-test.cpp's quadratic reference; here it gives a
//   clean, exact "does it fold or not" toggle as f0 sweeps past Nyquist/3.
//
// STIMULUS / INTEGER-CYCLE WINDOWS (aliasingMeasure's leakage-free
// contract, aliasing.h header comment): host/analysis/alias-sweep.h fixes a
// 48000 Hz sample rate and a 480-sample-per-step window (100 Hz/bin). Every
// frequency swept below is a whole multiple of 100 Hz, so its fundamental --
// and any folded image of its 3rd harmonic -- lands EXACTLY on a DFT bin.
//
// Sweep points (2000, 4400, 6800, 9200, 11600, 14000 Hz) are chosen so:
//   - the first three (2000/4400/6800 Hz) keep 3*f0 < 24000 Hz (Nyquist)   -> no fold
//   - the last three (9200/11600/14000 Hz) push 3*f0 > 24000 Hz            -> fold
//   - none of the folded images (48000 - 3*f0 = 20400/13200/6000) coincide
//     with an integer multiple of their own f0 (verified by hand; avoids the
//     rare-coincidence caveat documented in aliasing.h).
//
// Asserted contract for the NOT-YET-EXISTING host/analysis/alias-sweep.h:
//   - aliasSweep(fn, FrequencyRange{...}) returns a curve whose frequencyHz[]
//     matches the requested linearly-spaced sweep points and whose
//     inharmonicEnergy[] RISES once 3*f0 crosses Nyquist (naive cubic arm).
//   - A LINEAR (identity y = x) arm has no harmonic content at all, so its
//     inharmonicEnergy[] stays near zero across the ENTIRE sweep.
//
// host/analysis/alias-sweep.h does not exist yet at RED time -- this test is
// expected to FAIL TO BUILD until T017 lands it. Do NOT implement
// alias-sweep.h to make this test pass; that is a separate task.

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>

#include "analysis/alias-sweep.h"  // UNDER TEST (does not exist at RED time)
#include "dsp/span.h"

using acfx::analysis::AliasSweepCurve;
using acfx::analysis::FrequencyRange;
using acfx::analysis::aliasSweep;

namespace {

// A ceiling a genuinely-absent inharmonic component sits well under (float32
// round-off on unit-amplitude synthesized buffers is ~1e-6 amplitude / ~1e-12
// power); several orders above that floor yet far below any real folded
// component. Mirrors analysis-imd-test.cpp's kAbsentProductCeiling pattern.
constexpr double kNearZeroCeiling = 1.0e-4;

// Analytic folded-component power for y = x^3 (see file banner): amplitude
// 0.25 -> power 0.25*0.25/2 = 0.03125. 15% relative tolerance absorbs the
// float32 synthesis/measurement round-trip while still failing a wrong scale
// factor or a missed fold.
constexpr double kFoldedPower    = 0.03125;
constexpr double kFoldedRelTol   = 0.15;

// Naive (non-bandlimited) memoryless cubic nonlinearity: y = x^3. Computed
// directly on the sampled sequence -- exactly the kind of naive, non-
// bandlimited nonlinearity whose high harmonics fold once they exceed
// Nyquist (FR-004's "naive" arm).
auto cubic() {
    return [](float x) {
        const double xd = static_cast<double>(x);
        return static_cast<float>(xd * xd * xd);
    };
}

// Linear / band-limited arm: identity pass-through. No harmonic content
// beyond the fundamental at all, so nothing can ever fold.
auto identity() {
    return [](float x) { return x; };
}

// The sweep under test: 2000..14000 Hz, 6 linearly-spaced points, all whole
// multiples of the 100 Hz/bin resolution alias-sweep.h fixes internally (see
// file banner). Points [0..2] stay below the fold threshold (3*f0 < 24000);
// points [3..5] cross it.
constexpr FrequencyRange kSweep{/*startHz=*/2000.0, /*stopHz=*/14000.0, /*numSteps=*/6};

} // namespace

TEST_CASE("aliasSweep: curve frequencyHz matches the requested linearly-spaced sweep (FR-004, US2)") {
    const AliasSweepCurve curve = aliasSweep(identity(), kSweep);

    REQUIRE(curve.frequencyHz.size() == 6);
    REQUIRE(curve.inharmonicEnergy.size() == curve.frequencyHz.size());

    CHECK(curve.frequencyHz[0] == doctest::Approx(2000.0));
    CHECK(curve.frequencyHz[1] == doctest::Approx(4400.0));
    CHECK(curve.frequencyHz[2] == doctest::Approx(6800.0));
    CHECK(curve.frequencyHz[3] == doctest::Approx(9200.0));
    CHECK(curve.frequencyHz[4] == doctest::Approx(11600.0));
    CHECK(curve.frequencyHz[5] == doctest::Approx(14000.0));
}

TEST_CASE("aliasSweep: naive cubic (y=x^3) -- inharmonic energy RISES once the 3rd harmonic "
          "folds past Nyquist (FR-004, US2)") {
    const AliasSweepCurve curve = aliasSweep(cubic(), kSweep);
    REQUIRE(curve.inharmonicEnergy.size() == 6);

    // Low-frequency steps: 3*f0 stays under Nyquist (24 kHz) -> the 3rd
    // harmonic is in-band and cleanly subtracted as harmonic content, leaving
    // ~0 inharmonic energy.
    //   f0=2000 -> 3f0=6000;  f0=4400 -> 3f0=13200;  f0=6800 -> 3f0=20400.
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK_MESSAGE(curve.inharmonicEnergy[i] < kNearZeroCeiling,
                      "low-band step ", i, " freq=", curve.frequencyHz[i],
                      " inharmonic=", curve.inharmonicEnergy[i]);
    }

    // High-frequency steps: 3*f0 exceeds Nyquist and folds back onto a
    // non-harmonic frequency -> inharmonic energy jumps to the analytic
    // folded-component power (~0.03125).
    //   f0=9200 -> 3f0=27600 folds to 20400;  f0=11600 -> 3f0=34800 folds to
    //   13200;  f0=14000 -> 3f0=42000 folds to 6000.
    for (std::size_t i = 3; i < 6; ++i) {
        CHECK_MESSAGE(curve.inharmonicEnergy[i]
                          == doctest::Approx(kFoldedPower).epsilon(kFoldedRelTol),
                      "high-band step ", i, " freq=", curve.frequencyHz[i],
                      " inharmonic=", curve.inharmonicEnergy[i]);
    }

    // Explicit "rises past the fold threshold" comparison (FR-004's own
    // wording): the worst (largest) low-band reading is still far below the
    // best (smallest) high-band reading.
    double maxLowBand = 0.0;
    for (std::size_t i = 0; i < 3; ++i)
        maxLowBand = std::max(maxLowBand, curve.inharmonicEnergy[i]);
    double minHighBand = curve.inharmonicEnergy[3];
    for (std::size_t i = 4; i < 6; ++i)
        minHighBand = std::min(minHighBand, curve.inharmonicEnergy[i]);

    CHECK(minHighBand > 10.0 * maxLowBand);
    CHECK(maxLowBand < kNearZeroCeiling);
}

TEST_CASE("aliasSweep: linear identity pass-through -- inharmonic energy stays near zero "
          "across the ENTIRE sweep (FR-004, US2)") {
    const AliasSweepCurve curve = aliasSweep(identity(), kSweep);
    REQUIRE(curve.inharmonicEnergy.size() == 6);

    for (std::size_t i = 0; i < curve.inharmonicEnergy.size(); ++i) {
        CHECK_MESSAGE(curve.inharmonicEnergy[i] < kNearZeroCeiling,
                      "step ", i, " freq=", curve.frequencyHz[i],
                      " inharmonic=", curve.inharmonicEnergy[i]);
    }
}

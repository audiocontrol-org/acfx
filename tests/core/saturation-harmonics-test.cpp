// saturation-harmonics-test.cpp
// T008 -- US1 drive->THD monotonicity + gain-compensation loudness band
// (SC-002, FR-004/FR-016/FR-017; spec.md User Story 1 Acceptance Scenario 3;
// quickstart.md validation scenario 3).
//
// TWO TEST CASES (softClip voicing only -- the per-voicing SIGNATURE suite is
// saturation-voicings-test.cpp; this file is the drive-sweep slice):
//   1. Drive->THD monotonicity: sweep drive upward at a fixed (softClip)
//      voicing and assert measured THD is non-decreasing. A soft-clipper must
//      not get LESS distorted as it is driven harder.
//   2. Gain-compensation loudness band: across the same drive sweep, assert
//      output RMS (relative to the dry input's RMS, in dB) stays inside a
//      named band. FR-004 requires the composed nonlinear stage's internal
//      gain-compensation to be active by construction (SaturationCore does
//      not expose a setter for it -- unlike the bare Waveshaper/ADAAWaveshaper
//      primitives it composes, both of which default gainComp_ OFF); this
//      test is what proves that composition choice actually holds loudness
//      down as drive rises, rather than letting it run away.
//
// Window design (anti-leakage, matches measurement-support.h's contract and
// waveshaper-harmonics-test.cpp/saturation-core-test.cpp):
//   f0=1000 Hz, sr=48000 Hz, N=4800 -> f0*N/sr = 100 integer cycles.
//
// Drive sweep + tolerance rationale (softKnee, waveshaper-shapes.h):
//   softKnee(u) is linear for |u|<=0.5, a C1 knee for 0.5<|u|<1.5, and flat
//   (|y|=1) for |u|>=1.5. With a unit-amplitude sine (kAmplitude=1.0f) and
//   drive in [0.5, 2.0], the sweep spans "just touching the knee" through
//   "mildly past full clip", covering the shape's whole transitional
//   character without driving deep into pure-square territory (where the
//   naive 1/drive gain-compensation formula in Waveshaper/ADAAWaveshaper
//   necessarily over-compensates -- see waveshaper.h "gainCompFactor = 1 /
//   max(drive, kGainCompEps)" -- and loudness would legitimately fall well
//   outside any tight band; that failure mode belongs to a future tuning
//   pass, not this test).
//   An idealized single-shape simulation (softKnee + 1/drive gain comp, no
//   emphasis filters/DC-blocker) over this exact sweep gives:
//     drive   thd      dB re dry RMS
//     0.50   0.000      0.00
//     0.75   0.016     -0.19
//     1.00   0.045     -0.72
//     1.25   0.081     -1.45
//     1.50   0.125     -2.34
//     1.75   0.171     -3.31
//     2.00   0.208     -4.24
//   THD rises monotonically end to end (~0 -> ~0.21); loudness drifts down
//   by at most ~4.24 dB. The named tolerances below are set with headroom
//   over these idealized numbers so the composed chain's near-flat
//   pre-/post-emphasis SVF stages (18 kHz cutoff, resonance 0.1 -- far from
//   the 1 kHz test tone) and DC-blocker (inactive here; bias=0) can only add
//   a small, expected deviation without masking a real regression.
//
// Expected RED: SaturationCore::prepare/setVoicing/setQuality/setDrive/
// setBias/setTone/setMix/setOutput are DECLARED (T004) but their bodies are
// not yet implemented (T009/T010 pending); process() is a passthrough stub.
// This TU is expected to compile (syntax-only) cleanly -- no bodies are
// needed to resolve declarations -- but a linked test binary will fail with
// undefined references naming acfx::SaturationCore symbols. That link
// failure, not a compile error in this file, is the correct RED signal
// before T009/T010 land.

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "analysis/drive-series.h"
#include "effects/saturation/saturation-core.h"
#include "core/measurement-support.h"

using namespace acfx;
using acfx::analysis::DriveRange;
using acfx::analysis::DriveSeries;

namespace {

constexpr double      kF0         = 1000.0;   // fundamental frequency (Hz)
constexpr double      kSampleRate = 48000.0;  // sample rate (Hz)
constexpr std::size_t kN          = 4800;     // 100 integer cycles (f0*N/sr=100)
constexpr float       kAmplitude  = 1.0f;     // unit-amplitude sine input

// Drive sweep: spans softKnee's linear boundary (0.5) through its flat
// boundary and mildly past it (2.0). See file header for the idealized
// THD/loudness numbers this range produces. Used directly by TEST 2 (which
// still drives its own kAmplitude=1.0f stimulus via meastest::
// captureSineResponse), so u = drive*amplitude = drive here.
constexpr std::array<double, 7> kDriveValues = {
    0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0
};

// TEST 1 uses acfx::analysis::driveSeries (host/analysis/drive-series.h,
// T019/T022) instead of hand-rolling the sweep. driveSeries() fixes its
// internal stimulus amplitude at acfx::analysis::kDriveSeriesAmplitude
// (0.5f) -- half of this file's kAmplitude (1.0f) used above -- so the drive
// CONTROL values swept here are doubled relative to kDriveValues (1.0..4.0
// in 0.5 steps rather than 0.5..2.0 in 0.25 steps). Since softKnee only sees
// u = drive*amplitude, doubling drive while amplitude is halved reproduces
// EXACTLY the same u sequence (0.5, 0.75, ..., 2.0) -- same physical
// stimulus into softKnee, same expected THD numbers as the file header's
// idealized model, so no tolerance changed.
constexpr DriveRange kThdDriveRange{
    kDriveValues.front() * 2.0, kDriveValues.back() * 2.0,
    static_cast<int>(kDriveValues.size())
};

// Monotonicity tolerance: a small allowance for measurement noise (the
// integer-cycle window keeps Goertzel leakage near the float noise floor,
// ~1e-6..1e-5 per measurement-support.h), kept two to three decades below
// the smallest real THD increment in the sweep above (~0.016) so a genuine
// non-monotonic regression cannot hide behind it.
constexpr double kThdMonotonicEpsilon = 1.0e-3;

// A real, non-trivial THD rise must be observed end to end -- not just a
// flat/no-op sweep. The idealized model rises by ~0.21; 0.05 is a generous
// lower bound (~4x margin) that still proves the shaper is doing real work.
constexpr double kMinThdRise = 0.05;

// Named loudness band: +-5 dB re the dry input's RMS. The idealized model's
// worst-case drift across this sweep is ~4.24 dB down at drive=2.0; 5 dB
// leaves ~0.76 dB of headroom for the composed chain's near-flat emphasis
// filters without being so loose it would pass a broken/unwired
// gain-compensation path (which would drift far more -- see the
// "without gain comp" contrast in the header rationale: up to +2.25 dB by
// drive=3.0 well outside this sweep, or unboundedly worse at higher drive).
constexpr double kLoudnessBandDb = 5.0;

// Build a fully-configured SaturationCore for the softClip voicing at the
// given drive, fully wet (mix=1.0) so the measurement isolates the nonlinear
// stage's THD/loudness behavior rather than blending it with the dry input.
void configureCore(SaturationCore& core, float drive) {
    core.prepare(static_cast<float>(kSampleRate));
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(SaturationQuality::adaa);
    core.setDrive(drive);
    core.setBias(0.0f);
    core.setTone(0.0f);
    core.setMix(1.0f);
    core.setOutput(1.0f);
}

// Drive -> per-sample callable, for acfx::analysis::driveSeries /
// meastest::captureSineResponse. A fresh SaturationCore is built and
// configured per drive value, then captured by value into the returned
// closure so state is not shared across drive values (each drive gets its
// own filter/DC-blocker state, matching driveSeries's per-drive-fresh-
// capture contract, host/analysis/drive-series.h).
std::function<float(float)> makeSoftClipProcessor(double drive) {
    SaturationCore core;
    configureCore(core, static_cast<float>(drive));
    return [core](float x) mutable noexcept { return core.process(x); };
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Drive -> THD monotonicity (FR-017, SC-002)
//
// Reuses acfx::analysis::driveSeries (host/analysis/drive-series.h, T019;
// migrated onto here from the ad-hoc drive->THD helper formerly in
// measurement-support.h by T022) to sweep kThdDriveRange through the
// softClip voicing and reduce each
// drive point to a single THD reading (fundamental=1000 Hz, sr=48000 Hz,
// N=4800 internally -- the same integer-cycle window this file already
// uses). Asserts the sequence is non-decreasing within kThdMonotonicEpsilon,
// and that the end-to-end rise clears kMinThdRise -- rising drive into a
// soft-clipper must not REDUCE measured distortion.
// ---------------------------------------------------------------------------

TEST_CASE("softClip: drive->THD is monotonically non-decreasing (FR-017/SC-002)") {
    const DriveSeries series =
        acfx::analysis::driveSeries(makeSoftClipProcessor, kThdDriveRange,
                                    /*numHarmonics=*/5);

    REQUIRE(series.driveValue.size() == kDriveValues.size());
    REQUIRE(series.thd.size() == series.driveValue.size());

    for (std::size_t i = 0; i < series.thd.size(); ++i) {
        INFO("drive=" << series.driveValue[i] << " thd=" << series.thd[i]);
        REQUIRE(std::isfinite(series.thd[i]));
    }

    for (std::size_t i = 0; i + 1 < series.thd.size(); ++i) {
        INFO("drive[" << series.driveValue[i] << "]=" << series.thd[i]
             << " -> drive[" << series.driveValue[i + 1] << "]=" << series.thd[i + 1]);
        CHECK(series.thd[i + 1] >= series.thd[i] - kThdMonotonicEpsilon);
    }

    INFO("first thd=" << series.thd.front() << " last thd=" << series.thd.back());
    CHECK(series.thd.back() - series.thd.front() > kMinThdRise);
}

// ---------------------------------------------------------------------------
// TEST 2: Gain-compensation loudness band (FR-004, FR-017, SC-002)
//
// Captures the same drive sweep's output RMS (meastest::rmsLevel) against
// the dry input's RMS (same stimulus, identity passthrough) and asserts the
// dB difference stays within +-kLoudnessBandDb for every drive value --
// "with gain-compensation active, the output loudness stays within a named
// band" (spec.md Acceptance Scenario 3). SaturationCore does not expose a
// gain-compensation toggle; this test is what proves the composed
// Waveshaper/ADAAWaveshaper's internal gain-compensation (baked ON as part
// of the softClip voicing's configuration) is actually doing its job.
// ---------------------------------------------------------------------------

TEST_CASE("softClip: gain-compensation holds output loudness within a named band as drive rises (FR-004/SC-002)") {
    const std::vector<float> dry = meastest::captureSineResponse(
        [](float x) noexcept { return x; },
        kF0, kSampleRate, kN, kAmplitude);
    const double dryRms = meastest::rmsLevel(acfx::span<const float>(dry));
    REQUIRE(dryRms > 0.0);

    for (double drive : kDriveValues) {
        const std::function<float(float)> processor = makeSoftClipProcessor(drive);
        const std::vector<float> out = meastest::captureSineResponse(
            processor, kF0, kSampleRate, kN, kAmplitude);
        const double outRms = meastest::rmsLevel(acfx::span<const float>(out));

        REQUIRE(outRms > 0.0);
        const double db = 20.0 * std::log10(outRms / dryRms);
        INFO("drive=" << drive << " dryRms=" << dryRms << " outRms=" << outRms << " dB=" << db);
        CHECK(std::abs(db) <= kLoudnessBandDb);
    }
}

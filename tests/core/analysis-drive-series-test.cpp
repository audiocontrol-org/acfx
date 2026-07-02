// analysis-drive-series-test.cpp
// T018 -- harmonic-analysis feature, User Story 3: RED test for drive->THD
// and drive->per-harmonic series (contracts/analysis-engine-api.md
// "drive-series.h", data-model.md "DriveSeries", spec.md US3 / FR-005).
//
// Definition under test (FR-005, spec.md US3):
//   Sweep an effect's DRIVE (or amplitude/character control) across a
//   caller-specified range. At EACH drive point, drive a pure tone through
//   the drive-parameterized nonlinearity and reduce the captured output to a
//   THD figure and a per-harmonic magnitude curve, collected as one
//   first-class series across the sweep.
//
// ANALYTIC REFERENCE -- a KNOWN drive-parameterized cubic nonlinearity:
//   y = x + drive*x^3,   x = A*sin(theta)
//   sin^3(theta) = (3*sin(theta) - sin(3*theta)) / 4, so:
//     y = (A + 0.75*drive*A^3) * sin(theta)  -  (0.25*drive*A^3) * sin(3*theta)
//   i.e. a PURE cubic produces energy ONLY at the fundamental and the 3rd
//   harmonic (no 2nd/4th/5th):
//     V1(drive) = A + 0.75*A^3*drive            (fundamental amplitude)
//     V3(drive) = 0.25*A^3*drive                (3rd-harmonic amplitude)
//     THD(drive) = V3(drive) / V1(drive)         (since no other harmonic
//                                                  carries energy)
//   THD(drive) = (0.25*A^3*drive) / (A + 0.75*A^3*drive) is STRICTLY
//   INCREASING in drive for drive >= 0 (positive numerator slope, slower-
//   growing positive denominator) and THD(0) = 0 -- exactly the monotonic,
//   zero-at-minimal-drive model FR-005 / US3's Independent Test calls for.
//
// Asserted contract for the NOT-YET-EXISTING host/analysis/drive-series.h:
//   - driveSeries(...) returns one THD figure and one per-harmonic magnitude
//     per swept drive point (DriveSeries.driveValue / .thd / .harmonic).
//   - drive->THD is monotonically NON-DECREASING across increasing drive,
//     matching the analytic ratio above within a named tolerance at every
//     sampled drive point, and THD(0) ~ 0.
//   - drive->3rd-harmonic magnitude matches the analytic V3(drive) within a
//     named tolerance at every sampled drive point.
//   - the 2nd/4th/5th harmonic curves stay near the measurement floor at
//     every drive point (a pure cubic carries no energy there) -- proving the
//     per-harmonic curves are attributed to the correct bins, not smeared.
//
// host/analysis/drive-series.h does not exist yet at RED time -- this test is
// expected to FAIL TO BUILD until T019 lands it. Do NOT implement
// drive-series.h to make this test pass; that is a separate task.
//
// Integer-cycle window: the engine builds the stimulus at sampleRate = 48000,
// N = 4800 (10 Hz/bin) internally (mirrors imd.h / analysis-thdn-test.cpp /
// analysis-spectrum-test.cpp's fixed 1000 Hz fundamental), so the fundamental
// (1000 Hz) and every harmonic up to Nyquist land EXACTLY on their own
// Goertzel bin -- leakage-free.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "analysis/drive-series.h"  // UNDER TEST (does not exist at RED time)
#include "dsp/span.h"

using acfx::analysis::DriveRange;
using acfx::analysis::DriveSeries;
using acfx::analysis::driveSeries;

namespace {

// The stimulus/output are float32 buffers driven through an integer-cycle
// bin; an in-band Goertzel readout is analytically exact up to that float
// round-trip (~1e-6 relative). 2% mirrors analysis-imd-test.cpp's
// kProductRelTol / analysis-spectrum-test.cpp's kMagnitudeTolerance --
// generous, yet it fails a wrong scale factor or a mislocated harmonic bin.
constexpr double kRelTol = 0.02;

// A harmonic bin that should carry NO energy (2nd/4th/5th for a pure cubic,
// or the 3rd at drive=0) still shows the float round-off floor of the
// synthesized buffers. This ceiling sits several orders above that floor yet
// fails any real signal. Mirrors analysis-spectrum-test.cpp's
// kAbsentHarmonicMagnitudeCeiling.
constexpr double kAbsentCeiling = 1.0e-3;

// The fixed unit-amplitude analytic model (A, kept in sync with
// drive-series.h's internal kDriveSeriesAmplitude; the test recomputes its
// own analytic reference from this value rather than hard-coding derived
// numbers, so a future amplitude change in the header only requires updating
// this one constant here too).
constexpr double kA = 0.5;

double analyticFundamental(double drive) {
    return kA + 0.75 * kA * kA * kA * drive;
}

double analyticThirdHarmonic(double drive) {
    return 0.25 * kA * kA * kA * drive;
}

double analyticThd(double drive) {
    return analyticThirdHarmonic(drive) / analyticFundamental(drive);
}

// Drive-parameterized cubic nonlinearity factory: y = x + drive*x^3.
// makeProcessor(drive) returns a fresh per-sample callable float(float),
// matching the shape of the ad-hoc drive->THD helper this engine function
// generalizes (formerly tests/core/measurement-support.h; removed by T022).
auto cubic(double drive) {
    return [drive](float x) {
        const double xd = static_cast<double>(x);
        return static_cast<float>(xd + drive * xd * xd * xd);
    };
}

} // namespace

TEST_CASE("driveSeries: drive->THD is monotonically non-decreasing, ~0 at minimal drive (FR-005, US3)") {
    const DriveRange range{/*startDrive=*/0.0, /*stopDrive=*/4.0, /*numSteps=*/5};
    const DriveSeries series = driveSeries(cubic, range, /*numHarmonics=*/5);

    REQUIRE(series.driveValue.size() == 5);
    REQUIRE(series.thd.size() == series.driveValue.size());

    // drive = 0 -> a linear pass-through -> THD ~ 0 (analytic THD(0) == 0 exactly).
    CHECK(series.thd.front() < kAbsentCeiling);

    // Monotonically non-decreasing across increasing drive.
    for (std::size_t i = 1; i < series.thd.size(); ++i) {
        CHECK(series.thd[i] >= series.thd[i - 1]);
    }

    // Matches the analytic THD(drive) ratio within tolerance at every sampled
    // drive point (skip drive=0, an exact-zero comparison handled above).
    for (std::size_t i = 1; i < series.driveValue.size(); ++i) {
        const double drive = series.driveValue[i];
        CHECK(series.thd[i] == doctest::Approx(analyticThd(drive)).epsilon(kRelTol));
    }
}

TEST_CASE("driveSeries: drive->3rd-harmonic curve matches the analytic per-drive amplitude (FR-005, US3)") {
    const DriveRange range{/*startDrive=*/0.0, /*stopDrive=*/4.0, /*numSteps=*/5};
    const DriveSeries series = driveSeries(cubic, range, /*numHarmonics=*/5);

    REQUIRE(series.harmonic.size() == 5);          // one curve per requested harmonic (k=1..5)
    REQUIRE(series.harmonic[2].size() == series.driveValue.size());  // index 2 == harmonic k=3

    // drive = 0 -> no 3rd-harmonic energy.
    CHECK(series.harmonic[2].front() < kAbsentCeiling);

    // Grows with drive, matching the analytic V3(drive) = 0.25*A^3*drive at
    // every sampled drive point.
    for (std::size_t i = 1; i < series.driveValue.size(); ++i) {
        const double drive = series.driveValue[i];
        CHECK(series.harmonic[2][i] ==
              doctest::Approx(analyticThirdHarmonic(drive)).epsilon(kRelTol));
    }

    // Monotonically increasing across drive (a pure cubic's 3rd harmonic
    // scales linearly with drive).
    for (std::size_t i = 1; i < series.harmonic[2].size(); ++i) {
        CHECK(series.harmonic[2][i] > series.harmonic[2][i - 1]);
    }
}

TEST_CASE("driveSeries: 2nd/4th/5th-harmonic curves stay near the measurement floor (attribution, no smearing)") {
    const DriveRange range{/*startDrive=*/0.0, /*stopDrive=*/4.0, /*numSteps=*/5};
    const DriveSeries series = driveSeries(cubic, range, /*numHarmonics=*/5);

    REQUIRE(series.harmonic.size() == 5);

    // Indices 1, 3, 4 == harmonics k=2, k=4, k=5 -- a pure cubic carries no
    // energy there at any drive point in range.
    for (const std::size_t idx : {std::size_t{1}, std::size_t{3}, std::size_t{4}}) {
        REQUIRE(series.harmonic[idx].size() == series.driveValue.size());
        for (const double v : series.harmonic[idx]) {
            CHECK(v < kAbsentCeiling);
        }
    }
}

TEST_CASE("driveSeries: linear system (drive=0 only) -> THD and every non-fundamental harmonic ~0 (no distortion)") {
    const DriveRange range{/*startDrive=*/0.0, /*stopDrive=*/0.0, /*numSteps=*/1};
    const DriveSeries series = driveSeries(cubic, range, /*numHarmonics=*/5);

    REQUIRE(series.driveValue.size() == 1);
    REQUIRE(series.thd.size() == 1);
    CHECK(series.thd[0] < kAbsentCeiling);

    REQUIRE(series.harmonic.size() == 5);
    // Index 0 == the FUNDAMENTAL (k=1): at drive=0 the cubic collapses to a
    // linear pass-through, so the fundamental naturally carries the full
    // stimulus amplitude (kA, ~0.5) -- it is NOT expected to be near zero.
    // Only the harmonics (k=2..5, indices 1..4) carry no energy for a linear
    // system; those are the ones this "no distortion" case asserts on.
    REQUIRE(series.harmonic[0].size() == 1);
    CHECK(series.harmonic[0][0] == doctest::Approx(kA).epsilon(kRelTol));
    for (std::size_t idx = 1; idx < series.harmonic.size(); ++idx) {
        REQUIRE(series.harmonic[idx].size() == 1);
        CHECK(series.harmonic[idx][0] < kAbsentCeiling);
    }
}

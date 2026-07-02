#pragma once

// host/analysis/drive-series.h
//
// Drive->THD and drive->per-harmonic curves (contracts/analysis-engine-api.md
// "drive-series.h", data-model.md "DriveSeries", spec.md US3 / FR-005;
// harmonic-analysis T019, GREEN for T018).
//
// Namespace: acfx::analysis. Host-side / offline ONLY -- may allocate. NEVER
// reachable from portable core/ (Constitution IV); no audio-thread use.
//
// Definition (FR-005, spec.md US3):
//   Sweep a nonlinearity's DRIVE (or amplitude/character control) across a
//   caller-specified range. At EACH drive point, drive a pure tone through a
//   FRESH instance of the nonlinearity -- built per drive point via a
//   caller-supplied FACTORY, `makeProcessor(drive) -> per-sample callable
//   float(float)` -- and reduce the captured output to a THD figure and a
//   per-harmonic magnitude curve, collected as one first-class series across
//   the sweep. This generalizes the ad-hoc `meastest::driveThdSeries` helper
//   open-coded in the saturation lab (tests/core/measurement-support.h) into
//   a reusable engine function any effect can request (US3's stated goal).
//
// Method:
//   The stimulus is built at a FIXED, integer-cycle configuration
//   (kDriveSeriesSampleRate = 48000 Hz, kDriveSeriesFundamentalHz = 1000 Hz,
//   kDriveSeriesNumSamples = 4800 -> 10 Hz/bin, matching the fundamental/rate
//   convention analysis-spectrum-test.cpp, analysis-thdn-test.cpp and imd.h
//   already use in this tree) so the fundamental and every harmonic up to
//   Nyquist land EXACTLY on a Goertzel bin. At each drive point, a SINGLE
//   call to the shipped harmonicSpectrum() (spectrum.h) supplies every
//   per-harmonic magnitude for that point -- the harmonic MEASUREMENT itself
//   is fully reused, never re-derived, and one spectral pass serves both the
//   THD figure and the harmonic-curve output (no redundant second sweep).
//
// Deviation from tests/support/measurement/metrics.h's thd(): drive-series.h
//   lives in host/analysis/, and host/analysis/ MUST NOT depend on
//   tests/support/ (research.md Decision 1's one-way dependency: tests/support
//   -> host/analysis, never the reverse -- see spectrum.h's identical note;
//   acfx_analysis's own CMake target exposes only host/ on its include path,
//   so tests/support/measurement/metrics.h is not even reachable from here).
//   thd()'s formula -- sqrt(sum of in-band non-fundamental harmonic
//   magnitudes squared) / fundamental magnitude, NaN with no measurable
//   fundamental -- is mirrored EXACTLY but re-expressed as a local reduction
//   (detail::thdFromSpectrum) over the harmonicSpectrum() this function
//   already computed. This is the same relationship thdn.h documents against
//   thd() (see thdn.h's own banner): the harmonic ARITHMETIC/measurement is
//   fully reused via harmonicSpectrum(); only the thin ratio formula is
//   re-expressed locally because the upstream free function itself cannot be
//   #included across the layer boundary.
//
// Deviation from contracts/analysis-engine-api.md's sketch signature
//   `DriveSeries driveSeries(EffectFactory fx, DriveRange drive, int
//   numHarmonics)`: `EffectFactory` is realized here as a caller-supplied
//   FACTORY callable `makeProcessor(drive) -> (float(float))`, matching
//   meastest::driveThdSeries's established shape (the ad-hoc helper this task
//   generalizes) rather than the `Effect` contract directly. imd.h /
//   alias-sweep.h's Effect overloads take an ALREADY-CONSTRUCTED `Effect&`
//   and reset() it per sweep step; there is no established "construct a new
//   Effect instance at drive d from a bare factory" seam elsewhere in this
//   tree to mirror, so only the callable-factory overload ships here. An
//   Effect-contract front door is left for a follow-up task if a caller needs
//   one (US4's consolidation pass is the natural place to add it once a real
//   lab call site needs it).

#include <cmath>       // std::isnan, std::sqrt
#include <cstddef>     // std::size_t
#include <limits>      // std::numeric_limits
#include <stdexcept>   // std::invalid_argument
#include <string>      // std::to_string
#include <vector>      // std::vector (offline scratch; NOT audio path)

#include "analysis/analyzers.h"  // acfx::measure::captureCallable
#include "analysis/spectrum.h"   // harmonicSpectrum, HarmonicSpectrum (REUSED, not re-derived)
#include "analysis/stimulus.h"   // acfx::measure::SineGenerator
#include "dsp/span.h"

namespace acfx::analysis {

// Swept-drive description: linearly spaced from startDrive to stopDrive,
// numSteps points inclusive of both endpoints (mirrors alias-sweep.h's
// FrequencyRange). numSteps <= 0 yields an empty series; numSteps == 1
// measures only startDrive.
struct DriveRange {
    double startDrive;
    double stopDrive;
    int    numSteps;
};

// Drive-series result (data-model.md "DriveSeries").
struct DriveSeries {
    std::vector<double> driveValue;  // swept drive values, in sweep order

    // THD per drive point (data-model.md "thd[]"). NaN when the fundamental
    // is unmeasurable at that drive point (FR-008 convention, mirrored from
    // thd()) -- never a fabricated 0.0.
    std::vector<double> thd;

    // Per-harmonic amplitude curves across drive (data-model.md
    // "harmonic[k][]"). harmonic.size() == numHarmonics; harmonic[k-1][i]
    // holds harmonic k's magnitude at driveValue[i] -- 0-based index = k-1,
    // exactly mirroring HarmonicSpectrum::magnitude[i]'s own convention
    // (spectrum.h). Out-of-band harmonics (>= Nyquist) are NaN, matching
    // harmonicSpectrum()'s own "not-measured" sentinel (FR-008).
    std::vector<std::vector<double>> harmonic;
};

// Fixed internal per-drive-point stimulus configuration. 48000 Hz over 4800
// samples = 10 Hz/bin (matches analysis-spectrum-test.cpp / analysis-thdn-
// test.cpp / imd.h's convention), so the 1000 Hz fundamental and every
// harmonic up to Nyquist land exactly on a Goertzel bin (leakage-free).
inline constexpr double      kDriveSeriesSampleRate    = 48000.0;
inline constexpr double      kDriveSeriesFundamentalHz = 1000.0;
inline constexpr std::size_t kDriveSeriesNumSamples    = 4800;

// Fixed internal stimulus amplitude. Exposed (not just a magic number
// in-body) so a caller reasoning about the harmonic-curve scale, or a test
// deriving its own analytic reference, has a single named source of truth.
inline constexpr float kDriveSeriesAmplitude = 0.5f;

// Amplitude below which the fundamental is considered ABSENT (unmeasurable).
// Mirrors thd()'s kEpsilon / thdn.h's kThdnFundamentalFloor exactly (same
// rationale: a Goertzel amplitude under this floor is float round-off, not a
// real tone).
inline constexpr double kDriveSeriesFundamentalFloor = 1.0e-12;

namespace detail {

// Linearly-spaced drive points, startDrive..stopDrive inclusive, numSteps
// points (identical shape to alias-sweep.h's detail::sweepFrequencies).
inline std::vector<double> driveValues(const DriveRange& range) {
    std::vector<double> values;
    if (range.numSteps <= 0)
        return values;
    values.reserve(static_cast<std::size_t>(range.numSteps));
    if (range.numSteps == 1) {
        values.push_back(range.startDrive);
        return values;
    }
    const double step = (range.stopDrive - range.startDrive)
                       / static_cast<double>(range.numSteps - 1);
    for (int i = 0; i < range.numSteps; ++i)
        values.push_back(range.startDrive + step * static_cast<double>(i));
    return values;
}

// THD reduced from an already-computed HarmonicSpectrum: sqrt(sum V_k^2,
// k=2..numHarmonics, over every IN-BAND (non-NaN) harmonic) / V1. Mirrors
// tests/support/measurement/metrics.h's thd() formula exactly (see file
// banner "Deviation" note) -- re-expressed as a local reduction over spectrum
// values already measured by the shipped harmonicSpectrum(), rather than a
// second, redundant Goertzel sweep. V1 below kDriveSeriesFundamentalFloor ->
// NaN (no fabricated 0.0, FR-008).
inline double thdFromSpectrum(const HarmonicSpectrum& spectrum) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    const double v1 = spectrum.at(1).magnitude;
    if (!(v1 > kDriveSeriesFundamentalFloor))
        return nan;

    double sumSq = 0.0;
    for (int k = 2; k <= spectrum.numHarmonics; ++k) {
        const double vk = spectrum.at(k).magnitude;
        if (std::isnan(vk))
            continue;  // out-of-band harmonic: unmeasured, not zero (FR-008)
        sumSq += vk * vk;
    }
    return std::sqrt(sumSq) / v1;
}

} // namespace detail

// driveSeries(makeProcessor, drive, numHarmonics): sweep drive across
// `makeProcessor(drive) -> per-sample callable float(float)`, measuring a
// THD figure and a per-harmonic magnitude curve at each drive point
// (data-model.md "DriveSeries"). Generalizes the ad-hoc
// meastest::driveThdSeries helper (tests/core/measurement-support.h) into a
// reusable, effect-agnostic engine function (FR-005/FR-006).
//
// `numHarmonics` MUST be >= 1 (mirrors harmonicSpectrum()'s own guard, FR-001
// consumer); a non-positive count is rejected with a descriptive error, never
// a silently empty series.
template <class MakeProcessor>
inline DriveSeries driveSeries(MakeProcessor&& makeProcessor,
                               const DriveRange& drive,
                               int numHarmonics) {
    if (numHarmonics < 1) {
        throw std::invalid_argument(
            "acfx::analysis::driveSeries: numHarmonics must be >= 1; got " +
            std::to_string(numHarmonics));
    }

    DriveSeries series;
    series.driveValue = detail::driveValues(drive);
    series.thd.reserve(series.driveValue.size());
    series.harmonic.assign(static_cast<std::size_t>(numHarmonics),
                           std::vector<double>());
    for (auto& curve : series.harmonic)
        curve.reserve(series.driveValue.size());

    std::vector<float> stimulus(kDriveSeriesNumSamples, 0.0f);
    std::vector<float> out(kDriveSeriesNumSamples, 0.0f);

    for (const double d : series.driveValue) {
        acfx::measure::SineGenerator{kDriveSeriesFundamentalHz, kDriveSeriesSampleRate,
                                     kDriveSeriesAmplitude, 0.0}
            .fill(acfx::span<float>{stimulus});

        auto fn = makeProcessor(d);
        acfx::measure::captureCallable(fn,
                                       acfx::span<const float>(stimulus),
                                       acfx::span<float>{out});

        const HarmonicSpectrum spectrum = harmonicSpectrum(
            acfx::span<const float>(out), kDriveSeriesFundamentalHz,
            kDriveSeriesSampleRate, numHarmonics);

        series.thd.push_back(detail::thdFromSpectrum(spectrum));

        for (int k = 1; k <= numHarmonics; ++k) {
            series.harmonic[static_cast<std::size_t>(k - 1)].push_back(
                spectrum.at(k).magnitude);
        }
    }
    return series;
}

} // namespace acfx::analysis

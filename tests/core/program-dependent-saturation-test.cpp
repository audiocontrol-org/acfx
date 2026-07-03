// program-dependent-saturation-test.cpp
// T012 (US1) + T021 (US5) + T025 (US7) -- the dynamic saturator's core
// program-dependent behavior, measured through the shipped measurement +
// harmonic-analysis infrastructure (research.md Decision 8): drive the
// ProgramDependentSaturationCore directly with deterministic stimuli, reduce
// each to a harmonic reading via acfx::measure::thd (support/measurement/
// metrics.h) or read the modulation driver directly via
// ProgramDependentSaturationCore::lastNorm().
//
// WHY DRIVE THE CORE, NOT THE EFFECT: the core exposes the modulation driver
// (lastNorm(), the normalized-window envelope that feeds every DynamicsModulator)
// and takes native-unit setters (setStaticDrive in dB, setDepth/setCurve per
// target), so the level->THD and step-response measurements need no denormalize
// round-trip. The Effect-contract wrapper is covered by the sibling
// program-dependent-saturation-effect-test.cpp (T027).
//
// MEASUREMENT WINDOW (anti-leakage, matching measurement-support.h's contract
// and saturation-harmonics-test.cpp): f0=1000 Hz, sr=48000 Hz, analysis window
// N=4800 -> f0*N/sr = 100 integer cycles. The warmup prefix is also an integer
// number of 1 kHz periods (48 samples) so the captured tail is phase-clean.
//
// ROBUSTNESS POLICY (svf-reference named-tolerance pattern, spec Assumptions):
// every assertion is a TREND, ORDERING, or RATIO with a named tolerance --
// never a brittle absolute magic number. The softClip law's exact THD values
// are not predicted; only the mechanism-guaranteed relationships are asserted
// (drive->THD is monotonic, per saturation-harmonics-test.cpp, so more drive at
// a given input level is more THD; a bigger input level is more THD; the two
// compound under a positive drive depth).

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/saturation/saturation-core.h"        // SaturationVoicing / SaturationQuality
#include "primitives/dynamics/dynamics-modulator.h"     // ModCurve
#include "primitives/dynamics/envelope-follower.h"       // DetectMode / Ballistics

#include "core/measurement-support.h"                    // acfx::measure::SineGenerator, thd (re-exported)
#include "support/measurement/metrics.h"                 // acfx::measure::thd

using namespace acfx;

namespace {

constexpr double      kF0            = 1000.0;   // fundamental (Hz)
constexpr double      kSampleRate    = 48000.0;  // sample rate (Hz)
constexpr std::size_t kAnalysisN     = 4800;     // 100 integer cycles (f0*N/sr = 100)
constexpr std::size_t kWarmupSamples = 14400;    // 300 ms; 300 integer 1 kHz periods -> phase-clean tail
constexpr int         kNumHarmonics  = 8;        // harmonics 2..8 = 2k..8k Hz, all well below Nyquist

// ---------------------------------------------------------------------------
// Named tolerances (svf-reference pattern; no bare magic numbers in asserts).
// ---------------------------------------------------------------------------

// Monotonicity slack for a THD-vs-level sweep: the integer-cycle window keeps
// Goertzel leakage near the float noise floor, so a small allowance absorbs
// measurement noise while staying far below the real per-step THD increments
// the positive-depth sweep produces (tenths). A genuine non-monotonic
// regression cannot hide behind it.
constexpr double kThdMonotonicEps = 2.0e-3;

// A positive drive depth must produce a REAL, non-trivial end-to-end THD rise
// across the level sweep -- not a flat/no-op sweep. Generous lower bound.
constexpr double kMinDynamicThdRise = 0.05;

// The static (depth=0) sweep's end-to-end THD rise must be a small fraction of
// the dynamic sweep's: the dynamic sweep compounds rising input level with a
// rising, level-tracked drive, so its slope is far steeper. This is the
// signature program-dependent contrast (SC-001).
constexpr double kStaticFlatnessFrac = 0.5;

// Minimum THD separation between two response curves at the same depth/level,
// so "distinguishable" is asserted with a real margin (SC-004).
constexpr double kCurveSeparation = 0.02;

// A negative drive depth must produce a REAL downward THD trajectory (not just
// flat-within-noise) across the level sweep -- the softening/ducking signature.
constexpr double kMinNegThdDrop = 0.003;

// Step-response: the one-pole envelope crosses 1 - 1/e of a step in tau.
constexpr double kAttackCrossFrac = 0.63212055882855767;
// Attack-time tolerance (fraction of tau). Looser than the bare
// EnvelopeFollower ballistics suite's +-10% because the reading here is the
// normalized-window envelope (a linear remap of the dB envelope) taken through
// the whole composition, but still far tighter than the peak-vs-rms ordering
// margin so the ~63%-at-tau claim stays meaningful.
constexpr double kAttackTolFrac = 0.30;

// ---------------------------------------------------------------------------
// Configure a ProgramDependentSaturationCore for a drive-depth THD sweep:
// softClip voicing, ADAA quality (suppresses aliasing so the harmonic bins are
// clean), rms detection (a stable steady-state envelope for a sustained tone --
// unlike peak, whose per-sample ripple would modulate drive within the window),
// feedforward topology, fully wet, only the DRIVE target modulated. The
// normalization reference window is a parameter: the drive-sweep tests use the
// default -60..0 dBFS (the effect's shipped default), while the negative-depth
// softening test narrows it (see TEST 3's rationale).
// ---------------------------------------------------------------------------
void configureDriveThdCore(ProgramDependentSaturationCore& core,
                           float staticDriveDb, float driveDepth, ModCurve driveCurve,
                           float refLoDb = -60.0f, float refHiDb = 0.0f) {
    core.prepare(static_cast<float>(kSampleRate));
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(SaturationQuality::adaa);
    core.setDetectorMode(DetectMode::rms);
    core.setBallistics(Ballistics::branching);
    core.setDetection(Detection::feedForward);
    core.setAttack(0.010f);
    core.setRelease(0.100f);
    core.setRefWindow(refLoDb, refHiDb);
    core.setStaticDrive(staticDriveDb); // dB (the core converts to gain)
    core.setStaticBias(0.0f);
    core.setStaticTone(0.0f);
    core.setStaticMix(1.0f);
    core.setOutput(1.0f);
    core.setDepth(ModTarget::drive, driveDepth);
    core.setCurve(ModTarget::drive, driveCurve);
    // bias/tone/mix depths remain 0 (defaults) -- only drive tracks level here.
}

// Drive a sustained sine of `amplitude` through the (stateful) core, let the
// detector settle over the warmup prefix, and return the steady-state THD of
// the phase-clean analysis tail. Reuses acfx::measure::SineGenerator (stimulus)
// and acfx::measure::thd (single-bin Goertzel THD) -- no bespoke spectral code.
double steadyStateThd(ProgramDependentSaturationCore& core, float amplitude) {
    const std::size_t total = kWarmupSamples + kAnalysisN;
    std::vector<float> in(total, 0.0f);
    std::vector<float> out(total, 0.0f);
    acfx::measure::SineGenerator{kF0, kSampleRate, amplitude, 0.0}
        .fill(acfx::span<float>(in));
    for (std::size_t i = 0; i < total; ++i)
        out[i] = core.process(in[i], in[i]); // feedforward: key == main sample
    const acfx::span<const float> tail{out.data() + kWarmupSamples, kAnalysisN};
    return acfx::measure::thd(tail, kF0, kSampleRate, kNumHarmonics);
}

// US1 level sweep: quiet -> loud in ~6 dB steps up to -6 dBFS. At base drive
// 0 dB the softClip voicing is LINEAR below its knee, so a depth=0 (static)
// sweep sits at ~0 THD across the whole range (the genuinely flat static
// contrast), while a positive drive depth pushes the loud end deep into
// saturation.
constexpr std::array<float, 5> kLevelSweepUS1 = {0.02f, 0.04f, 0.08f, 0.16f, 0.32f};

// US5 level sweep: all points sit ABOVE the narrowed -30 dBFS reference floor
// TEST 3 uses (so norm is unclamped and the modulation is live at every point),
// spanning up to ~-7.5 dBFS.
constexpr std::array<float, 5> kLevelSweepUS5 = {0.06f, 0.10f, 0.16f, 0.26f, 0.42f};

// ---------------------------------------------------------------------------
// Step-response probe: read the modulation driver directly.
//
// Feed a constant-amplitude QUIET segment (let the detector settle), then a
// constant-amplitude LOUD segment; record lastNorm() -- the normalized-window
// envelope that scales every DynamicsModulator -- for every loud-segment
// sample. A constant-|x| stimulus presents a CLEAN level step to the detector
// (no per-sample rectified ripple), exactly as the EnvelopeFollower ballistics
// suite drives a constant x=1.0 step. The audio output is irrelevant here; the
// modulation trajectory is the measurand (US1 attack scenario; US7).
// ---------------------------------------------------------------------------
struct StepTrace {
    double              normQuiet; // settled normalized envelope before the step
    double              normLoud;  // settled normalized envelope after the step
    std::vector<double> norm;      // per-sample lastNorm() over the loud segment
};

StepTrace driveLevelStep(ProgramDependentSaturationCore& core,
                         float quietAmp, float loudAmp,
                         std::size_t quietSamples, std::size_t loudSamples) {
    for (std::size_t i = 0; i < quietSamples; ++i)
        core.process(quietAmp, quietAmp);
    const double normQuiet = static_cast<double>(core.lastNorm());

    std::vector<double> norm(loudSamples, 0.0);
    for (std::size_t i = 0; i < loudSamples; ++i) {
        core.process(loudAmp, loudAmp);
        norm[i] = static_cast<double>(core.lastNorm());
    }
    return StepTrace{normQuiet, norm.back(), std::move(norm)};
}

// First loud-segment sample index at which the modulation reaches `frac` of the
// way from the settled quiet level to the settled loud level, or -1 if never.
int crossingSample(const StepTrace& trace, double frac) {
    const double target = trace.normQuiet + frac * (trace.normLoud - trace.normQuiet);
    for (std::size_t i = 0; i < trace.norm.size(); ++i)
        if (trace.norm[i] >= target)
            return static_cast<int>(i);
    return -1;
}

// Configure a core for a modulation step-response probe with a chosen detector
// mode. drive depth is positive so the modulation is live, but the measurand is
// lastNorm() (the shared envelope), independent of the target/curve.
void configureStepCore(ProgramDependentSaturationCore& core, DetectMode mode,
                       float attackSeconds, float releaseSeconds) {
    core.prepare(static_cast<float>(kSampleRate));
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(SaturationQuality::adaa);
    core.setDetectorMode(mode);
    core.setBallistics(Ballistics::branching);
    core.setDetection(Detection::feedForward);
    core.setAttack(attackSeconds);
    core.setRelease(releaseSeconds);
    core.setStaticDrive(12.0f);
    core.setDepth(ModTarget::drive, 0.8f);
    core.setCurve(ModTarget::drive, ModCurve::linear);
}

} // namespace

// ===========================================================================
// TEST 1 (T012 / US1, SC-001): positive drive depth -> THD rises with level;
// static (depth=0) THD-vs-level is comparatively flat.
//
// Mechanism guarantee (so the assertions are robust, not fragile):
//   * drive->THD is monotonically non-decreasing for softClip (proven in
//     saturation-harmonics-test.cpp);
//   * a larger input LEVEL drives more of the tone into the nonlinearity, so
//     THD also rises with level at a FIXED drive.
// With a POSITIVE drive depth the two compound: louder input => higher detected
// envelope => higher modulated drive AND a larger signal into that higher drive
// => THD rises steeply and monotonically. With depth=0 only the (weaker) fixed-
// drive level dependence remains, so the static sweep stays near its near-linear
// THD floor -- the signature program-dependent contrast.
// ===========================================================================

TEST_CASE("dynamic drive depth makes THD rise with input level; static is flat-ish "
          "(T012/US1, SC-001)") {
    // Dynamic: base drive 0 dB, a strong positive drive depth, linear curve.
    ProgramDependentSaturationCore dyn;
    configureDriveThdCore(dyn, /*staticDriveDb=*/0.0f, /*driveDepth=*/0.8f, ModCurve::linear);

    // Static baseline: identical base drive, depth 0 -> the plain static
    // saturator (no level tracking). Same input sweep.
    ProgramDependentSaturationCore stat;
    configureDriveThdCore(stat, /*staticDriveDb=*/0.0f, /*driveDepth=*/0.0f, ModCurve::linear);

    std::array<double, kLevelSweepUS1.size()> dynThd{};
    std::array<double, kLevelSweepUS1.size()> statThd{};
    for (std::size_t i = 0; i < kLevelSweepUS1.size(); ++i) {
        // Fresh state per level so each measurement is an independent settle
        // (no cross-level detector carryover).
        ProgramDependentSaturationCore d;
        configureDriveThdCore(d, 0.0f, 0.8f, ModCurve::linear);
        ProgramDependentSaturationCore s;
        configureDriveThdCore(s, 0.0f, 0.0f, ModCurve::linear);
        dynThd[i]  = steadyStateThd(d, kLevelSweepUS1[i]);
        statThd[i] = steadyStateThd(s, kLevelSweepUS1[i]);
        INFO("level=" << kLevelSweepUS1[i] << " dynThd=" << dynThd[i] << " statThd=" << statThd[i]);
        REQUIRE(std::isfinite(dynThd[i]));
        REQUIRE(std::isfinite(statThd[i]));
    }

    // (a) Dynamic THD is monotonically non-decreasing with level (SC-001).
    for (std::size_t i = 0; i + 1 < dynThd.size(); ++i) {
        INFO("dyn level " << kLevelSweepUS1[i] << "->" << kLevelSweepUS1[i + 1]
             << " : " << dynThd[i] << " -> " << dynThd[i + 1]);
        CHECK(dynThd[i + 1] >= dynThd[i] - kThdMonotonicEps);
    }

    // (b) The dynamic sweep does real work: a substantial end-to-end THD rise.
    const double dynRise  = dynThd.back()  - dynThd.front();
    const double statRise = statThd.back() - statThd.front();
    INFO("dynRise=" << dynRise << " statRise=" << statRise);
    CHECK(dynRise > kMinDynamicThdRise);

    // (c) At every level the level-tracked drive is >= the static base drive, so
    // the dynamic path is never LESS saturated than the static one -- and the
    // gap widens toward the loud end (the level-tracking signature).
    for (std::size_t i = 0; i < dynThd.size(); ++i) {
        INFO("level=" << kLevelSweepUS1[i] << " dyn=" << dynThd[i] << " stat=" << statThd[i]);
        CHECK(dynThd[i] >= statThd[i] - kThdMonotonicEps);
    }
    const double gapQuiet = dynThd.front() - statThd.front();
    const double gapLoud  = dynThd.back()  - statThd.back();
    INFO("gapQuiet=" << gapQuiet << " gapLoud=" << gapLoud);
    CHECK(gapLoud > gapQuiet);

    // (d) Static THD-vs-level is comparatively flat: its end-to-end rise is a
    // small fraction of the dynamic sweep's (SC-001 contrast).
    CHECK(statRise < kStaticFlatnessFrac * dynRise);
}

// ===========================================================================
// TEST 2 (T012 / US1, SC-005): a level STEP drives the modulation to ~63% of
// its target within the configured attack time.
//
// Reads the modulation driver directly (lastNorm()). A clean constant-amplitude
// step through a peak/branching detector one-poles the dB envelope from the
// quiet level to the loud level, crossing 1 - 1/e at ~tau; lastNorm() is a
// linear remap of that dB envelope, so it crosses the same 63% fraction at the
// same tau.
// ===========================================================================

TEST_CASE("a level step drives the modulation to ~63% of target within the attack time "
          "(T012/US1, SC-005)") {
    constexpr float       kAttackSeconds = 0.010f;                       // 10 ms
    constexpr double      kTauSamples    = kAttackSeconds * kSampleRate; // 480 samples
    constexpr std::size_t kQuietSamples  = 4800;  // settle from the -120 dB cold floor
    constexpr std::size_t kLoudSamples   = 4800;  // settle loud + observe the crossing
    constexpr float       kQuietAmp      = 0.02f; // ~ -34 dBFS peak (norm well above 0)
    constexpr float       kLoudAmp       = 0.5f;  // ~  -6 dBFS peak (norm well below 1)

    ProgramDependentSaturationCore core;
    configureStepCore(core, DetectMode::peak, kAttackSeconds, /*release=*/0.100f);

    const StepTrace trace =
        driveLevelStep(core, kQuietAmp, kLoudAmp, kQuietSamples, kLoudSamples);

    INFO("normQuiet=" << trace.normQuiet << " normLoud=" << trace.normLoud);
    REQUIRE(trace.normLoud > trace.normQuiet); // the step actually raised the envelope

    const int cross = crossingSample(trace, kAttackCrossFrac);
    REQUIRE(cross >= 0); // the modulation reached 63% at all

    const double crossSeconds  = static_cast<double>(cross) / kSampleRate;
    const double tauSeconds    = kTauSamples / kSampleRate;
    const double tolSeconds    = kAttackTolFrac * tauSeconds;
    INFO("cross=" << cross << " samples (" << crossSeconds << " s), tau=" << tauSeconds << " s");
    CHECK(std::fabs(crossSeconds - tauSeconds) <= tolSeconds);
}

// ===========================================================================
// TEST 3 (T021 / US5, SC-004): a NEGATIVE drive depth DECREASES THD as level
// rises (mirror image of positive), and equal-magnitude opposite-sign depths
// produce mirror-direction trajectories about the static base.
//
// REFERENCE-WINDOW NOTE (documented gap, reported in the summary): for the
// measured HARMONIC CONTENT to literally FALL as level rises under a negative
// depth, the drive REDUCTION must outrun the input-level rise. With the default
// reference window (-60..0 dBFS = 60 dB) and the core's fixed 48 dB drive span,
// a linear curve changes drive by at most 48/60 = 0.8 dB per dB of level, which
// is LESS than the 1 dB/dB the rising input adds to the effective shaper input
// -- so at the default window a negative depth only SLOWS the THD rise, never
// reverses it (the drive OFFSET still mirrors exactly; the THD does not). This
// test therefore narrows the window to -30..0 dBFS (drive slope 48/30 = 1.6
// dB/dB > 1), the regime in which the softening genuinely reduces measured
// distortion. The base drive (24 dB, mid-range) leaves headroom for the
// negative offset to pull drive down without clamping at 0 dB.
// ===========================================================================

TEST_CASE("negative drive depth decreases THD with level; +/- depths mirror about the "
          "static base (T021/US5, SC-004)") {
    constexpr float kBaseDriveDb = 24.0f;
    constexpr float kDepthMag    = 0.7f;
    constexpr float kRefLoDb     = -30.0f; // narrowed window (see the note above)
    constexpr float kRefHiDb     = 0.0f;

    std::array<double, kLevelSweepUS5.size()> posThd{};
    std::array<double, kLevelSweepUS5.size()> negThd{};
    std::array<double, kLevelSweepUS5.size()> baseThd{};
    for (std::size_t i = 0; i < kLevelSweepUS5.size(); ++i) {
        ProgramDependentSaturationCore pos;
        configureDriveThdCore(pos, kBaseDriveDb, +kDepthMag, ModCurve::linear, kRefLoDb, kRefHiDb);
        ProgramDependentSaturationCore neg;
        configureDriveThdCore(neg, kBaseDriveDb, -kDepthMag, ModCurve::linear, kRefLoDb, kRefHiDb);
        ProgramDependentSaturationCore base;
        configureDriveThdCore(base, kBaseDriveDb, 0.0f, ModCurve::linear, kRefLoDb, kRefHiDb);

        posThd[i]  = steadyStateThd(pos, kLevelSweepUS5[i]);
        negThd[i]  = steadyStateThd(neg, kLevelSweepUS5[i]);
        baseThd[i] = steadyStateThd(base, kLevelSweepUS5[i]);
        INFO("level=" << kLevelSweepUS5[i] << " pos=" << posThd[i]
             << " neg=" << negThd[i] << " base=" << baseThd[i]);
        REQUIRE(std::isfinite(posThd[i]));
        REQUIRE(std::isfinite(negThd[i]));
        REQUIRE(std::isfinite(baseThd[i]));
    }

    // Positive depth: THD rises with level (as in US1).
    for (std::size_t i = 0; i + 1 < posThd.size(); ++i) {
        INFO("pos level " << kLevelSweepUS5[i] << "->" << kLevelSweepUS5[i + 1]
             << " : " << posThd[i] << " -> " << posThd[i + 1]);
        CHECK(posThd[i + 1] >= posThd[i] - kThdMonotonicEps);
    }

    // Negative depth: THD FALLS with level -- the softening / ducking mirror.
    for (std::size_t i = 0; i + 1 < negThd.size(); ++i) {
        INFO("neg level " << kLevelSweepUS5[i] << "->" << kLevelSweepUS5[i + 1]
             << " : " << negThd[i] << " -> " << negThd[i + 1]);
        CHECK(negThd[i + 1] <= negThd[i] + kThdMonotonicEps);
    }

    // The negative trajectory drops by a real, non-trivial amount end to end
    // (not merely flat within measurement noise).
    INFO("negThd front=" << negThd.front() << " back=" << negThd.back());
    CHECK(negThd.front() - negThd.back() > kMinNegThdDrop);

    // Mirror about the static base: at each level the positive-depth THD sits at
    // or above the base and the negative-depth THD sits at or below it (the +/-
    // drive offsets are exact mirrors; drive->THD is monotone, so the THD
    // trajectories straddle the base in opposite directions).
    for (std::size_t i = 0; i < kLevelSweepUS5.size(); ++i) {
        INFO("level=" << kLevelSweepUS5[i] << " pos=" << posThd[i]
             << " base=" << baseThd[i] << " neg=" << negThd[i]);
        CHECK(posThd[i] >= baseThd[i] - kThdMonotonicEps);
        CHECK(negThd[i] <= baseThd[i] + kThdMonotonicEps);
    }

    // The two trajectories genuinely diverge in opposite directions: at the loud
    // end the positive path is far more saturated than the negative path.
    CHECK(posThd.back() > negThd.back() + kCurveSeparation);
}

// ===========================================================================
// TEST 4 (T021 / US5, SC-004): the three response curves (linear/log/exp) at
// equal depth produce DISTINGUISHABLE THD-vs-level trajectories.
//
// At a fixed mid level the curve laws map the same normalized envelope to
// DIFFERENT shaped values: logarithmic is concave (early onset, above the
// diagonal), exponential is convex (late onset, below the diagonal), linear
// between them. So the modulated drive is ordered drive_log > drive_lin >
// drive_exp, and (drive->THD monotone) the THD readings inherit that strict
// ordering with a real margin.
// ===========================================================================

TEST_CASE("linear/log/exp curves at equal depth give distinguishable THD trajectories "
          "(T021/US5, SC-004)") {
    constexpr float kBaseDriveDb = 12.0f;
    constexpr float kDepth       = 0.6f;
    constexpr float kMidLevel    = 0.12f; // mid level: all three curves land in the
                                          // softClip's responsive (non-zero, non-plateau) band

    ProgramDependentSaturationCore lin;
    configureDriveThdCore(lin, kBaseDriveDb, kDepth, ModCurve::linear);
    ProgramDependentSaturationCore log;
    configureDriveThdCore(log, kBaseDriveDb, kDepth, ModCurve::logarithmic);
    ProgramDependentSaturationCore exp;
    configureDriveThdCore(exp, kBaseDriveDb, kDepth, ModCurve::exponential);

    const double linThd = steadyStateThd(lin, kMidLevel);
    const double logThd = steadyStateThd(log, kMidLevel);
    const double expThd = steadyStateThd(exp, kMidLevel);
    INFO("logThd=" << logThd << " linThd=" << linThd << " expThd=" << expThd);
    REQUIRE(std::isfinite(linThd));
    REQUIRE(std::isfinite(logThd));
    REQUIRE(std::isfinite(expThd));

    // log (early onset, most drive at mid level) > linear > exp (late onset),
    // each pair separated by a real margin -> the three are distinguishable.
    CHECK(logThd > linThd + kCurveSeparation);
    CHECK(linThd > expThd + kCurveSeparation);
    CHECK(logThd > expThd + kCurveSeparation);
}

// ===========================================================================
// TEST 5 (T025 / US7, SC-005): peak vs rms detection on the SAME level step
// produce different, characterized modulation responses -- peak faster/sharper,
// rms smoother/slower -- and a configured attack yields the ~63%-in-tau step
// response on the modulation.
//
// A constant-amplitude step has equal peak and rms steady-state levels (rms of
// a constant == its magnitude), so BOTH detectors settle to the same
// normLoud/normQuiet; only the transient speed differs. The rms detector adds
// its own leaky mean-square averaging window ON TOP of the shared ballistics
// smoother, so it reaches the 63% crossing strictly LATER than peak.
// ===========================================================================

TEST_CASE("peak vs rms detection give different (characterized) step responses; peak "
          "hits ~63% in the attack time (T025/US7, SC-005)") {
    constexpr float       kAttackSeconds = 0.010f;                       // 10 ms
    constexpr double      kTauSamples    = kAttackSeconds * kSampleRate; // 480 samples
    constexpr std::size_t kQuietSamples  = 4800;
    constexpr std::size_t kLoudSamples   = 4800;
    constexpr float       kQuietAmp      = 0.02f;
    constexpr float       kLoudAmp       = 0.5f;

    ProgramDependentSaturationCore peakCore;
    configureStepCore(peakCore, DetectMode::peak, kAttackSeconds, 0.100f);
    ProgramDependentSaturationCore rmsCore;
    configureStepCore(rmsCore, DetectMode::rms, kAttackSeconds, 0.100f);

    const StepTrace peakTrace =
        driveLevelStep(peakCore, kQuietAmp, kLoudAmp, kQuietSamples, kLoudSamples);
    const StepTrace rmsTrace =
        driveLevelStep(rmsCore, kQuietAmp, kLoudAmp, kQuietSamples, kLoudSamples);

    REQUIRE(peakTrace.normLoud > peakTrace.normQuiet);
    REQUIRE(rmsTrace.normLoud > rmsTrace.normQuiet);

    const int peakCross = crossingSample(peakTrace, kAttackCrossFrac);
    const int rmsCross  = crossingSample(rmsTrace, kAttackCrossFrac);
    REQUIRE(peakCross >= 0);
    REQUIRE(rmsCross >= 0);
    INFO("peakCross=" << peakCross << " rmsCross=" << rmsCross << " tau=" << kTauSamples);

    // Peak reaches ~63% in the configured attack time (the shared ballistics
    // smoother acting on an instantaneous detector).
    const double peakCrossSeconds = static_cast<double>(peakCross) / kSampleRate;
    const double tauSeconds       = kTauSamples / kSampleRate;
    CHECK(std::fabs(peakCrossSeconds - tauSeconds) <= kAttackTolFrac * tauSeconds);

    // rms is smoother/slower: its extra mean-square averaging window delays the
    // 63% crossing well past peak's (characterized ordering, US7 Scenario 1).
    CHECK(rmsCross > peakCross);
}

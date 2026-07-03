#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"
#include "support/measurement/metrics.h" // acfx::measure::thd

// T031 (US10) + T033 (US11) + T035 (US12) -- sidechain HPF, external key, and
// stereo-linking doctests.
//
// Written against specs/program-dependent-saturation/spec.md SC-009/SC-010/
// SC-011 and the composition documented in
// core/effects/program-dependent-saturation/program-dependent-saturation-core.h
// (detectNorm()'s `externalKey_ ? key : x` source-select -> scHpf -> detector
// chain; process(x,key) == processWithNorm(x, detectNorm(x,key))) and
// core/effects/program-dependent-saturation/program-dependent-saturation-
// routing.h (keyAt()/processBlock()'s linked-vs-perChannel dispatch).
//
// MEASURING "MODULATION": every case below engages ONLY driveDepth (+1,
// linear curve, static drive 0 dB) with every other target depth at 0
// (default). At norm~0 the modulated drive stays ~0 dB (near-linear, ~0 THD);
// as norm climbs toward 1 the modulated drive climbs toward 48 dB (hard
// clipping, high THD). Total harmonic distortion of the settled output
// (acfx::measure::thd, already shipped for FR-008) is therefore a direct,
// reusable proxy for "how much the drive-modulation engine actually pushed
// the drive up" -- i.e. for the modulation SC-009/010/011 talk about. This
// mirrors program-dependent-saturation-matrix-test.cpp's precedent of driving
// ONE target and reading a measurable, level-tracking side effect.
//
// Every assertion is COMPARATIVE (ordering + a named margin), never a
// fabricated absolute THD target -- matching tests/core/compressor-sidechain-
// test.cpp's philosophy.

using namespace acfx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float  kSampleRate = 48000.0f;

// Settle/measure windows shared by every case: 400 ms lets the 5 ms attack /
// 50 ms release ballistics fully converge; the trailing 100 ms is the
// measurement window every THD figure below is computed from (an integer
// number of cycles for every fundamental used in this file, so every
// Goertzel readout is leakage-free -- measurement-support.h's window
// contract).
constexpr int kSettleSamples  = static_cast<int>(0.400 * kSampleRate); // 19200
constexpr int kMeasureSamples = static_cast<int>(0.100 * kSampleRate); // 4800
constexpr int kTotalSamples   = kSettleSamples + kMeasureSamples;      // 24000

constexpr float kAttackSeconds  = 0.005f;
constexpr float kReleaseSeconds = 0.050f;

// ---------------------------------------------------------------------------
// Shared measurement helpers.
// ---------------------------------------------------------------------------

std::vector<float> sineBuffer(double freqHz, double amplitude, int n) {
    std::vector<float> buf(static_cast<std::size_t>(n));
    const double w = 2.0 * kPi * freqHz / static_cast<double>(kSampleRate);
    for (int i = 0; i < n; ++i)
        buf[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(w * static_cast<double>(i)));
    return buf;
}

// Total harmonic distortion (acfx::measure::thd, FR-008) over the settled
// tail of `buf` -- the drive-modulation proxy documented above.
double thdTail(const std::vector<float>& buf, double freqHz, int tailSamples) {
    const int n = static_cast<int>(buf.size());
    const int start = (tailSamples < n) ? (n - tailSamples) : 0;
    const span<const float> tail{buf.data() + start, static_cast<std::size_t>(n - start)};
    return acfx::measure::thd(tail, freqHz, static_cast<double>(kSampleRate));
}

// ---------------------------------------------------------------------------
// ProgramDependentSaturationCore direct-drive helpers (US10 -- the task
// explicitly permits driving the core directly for the SC-HPF case).
// ---------------------------------------------------------------------------

// A freshly-prepared core: softClip/adaa (defaults), rms/branching detection,
// feedForward, static drive 0 dB, ONLY driveDepth engaged (+1, linear) so a
// climbing envelope is the sole source of any measured distortion, and the
// given sidechain-HPF cutoff (0 = bypass).
ProgramDependentSaturationCore makeCore(float scHpfHz) {
    ProgramDependentSaturationCore core;
    core.prepare(kSampleRate);
    core.setAttack(kAttackSeconds);
    core.setRelease(kReleaseSeconds);
    core.setStaticDrive(0.0f);
    core.setDepth(ModTarget::drive, 1.0f);
    core.setCurve(ModTarget::drive, ModCurve::linear);
    core.setScHpf(scHpfHz);
    return core;
}

// Drive a core keyless (key == x, the internal-sidechain idiom) with one sine
// tone; returns the full settle+measure buffer.
std::vector<float> runKeylessCore(ProgramDependentSaturationCore& core, double freqHz,
                                  double amplitude) {
    std::vector<float> x = sineBuffer(freqHz, amplitude, kTotalSamples);
    std::vector<float> out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) out[i] = core.process(x[i], x[i]);
    return out;
}

// ---------------------------------------------------------------------------
// ProgramDependentSaturationEffect helpers (US11 external key + US12 stereo
// link -- both routing behaviors live only in the effect, per the task).
// ---------------------------------------------------------------------------

void setParam(ProgramDependentSaturationEffect& fx, std::uint8_t paramIndex, float plain) {
    fx.setParameter(ParamId{paramIndex},
                    normalize(ProgramDependentSaturationEffect::kParams[paramIndex], plain));
}

// Configure a declared ProgramDependentSaturationEffect in place (never
// returned by value -- it owns cross-thread atomics, same non-copyable
// constraint as CompressorEffect): softClip/adaa/rms/branching defaults,
// static drive 0 dB, ONLY driveDepth engaged (see makeCore()'s rationale).
void configureEffect(ProgramDependentSaturationEffect& fx, int numChannels) {
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, numChannels});
    setParam(fx, ProgramDependentSaturationEffect::kAttack, kAttackSeconds);   // descriptor is seconds
    setParam(fx, ProgramDependentSaturationEffect::kRelease, kReleaseSeconds);
    setParam(fx, ProgramDependentSaturationEffect::kDrive, 0.0f);
    setParam(fx, ProgramDependentSaturationEffect::kDriveDepth, 1.0f);
}

// Drive a configured (1-channel) effect keyless (process(io), no sidechain
// block) with one sine tone; returns the full settle+measure buffer.
std::vector<float> runKeylessEffect(ProgramDependentSaturationEffect& fx, double freqHz,
                                    double amplitude) {
    std::vector<float> buf = sineBuffer(freqHz, amplitude, kTotalSamples);
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, kTotalSamples);
    fx.process(block);
    return buf;
}

// Drive a configured (1-channel) effect via process(io, sidechain) with an
// INDEPENDENT main and key tone; returns the main (post-saturation) buffer.
std::vector<float> runKeyedEffect(ProgramDependentSaturationEffect& fx, double mainFreqHz,
                                  double mainAmplitude, double keyFreqHz, double keyAmplitude) {
    std::vector<float> main = sineBuffer(mainFreqHz, mainAmplitude, kTotalSamples);
    std::vector<float> key = sineBuffer(keyFreqHz, keyAmplitude, kTotalSamples);
    float* mainChans[1] = {main.data()};
    float* keyChans[1] = {key.data()};
    AudioBlock io(mainChans, 1, kTotalSamples);
    AudioBlock sc(keyChans, 1, kTotalSamples);
    fx.process(io, sc);
    return main;
}

struct StereoRun {
    std::vector<float> left;
    std::vector<float> right;
};

// Drive a configured (2-channel) effect keyless (process(io)) with an
// independent tone per channel; returns both settle+measure buffers.
StereoRun runStereoEffect(ProgramDependentSaturationEffect& fx, double leftFreqHz,
                          double leftAmplitude, double rightFreqHz, double rightAmplitude) {
    StereoRun run;
    run.left = sineBuffer(leftFreqHz, leftAmplitude, kTotalSamples);
    run.right = sineBuffer(rightFreqHz, rightAmplitude, kTotalSamples);
    float* chans[2] = {run.left.data(), run.right.data()};
    AudioBlock block(chans, 2, kTotalSamples);
    fx.process(block);
    return run;
}

} // namespace

// ===========================================================================
// T031 (US10) -- sidechain HPF only shapes DETECTION (SC-009)
// ===========================================================================

TEST_CASE("ProgramDependentSaturationCore - sidechain HPF at 120 Hz yields "
          "substantially less drive-modulation (THD) for a below-cutoff tone "
          "than an above-cutoff tone at the same level; 0 Hz restores "
          "full-band detection (T031/US10, SC-009)") {
    constexpr float  kHpfHz     = 120.0f;
    constexpr double kLowHz     = 60.0;   // below cutoff
    constexpr double kHighHz    = 1000.0; // well above cutoff
    // -20 dBFS: high enough that the above-cutoff tone clears the ref window
    // and drives real THD, but low enough that the below-cutoff tone's HPF
    // attenuation pushes its detected level (and THD) down near the window
    // floor -- a 0 dBFS probe leaves BOTH tones deep in heavy clipping, where
    // THD saturates and the modulation gap becomes invisible.
    constexpr double kAmplitude = 0.1;

    // Named tolerances (comparative, no fabricated absolute THD targets).
    constexpr double kModulationPresent = 0.05; // "clearly driven up" THD floor
    constexpr double kMarginRatio       = 3.0;  // "substantially less/more" (x3)

    // --- HPF engaged at 120 Hz: below-cutoff vs above-cutoff -----------------
    ProgramDependentSaturationCore lowOn = makeCore(kHpfHz);
    const double thdLowHpfOn =
        thdTail(runKeylessCore(lowOn, kLowHz, kAmplitude), kLowHz, kMeasureSamples);

    ProgramDependentSaturationCore highOn = makeCore(kHpfHz);
    const double thdHighHpfOn =
        thdTail(runKeylessCore(highOn, kHighHz, kAmplitude), kHighHz, kMeasureSamples);

    INFO("thdLowHpfOn=" << thdLowHpfOn << " thdHighHpfOn=" << thdHighHpfOn);
    REQUIRE(std::isfinite(thdLowHpfOn));
    REQUIRE(std::isfinite(thdHighHpfOn));

    // Sanity: the above-cutoff tone actually reaches the detector at full
    // level and is clearly driven (its own THD is well above the floor).
    CHECK(thdHighHpfOn > kModulationPresent);
    // AC1 (SC-009): the below-cutoff tone drives SUBSTANTIALLY LESS
    // modulation than the above-cutoff tone at the same input level (the SC
    // filter attenuated it out of detection).
    CHECK(thdHighHpfOn > thdLowHpfOn * kMarginRatio);

    // --- Bypass (scHpf = 0 Hz), SAME 60 Hz tone ------------------------------
    // Compared HPF-on vs HPF-off AT THE SAME frequency (not low vs high) so a
    // ballistics/period artifact at 60 Hz can never masquerade as the filter's
    // effect (mirrors compressor-sidechain-test.cpp's identical rationale).
    ProgramDependentSaturationCore lowOff = makeCore(0.0f);
    const double thdLowHpfOff =
        thdTail(runKeylessCore(lowOff, kLowHz, kAmplitude), kLowHz, kMeasureSamples);

    INFO("thdLowHpfOff=" << thdLowHpfOff);
    REQUIRE(std::isfinite(thdLowHpfOff));

    // AC2 (SC-009): 0 Hz restores full-band detection -- the 60 Hz tone now
    // drives materially MORE modulation than under the 120 Hz HPF.
    CHECK(thdLowHpfOff > thdLowHpfOn * kMarginRatio);
    CHECK(thdLowHpfOff > kModulationPresent);
}

// ===========================================================================
// T033 (US11) -- external sidechain key (SC-010)
// ===========================================================================

TEST_CASE("ProgramDependentSaturationEffect::process(io, sidechain) - a loud "
          "external key drives a quiet main's modulation as if it were loud, "
          "applied to the main path; with no key (or the flag off) detection "
          "reads the main input (T033/US11, SC-010)") {
    constexpr double kMainFreqHz    = 300.0;
    constexpr double kMainAmplitude = 0.02; // -34 dBFS: quiet, near the window floor
    constexpr double kKeyFreqHz     = 1100.0;
    constexpr double kLoudKeyAmplitude   = 0.5; // -9 dBFS: well inside the window
    constexpr double kLouderKeyAmplitude = 0.9; // ~-1 dBFS: much further inside

    constexpr double kModulationPresent = 0.05;
    constexpr double kQuietFloor        = 0.02; // quiet main alone barely modulates
    constexpr double kMarginRatio       = 2.0;

    // AC1 (SC-010 scenario 1): a LOUD external key drives a QUIET main's
    // modulation as if the main itself were loud.
    ProgramDependentSaturationEffect keyedFx;
    configureEffect(keyedFx, 1);
    setParam(keyedFx, ProgramDependentSaturationEffect::kExternalSidechain, 1.0f);
    const double thdKeyed = thdTail(
        runKeyedEffect(keyedFx, kMainFreqHz, kMainAmplitude, kKeyFreqHz, kLoudKeyAmplitude),
        kMainFreqHz, kMeasureSamples);

    // Reference: the SAME quiet main alone (process(io), no key at all) --
    // detection reads the main input, which never leaves the window floor.
    ProgramDependentSaturationEffect keylessFx;
    configureEffect(keylessFx, 1);
    const double thdKeyless =
        thdTail(runKeylessEffect(keylessFx, kMainFreqHz, kMainAmplitude), kMainFreqHz,
                kMeasureSamples);

    INFO("thdKeyed=" << thdKeyed << " thdKeyless=" << thdKeyless);
    REQUIRE(std::isfinite(thdKeyed));
    REQUIRE(std::isfinite(thdKeyless));

    CHECK(thdKeyless < kQuietFloor);
    CHECK(thdKeyed > kModulationPresent);
    CHECK(thdKeyed > thdKeyless * kMarginRatio);

    // "Tracks the key level, not the main's": main is UNCHANGED, only the key
    // gets louder -- modulation rises further (main level plays no part).
    ProgramDependentSaturationEffect louderKeyedFx;
    configureEffect(louderKeyedFx, 1);
    setParam(louderKeyedFx, ProgramDependentSaturationEffect::kExternalSidechain, 1.0f);
    const double thdLouderKeyed = thdTail(
        runKeyedEffect(louderKeyedFx, kMainFreqHz, kMainAmplitude, kKeyFreqHz,
                       kLouderKeyAmplitude),
        kMainFreqHz, kMeasureSamples);
    INFO("thdLouderKeyed=" << thdLouderKeyed);
    CHECK(thdLouderKeyed >= thdKeyed);

    // AC2 (SC-010 scenario 2): a sidechain block IS supplied, but
    // externalSidechain stays OFF -- the flag gates key usage, so detection
    // still falls back to the main input (behaves like the keyless case, not
    // the keyed one), even though loud key samples were available.
    ProgramDependentSaturationEffect flagOffFx;
    configureEffect(flagOffFx, 1); // externalSidechain left at its default (off)
    const double thdFlagOff = thdTail(
        runKeyedEffect(flagOffFx, kMainFreqHz, kMainAmplitude, kKeyFreqHz, kLoudKeyAmplitude),
        kMainFreqHz, kMeasureSamples);
    INFO("thdFlagOff=" << thdFlagOff);
    CHECK(thdFlagOff < kQuietFloor);
    CHECK(thdFlagOff < thdKeyed * 0.5);
}

// ===========================================================================
// T035 (US12) -- stereo linking (SC-011)
// ===========================================================================

TEST_CASE("ProgramDependentSaturationEffect::process(io) - linked stereo mode "
          "couples a quiet R channel's modulation to a loud L channel's level "
          "(cross-channel max); perChannel modulates R by its own (quiet) "
          "level; L is stable across both modes (T035/US12, SC-011)") {
    constexpr double kLeftFreqHz    = 1000.0;
    constexpr double kLeftAmplitude = 0.5;  // -9 dBFS: loud, well inside the window
    constexpr double kRightFreqHz   = 700.0;
    constexpr double kRightAmplitude = 0.02; // -34 dBFS: quiet, near the window floor

    constexpr double kModulationPresent = 0.05;
    constexpr double kQuietFloor        = 0.02;
    constexpr double kMarginRatio       = 2.0;
    constexpr double kStableTolerance   = 0.03; // "L barely moves across modes"

    // --- linked: ONE shared detection (cross-channel max) drives BOTH channels
    ProgramDependentSaturationEffect linkedFx;
    configureEffect(linkedFx, 2);
    setParam(linkedFx, ProgramDependentSaturationEffect::kStereoLink, 1.0f); // linked
    const StereoRun linkedRun =
        runStereoEffect(linkedFx, kLeftFreqHz, kLeftAmplitude, kRightFreqHz, kRightAmplitude);
    const double thdLeftLinked = thdTail(linkedRun.left, kLeftFreqHz, kMeasureSamples);
    const double thdRightLinked = thdTail(linkedRun.right, kRightFreqHz, kMeasureSamples);

    // --- perChannel: each channel detects + modulates independently
    ProgramDependentSaturationEffect perChannelFx;
    configureEffect(perChannelFx, 2);
    // stereoLink left at its default (perChannel) -- no setParam needed.
    const StereoRun perChannelRun = runStereoEffect(perChannelFx, kLeftFreqHz, kLeftAmplitude,
                                                     kRightFreqHz, kRightAmplitude);
    const double thdLeftPerChannel = thdTail(perChannelRun.left, kLeftFreqHz, kMeasureSamples);
    const double thdRightPerChannel = thdTail(perChannelRun.right, kRightFreqHz, kMeasureSamples);

    INFO("thdLeftLinked=" << thdLeftLinked << " thdLeftPerChannel=" << thdLeftPerChannel
         << " thdRightLinked=" << thdRightLinked
         << " thdRightPerChannel=" << thdRightPerChannel);
    REQUIRE(std::isfinite(thdLeftLinked));
    REQUIRE(std::isfinite(thdLeftPerChannel));
    REQUIRE(std::isfinite(thdRightLinked));
    REQUIRE(std::isfinite(thdRightPerChannel));

    // Sanity: the loud L channel is clearly driven in both modes -- it is the
    // channel supplying the shared cross-channel max, so this must hold for
    // the linked-R assertion below to mean anything.
    CHECK(thdLeftLinked > kModulationPresent);
    CHECK(thdLeftPerChannel > kModulationPresent);

    // perChannel: the quiet R channel is modulated by its OWN quiet level --
    // little to no drive-up.
    CHECK(thdRightPerChannel < kQuietFloor);

    // AC (SC-011): linked couples R's modulation to L's (louder) level -- R's
    // linked THD is substantially higher than its perChannel THD, tracking
    // the loud channel's character instead of its own quiet one.
    CHECK(thdRightLinked > kModulationPresent);
    CHECK(thdRightLinked > thdRightPerChannel * kMarginRatio);

    // L's own character stays stable across modes: in BOTH modes L supplies
    // (perChannel) or dominates (linked, as the cross-channel max) its own
    // detection, so its modulation should not materially differ between them.
    CHECK(std::abs(thdLeftLinked - thdLeftPerChannel) < kStableTolerance);
}

// ---------------------------------------------------------------------------
// T035 (US12) — linked + FEEDBACK uses the cross-channel max of the previous
// OUTPUTS, so a loud transient in a NON-primary channel (channel 1) still drives
// the shared modulation (FR-013/SC-011). Regression for the code-review finding
// that linked feedback previously tapped only channel 0's output.
// ---------------------------------------------------------------------------

TEST_CASE("ProgramDependentSaturationEffect::process(io) - linked + feedback drives "
          "the shared modulation from the loud channel even when it is NOT channel 0 "
          "(cross-channel max of outputs) (T035/US12, SC-011)") {
    constexpr double kQuietFreqHz  = 1000.0;
    constexpr double kQuietAmp     = 0.02;  // quiet channel
    constexpr double kLoudFreqHz   = 700.0;
    constexpr double kLoudAmp      = 0.5;   // loud channel
    constexpr double kModulationPresent = 0.05;
    constexpr double kSymmetryRatio     = 0.5; // the two cases must be within ~2x

    // Symmetry test: the shared feedback detection is the cross-channel MAX of
    // the previous outputs, so which channel carries the loud signal must not
    // matter. Case A puts the loud signal in channel 0 (the designated core);
    // Case B puts it in channel 1 (non-primary). We measure the LOUD channel's
    // own saturation THD in each: its drive is set by the shared modulation,
    // which only reflects it if its output feeds the shared detection. Before the
    // fix (linked feedback tapped ONLY channel 0's output), Case B's loud channel
    // 1 could not raise the shared norm, so it would saturate far less than Case
    // A — the asymmetry that exposes the bug. (The quiet channel is a poor probe:
    // it receives the same shared drive, but its far smaller input stays below
    // the softClip knee, so its THD reads ~0 regardless.)

    // Case A: loud in channel 0 — measure channel 0.
    ProgramDependentSaturationEffect fxA;
    configureEffect(fxA, 2);
    setParam(fxA, ProgramDependentSaturationEffect::kDetection, 1.0f);  // feedBack
    setParam(fxA, ProgramDependentSaturationEffect::kStereoLink, 1.0f); // linked
    const StereoRun runA =
        runStereoEffect(fxA, kLoudFreqHz, kLoudAmp, kQuietFreqHz, kQuietAmp);
    const double loudThdA = thdTail(runA.left, kLoudFreqHz, kMeasureSamples);

    // Case B: loud in channel 1 (non-primary) — measure channel 1.
    ProgramDependentSaturationEffect fxB;
    configureEffect(fxB, 2);
    setParam(fxB, ProgramDependentSaturationEffect::kDetection, 1.0f);  // feedBack
    setParam(fxB, ProgramDependentSaturationEffect::kStereoLink, 1.0f); // linked
    const StereoRun runB =
        runStereoEffect(fxB, kQuietFreqHz, kQuietAmp, kLoudFreqHz, kLoudAmp);
    const double loudThdB = thdTail(runB.right, kLoudFreqHz, kMeasureSamples);

    INFO("loudThdA(loud@ch0)=" << loudThdA << " loudThdB(loud@ch1)=" << loudThdB);
    REQUIRE(std::isfinite(loudThdA));
    REQUIRE(std::isfinite(loudThdB));

    // The loud channel saturates (its output feeds the shared detection) in both.
    CHECK(loudThdA > kModulationPresent);
    CHECK(loudThdB > kModulationPresent);
    // Position independence (the load-bearing assertion): a loud NON-primary
    // channel 1 raises the shared modulation just as a loud channel 0 does — the
    // shared detection is the max ACROSS channels' outputs, not channel 0 alone.
    CHECK(loudThdB > loudThdA * kSymmetryRatio);
    CHECK(loudThdA > loudThdB * kSymmetryRatio);
}

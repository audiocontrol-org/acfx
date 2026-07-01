#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string_view>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/saturation/saturation-core.h"    // SaturationVoicing / SaturationQuality
#include "effects/saturation/saturation-effect.h"  // SaturationEffect (T015/T016)
#include "core/measurement-support.h"
#include "support/allocation-sentinel.h"

// T014 -- User Story 3 suite: SaturationEffect (the host-facing Effect
// contract wrapping SaturationCore). Written TEST-FIRST against
// specs/saturation/contracts/saturation-api.md's "SaturationEffect" section
// while the header itself (core/effects/saturation/saturation-effect.h) is
// authored in parallel by T015/T016 from the SAME contract. This file
// mirrors the shipped SvfEffect idiom (core/effects/svf/svf-effect.h,
// tests/core/svf-test.cpp, tests/core/no-allocation-test.cpp) exactly:
// AudioBlock/ProcessContext construction, setParameter(ParamId, normalized)
// calls, and the pending-atomic cross-thread handoff.
//
// References: FR-009 (single parameter descriptor table), FR-010 (cross-
// thread setParameter, applied at a block boundary, no allocation/lock on
// publish), FR-011 (prepare/reset/process/setParameter, no base class/
// vtable), FR-012 (mix blend law); spec.md User Story 3 Acceptance Scenarios
// 1-3, SC-005.

using namespace acfx;
using acfx::test::AllocationSentinel;

namespace {

constexpr float  kPi          = 3.14159265358979323846f;
constexpr float  kSampleRateF = 48000.0f;
constexpr double kSampleRateD = 48000.0;

// Shared sine-stimulus window: 1 kHz fundamental over 100 integer cycles at
// 48 kHz (matches the anti-leakage window contract in measurement-support.h,
// and the same window every other saturation-*-test.cpp file in this suite
// uses -- every harmonic lands exactly on a Goertzel bin).
constexpr double      kFundamentalHz = 1000.0;
constexpr std::size_t kNumSamples    = 4800;  // 100 cycles * 48 samples/cycle
constexpr float       kAmplitude     = 0.5f;  // moderate: exercises the shaper without full clip

// Convert a desired PLAIN-units value for a SaturationEffect parameter into
// the normalized 0..1 value setParameter() expects, via the shared
// descriptor table -- mirrors svf-test.cpp's MonoDriver / measurement-
// support.h's configureLowpass idiom. Never hand-roll the normalize math
// here: the descriptor table (min/max/skew) is the single source of truth
// (FR-009), and exact ranges are a T015 tuning decision this test must not
// assume beyond what data-model.md fixes (tone/mix/bias) or documents as
// "e.g." (drive/output).
float normFor(SaturationEffect::Param p, float plainValue) {
    return acfx::normalize(SaturationEffect::kParams[p], plainValue);
}

// A fully-configured mono SaturationEffect driver, one sample per
// process() call. Configuration is published via setParameter() (as if
// from a non-audio/control thread) in the constructor; it is applied by the
// FIRST process() call this driver makes, since applyPending() (per the
// SvfEffect idiom) runs at the top of process() and consumes every pending
// edit in one shot. Mirrors svf-test.cpp's MonoDriver exactly.
struct MonoEffectDriver {
    SaturationEffect fx;
    float scratch = 0.0f;

    MonoEffectDriver(float driveDb, float biasPlain, float tonePlain, float mixPlain, float outputDb,
                      SaturationVoicing voicing = SaturationVoicing::softClip,
                      SaturationQuality quality = SaturationQuality::adaa) {
        fx.prepare(ProcessContext{kSampleRateD, 1, 1});
        fx.setParameter(ParamId{SaturationEffect::kVoicing},
                         normFor(SaturationEffect::kVoicing, static_cast<float>(static_cast<int>(voicing))));
        fx.setParameter(ParamId{SaturationEffect::kQuality},
                         normFor(SaturationEffect::kQuality, static_cast<float>(static_cast<int>(quality))));
        fx.setParameter(ParamId{SaturationEffect::kDrive}, normFor(SaturationEffect::kDrive, driveDb));
        fx.setParameter(ParamId{SaturationEffect::kBias}, normFor(SaturationEffect::kBias, biasPlain));
        fx.setParameter(ParamId{SaturationEffect::kTone}, normFor(SaturationEffect::kTone, tonePlain));
        fx.setParameter(ParamId{SaturationEffect::kMix}, normFor(SaturationEffect::kMix, mixPlain));
        fx.setParameter(ParamId{SaturationEffect::kOutput}, normFor(SaturationEffect::kOutput, outputDb));
    }

    float operator()(float in) noexcept {
        scratch = in;
        float* chans[1] = {&scratch};
        AudioBlock block(chans, 1, 1);
        fx.process(block);
        return scratch;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// TEST 1a/1b/1c: Descriptor-table invariants (FR-009) -- the runtime mirror
// of SaturationEffect::kParams's compile-time static_assert (contract:
// "isValidDescriptor over the table fails compilation, not the audio path").
// ---------------------------------------------------------------------------

TEST_CASE("SaturationEffect::parameters() returns exactly seven valid descriptors (FR-009)") {
    const span<const ParameterDescriptor> params = SaturationEffect::parameters();
    REQUIRE(params.size() == 7);
    for (const ParameterDescriptor& d : params)
        CHECK(isValidDescriptor(d));
}

TEST_CASE("SaturationEffect voicing and quality descriptors are discrete with correct label counts (FR-009)") {
    const auto& voicingDesc = SaturationEffect::kParams[SaturationEffect::kVoicing];
    CHECK(isValidDescriptor(voicingDesc));
    CHECK(voicingDesc.kind == ParamKind::discrete);
    REQUIRE(voicingDesc.discreteCount == 4);
    REQUIRE(voicingDesc.choices.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(voicingDesc.choices[i] == SaturationEffect::kVoicingLabels[i]);

    const auto& qualityDesc = SaturationEffect::kParams[SaturationEffect::kQuality];
    CHECK(isValidDescriptor(qualityDesc));
    CHECK(qualityDesc.kind == ParamKind::discrete);
    REQUIRE(qualityDesc.discreteCount == 2);
    REQUIRE(qualityDesc.choices.size() == 2);
    for (std::size_t i = 0; i < 2; ++i)
        CHECK(qualityDesc.choices[i] == SaturationEffect::kQualityLabels[i]);
}

TEST_CASE("SaturationEffect continuous parameters have sane, documented ranges (FR-009)") {
    constexpr std::array<SaturationEffect::Param, 5> kContinuous = {
        SaturationEffect::kDrive, SaturationEffect::kTone, SaturationEffect::kMix,
        SaturationEffect::kOutput, SaturationEffect::kBias};

    for (const auto p : kContinuous) {
        const auto& d = SaturationEffect::kParams[p];
        INFO("param index=" << static_cast<int>(p) << " name=" << d.name);
        CHECK(isValidDescriptor(d));
        CHECK(d.kind == ParamKind::continuous);
        CHECK(d.max > d.min);
        CHECK(d.defaultValue >= d.min);
        CHECK(d.defaultValue <= d.max);
    }

    // data-model.md fixes tone/mix/bias exactly (no "e.g." qualifier, unlike
    // drive/output which are an open tuning decision): tone/bias span
    // -1..+1, mix spans the meaningful dry/wet range 0..1.
    const auto& tone = SaturationEffect::kParams[SaturationEffect::kTone];
    CHECK(tone.unit == ParamUnit::none);
    CHECK(tone.min == doctest::Approx(-1.0f));
    CHECK(tone.max == doctest::Approx(1.0f));

    const auto& bias = SaturationEffect::kParams[SaturationEffect::kBias];
    CHECK(bias.unit == ParamUnit::none);
    CHECK(bias.min == doctest::Approx(-1.0f));
    CHECK(bias.max == doctest::Approx(1.0f));

    const auto& mix = SaturationEffect::kParams[SaturationEffect::kMix];
    CHECK(mix.unit == ParamUnit::none);
    CHECK(mix.min == doctest::Approx(0.0f));
    CHECK(mix.max == doctest::Approx(1.0f));

    // drive/output are dB-unit, open-range ("e.g.") per data-model.md --
    // only assert the unit, not the exact bounds.
    CHECK(SaturationEffect::kParams[SaturationEffect::kDrive].unit == ParamUnit::decibels);
    CHECK(SaturationEffect::kParams[SaturationEffect::kOutput].unit == ParamUnit::decibels);
}

// ---------------------------------------------------------------------------
// TEST 2: Cross-thread parameter handoff (FR-010, Acceptance Scenario 1)
//
// Publish a mix edit "from a non-audio thread" (a bare setParameter() call,
// single-threaded in the test but semantically identical -- FR-010 requires
// publish to never mutate audio-thread state directly) BEFORE any process()
// call consumes it, then assert:
//   (a) a block already processed under the OLD value is untouched by the
//       later publish (no retroactive rewrite of past output), and
//   (b) the NEXT process() call is where the pending edit is consumed
//       (applyPending() runs at the top of process(), per the SvfEffect
//       idiom) -- its output visibly reflects the NEW value.
// ---------------------------------------------------------------------------

TEST_CASE("cross-thread parameter handoff: setParameter published before process() takes effect only at the next block boundary (FR-010)") {
    constexpr int   kBlockSize = 32;
    constexpr float kDriveDb   = 18.0f;  // well past the knee -- drive range is "e.g. 0..48dB" (data-model.md)
    constexpr float kBiasVal   = 0.3f;

    // Float-precision floor for "output == dry input exactly" (mix=0, y =
    // 0*wet + 1*x with unity output gain) -- mirrors saturation-core-test.cpp
    // TEST 2's kDryTolerance.
    constexpr double kDryTolerance = 1.0e-5;
    // Two-plus orders of magnitude above kDryTolerance: a genuine "the wet
    // path is now audible" check, not a coincidental rounding difference
    // (mirrors saturation-core-test.cpp TEST 3's kWetDivergenceThreshold).
    constexpr double kWetDivergenceThreshold = 0.05;

    SaturationEffect fx;
    fx.prepare(ProcessContext{kSampleRateD, kBlockSize, 1});

    // Establish a known baseline: voicing/quality/drive/bias/tone/output
    // fixed, mix=0 (fully dry). Every pending edit here is applied together
    // at the FIRST process() call below.
    fx.setParameter(ParamId{SaturationEffect::kVoicing},
                     normFor(SaturationEffect::kVoicing,
                             static_cast<float>(static_cast<int>(SaturationVoicing::softClip))));
    fx.setParameter(ParamId{SaturationEffect::kQuality},
                     normFor(SaturationEffect::kQuality,
                             static_cast<float>(static_cast<int>(SaturationQuality::adaa))));
    fx.setParameter(ParamId{SaturationEffect::kDrive}, normFor(SaturationEffect::kDrive, kDriveDb));
    fx.setParameter(ParamId{SaturationEffect::kBias}, normFor(SaturationEffect::kBias, kBiasVal));
    fx.setParameter(ParamId{SaturationEffect::kTone}, normFor(SaturationEffect::kTone, 0.0f));
    fx.setParameter(ParamId{SaturationEffect::kOutput}, normFor(SaturationEffect::kOutput, 0.0f));
    fx.setParameter(ParamId{SaturationEffect::kMix}, normFor(SaturationEffect::kMix, 0.0f)); // fully dry

    std::vector<float> in(static_cast<std::size_t>(kBlockSize));
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRateF;
        in[static_cast<std::size_t>(i)] =
            kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t);
    }

    // Block A: the FIRST process() call applies every pending edit above,
    // including mix=0 (fully dry).
    std::vector<float> blockA = in;
    {
        float* chans[1] = {blockA.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    }
    const double dryErr = meastest::relativeRmsError(span<const float>(blockA), span<const float>(in));
    CHECK(dryErr < kDryTolerance);

    // Publish a mix=1 (fully wet) edit "from a non-audio thread". No
    // process() call has consumed it yet.
    fx.setParameter(ParamId{SaturationEffect::kMix}, normFor(SaturationEffect::kMix, 1.0f));

    // Block A's ALREADY-PROCESSED output is untouched by the pending edit --
    // publishing never retroactively rewrites past output.
    const double dryErrAfterPublish = meastest::relativeRmsError(span<const float>(blockA), span<const float>(in));
    CHECK(dryErrAfterPublish == doctest::Approx(dryErr));

    // Block B: the NEXT process() call is where the pending mix edit is
    // consumed -- its output now reflects the fully-wet path.
    std::vector<float> blockB = in;
    {
        float* chans[1] = {blockB.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    }
    const double wetErr = meastest::relativeRmsError(span<const float>(blockB), span<const float>(in));
    CHECK(wetErr > kWetDivergenceThreshold);
}

// ---------------------------------------------------------------------------
// TEST 3: No allocation on process() (FR-010/FR-020, SC-005)
//
// Mirrors no-allocation-test.cpp exactly: prepare() and voicing/quality
// selection run OUTSIDE the sentinel (SaturationCore::setVoicing()/
// setQuality() are documented control-thread-only operations -- e.g.
// ADAAWaveshaper::setShape is not noexcept -- and are excluded from the
// sentinel region in the SaturationCore-level no-allocation test for the
// same reason). Only process() and the continuous scalar setParameter()
// publishes (drive/bias/tone/mix/output -- proven allocation-free at the
// SaturationCore level) run INSIDE the sentinel.
// ---------------------------------------------------------------------------

TEST_CASE("SaturationEffect::process allocates nothing across block sizes (FR-010/FR-020, SC-005)") {
    for (int blockSize : {16, 64, 256, 512}) {
        SaturationEffect fx;
        fx.prepare(ProcessContext{kSampleRateD, blockSize, 1});

        // Establish voicing/quality via setParameter + a process() call
        // OUTSIDE the sentinel: this is where SaturationCore::setVoicing()/
        // setQuality() actually run (control-thread-only, may reconfigure
        // shapers/emphasis coefficients).
        fx.setParameter(ParamId{SaturationEffect::kVoicing},
                         normFor(SaturationEffect::kVoicing,
                                 static_cast<float>(static_cast<int>(SaturationVoicing::softClip))));
        fx.setParameter(ParamId{SaturationEffect::kQuality},
                         normFor(SaturationEffect::kQuality,
                                 static_cast<float>(static_cast<int>(SaturationQuality::adaa))));
        std::vector<float> warm(static_cast<std::size_t>(blockSize), 0.0f);
        {
            float* warmChans[1] = {warm.data()};
            AudioBlock warmBlock(warmChans, 1, blockSize);
            fx.process(warmBlock); // applies the voicing/quality edits, OUTSIDE the sentinel
        }

        std::vector<float> buf(static_cast<std::size_t>(blockSize), 0.1f);
        float* chans[1] = {buf.data()};

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
            // Continuous scalar parameters -- allocation-free at the
            // SaturationCore level (no-allocation-test.cpp); publishing is
            // also allocation-free (a lock-free atomic store, FR-010).
            fx.setParameter(ParamId{SaturationEffect::kDrive}, (i % 2 == 0) ? 0.3f : 0.6f);
            fx.setParameter(ParamId{SaturationEffect::kBias}, (i % 2 == 0) ? 0.4f : 0.5f);
            fx.setParameter(ParamId{SaturationEffect::kTone}, (i % 2 == 0) ? 0.4f : 0.6f);
            fx.setParameter(ParamId{SaturationEffect::kMix}, (i % 2 == 0) ? 0.25f : 0.75f);
            fx.setParameter(ParamId{SaturationEffect::kOutput}, (i % 2 == 0) ? 0.4f : 0.6f);
        }
        const std::size_t allocations = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " allocated ", allocations);
    }
}

// ---------------------------------------------------------------------------
// TEST 3b: No allocation when voicing/quality are changed IN-PROCESS (T016
// hardening pass)
//
// TEST 3 above conservatively excludes voicing/quality edits from the
// sentinel region because, at the SaturationCore level, setVoicing()/
// setQuality() are documented control-thread-only calls. But
// SaturationEffect::applyPending() (called at the top of every process())
// applies ALL pending edits -- including voicing and quality -- on the AUDIO
// thread; that in-process() reconfiguration path (SVF coefficient recompute
// via setFreq/setMode/setRes, plus ADAAWaveshaper::setShape) is real and must
// itself be proven allocation-free, not just throw-free (see the
// hasAntiderivative invariant test in saturation-voicings-test.cpp for the
// throw-safety half of this hardening pass). This test publishes a voicing
// AND quality edit via setParameter() before every process() call, all
// INSIDE the sentinel, cycling through every voicing across both quality
// modes.
// ---------------------------------------------------------------------------

TEST_CASE("SaturationEffect::process allocates nothing when voicing/quality change every block (T016)") {
    constexpr SaturationVoicing kVoicings[] = {
        SaturationVoicing::softClip, SaturationVoicing::tape,
        SaturationVoicing::console, SaturationVoicing::tubePreamp};
    constexpr SaturationQuality kQualities[] = {SaturationQuality::naive, SaturationQuality::adaa};
    constexpr int kBlockSize = 64;

    SaturationEffect fx;
    fx.prepare(ProcessContext{kSampleRateD, kBlockSize, 1});

    std::vector<float> buf(static_cast<std::size_t>(kBlockSize), 0.1f);
    float* chans[1] = {buf.data()};

    AllocationSentinel::reset();
    int i = 0;
    for (int rep = 0; rep < 25; ++rep) {
        for (SaturationVoicing voicing : kVoicings) {
            for (SaturationQuality quality : kQualities) {
                // Publish from "off the audio thread" (a bare call here, same as
                // the rest of this suite); applyPending() consumes both edits at
                // the top of the NEXT process() call, on the audio thread.
                fx.setParameter(ParamId{SaturationEffect::kVoicing},
                                 normFor(SaturationEffect::kVoicing,
                                         static_cast<float>(static_cast<int>(voicing))));
                fx.setParameter(ParamId{SaturationEffect::kQuality},
                                 normFor(SaturationEffect::kQuality,
                                         static_cast<float>(static_cast<int>(quality))));

                AudioBlock block(chans, 1, kBlockSize);
                fx.process(block); // applies voicing+quality reconfiguration IN-PROCESS
                ++i;
            }
        }
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "voicing/quality-in-process sweep (", i, " blocks) allocated ", allocations);
}

// ---------------------------------------------------------------------------
// TEST 4: Mix dry/wet blend law (FR-012, Acceptance Scenario 3, SC-003)
//
// Sweeps kMix and asserts, at every setting, that the effect's output
// matches the documented linear blend law y = mix*wet + (1-mix)*dry
// (saturation-api.md's SaturationCore signal chain, which SaturationEffect
// composes per-channel) via meastest::mixBalance's blendResidual.
// ---------------------------------------------------------------------------

TEST_CASE("mix blends dry input and wet (saturated) path per the documented linear law (FR-012)") {
    constexpr float kDriveDb      = 18.0f;
    constexpr float kBiasVal      = 0.0f;  // isolate the mix law from bias-induced asymmetry
    constexpr float kToneFlat     = 0.0f;
    constexpr float kOutputFlatDb = 0.0f;
    constexpr std::array<float, 5> kMixSweep = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    // Blend-law residual tolerance: predicted[n] = mix*wet[n] + (1-mix)*dry[n]
    // is built from an INDEPENDENTLY captured dry buffer (identity
    // passthrough) and an INDEPENDENTLY captured fully-wet buffer (mix=1) --
    // the composed SaturationCore filter/DC-blocker state evolves
    // identically for any mix value given identical drive/bias/tone/input
    // (mix only scales the FINAL combination per the documented signal
    // chain), so the residual should sit only slightly above float noise.
    // Looser than the raw-SaturationCore fully-dry check (saturation-core-
    // test.cpp kDryTolerance=1e-5) because this path adds a
    // normalize()/denormalize() round-trip through the parameter descriptor
    // table for every parameter, each a small additional float32 rounding
    // step.
    constexpr double kBlendResidualTolerance = 1.0e-3;

    const std::vector<float> dry = meastest::captureSineResponse(
        [](float x) noexcept { return x; }, kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    MonoEffectDriver wetDriver(kDriveDb, kBiasVal, kToneFlat, /*mixPlain=*/1.0f, kOutputFlatDb);
    const std::vector<float> wet = meastest::captureSineResponse(
        [&](float x) noexcept { return wetDriver(x); }, kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    for (float mix : kMixSweep) {
        MonoEffectDriver driver(kDriveDb, kBiasVal, kToneFlat, mix, kOutputFlatDb);
        const std::vector<float> out = meastest::captureSineResponse(
            [&](float x) noexcept { return driver(x); }, kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

        const meastest::MixBalance balance =
            meastest::mixBalance(span<const float>(dry), span<const float>(wet), span<const float>(out),
                                  static_cast<double>(mix));

        INFO("mix=" << mix << " blendResidual=" << balance.blendResidual << " dryRms=" << balance.dryRms
             << " wetRms=" << balance.wetRms << " outRms=" << balance.outRms);
        CHECK(balance.blendResidual < kBlendResidualTolerance);
    }
}

// ---------------------------------------------------------------------------
// TEST 5: User bias -> even-harmonic content + DC-free output (FR-007-
// adjacent, SC-005, Acceptance Scenario 2)
//
// Mirrors saturation-core-test.cpp TEST 5's transient-settling approach and
// waveshaper-harmonics-test.cpp's kEven2ndFloor convention, but drives the
// FULL SaturationEffect wrapper (not the bare SaturationCore) so the
// assertion covers the parameter-descriptor/setParameter path this file
// exists to validate.
// ---------------------------------------------------------------------------

TEST_CASE("user bias introduces even-harmonic content and stays DC-free at the effect level (FR-007-adjacent, SC-005)") {
    constexpr float kDriveDb      = 6.0f;   // moderate: away from the linear region, not deep clip
    constexpr float kBiasVal      = 0.5f;   // matches saturation-core-test.cpp TEST 5's bias magnitude
    constexpr float kToneFlat     = 0.0f;
    constexpr float kOutputFlatDb = 0.0f;

    constexpr int kTransientSamples = 16000;  // matches saturation-voicings-test.cpp's settling window
    constexpr int kMeasureSamples   = 4800;   // 100 integer cycles at 1kHz/48kHz
    constexpr int kNumHarmonics     = 4;

    // 2nd harmonic must clear 1% of the fundamental -- matches waveshaper-
    // harmonics-test.cpp's kEven2ndFloor (one order of magnitude above the
    // ~1e-6 leakage-noise floor documented in measurement-support.h).
    constexpr double kEven2ndFloor = 0.01;

    // 5x looser than the raw-Waveshaper DC test (waveshaper-test.cpp:
    // 0.001) -- matches saturation-core-test.cpp TEST 5's rationale: the
    // wrapper's composed post-de-emphasis + tone-tilt LTI stages are not
    // assumed to have EXACTLY unity DC gain.
    constexpr double kBiasDcTolerance = 0.005;

    SaturationEffect fx;
    fx.prepare(ProcessContext{kSampleRateD, 1, 1});
    fx.setParameter(ParamId{SaturationEffect::kVoicing},
                     normFor(SaturationEffect::kVoicing,
                             static_cast<float>(static_cast<int>(SaturationVoicing::softClip))));
    fx.setParameter(ParamId{SaturationEffect::kQuality},
                     normFor(SaturationEffect::kQuality,
                             static_cast<float>(static_cast<int>(SaturationQuality::adaa))));
    fx.setParameter(ParamId{SaturationEffect::kDrive}, normFor(SaturationEffect::kDrive, kDriveDb));
    fx.setParameter(ParamId{SaturationEffect::kBias}, normFor(SaturationEffect::kBias, kBiasVal));
    fx.setParameter(ParamId{SaturationEffect::kTone}, normFor(SaturationEffect::kTone, kToneFlat));
    fx.setParameter(ParamId{SaturationEffect::kOutput}, normFor(SaturationEffect::kOutput, kOutputFlatDb));
    fx.setParameter(ParamId{SaturationEffect::kMix}, normFor(SaturationEffect::kMix, 1.0f)); // fully wet

    auto step = [&](float x) noexcept -> float {
        float scratch = x;
        float* chans[1] = {&scratch};
        AudioBlock block(chans, 1, 1);
        fx.process(block);
        return scratch;
    };

    // Phase 1: warm up -- let the composed DC-blocker and emphasis-filter
    // transients settle.
    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRateF;
        (void)step(kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    // Phase 2: capture steady-state output.
    std::vector<float> outBuf(static_cast<std::size_t>(kMeasureSamples));
    for (int i = 0; i < kMeasureSamples; ++i) {
        const float t = static_cast<float>(kTransientSamples + i) / kSampleRateF;
        outBuf[static_cast<std::size_t>(i)] =
            step(kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    const meastest::HarmonicSignature sig =
        meastest::harmonicSignature(span<const float>(outBuf), kFundamentalHz, kSampleRateD, kNumHarmonics);
    const double dc = meastest::dcOffset(span<const float>(outBuf));

    INFO("ratio2=" << sig.ratio(2) << " dc=" << dc);
    CHECK(sig.ratio(2) > kEven2ndFloor);
    CHECK(std::abs(dc) < kBiasDcTolerance);
}

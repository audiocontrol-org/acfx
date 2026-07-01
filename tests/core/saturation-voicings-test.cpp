#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "effects/saturation/saturation-core.h"
#include "core/measurement-support.h"

// T011 -- User Story 2 voicing suite: per-voicing harmonic+spectral signature,
// mutual distinguishability, runtime-switch state hygiene, and the bias-is-a-
// user-control contract.
// References: FR-005 (four named voicings), FR-006 (each voicing fixes shape +
// pre-/post-emphasis, producing a documented, mutually-distinguishable
// signature), FR-007 (bias is NOT baked into the voicing), FR-008 (runtime
// voicing switch carries no stale filter/DC state); spec.md User Story 2
// Acceptance Scenarios 1-3; SC-001.
//
// TEST-FIRST NOTE (expected RED until T012): `tape`, `console`, and
// `tubePreamp` in saturation-voicings.h are documented PLACEHOLDERs (real
// numbers land in T012's tuning pass). TEST 1 (well-formedness), TEST 3
// (runtime-switch hygiene) and TEST 4 (bias is a user control) exercise
// mechanisms already implemented (T009/T010) and are expected to be GREEN
// today. TEST 2 (mutual distinguishability) asserts the actual spectral
// separation FR-006 requires between every voicing PAIR; because three of the
// four voicings are still placeholders, TEST 2 is EXPECTED TO BE RED until
// T012 lands real, deliberately-differentiated per-voicing configs.

using namespace acfx;

namespace {

constexpr float  kPi         = 3.14159265358979323846f;
constexpr float  kSampleRate = 48000.0f;
constexpr double kSampleRateD = 48000.0;

// Shared sine-stimulus window: 1 kHz fundamental over 100 integer cycles at
// 48 kHz (matches the anti-leakage window contract in measurement-support.h --
// every harmonic lands exactly on a Goertzel bin).
constexpr double      kFundamentalHz = 1000.0;
constexpr std::size_t kNumSamples    = 4800;  // 100 cycles * 48 samples/cycle
constexpr int         kNumHarmonics  = 6;     // harmonics 1..6, all < Nyquist
constexpr float       kAmplitude     = 0.5f;  // moderate: exercises every shape class
constexpr float       kDrive         = 3.0f;  // fixed across voicings (Scenario 1: "identical drive")
constexpr float       kFixedBias     = 0.0f;  // held at the default; bias is exercised separately in TEST 4
constexpr float       kFullWetMix    = 1.0f;  // isolate the wet (voicing-colored) path

// Settling window: the SLOWEST LTI element in the composed chain sets the
// bound, not the DC-blocker (tau=200 samples) or the per-voicing emphasis
// SVFs (settle in tens of samples at their 10-18kHz cutoffs). That slowest
// element is the shared post-de-emphasis tone-tilt stage, which at tone=0
// runs as a single-pole highpass at kToneFlatHpHz~=5Hz (saturation-core.h) --
// tau = 1/(2*pi*5Hz) ~= 1592 samples @ 48kHz. Empirically confirmed (scratch
// measurement against this file's own runtime-switch scenario): the
// switched-vs-fresh residual crosses below 1e-4 around sample ~3300 and below
// 1e-6 around sample ~14000. 16000 samples (~10*tau) leaves comfortable
// headroom above both the tone-tilt tau and the much-faster DC-blocker/
// emphasis-SVF time constants, so every LTI stage in the chain -- not just
// the fastest one -- has genuinely settled before signatures are compared.
constexpr int kTransientSamples = 16000;

// The four named voicings (saturation-api.md kVoicingLabels order == the enum
// declaration order in saturation-voicings.h).
constexpr std::array<SaturationVoicing, 4> kAllVoicings = {
    SaturationVoicing::softClip,
    SaturationVoicing::tape,
    SaturationVoicing::console,
    SaturationVoicing::tubePreamp,
};

// Returns std::string (not const char*): doctest 2.5.2's INFO/MESSAGE stream
// builder mis-renders a runtime const char* as its truthiness ("1") rather
// than the pointed-to text -- confirmed by isolated repro against doctest
// directly. std::string streams correctly, so labels stay legible in output.
std::string voicingName(SaturationVoicing v) {
    switch (v) {
    case SaturationVoicing::softClip:   return "softClip";
    case SaturationVoicing::tape:       return "tape";
    case SaturationVoicing::console:    return "console";
    case SaturationVoicing::tubePreamp: return "tubePreamp";
    }
    return "unknown";
}

// Build a fully-configured SaturationCore for one voicing. Mirrors
// saturation-core-test.cpp's configureCore() so prepare()/setter ordering
// matches the rest of the suite exactly.
void configureCore(SaturationCore& core,
                    SaturationVoicing voicing,
                    float drive,
                    float bias,
                    float mix,
                    SaturationQuality quality = SaturationQuality::adaa) {
    core.prepare(kSampleRate);
    core.setVoicing(voicing);
    core.setQuality(quality);
    core.setDrive(drive);
    core.setBias(bias);
    core.setTone(0.0f);
    core.setMix(mix);
    core.setOutput(1.0f);
}

// Configure `core` for `voicing`, run kTransientSamples of warm-up to let the
// composed pre-/post-emphasis SVF stages and the shaper's DC-blocker settle
// (transient-settling approach reused from saturation-core-test.cpp TEST 5),
// then capture kNumSamples of steady-state output and reduce it to a
// HarmonicSignature. Phase continues across the warm-up/capture boundary
// (t = (kTransientSamples + i) / sr) to avoid a discontinuity artifact.
meastest::HarmonicSignature captureVoicingSignature(SaturationCore& core, SaturationVoicing voicing) {
    configureCore(core, voicing, kDrive, kFixedBias, kFullWetMix);

    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        (void)core.process(kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    std::vector<float> outBuf(kNumSamples);
    for (std::size_t i = 0; i < kNumSamples; ++i) {
        const float t = static_cast<float>(kTransientSamples + static_cast<int>(i)) / kSampleRate;
        const float x = kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t);
        outBuf[i] = core.process(x);
    }

    return meastest::harmonicSignature(span<const float>(outBuf), kFundamentalHz, kSampleRateD, kNumHarmonics);
}

// ---------------------------------------------------------------------------
// spectralDistance -- local distinctness metric (FR-006 / SC-001)
//
// Max-abs difference between two voicings' harmonic signatures, each
// normalized to its OWN fundamental via HarmonicSignature::ratio(). Comparing
// normalized ratios (rather than raw Goertzel magnitudes) makes the distance
// a measure of spectral SHAPE, not loudness -- gain-compensation (FR-004)
// already targets a bounded loudness band across voicings, so a raw-magnitude
// distance would partly just be re-measuring that band. ratio(1) is always
// 1.0 for both signatures (trivially normalized) and so never drives the max;
// it is included only for symmetry with the 1-based harmonic numbering.
// Non-finite ratios (an effectively-silent fundamental) are skipped so a
// degenerate signature cannot masquerade as "maximally distinct".
// ---------------------------------------------------------------------------
double spectralDistance(const meastest::HarmonicSignature& a, const meastest::HarmonicSignature& b) {
    double maxDiff = 0.0;
    for (int h = 1; h <= kNumHarmonics; ++h) {
        const double ra = a.ratio(h);
        const double rb = b.ratio(h);
        if (!std::isfinite(ra) || !std::isfinite(rb))
            continue;
        const double diff = std::abs(ra - rb);
        if (diff > maxDiff)
            maxDiff = diff;
    }
    return maxDiff;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Per-voicing signature is well-formed (FR-006, SC-001)
//
// For each of the four voicings, drive an IDENTICAL sine (same drive/bias/
// mix/tone) through a SaturationCore configured for that voicing and capture
// its harmonic signature. This does not yet assert cross-voicing distance
// (TEST 2) or an exact documented numeric target (no per-voicing numeric spec
// exists yet -- that is a T012 tuning-pass deliverable); it asserts the
// signature itself is a legitimate measurement: right-sized, finite, and a
// nonzero fundamental. Each signature is also emitted via MESSAGE so the
// "documented signature" is visible in test output for the T012 tuning pass
// to consume.
// ---------------------------------------------------------------------------

TEST_CASE("per-voicing harmonic signature: every voicing yields a finite, well-formed signature (FR-006, SC-001)") {
    for (SaturationVoicing v : kAllVoicings) {
        SaturationCore core;
        const meastest::HarmonicSignature sig = captureVoicingSignature(core, v);

        INFO("voicing=" << voicingName(v));
        CHECK(sig.mags.size() == static_cast<std::size_t>(kNumHarmonics));
        CHECK(std::isfinite(sig.fundamental()));
        CHECK(sig.fundamental() > 0.0);
        for (int h = 1; h <= kNumHarmonics; ++h)
            CHECK(std::isfinite(sig.at(h)));

        MESSAGE("voicing=" << voicingName(v)
                << " fundamental=" << sig.fundamental()
                << " 2nd=" << sig.ratio(2) << " 3rd=" << sig.ratio(3)
                << " 4th=" << sig.ratio(4) << " 5th=" << sig.ratio(5)
                << " 6th=" << sig.ratio(6));
    }
}

// ---------------------------------------------------------------------------
// TEST 2: Mutual distinguishability (FR-006, SC-001, Acceptance Scenario 1)
//
// Every PAIR of the four voicings' harmonic signatures must differ by at
// least kVoicingDistinctMargin under spectralDistance() above.
//
// kVoicingDistinctMargin = 0.02 (2% of fundamental, in at least one harmonic
// ratio): one full order of magnitude above the ~1e-6 leakage-noise floor
// documented in measurement-support.h's window contract, and half the
// detection floors already used elsewhere in this suite (kOddPresentFloor /
// kEven2ndFloor = 0.01 in waveshaper-harmonics-test.cpp) -- registering a
// genuine, audible-scale spectral separation without demanding an
// unrealistically large tuning gap between voicings that are meant to share
// a musical family (all are "saturation").
//
// EXPECTED RED until T012: tape/console/tubePreamp are still placeholder
// configs (saturation-voicings.h). This assertion is the one that turns GREEN
// once T012 lands deliberately-differentiated shape/emphasis pairs.
// ---------------------------------------------------------------------------

TEST_CASE("mutual distinguishability: every voicing pair differs by at least the named margin (FR-006, SC-001)") {
    constexpr double kVoicingDistinctMargin = 0.02;

    std::array<meastest::HarmonicSignature, kAllVoicings.size()> sigs;
    for (std::size_t i = 0; i < kAllVoicings.size(); ++i) {
        SaturationCore core;
        sigs[i] = captureVoicingSignature(core, kAllVoicings[i]);
    }

    for (std::size_t i = 0; i < kAllVoicings.size(); ++i) {
        for (std::size_t j = i + 1; j < kAllVoicings.size(); ++j) {
            const double distance = spectralDistance(sigs[i], sigs[j]);
            INFO("pair=" << voicingName(kAllVoicings[i]) << " vs " << voicingName(kAllVoicings[j])
                 << " distance=" << distance);
            CHECK(distance >= kVoicingDistinctMargin);
        }
    }
}

// ---------------------------------------------------------------------------
// TEST 3: Runtime voicing switch carries no stale state (FR-008, Acceptance
// Scenario 2)
//
// A core driven through voicing A, then switched to voicing B mid-stream via
// setVoicing(), must -- after its filter/DC-blocker transients settle --
// match a FRESH core configured for voicing B from the start. Any residual
// character from A surviving the switch (e.g. an SVF stage left at A's
// cutoff, or DC-blocker history seeded by A's asymmetry) would show up as a
// steady-state mismatch here. Transient-settling approach reused from
// saturation-core-test.cpp TEST 5/6 (kTransientSamples ~= 10*tau).
// ---------------------------------------------------------------------------

TEST_CASE("runtime voicing switch: switching from A to B carries no stale filter/DC state (FR-008)") {
    // Float-arithmetic floor: once both cores are driven by identical inputs
    // with identical (post-switch) parameters, their outputs are a
    // deterministic function of state + input; any survived difference above
    // a few ULPs indicates residual state from the pre-switch voicing.
    constexpr double kNoStaleStateTolerance = 1.0e-4;

    // Core "switched": starts on softClip, is driven for a while, then
    // switched to tape mid-stream.
    SaturationCore switched;
    configureCore(switched, SaturationVoicing::softClip, kDrive, kFixedBias, kFullWetMix);
    for (int i = 0; i < 500; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        (void)switched.process(0.3f * std::sin(2.0f * kPi * 440.0f * t));
    }
    switched.setVoicing(SaturationVoicing::tape);

    // Core "fresh": configured for tape from the start -- the ground truth
    // for "no residual softClip character".
    SaturationCore fresh;
    configureCore(fresh, SaturationVoicing::tape, kDrive, kFixedBias, kFullWetMix);

    // Drive both with an IDENTICAL post-switch signal and let transients settle.
    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float x = kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t);
        (void)switched.process(x);
        (void)fresh.process(x);
    }

    // Steady-state outputs must match sample-for-sample.
    for (int i = 0; i < 200; ++i) {
        const float t = static_cast<float>(kTransientSamples + i) / kSampleRate;
        const float x = kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t);
        const float outSwitched = switched.process(x);
        const float outFresh    = fresh.process(x);
        CHECK(std::abs(outSwitched - outFresh) < kNoStaleStateTolerance);
    }
}

// ---------------------------------------------------------------------------
// TEST 4a: bias is a USER control, not baked per-voicing -- observable
// behavior (FR-007, Acceptance Scenario 3)
//
// At a FIXED voicing, two otherwise-identical configs differing only in
// setBias() must produce different output: bias is realized live by
// SaturationCore/Waveshaper, not fixed by the voicing table.
// ---------------------------------------------------------------------------

TEST_CASE("bias is a user control: at a fixed voicing, differing bias produces different output (FR-007)") {
    // Two orders of magnitude above the float-precision floor established by
    // saturation-core-test.cpp TEST 2 (kDryTolerance = 1e-5): a genuine "bias
    // changed the sound" check, not a coincidental rounding difference.
    constexpr double kBiasEffectThreshold = 0.01;

    SaturationCore coreNoBias;
    configureCore(coreNoBias, SaturationVoicing::tubePreamp, kDrive, /*bias=*/0.0f, kFullWetMix);
    SaturationCore coreBiased;
    configureCore(coreBiased, SaturationVoicing::tubePreamp, kDrive, /*bias=*/0.4f, kFullWetMix);

    const std::vector<float> outNoBias = meastest::captureSineResponse(
        [&](float x) noexcept { return coreNoBias.process(x); },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);
    const std::vector<float> outBiased = meastest::captureSineResponse(
        [&](float x) noexcept { return coreBiased.process(x); },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    const double err = meastest::relativeRmsError(span<const float>(outBiased), span<const float>(outNoBias));
    CHECK(err > kBiasEffectThreshold);
}

// ---------------------------------------------------------------------------
// TEST 4b: VoicingConfig structurally carries no bias field (design Decision
// 5, FR-007)
//
// VoicingConfig (saturation-voicings.h) is documented to bake `shape` plus
// `pre`/`post` emphasis ONLY -- bias is a separate USER control realized by
// SaturationCore::setBias()/Waveshaper::setBias(), never baked into the
// per-voicing table (data-model.md Decision 5; FR-007's "MUST NOT bake the
// bias control"). Structured-binding decomposition of an aggregate requires
// an EXACT member-count match: if a `bias` field were ever appended to
// VoicingConfig, the three-identifier binding below would fail to compile
// with a member-count mismatch, surfacing that regression as a build break
// rather than a silent baked-bias drift.
// ---------------------------------------------------------------------------

TEST_CASE("VoicingConfig structurally excludes bias (design Decision 5, FR-007)") {
    constexpr VoicingConfig cfg = voicingConfig(SaturationVoicing::softClip);
    const auto& [shape, pre, post] = cfg;  // fails to compile if a 4th field (e.g. bias) is added
    (void)shape;
    (void)pre;
    (void)post;
    CHECK(true);
}

// ---------------------------------------------------------------------------
// T016 -- ADAA throw-safety invariant (hardening the SaturationEffect
// parameter-apply path, saturation-effect.h). SaturationEffect::applyPending()
// applies voicing/quality changes INSIDE process() (the audio thread), which
// calls SaturationCore::setVoicing() -> configureShapers() ->
// ADAAWaveshaper::setShape() (adaa-waveshaper.h). setShape() is NOT noexcept:
// it throws std::invalid_argument for a shape with no analytic antiderivative
// (shape::hasAntiderivative() == false, e.g. Shape::biasedAsym). For the
// in-process() voicing-switch path to be provably throw-free, EVERY voicing's
// shape must be ADAA-safe (hasAntiderivative() == true). This test locks that
// invariant at the source: if a future voicing is ever given an
// antiderivative-less shape, THIS test fails at the unit level, rather than
// the failure surfacing as a std::invalid_argument thrown out of
// SaturationEffect::process() on the audio thread.
// ---------------------------------------------------------------------------

TEST_CASE("every voicing's shape has an antiderivative (ADAA-safe, T016 in-process throw-safety invariant)") {
    constexpr SaturationVoicing kVoicings[] = {
        SaturationVoicing::softClip, SaturationVoicing::tape,
        SaturationVoicing::console, SaturationVoicing::tubePreamp};

    for (SaturationVoicing voicing : kVoicings) {
        const VoicingConfig cfg = voicingConfig(voicing);
        INFO("voicing=" << static_cast<int>(voicing) << " shape=" << static_cast<int>(cfg.shape));
        CHECK(shape::hasAntiderivative(cfg.shape));
    }
}

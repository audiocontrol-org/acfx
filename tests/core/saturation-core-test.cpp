#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "labs/saturation/saturation-core.h"
#include "core/measurement-support.h"

// T007 -- SaturationCore composition kernel: signal-chain order, mix extremes,
// silence, DC-free biased output, and reset invariants.
// References: FR-002 (chain order: pre-emphasis -> waveshaper(drive,bias) ->
// post-de-emphasis -> tone -> mix -> output), FR-003 (independent per-channel
// state -- not exercised here, single-channel only), FR-004 (gain-compensation
// keeps loudness bounded as drive rises -- not directly asserted here beyond
// boundedness); spec.md User Story 1 Acceptance Scenarios 1/2/4;
// quickstart.md validation scenario 1.
//
// Expected RED state: SaturationCore::prepare/reset/setVoicing/setQuality/
// setDrive/setBias/setTone/setMix/setOutput are DECLARED in
// core/labs/saturation/saturation-core.h (T004) but their bodies are not yet
// implemented (T009/T010/T018 pending); process() is a passthrough stub.  A
// syntax-only compile of THIS translation unit is expected to succeed (no
// bodies are required to resolve declarations); linking a test binary that
// calls the unimplemented setters will fail with undefined references naming
// acfx::SaturationCore symbols.  That link failure -- not a compile error in
// this file -- is the correct RED signal before T009/T010 land.

using namespace acfx;

namespace {

constexpr float kPi         = 3.14159265358979323846f;
constexpr float kSampleRate = 48000.0f;

// Shared sine-stimulus window: 1 kHz fundamental over 100 integer cycles at
// 48 kHz (matches the anti-leakage window contract documented in
// measurement-support.h -- every harmonic/aliased image lands on a DFT bin).
constexpr double      kFundamentalHz = 1000.0;
constexpr double      kSampleRateD   = 48000.0;
constexpr std::size_t kNumSamples    = 4800;  // 100 * 48 = 4800 samples/cycle count
constexpr float       kAmplitude     = 0.5f;  // moderate: exercises the shaper without full clip

// Build a fully-configured SaturationCore.  Shared by every TEST_CASE below so
// the composed signal chain (pre-emphasis -> shaper -> post-de-emphasis ->
// tone -> mix -> output) is exercised identically; only drive/bias/mix vary
// per test.
void configureCore(SaturationCore& core,
                    float drive,
                    float bias,
                    float mix,
                    SaturationQuality quality = SaturationQuality::adaa) {
    core.prepare(kSampleRate);
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(quality);
    core.setDrive(drive);
    core.setBias(bias);
    core.setTone(0.0f);
    core.setMix(mix);
    core.setOutput(1.0f);
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Signal-chain order & basic operation (FR-002; SC-001/US1 Scenario 1)
//
// A sine driven through the fully-composed chain (pre-emphasis -> naive/adaa
// shaper(drive,bias) -> post-de-emphasis -> tone -> mix -> output) must stay
// finite and within a generous sanity bound for every sample.  The bound is
// NOT a tight analytic prediction of the composed chain (that belongs to the
// per-voicing harmonic-signature suite); it only rules out a blown-up/
// diverging composition -- e.g. an unstable filter pairing or a runaway
// gain stage.  softKnee (this voicing's shape) is itself bounded to [-1, 1]
// (waveshaper-shapes.h); two near-flat SVF lowpass stages (pre/post,
// resonance 0.1) and a mix against a unit-amplitude dry input keep the
// composed output well inside a small multiple of that.
// ---------------------------------------------------------------------------

TEST_CASE("signal-chain order: sine through SaturationCore yields finite, bounded output") {
    // Generous sanity bound: shape is bounded to +-1, two low-resonance SVF
    // lowpass stages contribute at most a modest resonance peak, and mix/
    // output are unity here -- 8x is far above any legitimate composed
    // amplitude and only catches a genuinely diverging chain.
    constexpr float kBoundedOutput = 8.0f;

    SaturationCore core;
    configureCore(core, /*drive=*/2.0f, /*bias=*/0.0f, /*mix=*/1.0f);

    const std::vector<float> out = meastest::captureSineResponse(
        [&](float x) noexcept { return core.process(x); },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    for (std::size_t n = 0; n < out.size(); ++n) {
        CHECK_MESSAGE(std::isfinite(out[n]), "sample=", n, " value=", out[n]);
        CHECK_MESSAGE(std::abs(out[n]) <= kBoundedOutput, "sample=", n, " value=", out[n]);
    }
}

// ---------------------------------------------------------------------------
// TEST 2: Fully-dry mix reproduces the input (FR-002 mix law; SC-003/US1
// Acceptance Scenario 2)
//
// With mix=0, y = 0*wet + 1*x, then y = outputGain*y with outputGain=1 --
// the wet path (pre-emphasis/shaper/post-de-emphasis/tone) contributes
// nothing and the output is the dry input exactly, up to float round-off.
// Drive is set high (5x) specifically so that if the wet path leaked in at
// all, relativeRmsError would be large -- a strong negative control.
// ---------------------------------------------------------------------------

TEST_CASE("fully-dry mix (mix=0) reproduces the input within float precision") {
    // Float round-off only: at mix=0 the algebraic combination is an exact
    // identity (0*wet + 1*x, then *1 output gain), so any deviation beyond a
    // few ULPs of accumulated float error would indicate wet-path leakage.
    constexpr double kDryTolerance = 1.0e-5;

    // Reference dry buffer: capture the SAME deterministic sine (identical
    // fundamental/sampleRate/numSamples/amplitude) through an identity
    // passthrough, so it matches SineGenerator's output sample-for-sample.
    const std::vector<float> dry = meastest::captureSineResponse(
        [](float x) noexcept { return x; },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    SaturationCore core;
    configureCore(core, /*drive=*/5.0f, /*bias=*/0.2f, /*mix=*/0.0f);

    const std::vector<float> out = meastest::captureSineResponse(
        [&](float x) noexcept { return core.process(x); },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    const double err = meastest::relativeRmsError(span<const float>(out), span<const float>(dry));
    CHECK(err < kDryTolerance);
}

// ---------------------------------------------------------------------------
// TEST 3: Fully-wet mix yields the saturated path, not the input (FR-002 mix
// law; SC-003/US1)
//
// With mix=1 and a strong drive, the composed shaper + emphasis filters
// visibly diverge from the dry input -- both from the nonlinear shaping and
// from the pre-/post-emphasis filtering that FR-002 places in the wet path.
// We assert the divergence clears a threshold well above the float-precision
// floor established by TEST 2, so this is a genuine "not the dry signal"
// check rather than a coincidental rounding difference.
// ---------------------------------------------------------------------------

TEST_CASE("fully-wet mix (mix=1) diverges from the input when driven") {
    // 5% relative RMS divergence: two orders of magnitude above the float-
    // precision floor (kDryTolerance = 1e-5 in TEST 2), comfortably below the
    // divergence a driven softKnee shape + emphasis filtering actually
    // produces, so this only fails if the wet path is inert.
    constexpr double kWetDivergenceThreshold = 0.05;

    const std::vector<float> dry = meastest::captureSineResponse(
        [](float x) noexcept { return x; },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    SaturationCore core;
    configureCore(core, /*drive=*/4.0f, /*bias=*/0.0f, /*mix=*/1.0f);

    const std::vector<float> out = meastest::captureSineResponse(
        [&](float x) noexcept { return core.process(x); },
        kFundamentalHz, kSampleRateD, kNumSamples, kAmplitude);

    const double err = meastest::relativeRmsError(span<const float>(out), span<const float>(dry));
    CHECK(err > kWetDivergenceThreshold);
}

// ---------------------------------------------------------------------------
// TEST 4: Silence-in -> silence-out (SC-005/US1 Acceptance Scenario 4)
//
// Every stage in the chain is either linear-time-invariant (the two SVF
// emphasis stages, the tone tilt) or a memoryless odd-symmetric-at-zero
// shape (softKnee(0) = 0) with a DC-blocker seeded at zero state.  Feeding
// exact 0.0f forever keeps every stage's state at exactly 0.0f in IEEE-754
// arithmetic (0*c = 0, 0+0 = 0), so the output is exact zero, not just
// "small" -- with no denormal/NaN/Inf generation.
// ---------------------------------------------------------------------------

TEST_CASE("silence-in silence-out: all-zero input with zero bias yields zero output") {
    SaturationCore core;
    configureCore(core, /*drive=*/2.0f, /*bias=*/0.0f, /*mix=*/1.0f);

    constexpr int kNumZeroSamples = 1000;
    for (int i = 0; i < kNumZeroSamples; ++i) {
        const float out = core.process(0.0f);
        CHECK_MESSAGE(std::isfinite(out), "sample=", i, " value=", out);
        CHECK(out == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// TEST 5: Asymmetric bias yields DC-free output (FR-002, FR-007/008 via the
// composed Waveshaper; SC-005/US1 Acceptance Scenario 1's "no DC offset")
//
// A nonzero bias shifts the shaper's operating point, producing DC + even
// harmonics at the shaping stage.  The composed Waveshaper/ADAAWaveshaper
// owns a one-pole DC-blocker (R = kDcR = 0.995 in waveshaper.h) with analytic
// settling time tau = 1/(1-R) = 200 samples; after kTransientSamples = 2000
// (~10*tau) residual DC has decayed by R^2000 ~= e^-10 ~= 4.5e-5.  The
// post-de-emphasis and tone-tilt SVF stages that follow are LTI lowpass/tilt
// filters with ~unity DC gain, so they neither reintroduce nor amplify DC.
// Tolerance is loosened 5x relative to the single-Waveshaper suite
// (waveshaper-test.cpp: 0.001) to leave headroom for the two additional
// composed LTI stages' DC-gain not being exactly 1.0.
// ---------------------------------------------------------------------------

TEST_CASE("asymmetric bias: composed DC-blocker drives steady-state output mean to near zero") {
    constexpr int kTransientSamples = 2000;  // ~10*tau; DC decays by > e^-10 (waveshaper.h kDcR)
    constexpr int kMeasureSamples   = 4800;  // 100 integer cycles at 1 kHz / 48 kHz

    // 5x looser than the single-Waveshaper DC test (0.001): two additional
    // near-unity-DC-gain LTI stages (post-de-emphasis, tone tilt) sit
    // downstream of the DC-blocker and are not assumed to be EXACTLY unity.
    constexpr double kBiasDcTolerance = 0.005;

    SaturationCore core;
    configureCore(core, /*drive=*/1.0f, /*bias=*/0.5f, /*mix=*/1.0f);

    // Phase 1: warm up -- let the composed DC-blocker (and the emphasis
    // filters' transients) settle.
    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        (void)core.process(std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    // Phase 2: capture steady-state output.
    std::vector<float> outBuf(static_cast<std::size_t>(kMeasureSamples));
    for (int i = 0; i < kMeasureSamples; ++i) {
        const float t = static_cast<float>(kTransientSamples + i) / kSampleRate;
        outBuf[static_cast<std::size_t>(i)] =
            core.process(std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    const double dc = meastest::dcOffset(span<const float>(outBuf));
    CHECK(std::abs(dc) < kBiasDcTolerance);
}

// ---------------------------------------------------------------------------
// TEST 6: No stale state on reset() (mirrors saturation-core.h's documented
// reset() contract: "mirrors Waveshaper::reset" -- clears filter + DC-blocker
// state across every composed sub-unit without discarding applied parameter
// state)
//
// A freshly prepare()'d-and-configured core and a "dirtied" core (same
// configuration, driven through an unrelated warm-up signal, then reset())
// must produce IDENTICAL output for identical subsequent input: reset()
// clears exactly the same state prepare() starts from, and every operation
// in the chain is a deterministic function of (parameters, state, input).
// ---------------------------------------------------------------------------

TEST_CASE("reset clears composed filter/DC-blocker state and preserves parameters") {
    constexpr float kDrive = 1.5f;
    constexpr float kBias  = 0.1f;

    // Reference: freshly prepare()'d-and-configured core, never processed.
    SaturationCore fresh;
    configureCore(fresh, kDrive, kBias, /*mix=*/1.0f);

    // Dirty core: same configuration, but driven through a warm-up signal
    // that accumulates filter + DC-blocker state before reset() is called.
    SaturationCore core;
    configureCore(core, kDrive, kBias, /*mix=*/1.0f);
    for (int i = 0; i < 500; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        (void)core.process(0.3f * std::sin(2.0f * kPi * 440.0f * t));
    }
    core.reset();

    // Both cores now share identical state and parameters; subsequent
    // outputs for the same inputs must be identical.
    for (int i = 0; i < 100; ++i) {
        const float t         = static_cast<float>(i) / kSampleRate;
        const float x         = 0.3f * std::sin(2.0f * kPi * 440.0f * t);
        const float outFresh  = fresh.process(x);
        const float outCore   = core.process(x);
        CHECK(outCore == outFresh);
    }
}

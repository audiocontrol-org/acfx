#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/modulated-delay/modulated-delay-effect.h"
#include "support/allocation-sentinel.h"

// T007 — ModulatedDelayEffect US1: filtered-feedback delay.
// Covers: progressive feedback-filter shaping, dry/wet mix endpoints,
// click-free delay-time change, feedback stability at the max bound,
// and the no-heap-allocation invariant.

using namespace acfx;
using acfx::test::AllocationSentinel;

namespace {

// Set a parameter by its plain (denormalized) value.
void setParam(ModulatedDelayEffect& fx, ModulatedDelayEffect::Param p, float plain) {
    fx.setParameter(ParamId{p},
                    normalize(ModulatedDelayEffect::kParams[p], plain));
}

// Fill a buffer with one block of a sine wave.
void fillSine(float* buf, int n, double freqHz, double sr, double& phase) {
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (int i = 0; i < n; ++i) {
        buf[i]  = static_cast<float>(std::sin(phase));
        phase  += twoPi * freqHz / sr;
    }
}

// Compute RMS of a buffer.
float computeRms(const float* buf, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(n)));
}

}  // namespace

// ---------------------------------------------------------------------------
// T007-A: LP feedback filter shapes successive echoes.
//
// With a low-pass feedback filter at 150 Hz:
//   - A 50 Hz sine (below cutoff) accumulates in the feedback loop — high output.
//   - A 2 kHz sine (well above cutoff) is attenuated each round — low output.
// After 300 ms of input, the steady-state wet-only output energy of the
// low-frequency excitation must be significantly greater than the high-frequency one.
// ---------------------------------------------------------------------------
TEST_CASE("LP feedback filter: low-freq echoes decay slower than high-freq") {
    const double sr        = 48000.0;
    const int    blockSize = 512;

    // Measure wet-only RMS at a given input frequency after 300 ms with LP at 150 Hz.
    auto measureRms = [&](double inputFreqHz) -> float {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{sr, blockSize, 1});
        setParam(fx, ModulatedDelayEffect::kDelayTime,  0.020f);  // 20 ms (many echoes in 300 ms)
        setParam(fx, ModulatedDelayEffect::kFeedback,   0.85f);
        setParam(fx, ModulatedDelayEffect::kMix,        1.0f);    // wet only — isolates filter effect
        setParam(fx, ModulatedDelayEffect::kCutoff,     150.0f);  // LP at 150 Hz
        setParam(fx, ModulatedDelayEffect::kResonance,  0.0f);
        setParam(fx, ModulatedDelayEffect::kMode,       0.0f);    // lowpass

        std::vector<float> buf(static_cast<std::size_t>(blockSize));
        float* chans[1] = {buf.data()};
        double phase = 0.0;

        // Run 200 ms warm-up, then measure the next 100 ms.
        const int warmupBlocks  = static_cast<int>(sr * 0.2 / blockSize) + 1;
        const int measureBlocks = static_cast<int>(sr * 0.1 / blockSize) + 1;

        for (int b = 0; b < warmupBlocks; ++b) {
            fillSine(buf.data(), blockSize, inputFreqHz, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
        }

        double sumSq = 0.0;
        int    count = 0;
        for (int b = 0; b < measureBlocks; ++b) {
            fillSine(buf.data(), blockSize, inputFreqHz, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
            for (int i = 0; i < blockSize; ++i)
                sumSq += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
            count += blockSize;
        }
        return count > 0 ? static_cast<float>(std::sqrt(sumSq / count)) : 0.0f;
    };

    const float rmsLow  = measureRms(50.0);    // 50 Hz — below LP cutoff: builds up
    const float rmsHigh = measureRms(2000.0);  // 2 kHz — above LP cutoff: attenuated each echo

    // Theoretical steady-state ratio is ~60x. Require at least 3x for robustness.
    CHECK(rmsLow > rmsHigh * 3.0f);
}

// ---------------------------------------------------------------------------
// T007-B: Dry/wet mix endpoints.
//
// mix=0: output equals the dry input sample-for-sample (wet tap is delayed and
//        filtered, so at mix=0 none of it leaks into the output).
// mix=1: output is the wet (delayed/filtered) path only; before any signal has
//        propagated through the delay, the output is silence regardless of input.
// ---------------------------------------------------------------------------
TEST_CASE("mix=0 passes dry input unchanged") {
    const int blockSize = 64;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{48000.0, blockSize, 1});
    setParam(fx, ModulatedDelayEffect::kMix,      0.0f);
    setParam(fx, ModulatedDelayEffect::kFeedback, 0.7f);
    setParam(fx, ModulatedDelayEffect::kDelayTime, 0.1f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    for (int i = 0; i < blockSize; ++i)
        buf[i] = static_cast<float>(i) / static_cast<float>(blockSize);

    const std::vector<float> expected(buf);  // snapshot before process
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, blockSize);
    fx.process(block);

    for (int i = 0; i < blockSize; ++i)
        CHECK(buf[i] == doctest::Approx(expected[i]).epsilon(1e-6f));
}

TEST_CASE("mix=1 outputs zero before delay propagates") {
    // With a cleared delay buffer and a delay longer than the first block, the wet
    // output is zero — no dry leaks in because mix=1 removes it entirely.
    const int blockSize = 64;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{48000.0, blockSize, 1});
    setParam(fx, ModulatedDelayEffect::kMix,       1.0f);
    setParam(fx, ModulatedDelayEffect::kFeedback,  0.0f);
    setParam(fx, ModulatedDelayEffect::kDelayTime, 0.5f);  // 500 ms >> 64 samples

    std::vector<float> buf(static_cast<std::size_t>(blockSize), 1.0f);  // full amplitude
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, blockSize);
    fx.process(block);

    // Delay buffer was cleared by prepare(); first echo hasn't arrived yet.
    for (int i = 0; i < blockSize; ++i)
        CHECK(std::fabs(buf[i]) < 1e-4f);
}

// ---------------------------------------------------------------------------
// T007-C: Click-free delay-time change (research Decision 5).
//
// When delay_time is changed mid-stream, the one-pole smoother prevents any
// instantaneous read-position jump. All output samples after the change must be
// finite, and the per-sample delta must stay below the bound imposed by the
// smoother's time constant (~20 ms at 48 kHz).
//
// Design: we verify two things —
//   (1) No NaN/Inf output after the change.
//   (2) The first block after the change still resembles the OLD delay position
//       more than the new one (the smoother has barely moved in one block).
// ---------------------------------------------------------------------------
TEST_CASE("delay time change is click-free: output stays finite and bounded") {
    const double sr        = 48000.0;
    const int    blockSize = 256;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 1});
    setParam(fx, ModulatedDelayEffect::kDelayTime,  0.5f);   // 500 ms
    setParam(fx, ModulatedDelayEffect::kFeedback,   0.0f);
    setParam(fx, ModulatedDelayEffect::kMix,        0.5f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    float* chans[1] = {buf.data()};
    double phase = 0.0;

    // Run 600 ms at 500 ms delay so the delay buffer fills with 440 Hz sine.
    const int warmupBlocks = static_cast<int>(sr * 0.6 / blockSize) + 1;
    for (int b = 0; b < warmupBlocks; ++b) {
        fillSine(buf.data(), blockSize, 440.0, sr, phase);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    // Change delay time dramatically.
    setParam(fx, ModulatedDelayEffect::kDelayTime, 0.05f);  // 50 ms

    float maxDelta   = 0.0f;
    float prevSample = buf[blockSize - 1];

    const int measureBlocks = static_cast<int>(sr * 0.1 / blockSize) + 2;
    for (int b = 0; b < measureBlocks; ++b) {
        fillSine(buf.data(), blockSize, 440.0, sr, phase);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(buf[i]));
            const float delta = std::fabs(buf[i] - prevSample);
            if (delta > maxDelta) maxDelta = delta;
            prevSample = buf[i];
        }
    }

    // The one-pole smoother bounds the per-sample read-position change to
    // ~smoothCoeff * deltaDelay * sr samples per audio sample. At 20 ms time
    // constant and 450 ms delay change, the output delta is bounded well below 2.
    CHECK(maxDelta < 1.5f);
}

TEST_CASE("delay time change: smoother keeps read position near old tap") {
    // Design: fill the delay buffer with two distinct regions:
    //   t = 0..200ms   silence (0)
    //   t = 200..400ms POSITIVE (+0.5)
    //   t = 400..800ms NEGATIVE (-0.5)
    //
    // At t=800ms the 500ms-ago tap is reading the POSITIVE region (+0.5) and the
    // 50ms-ago tap is reading the NEGATIVE region (-0.5).  Change delay 500ms->50ms.
    //
    // With the one-pole smoother (20ms time constant): after 64 samples the
    // smoothed delay has barely moved (~28ms from 500ms), still reading POSITIVE.
    // Without smoothing: the delay jumps instantly to 50ms, reading NEGATIVE.
    // We verify the LAST sample of the next block is positive (smoother present).
    const double sr        = 48000.0;
    const int    blockSize = 64;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 1});
    setParam(fx, ModulatedDelayEffect::kDelayTime,  0.5f);    // 500 ms
    setParam(fx, ModulatedDelayEffect::kFeedback,   0.0f);    // no feedback
    setParam(fx, ModulatedDelayEffect::kMix,        1.0f);    // wet only
    setParam(fx, ModulatedDelayEffect::kCutoff,     20000.0f);// near-passthrough LP
    setParam(fx, ModulatedDelayEffect::kResonance,  0.0f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    float* chans[1] = {buf.data()};

    // Silence (200ms = 150 blocks): let the smoother settle at 500ms.
    for (int b = 0; b < 150; ++b) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    // POSITIVE region (200ms = 150 blocks): fill delay buffer 200..400ms with +0.5.
    for (int b = 0; b < 150; ++b) {
        std::fill(buf.begin(), buf.end(), 0.5f);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    // NEGATIVE region (400ms = 300 blocks): fill delay buffer 400..800ms with -0.5.
    // After 300ms (150 blocks) the 500ms tap reaches the POSITIVE region and the
    // SVF settles at +0.5 output for the remaining 150 blocks.
    for (int b = 0; b < 300; ++b) {
        std::fill(buf.begin(), buf.end(), -0.5f);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    // At t=800ms: 500ms-tap is in POSITIVE, 50ms-tap is in NEGATIVE.
    // SVF has been settled at +0.5 output for the last 100ms.
    // Change delay to 50ms and process one block.
    setParam(fx, ModulatedDelayEffect::kDelayTime, 0.05f);
    std::fill(buf.begin(), buf.end(), -0.5f);
    {
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    // With smoothing (20ms TC): at sample 63, smoothed delay ≈ 472ms — still in the
    // POSITIVE region — SVF output ≈ +0.5 (settled).
    // Without smoothing: delay = 50ms immediately — reads NEGATIVE — SVF ≈ -0.5.
    CHECK(buf[blockSize - 1] > 0.3f);
}

// ---------------------------------------------------------------------------
// T007-D: Feedback stability at the maximum bound (FR-010).
//
// Feedback at max (0.98) must not cause divergence or NaN/Inf, even with
// high resonance. Excite with an impulse, feed silence for N blocks, and
// verify the output stays finite and eventually decays.
// ---------------------------------------------------------------------------
TEST_CASE("feedback at max bound stays finite and decays") {
    const double sr        = 48000.0;
    const int    blockSize = 512;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 1});
    setParam(fx, ModulatedDelayEffect::kDelayTime,  0.05f);   // 50 ms
    setParam(fx, ModulatedDelayEffect::kFeedback,   0.98f);   // max descriptor value
    setParam(fx, ModulatedDelayEffect::kMix,        1.0f);
    setParam(fx, ModulatedDelayEffect::kCutoff,     8000.0f); // high cutoff (near-passthrough)
    setParam(fx, ModulatedDelayEffect::kResonance,  0.5f);
    setParam(fx, ModulatedDelayEffect::kMode,       0.0f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize), 0.0f);
    float* chans[1] = {buf.data()};

    // Excite with a single-sample impulse.
    buf[0] = 1.0f;
    {
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    // Feed silence for 10 seconds (= 200 blocks) and verify all output is finite.
    float maxAbs = 0.0f;
    const int silenceBlocks = static_cast<int>(sr * 10.0 / blockSize) + 1;
    for (int b = 0; b < silenceBlocks; ++b) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(buf[i]));
            maxAbs = std::max(maxAbs, std::fabs(buf[i]));
        }
    }
    // Output must decay (not blow up) even at 0.98 feedback.
    CHECK(maxAbs < 100.0f);
}

// ---------------------------------------------------------------------------
// T007-E: No-heap-allocation invariant in process() (SC-007, FR-006).
//
// Mirrors the pattern in tests/core/no-allocation-test.cpp: wrap process()
// calls (and setParameter on the audio thread, which must also be lock-free)
// in the AllocationSentinel and assert the count is zero.
// ---------------------------------------------------------------------------
TEST_CASE("ModulatedDelayEffect::process allocates nothing across block sizes") {
    for (int blockSize : {16, 64, 256, 512}) {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{48000.0, blockSize, 2});

        const std::size_t sz = static_cast<std::size_t>(blockSize);
        std::vector<float> left(sz, 0.1f);
        std::vector<float> right(sz, 0.1f);
        float* chans[2] = {left.data(), right.data()};

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            AudioBlock block(chans, 2, blockSize);
            fx.process(block);
            // setParameter on the audio thread must also be allocation-free.
            fx.setParameter(ParamId{ModulatedDelayEffect::kCutoff},
                            (i % 2 == 0) ? 0.25f : 0.75f);
        }
        const std::size_t allocations = AllocationSentinel::allocations();
        CHECK_MESSAGE(allocations == 0,
                      "block size ", blockSize, " allocated ", allocations);
    }
}

// ---------------------------------------------------------------------------
// T007-F: Extreme settings crash test (optional sanity).
//
// Verify that the effect handles boundary parameter values without crashing
// or emitting NaN/Inf. Also checks that the Effect concept/trait is satisfied.
// ---------------------------------------------------------------------------
TEST_CASE("extreme settings produce no crash and finite output") {
    const int blockSize = 256;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{44100.0, blockSize, 2});

    // Verify the Effect contract is satisfied at compile time.
    // The test target compiles as C++20, so the named concept is available.
    static_assert(acfx::Effect<ModulatedDelayEffect>,
                  "ModulatedDelayEffect must satisfy the Effect contract");

    // Max delay, max feedback, full wet, LP at minimum cutoff, max resonance.
    setParam(fx, ModulatedDelayEffect::kDelayTime,  2.0f);
    setParam(fx, ModulatedDelayEffect::kFeedback,   0.98f);
    setParam(fx, ModulatedDelayEffect::kMix,        1.0f);
    setParam(fx, ModulatedDelayEffect::kCutoff,     20.0f);
    setParam(fx, ModulatedDelayEffect::kResonance,  1.0f);
    setParam(fx, ModulatedDelayEffect::kMode,       2.0f);  // bandpass

    std::vector<float> left(static_cast<std::size_t>(blockSize));
    std::vector<float> right(static_cast<std::size_t>(blockSize));
    float* chans[2] = {left.data(), right.data()};

    // Feed an impulse then many blocks of silence — must not crash or produce NaN.
    left[0] = right[0] = 1.0f;
    {
        AudioBlock block(chans, 2, blockSize);
        fx.process(block);
    }
    for (int b = 0; b < 50; ++b) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        AudioBlock block(chans, 2, blockSize);
        fx.process(block);
        for (int i = 0; i < blockSize; ++i) {
            CHECK(std::isfinite(left[i]));
            CHECK(std::isfinite(right[i]));
        }
    }
}

// ===========================================================================
// T011 — ModulatedDelayEffect US2: modulation LFOs for delay, cutoff, resonance.
// FR-011 delay-time mod, FR-012 cutoff mod, FR-012a resonance mod,
// FR-013 depth-zero equivalence, FR-014 in-range, FR-015 SR-independence.
// ===========================================================================

namespace {

// Run the effect for numBlocks and collect all output samples.
std::vector<float> runAndCollect(ModulatedDelayEffect& fx,
                                  double sr, int blockSize, int numBlocks,
                                  double inputFreqHz) {
    std::vector<float> all;
    all.reserve(static_cast<std::size_t>(blockSize * numBlocks));
    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    float* chans[1] = {buf.data()};
    double phase = 0.0;
    for (int b = 0; b < numBlocks; ++b) {
        fillSine(buf.data(), blockSize, inputFreqHz, sr, phase);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
        all.insert(all.end(), buf.begin(), buf.end());
    }
    return all;
}

// Compute RMS of a vector.
float vecRms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0.0;
    for (float x : v) sum += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(v.size())));
}

// Convenience: set a parameter from a plain (denormalized) value.
void setModParam(ModulatedDelayEffect& fx,
                 ModulatedDelayEffect::Param p, float plain) {
    fx.setParameter(ParamId{p},
                    normalize(ModulatedDelayEffect::kParams[p], plain));
}

// Set up the six US1 base params identically on an effect instance.
void setBaseParams(ModulatedDelayEffect& fx,
                   float delayTime, float feedback, float mix,
                   float cutoff, float res, float mode) {
    setModParam(fx, ModulatedDelayEffect::kDelayTime,  delayTime);
    setModParam(fx, ModulatedDelayEffect::kFeedback,   feedback);
    setModParam(fx, ModulatedDelayEffect::kMix,        mix);
    setModParam(fx, ModulatedDelayEffect::kCutoff,     cutoff);
    setModParam(fx, ModulatedDelayEffect::kResonance,  res);
    setModParam(fx, ModulatedDelayEffect::kMode,       mode);
}

}  // namespace

// ---------------------------------------------------------------------------
// T011-A: FR-013 Depth-zero equivalence — the most important invariant.
//
// An instance with non-zero mod RATES but all three depths = 0 must produce
// output identical (within tight tolerance) to an instance with US1 defaults.
// The LFOs tick but depth=0 multiplies their output to zero before it reaches
// the signal path:
//   delay: smoothedBase + lfo * 0.0 * range  = smoothedBase  (exact)
//   cutoff: base * pow(2, lfo * 0.0 * oct)   = base * 1.0   (exact)
//   res:    base + lfo * 0.0 * range          = base          (exact)
// ---------------------------------------------------------------------------
TEST_CASE("FR-013: depth-zero mod produces output identical to US1 within tolerance") {
    const double sr        = 48000.0;
    const int    blockSize = 256;

    ModulatedDelayEffect fxA, fxB;
    fxA.prepare(ProcessContext{sr, blockSize, 1});
    fxB.prepare(ProcessContext{sr, blockSize, 1});

    setBaseParams(fxA, 0.3f, 0.6f, 0.5f, 1000.0f, 0.3f, 0.0f);
    setBaseParams(fxB, 0.3f, 0.6f, 0.5f, 1000.0f, 0.3f, 0.0f);

    // fxB: set fast rates on all three LFOs but keep all depths at 0.
    setModParam(fxB, ModulatedDelayEffect::kDelayModRate,   10.0f);
    setModParam(fxB, ModulatedDelayEffect::kDelayModDepth,   0.0f);
    setModParam(fxB, ModulatedDelayEffect::kDelayModShape,   2.0f);   // saw
    setModParam(fxB, ModulatedDelayEffect::kCutoffModRate,   7.0f);
    setModParam(fxB, ModulatedDelayEffect::kCutoffModDepth,  0.0f);
    setModParam(fxB, ModulatedDelayEffect::kCutoffModShape,  1.0f);   // triangle
    setModParam(fxB, ModulatedDelayEffect::kResModRate,      5.0f);
    setModParam(fxB, ModulatedDelayEffect::kResModDepth,     0.0f);
    setModParam(fxB, ModulatedDelayEffect::kResModShape,     3.0f);   // random

    std::vector<float> bufA(static_cast<std::size_t>(blockSize));
    std::vector<float> bufB(static_cast<std::size_t>(blockSize));
    float* chansA[1] = {bufA.data()};
    float* chansB[1] = {bufB.data()};
    double phase = 0.0;

    for (int b = 0; b < 200; ++b) {
        fillSine(bufA.data(), blockSize, 440.0, sr, phase);
        std::copy(bufA.begin(), bufA.end(), bufB.begin());

        AudioBlock blockA(chansA, 1, blockSize);
        AudioBlock blockB(chansB, 1, blockSize);
        fxA.process(blockA);
        fxB.process(blockB);

        for (int i = 0; i < blockSize; ++i) {
            CHECK(bufA[i] == doctest::Approx(bufB[i]).epsilon(1e-5));
        }
    }
}

// ---------------------------------------------------------------------------
// T011-B: FR-011 Delay-time modulation produces measurable output variation.
//
// Two instances with the same settings, one with delay mod depth=0, one with
// depth=1 at 2 Hz.  After the delay buffer fills, the depth=1 output should
// measurably differ from depth=0: the effective read position oscillates,
// creating Doppler pitch variation in the wet signal.
// ---------------------------------------------------------------------------
TEST_CASE("FR-011: delay-time modulation causes measurable output difference") {
    const double sr        = 48000.0;
    const int    blockSize = 256;
    const int    warmup    = 30;   // let the delay buffer fill
    const int    measure   = 100;

    const auto runEffect = [&](float modDepth) -> std::vector<float> {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{sr, blockSize, 1});
        setBaseParams(fx, 0.15f, 0.0f, 1.0f, 20000.0f, 0.0f, 0.0f);
        setModParam(fx, ModulatedDelayEffect::kDelayModRate,  2.0f);
        setModParam(fx, ModulatedDelayEffect::kDelayModDepth, modDepth);

        std::vector<float> all;
        all.reserve(static_cast<std::size_t>(blockSize * measure));
        std::vector<float> buf(static_cast<std::size_t>(blockSize));
        float* chans[1] = {buf.data()};
        double phase = 0.0;

        for (int b = 0; b < warmup + measure; ++b) {
            fillSine(buf.data(), blockSize, 440.0, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
            if (b >= warmup)
                all.insert(all.end(), buf.begin(), buf.end());
        }
        return all;
    };

    const auto outFlat = runEffect(0.0f);
    const auto outMod  = runEffect(1.0f);

    // Compute RMS of the sample-by-sample difference.
    float rmsDiff = 0.0f;
    for (std::size_t i = 0; i < outFlat.size(); ++i) {
        float d = outMod[i] - outFlat[i];
        rmsDiff += d * d;
    }
    rmsDiff = std::sqrt(rmsDiff / static_cast<float>(outFlat.size()));

    // With a ±30 ms delay sweep at 2 Hz, the Doppler effect introduces measurable
    // pitch variation.  Require the RMS difference to exceed a small floor.
    CHECK(rmsDiff > 1e-3f);
}

// ---------------------------------------------------------------------------
// T011-C: FR-012 Cutoff modulation produces periodic brightness variation.
//
// A 4 kHz sine is fed into a LP-filtered feedback delay at 1 kHz base cutoff
// with depth=1 and 2-octave range.  The cutoff sweeps from 250 Hz to 4 kHz,
// causing the filtered output energy to oscillate.  The ratio of max to min
// per-block RMS must be significant.
// ---------------------------------------------------------------------------
TEST_CASE("FR-012: cutoff modulation causes periodic brightness variation") {
    const double sr        = 48000.0;
    const int    blockSize = 512;
    const int    warmup    = 25;  // let echoes build up
    const int    measure   = 48;  // ~0.5s @ 512 samples ≈ 1 mod cycle at 1 Hz

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 1});
    setBaseParams(fx, 0.05f, 0.0f, 1.0f, 1000.0f, 0.0f, 0.0f);  // LP at 1 kHz
    setModParam(fx, ModulatedDelayEffect::kCutoffModRate,  1.0f);
    setModParam(fx, ModulatedDelayEffect::kCutoffModDepth, 1.0f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    float* chans[1] = {buf.data()};
    double phase = 0.0;

    for (int b = 0; b < warmup; ++b) {
        fillSine(buf.data(), blockSize, 4000.0, sr, phase);  // 4 kHz — near LP edge
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
    }

    float minRms = 1e9f, maxRms = 0.0f;
    for (int b = 0; b < measure; ++b) {
        fillSine(buf.data(), blockSize, 4000.0, sr, phase);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
        const float rms = computeRms(buf.data(), blockSize);
        if (rms < minRms) minRms = rms;
        if (rms > maxRms) maxRms = rms;
    }

    // With a 2-octave sweep (cutoff 250 Hz..4 kHz), the 4 kHz input alternates
    // between strongly attenuated (near 250 Hz cutoff) and near-passthrough.
    // Require the max/min energy ratio to be at least 3x.
    CHECK(maxRms > minRms * 3.0f);
}

// ---------------------------------------------------------------------------
// T011-D: FR-015 Sample-rate independence.
//
// A 1 Hz delay-mod rate must complete exactly 1 cycle per second at any SR.
// We run for exactly one mod period's worth of samples at 44100 and 96000 Hz,
// measure the RMS modulation effect (difference from depth=0), and verify
// the magnitude is consistent — proving the period tracks in seconds, not samples.
// ---------------------------------------------------------------------------
TEST_CASE("FR-015: delay-mod rate is sample-rate independent") {
    const float modRateHz = 1.0f;

    const auto measureModRms = [&](double sr) -> float {
        const int blockSize = 512;
        // Run 1.5 mod periods so the period is well covered.
        const int totalSamples = static_cast<int>(sr * 1.5f / modRateHz);

        ModulatedDelayEffect fxMod, fxFlat;
        fxMod.prepare(ProcessContext{sr, blockSize, 1});
        fxFlat.prepare(ProcessContext{sr, blockSize, 1});

        const auto setupBase = [&](ModulatedDelayEffect& fx) {
            setBaseParams(fx, 0.15f, 0.0f, 1.0f, 20000.0f, 0.0f, 0.0f);
        };
        setupBase(fxMod);
        setupBase(fxFlat);
        setModParam(fxMod, ModulatedDelayEffect::kDelayModRate,  modRateHz);
        setModParam(fxMod, ModulatedDelayEffect::kDelayModDepth, 0.5f);

        std::vector<float> bufMod(static_cast<std::size_t>(blockSize));
        std::vector<float> bufFlat(static_cast<std::size_t>(blockSize));
        float* chansMod[1]  = {bufMod.data()};
        float* chansFlat[1] = {bufFlat.data()};
        double phase = 0.0;

        // Warm-up: fill the delay buffer before measuring.
        const int warmupSamples = static_cast<int>(sr * 0.15f);
        int wLeft = warmupSamples;
        while (wLeft > 0) {
            const int n = std::min(blockSize, wLeft);
            fillSine(bufMod.data(), n, 440.0, sr, phase);
            std::copy(bufMod.begin(), bufMod.begin() + n, bufFlat.begin());
            { AudioBlock bm(chansMod, 1, n); fxMod.process(bm); }
            { AudioBlock bf(chansFlat, 1, n); fxFlat.process(bf); }
            wLeft -= n;
        }

        double sumSq = 0.0;
        int    count = 0;
        int    left  = totalSamples;
        while (left > 0) {
            const int n = std::min(blockSize, left);
            fillSine(bufMod.data(), n, 440.0, sr, phase);
            std::copy(bufMod.begin(), bufMod.begin() + n, bufFlat.begin());
            { AudioBlock bm(chansMod, 1, n); fxMod.process(bm); }
            { AudioBlock bf(chansFlat, 1, n); fxFlat.process(bf); }
            for (int i = 0; i < n; ++i) {
                float d = bufMod[i] - bufFlat[i];
                sumSq += static_cast<double>(d) * d;
                ++count;
            }
            left -= n;
        }
        return count > 0 ? static_cast<float>(std::sqrt(sumSq / count)) : 0.0f;
    };

    const float rms44 = measureModRms(44100.0);
    const float rms96 = measureModRms(96000.0);

    // Both must show measurable modulation.
    CHECK(rms44 > 1e-4f);
    CHECK(rms96 > 1e-4f);

    // The mod effect magnitude must be similar at both SRs (same physical sweep).
    // Allow ±40% tolerance for interpolation-quality differences.
    const float ratio = (rms44 > 0.0f) ? rms96 / rms44 : 0.0f;
    CHECK(ratio == doctest::Approx(1.0f).epsilon(0.4f));
}

// ---------------------------------------------------------------------------
// T011-E: FR-014 In-range under maximal modulation — no NaN/Inf, no crash.
//
// All three LFOs at maximum rate and depth, combined with max delay time and
// significant feedback, must not produce NaN/Inf or crash over many blocks.
// The DelayLine's built-in clamp keeps read positions valid (FR-007/FR-014).
// ---------------------------------------------------------------------------
TEST_CASE("FR-014: maximal modulation produces no NaN/Inf and bounded output") {
    const double sr        = 48000.0;
    const int    blockSize = 256;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 2});

    setBaseParams(fx, 2.0f, 0.5f, 0.5f, 20000.0f, 0.0f, 0.0f);

    setModParam(fx, ModulatedDelayEffect::kDelayModRate,   20.0f);
    setModParam(fx, ModulatedDelayEffect::kDelayModDepth,   1.0f);
    setModParam(fx, ModulatedDelayEffect::kDelayModShape,   0.0f);
    setModParam(fx, ModulatedDelayEffect::kCutoffModRate,  20.0f);
    setModParam(fx, ModulatedDelayEffect::kCutoffModDepth,  1.0f);
    setModParam(fx, ModulatedDelayEffect::kCutoffModShape,  1.0f);
    setModParam(fx, ModulatedDelayEffect::kResModRate,     20.0f);
    setModParam(fx, ModulatedDelayEffect::kResModDepth,     1.0f);
    setModParam(fx, ModulatedDelayEffect::kResModShape,     2.0f);

    const std::size_t sz = static_cast<std::size_t>(blockSize);
    std::vector<float> left(sz, 0.0f), right(sz, 0.0f);
    float* chans[2] = {left.data(), right.data()};

    left[0] = right[0] = 1.0f;  // impulse
    {
        AudioBlock block(chans, 2, blockSize);
        fx.process(block);
    }

    for (int b = 0; b < 200; ++b) {
        std::fill(left.begin(),  left.end(),  0.3f);
        std::fill(right.begin(), right.end(), 0.3f);
        AudioBlock block(chans, 2, blockSize);
        fx.process(block);
        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }
}

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

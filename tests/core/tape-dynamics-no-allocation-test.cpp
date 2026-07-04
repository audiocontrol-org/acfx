#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/tape-dynamics/tape-dynamics-effect.h"
#include "support/allocation-sentinel.h"

// T023 -- the no-heap-allocation / no-locks-in-process() invariant (US5,
// SC-007) for TapeDynamicsEffect::process(), mirroring compressor-
// no-allocation-test.cpp / program-dependent-saturation-no-allocation-test.cpp
// EXACTLY: prepare() and every setParameter() publish + the ONE process() call
// that consumes them (applyPending() only runs at the top of process(), per
// tape-dynamics-effect.h's documented thread-ownership) all run OUTSIDE the
// shared thread-local AllocationSentinel scope -- that priming call is
// control-thread configuration, not the measured region. Only the REPEATED
// process() calls after that (with no further parameter edits in between) are
// asserted allocation-free. Split into its own file (not folded into
// no-allocation-test.cpp) per the Constitution VII ~300-500 line module
// budget, matching the sibling per-feature no-allocation suites.

using namespace acfx;
using acfx::test::AllocationSentinel;

namespace {

// Convert a desired PLAIN-units value for a TapeDynamicsEffect parameter into
// the normalized 0..1 value setParameter() expects, via the shared descriptor
// table -- mirrors tape-dynamics-effect-test.cpp's normFor /
// compressor-no-allocation-test.cpp's paramNorm. Never hand-roll the
// normalize math here: the descriptor table (min/max/skew) is the single
// source of truth (FR-019).
float normFor(TapeDynamicsEffect::Param p, float plainValue) {
    return normalize(TapeDynamicsEffect::kParams[p], plainValue);
}

} // namespace

// T023 (SC-007) -- the no-allocation invariant across the full configuration
// matrix named in the task: all 3 oversampling factors (2x/4x/8x), all 3
// solvers (rk2/rk4/newtonRaphson), trim on/off (trim.enabled/attack/release/
// amount are all real per-core setters -- see applyTrimEnabled()/
// applyTrimAttack()/applyTrimRelease()/applyTrimAmount(), T026), crossed with
// block sizes spanning 1-sample blocks through a large block. A fresh
// TapeDynamicsEffect is built per row (mirrors the SvfEffect/Compressor/PDS
// pattern of preparing per block size) so prepare()'s maxBlockSize always
// matches the block actually driven through it.
TEST_CASE("TapeDynamicsEffect::process allocates nothing across oversampling x solver x "
          "trim x block-size matrix (SC-007)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kNumChannels = 2;
    constexpr int kIterations = 32;

    constexpr int kOversamplingIndices[] = {0, 1, 2}; // 2x, 4x, 8x (kTapeDynamicsOversamplingLabels order)
    constexpr int kSolverIndices[] = {0, 1, 2};        // rk2, rk4, newtonRaphson (kTapeDynamicsSolverLabels order)
    constexpr bool kTrimValues[] = {false, true};
    constexpr int kBlockSizes[] = {1, 37, 2048};       // 1-sample, small/odd, and a large block

    for (int osIndex : kOversamplingIndices) {
        for (int solverIndex : kSolverIndices) {
            for (bool trimOn : kTrimValues) {
                for (int blockSize : kBlockSizes) {
                    INFO("oversamplingIndex=" << osIndex << " solverIndex=" << solverIndex
                                               << " trimOn=" << trimOn << " blockSize=" << blockSize);

                    TapeDynamicsEffect fx;
                    fx.prepare(
                        ProcessContext{static_cast<double>(kSampleRate), blockSize, kNumChannels});

                    // Publish every axis under test (control thread -- may allocate).
                    fx.setParameter(ParamId{TapeDynamicsEffect::kDrive},
                                    normFor(TapeDynamicsEffect::kDrive, 12.0f));
                    fx.setParameter(
                        ParamId{TapeDynamicsEffect::kOversampling},
                        normFor(TapeDynamicsEffect::kOversampling, static_cast<float>(osIndex)));
                    fx.setParameter(
                        ParamId{TapeDynamicsEffect::kSolver},
                        normFor(TapeDynamicsEffect::kSolver, static_cast<float>(solverIndex)));
                    fx.setParameter(ParamId{TapeDynamicsEffect::kTrimEnabled},
                                    normFor(TapeDynamicsEffect::kTrimEnabled, trimOn ? 1.0f : 0.0f));
                    // trim.attack/release/amount -- real per-core setters as of T026.
                    fx.setParameter(ParamId{TapeDynamicsEffect::kTrimAttack},
                                    normFor(TapeDynamicsEffect::kTrimAttack, 0.01f));
                    fx.setParameter(ParamId{TapeDynamicsEffect::kTrimRelease},
                                    normFor(TapeDynamicsEffect::kTrimRelease, 0.1f));
                    fx.setParameter(ParamId{TapeDynamicsEffect::kTrimAmount},
                                    normFor(TapeDynamicsEffect::kTrimAmount, trimOn ? 0.7f : 0.0f));

                    // The priming process() call that consumes every pending edit above
                    // via applyPending() -- control-thread configuration, OUTSIDE the
                    // sentinel scope (mirrors compressor-no-allocation-test.cpp /
                    // program-dependent-saturation-no-allocation-test.cpp).
                    std::vector<float> primeLeft(static_cast<std::size_t>(blockSize), 0.1f);
                    std::vector<float> primeRight(static_cast<std::size_t>(blockSize), 0.1f);
                    float* primeChans[kNumChannels] = {primeLeft.data(), primeRight.data()};
                    AudioBlock primeBlock(primeChans, kNumChannels, blockSize);
                    fx.process(primeBlock);

                    std::vector<float> left(static_cast<std::size_t>(blockSize));
                    std::vector<float> right(static_cast<std::size_t>(blockSize));
                    for (int i = 0; i < blockSize; ++i) {
                        const float t = static_cast<float>(i) / kSampleRate;
                        const float s = 0.7f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
                        left[static_cast<std::size_t>(i)] = s;
                        right[static_cast<std::size_t>(i)] = 0.9f * s; // slightly different per-channel level
                    }
                    float* chans[kNumChannels] = {left.data(), right.data()};

                    bool allFinite = true;
                    AllocationSentinel::reset();
                    for (int i = 0; i < kIterations; ++i) {
                        AudioBlock block(chans, kNumChannels, blockSize);
                        fx.process(block);
                        for (int ch = 0; ch < kNumChannels; ++ch)
                            for (int n = 0; n < blockSize; ++n)
                                if (!std::isfinite(chans[ch][n]))
                                    allFinite = false;
                    }
                    const std::size_t allocations = AllocationSentinel::allocations();

                    CHECK_MESSAGE(allocations == 0, "allocated ", allocations);
                    CHECK_MESSAGE(allFinite, "process() produced a non-finite sample");
                }
            }
        }
    }
}

// T023 (SC-007) -- block-size invariance / continuity: the SAME stimulus, run
// through otherwise-identical fresh TapeDynamicsEffect instances, must reach
// the same output whether it is delivered as 4096 one-sample blocks, as
// 37-sample chunks, or as a single 4096-sample block. processBlock() is
// already a plain per-sample loop over persistent per-channel core state (no
// block-scoped buffering), so this is expected to hold to floating-point
// exactness -- a real bug that introduced any block-scoped state (e.g. a
// stale per-block cache not reset between calls) would show up here as a
// mismatch precisely at chunk boundaries. Also directly covers the task's
// "produces correct output across block sizes including 1-sample and large
// blocks" requirement, complementing the pure allocation-count matrix above.
TEST_CASE("TapeDynamicsEffect::process is block-size-invariant across 1-sample, "
          "small, and one large block (finite + continuous, SC-007)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kTotalSamples = 4096;
    constexpr float kDriveDb = 12.0f;

    // A full-scale swept sine so both the JA hysteresis memory and the
    // oversampling/solver paths see a genuinely time-varying signal (not just
    // a fixed tone) across the whole run.
    std::vector<float> stimulus(static_cast<std::size_t>(kTotalSamples));
    for (int n = 0; n < kTotalSamples; ++n) {
        const float t = static_cast<float>(n) / kSampleRate;
        const float freqHz =
            200.0f + 800.0f * (static_cast<float>(n) / static_cast<float>(kTotalSamples));
        stimulus[static_cast<std::size_t>(n)] = 0.8f * std::sin(2.0f * 3.14159265f * freqHz * t);
    }

    auto runChunked = [&](int chunkSize) {
        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, kDriveDb));

        // Priming call (a single dummy silent sample) that consumes the pending
        // drive edit via applyPending() BEFORE any of the actual stimulus is
        // processed, so every chunking below sees the identical drive setting
        // (and identical one-sample-of-silence perturbation) from sample 0 --
        // control-thread configuration, mirrors the priming-call idiom above.
        float primeSample = 0.0f;
        float* primeChans[1] = {&primeSample};
        AudioBlock primeBlock(primeChans, 1, 1);
        fx.process(primeBlock);

        std::vector<float> buf = stimulus; // fresh copy, processed in place chunk by chunk
        for (int offset = 0; offset < kTotalSamples; offset += chunkSize) {
            const int n = std::min(chunkSize, kTotalSamples - offset);
            float* chans[1] = {buf.data() + offset};
            AudioBlock block(chans, 1, n);
            fx.process(block);
        }
        return buf;
    };

    const std::vector<float> oneSampleBlocks = runChunked(1);
    const std::vector<float> smallChunks = runChunked(37);
    const std::vector<float> oneBigBlock = runChunked(kTotalSamples);

    for (int n = 0; n < kTotalSamples; ++n) {
        INFO("n=" << n);
        REQUIRE(std::isfinite(oneSampleBlocks[static_cast<std::size_t>(n)]));
        REQUIRE(std::isfinite(smallChunks[static_cast<std::size_t>(n)]));
        REQUIRE(std::isfinite(oneBigBlock[static_cast<std::size_t>(n)]));
        CHECK(oneSampleBlocks[static_cast<std::size_t>(n)] ==
              doctest::Approx(oneBigBlock[static_cast<std::size_t>(n)]).epsilon(1e-5));
        CHECK(smallChunks[static_cast<std::size_t>(n)] ==
              doctest::Approx(oneBigBlock[static_cast<std::size_t>(n)]).epsilon(1e-5));
    }
}

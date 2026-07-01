#include <doctest/doctest.h>

#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include "effects/saturation/saturation-core.h"
#include "processor-node/processor-node.h"
#include "support/allocation-sentinel.h"

// T018 — the no-heap-allocation-in-process() invariant (FR-014). Uses the
// thread-local allocation sentinel across several block sizes, on both the bare
// SvfEffect and the EffectNode<SvfEffect> host boundary. The count is captured
// out of the measured region before any assertion macro runs.

using namespace acfx;
using acfx::test::AllocationSentinel;

TEST_CASE("SvfEffect::process allocates nothing across block sizes") {
    for (int blockSize : {16, 64, 256, 512}) {
        SvfEffect fx;
        fx.prepare(ProcessContext{48000.0, blockSize, 2});

        std::vector<float> left(static_cast<std::size_t>(blockSize), 0.1f);
        std::vector<float> right(static_cast<std::size_t>(blockSize), 0.1f);
        float* chans[2] = {left.data(), right.data()};

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            AudioBlock block(chans, 2, blockSize);
            fx.process(block);
            // parameter changes on the audio thread must also be allocation-free
            fx.setParameter(ParamId{SvfEffect::kCutoff}, (i % 2 == 0) ? 0.25f : 0.75f);
        }
        const std::size_t allocations = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " allocated ", allocations);
    }
}

TEST_CASE("EffectNode<SvfEffect>::processBlock allocates nothing") {
    EffectNode<SvfEffect> node;
    const int blockSize = 256;
    node.prepare(ProcessContext{48000.0, blockSize, 2});

    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.1f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.1f);
    float* chans[2] = {left.data(), right.data()};

    AllocationSentinel::reset();
    for (int i = 0; i < 100; ++i) {
        AudioBlock block(chans, 2, blockSize);
        node.processBlock(block);
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "EffectNode processBlock allocated ", allocations);
}

// T010 — the no-heap-allocation-in-process() invariant for SaturationCore
// (FR-018/FR-020, spec.md SC-005). Mirrors the SvfEffect case above: prepare()
// and voicing/quality selection reconfigure filter coefficients (and, for
// ADAAWaveshaper::setShape, are a documented control-thread-only call that may
// throw) so they run OUTSIDE the sentinel scope. Only the true audio-path
// calls — process() and the lightweight scalar setters (setDrive/setBias/
// setTone/setMix/setOutput, all noexcept and allocation-free per
// saturation-core.h/waveshaper.h/adaa-waveshaper.h/svf-primitive.h) — run
// INSIDE the sentinel, across all four voicings and both quality modes.
TEST_CASE("SaturationCore::process allocates nothing across voicings and quality modes") {
    constexpr SaturationVoicing kVoicings[] = {
        SaturationVoicing::softClip, SaturationVoicing::tape,
        SaturationVoicing::console, SaturationVoicing::tubePreamp};
    constexpr SaturationQuality kQualities[] = {SaturationQuality::naive, SaturationQuality::adaa};

    for (SaturationVoicing voicing : kVoicings) {
        for (SaturationQuality quality : kQualities) {
            SaturationCore core;
            core.prepare(48000.0f);   // control thread: filter/table setup, may allocate
            core.setVoicing(voicing); // control thread: reconfigures shapers + emphasis SVFs
            core.setQuality(quality); // control thread: selects naive/adaa path

            AllocationSentinel::reset();
            for (int i = 0; i < 200; ++i) {
                const float x = (i % 2 == 0) ? 0.3f : -0.3f;
                (void)core.process(x);
                // audio-thread-callable scalar parameter setters (FR-018/020)
                core.setDrive(1.0f + 0.01f * static_cast<float>(i % 5));
                core.setBias(0.05f * static_cast<float>((i % 3) - 1));
                core.setTone(0.01f * static_cast<float>((i % 7) - 3));
                core.setMix(0.5f + 0.001f * static_cast<float>(i % 2));
                core.setOutput(1.0f - 0.001f * static_cast<float>(i % 2));
            }
            const std::size_t allocations = AllocationSentinel::allocations();

            CHECK_MESSAGE(allocations == 0, "voicing=", static_cast<int>(voicing),
                          " quality=", static_cast<int>(quality), " allocated ", allocations);
        }
    }
}

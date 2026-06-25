#include <doctest/doctest.h>

#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
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

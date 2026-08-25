#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "sample-format.h"
#include "support/allocation-sentinel.h"

// Sample-format no-allocation contract (SF4, FR-030, SC-009/SC-010): neither
// conversion function nor the payload-length helper may allocate, because they
// run on the audio path. Split out of nucleo-sample-format-test.cpp to stay
// within the per-file line budget, mirroring compressor-no-allocation-test.cpp
// and program-dependent-saturation-no-allocation-test.cpp.

using namespace acfx::nucleo;
using acfx::test::AllocationSentinel;

TEST_CASE("SF4: deinterleaveToFloat allocates nothing") {
    // Prepare buffers outside the sentinel region.
    std::vector<std::int16_t> input(static_cast<std::size_t>(2 * kBlockFrames));
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::int16_t>(i & 0xFFFF);
    }

    std::vector<float> left(static_cast<std::size_t>(kBlockFrames));
    std::vector<float> right(static_cast<std::size_t>(kBlockFrames));

    AllocationSentinel::reset();
    for (int iter = 0; iter < 100; ++iter) {
        float* dst[2] = {left.data(), right.data()};
        deinterleaveToFloat(input.data(), dst, kBlockFrames);
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "deinterleaveToFloat allocated ", allocations);
}

TEST_CASE("SF4: interleaveToInt16 allocates nothing") {
    std::vector<float> left(static_cast<std::size_t>(kBlockFrames), 0.5f);
    std::vector<float> right(static_cast<std::size_t>(kBlockFrames), 0.3f);
    std::vector<std::int16_t> output(static_cast<std::size_t>(2 * kBlockFrames));

    AllocationSentinel::reset();
    for (int iter = 0; iter < 100; ++iter) {
        const float* src[2] = {left.data(), right.data()};
        interleaveToInt16(src, output.data(), kBlockFrames);
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "interleaveToInt16 allocated ", allocations);
}

TEST_CASE("SF4: payloadToFrameCount allocates nothing") {
    AllocationSentinel::reset();
    for (int byteCount = 0; byteCount <= 500; ++byteCount) {
        (void)payloadToFrameCount(byteCount);
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "payloadToFrameCount allocated ", allocations);
}

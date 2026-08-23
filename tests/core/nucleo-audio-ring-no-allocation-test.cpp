#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "support/allocation-sentinel.h"

// Audio ring no-allocation contract (AR5, FR-030, SC-009/SC-010): the AudioRing
// class may not allocate on the audio path, because it holds a fixed member
// array and operates on a single-producer / single-consumer basis. Split out
// of nucleo-audio-ring-test.cpp to stay within the per-file line budget,
// mirroring nucleo-sample-format-no-allocation-test.cpp.

using namespace acfx::nucleo;
using acfx::test::AllocationSentinel;

TEST_CASE("AR5: write allocates nothing") {
    AudioRing<48> ring(24);
    std::vector<float> src_l(kBlockFrames, 0.5f);
    std::vector<float> src_r(kBlockFrames, 0.5f);

    AllocationSentinel::reset();
    for (int iter = 0; iter < 100; ++iter) {
        const float* src[2] = {src_l.data(), src_r.data()};
        ring.write(src, kBlockFrames);
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "write allocated ", allocations);
}

TEST_CASE("AR5: read allocates nothing") {
    AudioRing<48> ring(24);
    std::vector<float> src_l(kBlockFrames, 0.5f);
    std::vector<float> src_r(kBlockFrames, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    // Pre-fill the ring to avoid empty reads.
    for (int i = 0; i < 10; ++i) {
        ring.write(src, 4);
    }

    std::vector<float> dst_l(kBlockFrames);
    std::vector<float> dst_r(kBlockFrames);

    AllocationSentinel::reset();
    for (int iter = 0; iter < 100; ++iter) {
        float* dst[2] = {dst_l.data(), dst_r.data()};
        ring.read(dst, 4);
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "read allocated ", allocations);
}

TEST_CASE("AR5: occupancy allocates nothing") {
    AudioRing<48> ring(24);
    std::vector<float> src_l(kBlockFrames, 0.5f);
    std::vector<float> src_r(kBlockFrames, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 10);

    AllocationSentinel::reset();
    for (int iter = 0; iter < 1000; ++iter) {
        (void)ring.occupancy();
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "occupancy allocated ", allocations);
}

TEST_CASE("AR5: capacity allocates nothing") {
    AudioRing<48> ring(24);

    AllocationSentinel::reset();
    for (int iter = 0; iter < 1000; ++iter) {
        (void)ring.capacity();
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "capacity allocated ", allocations);
}

TEST_CASE("AR5: reset allocates nothing") {
    AudioRing<48> ring(24);
    std::vector<float> src_l(kBlockFrames, 0.5f);
    std::vector<float> src_r(kBlockFrames, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    AllocationSentinel::reset();
    for (int iter = 0; iter < 100; ++iter) {
        ring.write(src, 4);
        ring.reset();
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "reset allocated ", allocations);
}

TEST_CASE("AR5: mixed write/read sequence allocates nothing") {
    AudioRing<48> ring(24);
    std::vector<float> src_l(kBlockFrames, 0.5f);
    std::vector<float> src_r(kBlockFrames, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    std::vector<float> dst_l(kBlockFrames);
    std::vector<float> dst_r(kBlockFrames);

    AllocationSentinel::reset();
    for (int iter = 0; iter < 50; ++iter) {
        ring.write(src, 6);
        float* dst[2] = {dst_l.data(), dst_r.data()};
        ring.read(dst, 3);
        (void)ring.occupancy();
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "mixed sequence allocated ", allocations);
}

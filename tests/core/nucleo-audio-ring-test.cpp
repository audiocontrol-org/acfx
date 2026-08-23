#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"

// Audio ring storage and data contracts (FR-030, FR-031, FR-032).
// Tests cover:
// AR1 — Bounded occupancy: occupancy() is always in [0, capacity()] on every path.
// AR2 — Defined underflow: read() always writes exactly `frames` frames; shortfall
//       is silence, and the count of substituted frames is returned.
// AR3 — Defined overflow: write() drops the OLDEST frames, never the newest, and
//       returns the count dropped (verified by proving newest data survives).
// AR8 — No re-centring: ring never drops/duplicates frames to steer occupancy back
//       toward a target; drift stays visible.
// State-machine contracts (AR7, AR9) and lifecycle tests (FR-051-054) are covered
// in nucleo-audio-ring-lifecycle-test.cpp.

using namespace acfx::nucleo;

namespace {

// Helper to create a test buffer with known values for frame tracking.
// Each frame contains a pattern: [channel0_value, channel1_value, ...]
void fillTestBuffer(float* const* channels, int frames, float baseValue) noexcept {
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            channels[channel][frame] = baseValue + (channel * 0.1f);
        }
    }
}

// Helper to check if a buffer region contains silence (all zeros).
bool isSilence(const float* buf, int frames) noexcept {
    for (int i = 0; i < frames; ++i) {
        if (buf[i] != 0.0f) {
            return false;
        }
    }
    return true;
}

// Helper to check if buffers match expected values.
bool buffersMatch(float* const* actual, const float* const* expected, int frames) noexcept {
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            if (actual[channel][frame] != expected[channel][frame]) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

// ============================================================================
// AR1: Bounded occupancy
// ============================================================================

TEST_CASE("AR1: occupancy is 0 immediately after construction") {
    AudioRing<48> ring(24);
    CHECK(ring.occupancy() == 0);
    CHECK(ring.occupancy() >= 0);
    CHECK(ring.occupancy() <= ring.capacity());
}

TEST_CASE("AR1: occupancy never exceeds capacity") {
    AudioRing<48> ring(24);
    std::vector<float> left(kBlockFrames, 0.5f);
    std::vector<float> right(kBlockFrames, 0.3f);

    // Write multiple times and verify occupancy stays bounded.
    for (int iter = 0; iter < 10; ++iter) {
        const float* src[2] = {left.data(), right.data()};
        ring.write(src, kBlockFrames);
        CHECK(ring.occupancy() >= 0);
        CHECK(ring.occupancy() <= ring.capacity());
    }
}

TEST_CASE("AR1: occupancy increases with writes, bounded by capacity") {
    AudioRing<48> ring(24);
    std::vector<float> left(24, 0.5f);
    std::vector<float> right(24, 0.3f);

    const float* src[2] = {left.data(), right.data()};
    const int frames1 = ring.write(src, 12);
    CHECK(ring.occupancy() == 12);

    const int frames2 = ring.write(src, 12);
    CHECK(ring.occupancy() == 24);

    const int frames3 = ring.write(src, 24);
    CHECK(ring.occupancy() <= ring.capacity());
}

TEST_CASE("AR1: occupancy decreases with reads, stays within bounds") {
    AudioRing<48> ring(24);
    std::vector<float> left(kBlockFrames, 0.5f);
    std::vector<float> right(kBlockFrames, 0.3f);
    const float* src[2] = {left.data(), right.data()};
    ring.write(src, kBlockFrames);

    std::vector<float> readLeft(kBlockFrames);
    std::vector<float> readRight(kBlockFrames);
    float* dst[2] = {readLeft.data(), readRight.data()};

    ring.read(dst, 12);
    CHECK(ring.occupancy() == kBlockFrames - 12);
    CHECK(ring.occupancy() >= 0);
    CHECK(ring.occupancy() <= ring.capacity());

    ring.read(dst, 24);
    CHECK(ring.occupancy() >= 0);
    CHECK(ring.occupancy() <= ring.capacity());
}

// ============================================================================
// AR2: Defined underflow
// ============================================================================

TEST_CASE("AR2: read from empty ring fills with silence and reports all frames substituted") {
    AudioRing<48> ring(24);

    std::vector<float> left(16, 999.0f);
    std::vector<float> right(16, 999.0f);
    float* dst[2] = {left.data(), right.data()};

    const int substituted = ring.read(dst, 16);

    // All 16 frames should be substituted with silence.
    CHECK(substituted == 16);
    // Buffers should contain silence.
    CHECK(isSilence(left.data(), 16));
    CHECK(isSilence(right.data(), 16));
}

TEST_CASE("AR2: read returns exactly the number of frames requested, no more, no less") {
    AudioRing<48> ring(24);

    // Write 12 frames.
    std::vector<float> left(12, 0.5f);
    std::vector<float> right(12, 0.3f);
    const float* src[2] = {left.data(), right.data()};
    ring.write(src, 12);

    // Read 20 frames (12 available + 8 shortfall).
    std::vector<float> readLeft(20, 999.0f);
    std::vector<float> readRight(20, 999.0f);
    float* dst[2] = {readLeft.data(), readRight.data()};

    const int substituted = ring.read(dst, 20);

    // 8 frames should be missing and substituted with silence.
    CHECK(substituted == 8);
    // First 12 frames should contain written data.
    for (int i = 0; i < 12; ++i) {
        CHECK(readLeft[i] != 0.0f);
        CHECK(readRight[i] != 0.0f);
    }
    // Last 8 frames should be silence.
    CHECK(isSilence(readLeft.data() + 12, 8));
    CHECK(isSilence(readRight.data() + 12, 8));
}

TEST_CASE("AR2: partial underflow fills shortfall with silence") {
    AudioRing<48> ring(24);

    // Write 10 frames.
    std::vector<float> left(10, 0.7f);
    std::vector<float> right(10, 0.4f);
    const float* src[2] = {left.data(), right.data()};
    ring.write(src, 10);

    // Read 20 frames.
    std::vector<float> readLeft(20, 0.0f);
    std::vector<float> readRight(20, 0.0f);
    float* dst[2] = {readLeft.data(), readRight.data()};

    const int substituted = ring.read(dst, 20);

    CHECK(substituted == 10);
    // Verify first 10 are data, last 10 are silence.
    for (int i = 0; i < 10; ++i) {
        CHECK(readLeft[i] > 0.0f);
        CHECK(readRight[i] > 0.0f);
    }
    CHECK(isSilence(readLeft.data() + 10, 10));
    CHECK(isSilence(readRight.data() + 10, 10));
}

TEST_CASE("AR2: read with sufficient data returns 0 substituted frames") {
    AudioRing<48> ring(24);

    std::vector<float> left(20, 0.8f);
    std::vector<float> right(20, 0.2f);
    const float* src[2] = {left.data(), right.data()};
    ring.write(src, 20);

    std::vector<float> readLeft(15, 0.0f);
    std::vector<float> readRight(15, 0.0f);
    float* dst[2] = {readLeft.data(), readRight.data()};

    const int substituted = ring.read(dst, 15);

    // No underflow, so substituted == 0.
    CHECK(substituted == 0);
    // All frames should contain valid data.
    for (int i = 0; i < 15; ++i) {
        CHECK(readLeft[i] != 0.0f);
        CHECK(readRight[i] != 0.0f);
    }
}

// ============================================================================
// AR3: Defined overflow (oldest dropped, newest preserved)
// ============================================================================

TEST_CASE("AR3: write beyond capacity drops oldest frames") {
    AudioRing<16> ring(8);

    // Write 16 frames: pattern 1.0 (fill to capacity).
    std::vector<float> left1(16, 1.0f);
    std::vector<float> right1(16, 1.0f);
    const float* src1[2] = {left1.data(), right1.data()};
    int dropped1 = ring.write(src1, 16);
    CHECK(dropped1 == 0);
    CHECK(ring.occupancy() == 16);

    // Write 8 more frames: pattern 2.0 (overflow by 8).
    std::vector<float> left2(8, 2.0f);
    std::vector<float> right2(8, 2.0f);
    const float* src2[2] = {left2.data(), right2.data()};
    int dropped2 = ring.write(src2, 8);

    // 8 frames should have been dropped (the oldest).
    CHECK(dropped2 == 8);
    CHECK(ring.occupancy() == 16);
}

TEST_CASE("AR3: newest frames survive overflow (oldest dropped, not newest)") {
    AudioRing<16> ring(8);

    // Write 16 frames with value 1.0 (fill the ring).
    std::vector<float> left1(16, 1.0f);
    std::vector<float> right1(16, 1.0f);
    const float* src1[2] = {left1.data(), right1.data()};
    ring.write(src1, 16);

    // Write 8 new frames with value 2.0 (overflow).
    std::vector<float> left2(8, 2.0f);
    std::vector<float> right2(8, 2.0f);
    const float* src2[2] = {left2.data(), right2.data()};
    ring.write(src2, 8);

    // Read all 16 remaining frames.
    std::vector<float> readLeft(16, 0.0f);
    std::vector<float> readRight(16, 0.0f);
    float* dst[2] = {readLeft.data(), readRight.data()};
    ring.read(dst, 16);

    // Last 8 frames should be 2.0 (the newest data), first 8 should be 1.0 (oldest surviving).
    for (int i = 0; i < 8; ++i) {
        CHECK(readLeft[i] == doctest::Approx(1.0f));
        CHECK(readRight[i] == doctest::Approx(1.0f));
    }
    for (int i = 8; i < 16; ++i) {
        CHECK(readLeft[i] == doctest::Approx(2.0f));
        CHECK(readRight[i] == doctest::Approx(2.0f));
    }
}

TEST_CASE("AR3: overflow with multiple sequential writes drops correct old frames") {
    AudioRing<24> ring(12);

    // Write 24 frames (value 1.0).
    std::vector<float> src1_l(24, 1.0f);
    std::vector<float> src1_r(24, 1.0f);
    const float* src1[2] = {src1_l.data(), src1_r.data()};
    ring.write(src1, 24);

    // Write 12 more frames (value 2.0), causing 12 oldest to drop.
    std::vector<float> src2_l(12, 2.0f);
    std::vector<float> src2_r(12, 2.0f);
    const float* src2[2] = {src2_l.data(), src2_r.data()};
    int drop1 = ring.write(src2, 12);
    CHECK(drop1 == 12);

    // Ring now has: 12 frames of 1.0 (the survived oldest) + 12 frames of 2.0.
    // Write 8 more frames (value 3.0), causing 8 more to drop.
    std::vector<float> src3_l(8, 3.0f);
    std::vector<float> src3_r(8, 3.0f);
    const float* src3[2] = {src3_l.data(), src3_r.data()};
    int drop2 = ring.write(src3, 8);
    CHECK(drop2 == 8);

    // Ring now has: 4 frames of 1.0 + 12 frames of 2.0 + 8 frames of 3.0 = 24.
    std::vector<float> readLeft(24, 0.0f);
    std::vector<float> readRight(24, 0.0f);
    float* dst[2] = {readLeft.data(), readRight.data()};
    ring.read(dst, 24);

    // Verify the sequence.
    for (int i = 0; i < 4; ++i) {
        CHECK(readLeft[i] == doctest::Approx(1.0f));
    }
    for (int i = 4; i < 16; ++i) {
        CHECK(readLeft[i] == doctest::Approx(2.0f));
    }
    for (int i = 16; i < 24; ++i) {
        CHECK(readLeft[i] == doctest::Approx(3.0f));
    }
}

// ============================================================================
// AR8: No re-centring
// ============================================================================

TEST_CASE("AR8: occupancy drift persists, no re-centring toward target") {
    AudioRing<48> ring(24);

    // Write 20 frames (capacity is 48, so occupancy = 20).
    std::vector<float> src_l(20, 0.5f);
    std::vector<float> src_r(20, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 20);

    const int occ1 = ring.occupancy();
    CHECK(occ1 == 20);

    // Read 5 frames.
    std::vector<float> dst_l(5, 0.0f);
    std::vector<float> dst_r(5, 0.0f);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    ring.read(dst, 5);

    const int occ2 = ring.occupancy();
    CHECK(occ2 == 15);

    // If re-centring occurred, occupancy would have been steered back toward 24 (capacity/2).
    // But AR8 says no re-centring, so drift should persist.
    // Just verify occupancy decreased by exactly 5.
    CHECK(occ2 == occ1 - 5);

    // Write 10 more frames (occ should be 25, not re-centered).
    ring.write(src, 10);
    const int occ3 = ring.occupancy();
    CHECK(occ3 == 25);  // 15 + 10, no re-centering adjustment.
}

TEST_CASE("AR8: asymmetric occupancy levels are maintained") {
    AudioRing<32> ring(16);

    // Write 30 frames.
    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 30);
    CHECK(ring.occupancy() == 30);

    // Read only 2 frames.
    std::vector<float> dst_l(2, 0.0f);
    std::vector<float> dst_r(2, 0.0f);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    ring.read(dst, 2);
    CHECK(ring.occupancy() == 28);

    // If re-centring existed, occupancy would drift toward 16 (capacity/2).
    // But with no re-centring, it should stay at 28 exactly.
    CHECK(ring.occupancy() == 28);
}

// ============================================================================
// Cross-contract scenarios
// ============================================================================

TEST_CASE("AR1+AR2: occupancy and underflow consistency") {
    AudioRing<32> ring(16);

    // Write 10 frames, then try to read 30.
    std::vector<float> src_l(10, 0.7f);
    std::vector<float> src_r(10, 0.3f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 10);

    std::vector<float> dst_l(30, 0.0f);
    std::vector<float> dst_r(30, 0.0f);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    const int substituted = ring.read(dst, 30);

    // 20 frames should be substituted.
    CHECK(substituted == 20);
    // Ring should now be empty.
    CHECK(ring.occupancy() == 0);
}

TEST_CASE("AR2+AR3: overflow and underflow in sequence") {
    AudioRing<16> ring(8);

    // Fill to capacity.
    std::vector<float> src_l(16, 0.5f);
    std::vector<float> src_r(16, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 16);

    // Overflow by 4 frames.
    ring.write(src, 4);

    // Read only 10 frames.
    std::vector<float> dst_l(10, 0.0f);
    std::vector<float> dst_r(10, 0.0f);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    ring.read(dst, 10);
    CHECK(ring.occupancy() == 6);

    // Try to read 20 frames (only 6 available).
    const int substituted = ring.read(dst, 20);
    CHECK(substituted == 14);
    CHECK(ring.occupancy() == 0);
}

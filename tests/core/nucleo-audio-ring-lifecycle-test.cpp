#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"

// Audio ring state-machine and lifecycle contracts (FR-030d, FR-030b, FR-051-054).
// Tests cover:
// AR7 — Priming vs Running: State is observable via state(). Priming is the initial state
//       and persists while occupancy < startupFill(). When a write carries occupancy to
//       startupFill(), the ring transitions to Running. While Priming, a short read is
//       normal operation and no underrun is counted. Once Running, a short read is a
//       genuine underrun and IS counted. Running is never demoted by starvation.
// AR8 — No re-centring: ring never drops or duplicates frames to steer occupancy back
//       toward a target; drift stays visible.
// AR9 — reset() clears contents and returns to Priming. stop() enters Stopped.
//       Neither touches counters.

using namespace acfx::nucleo;

namespace {

// Helper to create a test buffer with known values.
void fillTestBuffer(float* const* channels, int frames, float baseValue) noexcept {
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            channels[channel][frame] = baseValue + (channel * 0.1f);
        }
    }
}

// Helper to check if a buffer region contains silence.
bool isSilence(const float* buf, int frames) noexcept {
    for (int i = 0; i < frames; ++i) {
        if (buf[i] != 0.0f) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ============================================================================
// AR7: Priming vs Running (state-observable, threshold-driven)
// ============================================================================

TEST_CASE("AR7: construction leaves ring in Priming state") {
    AudioRing<48> ring(24);  // startupFillFrames = 24
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 0);
}

TEST_CASE("AR7: occupancy < startupFill stays Priming across multiple writes") {
    AudioRing<48> ring(24);  // startupFillFrames = 24

    // Write 8 frames; occupancy = 8 < 24
    std::vector<float> src_l(8, 0.5f);
    std::vector<float> src_r(8, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    ring.write(src, 8);
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 8);

    // Write 8 more; occupancy = 16 < 24
    ring.write(src, 8);
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 16);
}

TEST_CASE("AR7: write carrying occupancy to startupFill promotes Priming -> Running") {
    AudioRing<48> ring(24);  // startupFillFrames = 24

    std::vector<float> src_l(24, 0.5f);
    std::vector<float> src_r(24, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    // Write 20 frames; state is still Priming
    ring.write(src, 20);
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 20);

    // Write 4 more, carrying occupancy to exactly 24 (the startupFill target)
    ring.write(src, 4);
    CHECK(ring.state() == RingState::Running);
    CHECK(ring.occupancy() == 24);
}

TEST_CASE("AR7: once Running, ring is never demoted by starvation") {
    AudioRing<48> ring(24);  // startupFillFrames = 24

    // Fill to Running
    std::vector<float> src_l(24, 0.5f);
    std::vector<float> src_r(24, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 24);
    CHECK(ring.state() == RingState::Running);

    // Drain the ring completely
    std::vector<float> dst_l(48);
    std::vector<float> dst_r(48);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    const int substituted = ring.read(dst, 24);
    CHECK(ring.occupancy() == 0);

    // Even though occupancy is 0, state should still be Running
    CHECK(ring.state() == RingState::Running);

    // A short read in this state IS a genuine underrun and DOES count
    const int underrun = ring.read(dst, 8);
    CHECK(underrun == 8);  // All 8 frames were substituted (underrun)
    CHECK(isSilence(dst_l.data(), 8));
}

TEST_CASE("AR7: read while Priming fills with silence (count is caller's obligation)") {
    AudioRing<48> ring(24);  // startupFillFrames = 24

    // Ring is in Priming with occupancy 0
    CHECK(ring.state() == RingState::Priming);

    // Calling read() while Priming is a caller error (contract AR7): the ring should only
    // be read while Running. However, the ring is still predictable — it returns silence
    // and a substitution count. The contract's AR7 emphasis is that the count is MEANINGLESS
    // in Priming (an empty ring while filling is normal, not a shortfall) and the caller
    // MUST NOT record it as an underrun. The ring reports the fact; deciding its meaning is
    // the transport's job. This test verifies the ring's half: silence is returned.
    std::vector<float> dst_l(8);
    std::vector<float> dst_r(8);
    float* dst[2] = {dst_l.data(), dst_r.data()};

    ring.read(dst, 8);
    CHECK(isSilence(dst_l.data(), 8));
}

TEST_CASE("AR7: state machine is threshold-driven: startupFillFrames = 0") {
    AudioRing<48> ring(0);  // startupFillFrames = 0

    // Per contract state-transition table: construction ALWAYS yields Priming,
    // even when startupFill() == 0. The threshold is evaluated at the END of write(),
    // and only there. So a ring with startupFill() == 0 is Priming until its first write.
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 0);

    std::vector<float> src_l(1, 0.5f);
    std::vector<float> src_r(1, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    // First write carries occupancy from 0 to 1, which satisfies >= 0 (startupFill).
    ring.write(src, 1);
    CHECK(ring.state() == RingState::Running);
    CHECK(ring.occupancy() == 1);
}

TEST_CASE("AR7: state machine is threshold-driven: startupFillFrames = 48") {
    AudioRing<48> ring(48);  // startupFillFrames = 48 (entire capacity)

    std::vector<float> src_l(48, 0.5f);
    std::vector<float> src_r(48, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};

    // Write 47 frames; still Priming
    ring.write(src, 47);
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 47);

    // Write 1 more to reach 48
    ring.write(src, 1);
    CHECK(ring.state() == RingState::Running);
    CHECK(ring.occupancy() == 48);
}

TEST_CASE("AR7: constructor throws std::invalid_argument if startupFillFrames > CapacityFrames") {
    // Such a ring could never reach Running and would prime forever, silently.
    // Constructor must throw std::invalid_argument to be fail-loud.
    CHECK_THROWS_AS(AudioRing<48> ring(49), std::invalid_argument);
    CHECK_THROWS_AS(AudioRing<16> ring(17), std::invalid_argument);
    CHECK_THROWS_AS(AudioRing<32> ring(100), std::invalid_argument);
    CHECK_THROWS_AS(AudioRing<48> ring(96), std::invalid_argument);
}

TEST_CASE("AR7: constructor throws std::invalid_argument if startupFillFrames is negative") {
    // Negative fill threshold is nonsensical and must be rejected at construction.
    CHECK_THROWS_AS(AudioRing<48> ring(-1), std::invalid_argument);
    CHECK_THROWS_AS(AudioRing<32> ring(-10), std::invalid_argument);
}

TEST_CASE("AR7: constructor accepts startupFillFrames == CapacityFrames") {
    // This should NOT throw; it's a valid (if extreme) configuration
    AudioRing<48> ring(48);  // This should compile and not throw
    CHECK(ring.startupFill() == 48);
}

// ============================================================================
// AR8: No re-centring
// ============================================================================

TEST_CASE("AR8: occupancy drift persists, no re-centring toward target") {
    AudioRing<48> ring(24);  // Ignore the threshold for this test

    // Write 20 frames (occupancy = 20).
    std::vector<float> src_l(20, 0.5f);
    std::vector<float> src_r(20, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 20);

    const int occ1 = ring.occupancy();
    CHECK(occ1 == 20);

    // Read 5 frames.
    std::vector<float> dst_l(5);
    std::vector<float> dst_r(5);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    ring.read(dst, 5);

    const int occ2 = ring.occupancy();
    CHECK(occ2 == 15);

    // If re-centring occurred, occupancy would have been steered back toward 24.
    // But AR8 says no re-centring, so drift should persist.
    CHECK(occ2 == occ1 - 5);

    // Write 10 more frames (occ should be 25, not re-centered).
    ring.write(src, 10);
    const int occ3 = ring.occupancy();
    CHECK(occ3 == 25);  // 15 + 10, no re-centering adjustment.
}

TEST_CASE("AR8: asymmetric occupancy levels are maintained") {
    AudioRing<32> ring(24);  // Ignore the threshold

    // Write 30 frames.
    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 30);
    CHECK(ring.occupancy() == 30);

    // Read only 2 frames.
    std::vector<float> dst_l(2);
    std::vector<float> dst_r(2);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    ring.read(dst, 2);
    CHECK(ring.occupancy() == 28);

    // If re-centring existed, occupancy would drift toward 16 (capacity/2).
    // But with no re-centring, it should stay at 28 exactly.
    CHECK(ring.occupancy() == 28);
}

// ============================================================================
// AR9: reset() and stop()
// ============================================================================

TEST_CASE("AR9: reset() returns ring to Priming state") {
    AudioRing<48> ring(24);

    // Fill past the threshold into Running
    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 30);
    CHECK(ring.state() == RingState::Running);

    // Reset
    ring.reset();
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 0);
}

TEST_CASE("AR9: reset() clears contents and occupancy") {
    AudioRing<48> ring(24);

    // Write 30 frames.
    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 30);
    CHECK(ring.occupancy() == 30);

    // Reset.
    ring.reset();
    CHECK(ring.occupancy() == 0);

    // Read should now get silence
    std::vector<float> dst_l(10);
    std::vector<float> dst_r(10);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    const int substituted = ring.read(dst, 10);

    CHECK(substituted == 10);
    CHECK(isSilence(dst_l.data(), 10));
    CHECK(isSilence(dst_r.data(), 10));
}

TEST_CASE("AR9: reset() does not touch counters (caller responsibility)") {
    AudioRing<48> ring(24);

    // Write and read a few times (hypothetically updating external counters).
    std::vector<float> src_l(20, 0.5f);
    std::vector<float> src_r(20, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 20);

    std::vector<float> dst_l(10);
    std::vector<float> dst_r(10);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    ring.read(dst, 10);

    // Reset: note that the ring's operation and state are cleared,
    // but external counters (not stored in the ring per AR4) are not touched.
    ring.reset();

    // Verify reset worked: state and occupancy cleared
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 0);

    // (The ring does not own counters, so we can't verify they weren't touched
    // from the ring's perspective alone; this is a design invariant in AR4.)
}

TEST_CASE("AR9: stop() puts ring in Stopped state") {
    AudioRing<48> ring(24);

    // Write to enter Running
    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 30);
    CHECK(ring.state() == RingState::Running);

    // Stop
    ring.stop();
    CHECK(ring.state() == RingState::Stopped);
}

TEST_CASE("AR9: stop() clears contents") {
    AudioRing<48> ring(24);

    // Write 30 frames.
    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    ring.write(src, 30);
    CHECK(ring.occupancy() == 30);

    // Stop.
    ring.stop();
    CHECK(ring.occupancy() == 0);
}

TEST_CASE("AR9: stop() from Priming state") {
    AudioRing<48> ring(24);

    // Still in Priming with occupancy 0
    CHECK(ring.state() == RingState::Priming);

    // Stop: should transition to Stopped without issues
    ring.stop();
    CHECK(ring.state() == RingState::Stopped);
    CHECK(ring.occupancy() == 0);
}

TEST_CASE("AR9: reset() from Stopped state returns to Priming") {
    AudioRing<48> ring(24);

    // Stop to enter Stopped state
    ring.stop();
    CHECK(ring.state() == RingState::Stopped);

    // Reset: should return to Priming
    ring.reset();
    CHECK(ring.state() == RingState::Priming);
    CHECK(ring.occupancy() == 0);
}

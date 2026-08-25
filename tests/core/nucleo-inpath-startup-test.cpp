#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-in-path.h"

// The cold-drain startup defect (FR-007): at startup, when the host begins
// pulling at the SOF cadence immediately after stream-open, the output ring
// can be drained faster than the DSP replenishes it, producing an underrun on
// the very first host read.
//
// This RED test captures the condition and will FAIL against the current
// (unfixed) code. T006 implements the fix to make this PASS by ensuring
// sufficient startup cushion so the first SOF-paced pull does not starve.
//
// The scenario (FR-007, edge case):
// 1. Ring is Priming with a startupFill threshold (e.g., 48 frames per USB frame).
// 2. DSP writes exactly startupFill frames → ring transitions to Running.
// 3. Host immediately pulls at SOF cadence (max 49 frames per packet).
// 4. Without additional startup cushion, service() reads min(sink room, 49) frames
//    from the ring, which only has 48, producing a substituted frame count > 0.
// 5. The ASSERTION: this MUST NOT happen (framesSubstituted == 0) — the startup
//    must cushion against this cold-drain, not seed an immediate underrun.
//
// This test will FAIL now and PASS after T006 implements the fix.

using namespace acfx::nucleo;

namespace {

constexpr int kBytesPerFrame = kChannels * static_cast<int>(sizeof(std::int16_t));

// A ring sized for the test: room for multiple USB frames' worth of data.
using TestRing = AudioRing<256, kChannels>;

// Push `frames` of constant (left, right) samples into the ring.
template <typename Ring, typename Left, typename Right>
void pushFrames(Ring& ring, int frames, Left left, Right right) {
    std::vector<float> l(static_cast<std::size_t>(frames));
    std::vector<float> r(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; ++frame) {
        l[static_cast<std::size_t>(frame)] = left(frame);
        r[static_cast<std::size_t>(frame)] = right(frame);
    }
    float* channels[kChannels] = {l.data(), r.data()};
    ring.write(channels, frames);
}

// Simulated TinyUSB IN endpoint for testing. Accepts writes up to a configured
// room limit and tracks how much data was actually accepted.
class FakeInSink {
public:
    explicit FakeInSink(int roomBytes) : roomBytes_(roomBytes) {}

    int write(const std::int16_t* data, int len) noexcept {
        ++writeCalls_;
        const int accept = (acceptLimit_ >= 0) ? (acceptLimit_ < len ? acceptLimit_ : len) : len;
        const int accepted = (accept > 0) ? accept : 0;
        const auto* raw = reinterpret_cast<const std::uint8_t*>(data);
        received_.insert(received_.end(), raw, raw + accepted);
        return accepted;
    }

    int writeAvailable() noexcept {
        ++writeAvailableCalls_;
        return roomBytes_;
    }
    int capacity() noexcept { return roomBytes_; }  // held==0: IN guard inert

    void setRoomBytes(int bytes) { roomBytes_ = bytes; }

    std::vector<std::uint8_t> received_;
    int writeCalls_ = 0;
    int writeAvailableCalls_ = 0;

private:
    int roomBytes_;
    int acceptLimit_ = -1;  // -1: accept everything offered
};

}  // namespace

// ============================================================================
// Cold-drain startup test: the RED test for FR-007
// ============================================================================

TEST_CASE("Startup cold-drain: IN path does not underrun on first SOF-paced pull") {
    // Create a ring with startupFill of 48 frames — one USB frame worth at 48 kHz.
    // This is the minimal startup state: just barely reached Running.
    TestRing ring(48);
    REQUIRE(ring.state() == RingState::Priming);
    REQUIRE(ring.occupancy() == 0);

    // DSP writes exactly 48 frames to reach startupFill threshold.
    // This transition moves the ring from Priming to Running.
    pushFrames(ring, 48, [](int f) { return 0.5f; }, [](int f) { return -0.5f; });
    REQUIRE(ring.state() == RingState::Running);
    REQUIRE(ring.occupancy() == 48);

    // Sink reports room for 49 frames (one maximum packet at 48 kHz):
    // this simulates the host's first SOF-paced pull, ready to consume
    // the standard packet size (kMaxPacketFrames = 49).
    FakeInSink sink(49 * kBytesPerFrame);
    AudioTransportStats stats;
    UsbInPath path;

    // First host read (SOF-paced pull): service() will be called to fill
    // the sink's available room.
    const InServicePass pass = path.service(ring, sink, stats);

    // ========================================================================
    // THE RED ASSERTIONS: these FAIL with the current (unfixed) code
    // ========================================================================
    // Before T006's fix, the ring only has 48 frames and the host wants 49.
    // service() reads all 48 available frames and substitutes 1 frame of silence,
    // incrementing outputUnderruns and setting framesSubstituted = 1.
    // This test MUST FAIL at this assertion to confirm the cold-drain is present.
    CHECK(pass.framesSubstituted == 0);   // RED: will fail because substituted == 1
    CHECK(stats.outputUnderruns == 0u);   // RED: will fail because counter == 1

    // After T006 implements startup cushioning (additional buffer space beyond
    // startupFill), the ring will have enough frames (at least 49) when it
    // reaches Running, so this first pull will succeed without substitution,
    // and this test will PASS.
}

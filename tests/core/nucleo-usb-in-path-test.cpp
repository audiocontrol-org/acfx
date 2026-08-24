#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "support/allocation-sentinel.h"

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-in-path.h"

// The polled IN path (device -> host), T035 (FR-026, FR-032, FR-038a).
//
// This is the producer side of the OUTPUT ring — the counterpart to
// nucleo-usb-out-path-test.cpp's consumer side of the INPUT ring, and the
// downstream neighbour of nucleo-dsp-block-path-test.cpp, which fills the
// same ring this path drains.
//
// Driven with a FAKE sink (write()/writeAvailable()) rather than TinyUSB, for
// the same reason the OUT/DSP-block paths use fakes: tud_audio_write() only
// exists on the firmware targets, and a fake lets every case here assert
// EXACTLY what bytes reached "the host" and when, which is the only way to
// prove the back-pressure/no-silent-drop property the file header promises.
//
// Cases:
// IN1 — the ring-state gate: Priming and Stopped both draw nothing and touch
//       the sink not at all (mirrors DB1's input-ring gate, on the output
//       side).
// IN2 — the pull is bounded by the sink's reported room, not by
//       kMaxPacketFrames alone; the bytes delivered are correctly
//       interleaved, correctly rounded (including a tie, both signs) and
//       correctly CLAMPED (both rails) — FR-038a end to end through this
//       path, not merely in sample-format.h in isolation.
// IN3 — a ring shorter than the room-bounded pull: the shortfall is
//       silence-filled and `outputUnderruns` increments exactly once (US3
//       AS3).
// IN4 — sink back-pressure: a partial accept is retried, byte for byte, on
//       the NEXT pass before any new frames are pulled from the ring, and the
//       ring is not touched again until the carryover clears. Every byte
//       offered eventually reaches the sink; none is lost or duplicated.
// IN5 — no heap allocation across a run of passes (FR-030, D16).

using namespace acfx::nucleo;

namespace {

constexpr int kBytesPerFrame = kChannels * static_cast<int>(sizeof(std::int16_t));

// A ring roomy enough that nothing here overflows or underflows by accident;
// each case sizes its own occupancy deliberately.
using TestRing = AudioRing<256, kChannels>;

// Push `frames` of (left, right) into `ring` using the two per-frame
// callables, evaluated at ring-relative frame index 0, 1, 2, ...
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

// A simulated TinyUSB IN endpoint: write() accepts up to `acceptLimit_`
// bytes of what it is offered (unlimited by default, i.e. it accepts
// everything — real back-pressure is opted into per test via
// setAcceptLimit()), records every accepted byte in order, and
// writeAvailable() reports whatever room the test has configured. Both
// methods count their own calls so a test can assert WHICH branch of
// UsbInPath::service() ran, not merely its net effect.
class FakeInSink {
public:
    explicit FakeInSink(int roomBytes) : roomBytes_(roomBytes) {}

    int write(const std::int16_t* data, int len) noexcept {
        ++writeCalls_;
        const int accept = (acceptLimit_ >= 0) ? std::min(acceptLimit_, len) : len;
        const int accepted = (accept > 0) ? accept : 0;
        const auto* raw = reinterpret_cast<const std::uint8_t*>(data);
        received_.insert(received_.end(), raw, raw + accepted);
        return accepted;
    }

    int writeAvailable() noexcept {
        ++writeAvailableCalls_;
        return roomBytes_;
    }

    void setRoomBytes(int bytes) { roomBytes_ = bytes; }
    void setAcceptLimit(int bytes) { acceptLimit_ = bytes; }

    std::vector<std::uint8_t> received_;
    int writeCalls_ = 0;
    int writeAvailableCalls_ = 0;

private:
    int roomBytes_;
    int acceptLimit_ = -1;  // -1: accept everything offered
};

std::vector<std::uint8_t> toBytes(std::initializer_list<std::int16_t> samples) {
    std::vector<std::uint8_t> bytes(samples.size() * sizeof(std::int16_t));
    std::memcpy(bytes.data(), samples.begin(), bytes.size());
    return bytes;
}

}  // namespace

// ============================================================================
// IN1 — the ring-state gate
// ============================================================================

TEST_CASE("IN1: Priming and Stopped both draw nothing and never touch the sink") {
    AudioTransportStats stats;
    UsbInPath path;
    FakeInSink sink(UsbInPath::maxPayloadBytes());

    // Priming: never written to, so it never reached its (zero) startup fill
    // via a promoting write. Constructed fresh, this ring is Priming by
    // definition (AudioRing's constructor "ALWAYS yields Priming").
    TestRing priming(4);
    REQUIRE(priming.state() == RingState::Priming);

    const InServicePass primingPass = path.service(priming, sink, stats);
    CHECK(primingPass.framesRead == 0);
    CHECK(primingPass.bytesOffered == 0);
    CHECK(sink.writeAvailableCalls_ == 0);
    CHECK(sink.writeCalls_ == 0);
    CHECK(stats.outputUnderruns == 0u);

    // Stopped: reached Running, then explicitly stopped (suspend/no
    // streaming alt-setting, FR-051).
    TestRing stopped(0);
    pushFrames(stopped, 4, [](int) { return 0.1f; }, [](int) { return -0.1f; });
    REQUIRE(stopped.state() == RingState::Running);
    stopped.stop();
    REQUIRE(stopped.state() == RingState::Stopped);

    const InServicePass stoppedPass = path.service(stopped, sink, stats);
    CHECK(stoppedPass.framesRead == 0);
    CHECK(stoppedPass.bytesOffered == 0);
    CHECK(sink.writeAvailableCalls_ == 0);
    CHECK(sink.writeCalls_ == 0);
    CHECK(stats.outputUnderruns == 0u);
}

// ============================================================================
// IN2 — room-bounded pull size, interleave order, round-to-nearest (both
// signs of a tie), and clamp (both rails) — FR-038a through this path
// ============================================================================

TEST_CASE("IN2: the room-bounded pull is correctly interleaved, rounded and clamped") {
    // Four frames, each exercising a different FR-038a case:
    //   frame 0: an ordinary, non-boundary value.
    //   frame 1: overshoots +1.0 / undershoots -1.0 -> CLAMPS to the rails,
    //            not a wraparound.
    //   frame 2: scales to exactly a half-LSB tie -> rounds AWAY from zero,
    //            both signs.
    //   frame 3: silence and the negative rail's exact boundary.
    TestRing ring(0);
    pushFrames(
        ring, 4,
        [](int frame) {
            switch (frame) {
                case 0: return 3000.0f / kInt16Scale;
                case 1: return 1.5f;                        // clamps to +32767
                case 2: return 24691.0f / 65536.0f;          // scaled = 12345.5
                default: return 0.0f;
            }
        },
        [](int frame) {
            switch (frame) {
                case 0: return -3000.0f / kInt16Scale;
                case 1: return -1.5f;                        // clamps to -32768
                case 2: return -24691.0f / 65536.0f;         // scaled = -12345.5
                default: return -1.0f;                       // exact -32768 boundary
            }
        });
    REQUIRE(ring.state() == RingState::Running);
    REQUIRE(ring.occupancy() == 4);

    // Room for exactly 4 frames — smaller than kMaxPacketFrames (49) — so the
    // pull size is bounded by ROOM, not by the per-pass ceiling.
    FakeInSink sink(4 * kBytesPerFrame);
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass = path.service(ring, sink, stats);

    CHECK(pass.flushedCarryover == false);
    CHECK(pass.framesRead == 4);
    CHECK(pass.framesSubstituted == 0);
    CHECK(pass.bytesOffered == 4 * kBytesPerFrame);
    CHECK(pass.bytesWritten == 4 * kBytesPerFrame);
    CHECK(stats.outputUnderruns == 0u);
    CHECK(ring.occupancy() == 0);

    const std::vector<std::uint8_t> expected = toBytes({
        3000, -3000,      // frame 0: ordinary
        32767, -32768,    // frame 1: clamped, not wrapped
        12346, -12346,    // frame 2: tie, rounded away from zero both signs
        0, -32768,         // frame 3: silence, exact negative rail
    });
    CHECK(sink.received_ == expected);
}

// ============================================================================
// IN3 — a ring shorter than the room-bounded pull: silence + outputUnderruns
// ============================================================================

TEST_CASE("IN3: a short ring is silence-filled and counts outputUnderruns once") {
    TestRing ring(0);
    pushFrames(
        ring, 2, [](int frame) { return static_cast<float>(frame + 1) / kInt16Scale; },
        [](int frame) { return -static_cast<float>(frame + 1) / kInt16Scale; });
    REQUIRE(ring.occupancy() == 2);

    // Room for 5 frames: the pull asks for more than the ring holds.
    FakeInSink sink(5 * kBytesPerFrame);
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass = path.service(ring, sink, stats);

    CHECK(pass.framesRead == 5);
    CHECK(pass.framesSubstituted == 3);
    CHECK(stats.outputUnderruns == 1u);
    CHECK(ring.occupancy() == 0);
    CHECK(pass.bytesWritten == 5 * kBytesPerFrame);

    const std::vector<std::uint8_t> expected =
        toBytes({1, -1, 2, -2, 0, 0, 0, 0, 0, 0});
    CHECK(sink.received_ == expected);
}

// ============================================================================
// IN4 — sink back-pressure: retried, not dropped
// ============================================================================

TEST_CASE("IN4: a partial accept is retried next pass; nothing is lost or duplicated") {
    TestRing ring(0);
    pushFrames(
        ring, 4, [](int frame) { return static_cast<float>(frame + 1) / kInt16Scale; },
        [](int frame) { return -static_cast<float>(frame + 1) / kInt16Scale; });
    REQUIRE(ring.occupancy() == 4);

    FakeInSink sink(4 * kBytesPerFrame);  // room for exactly the 4 frames
    AudioTransportStats stats;
    UsbInPath path;

    // Pass 1: the sink only accepts the first 6 of the 16 offered bytes — a
    // BYTE-granular short accept (1.5 frames), deliberately not aligned to a
    // frame boundary, since tu_fifo_write_n() has no notion of frames at all.
    sink.setAcceptLimit(6);
    const InServicePass first = path.service(ring, sink, stats);

    CHECK(first.flushedCarryover == false);
    CHECK(first.framesRead == 4);
    CHECK(first.bytesOffered == 16);
    CHECK(first.bytesWritten == 6);
    CHECK(path.pendingBytes() == 10);
    CHECK(ring.occupancy() == 0);  // the ring was drawn from exactly once

    // Pass 2: the carryover is retried BEFORE anything else is considered.
    // The ring holds nothing further to give it a reason to look; asserting
    // writeAvailableCalls_ stays at its pass-1 value proves this pass took
    // the carryover branch rather than coincidentally reaching the same
    // outcome via a fresh (empty) pull.
    sink.setAcceptLimit(-1);  // now accepts everything offered
    const InServicePass second = path.service(ring, sink, stats);

    CHECK(second.flushedCarryover == true);
    CHECK(second.framesRead == 0);
    CHECK(second.bytesOffered == 10);
    CHECK(second.bytesWritten == 10);
    CHECK(path.pendingBytes() == 0);
    CHECK(sink.writeAvailableCalls_ == 1);  // unchanged since pass 1

    // Every one of the 16 original bytes reached the sink, in order, exactly
    // once — no loss on the short accept, no duplication on the retry.
    const std::vector<std::uint8_t> expected = toBytes({1, -1, 2, -2, 3, -3, 4, -4});
    CHECK(sink.received_ == expected);
    CHECK(stats.outputUnderruns == 0u);
}

// ============================================================================
// IN5 — no heap allocation (FR-030, D16)
// ============================================================================

TEST_CASE("IN5: a run of service passes performs no heap allocation") {
    TestRing ring(0);
    pushFrames(
        ring, 4 * UsbInPath::maxPayloadBytes() / kBytesPerFrame,
        [](int frame) { return static_cast<float>(frame % 100) / kInt16Scale; },
        [](int frame) { return -static_cast<float>(frame % 100) / kInt16Scale; });

    FakeInSink sink(UsbInPath::maxPayloadBytes());
    // The fake's own recording vector would allocate as it grows, so it is
    // reserved to its final capacity BEFORE the measured region — what is
    // under test here is the path, not the test double (mirrors DB8's
    // identical note in nucleo-dsp-block-path-test.cpp).
    sink.received_.reserve(static_cast<std::size_t>(8 * UsbInPath::maxPayloadBytes()));
    AudioTransportStats stats;
    UsbInPath path;

    acfx::test::AllocationSentinel::reset();
    for (int pass = 0; pass < 8; ++pass) {
        path.service(ring, sink, stats);
    }
    const std::size_t allocations = acfx::test::AllocationSentinel::allocations();

    CHECK(allocations == 0u);
}

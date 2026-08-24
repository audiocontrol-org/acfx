#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-in-path.h"

// The capture-only alt-setting state (T047, US7; FR-029, FR-029a, D22): the
// host has opened the IN streaming interface while OUT sits at its
// zero-bandwidth alt — no playback stream is open AT ALL, distinct from the
// output ring merely being momentarily empty (that case is
// nucleo-usb-in-path-test.cpp's IN3, and counts `outputUnderruns`, never
// `inputStarved`). This file exercises UsbInPath::service()'s `captureOnly`
// parameter directly, via the same FakeInSink/TestRing convention as its
// sibling.
//
// Cases:
// CO1 — captureOnly with room: the sink receives a non-empty block of
//       zeroed samples and `inputStarved` increments by exactly one;
//       `outputUnderruns` stays at zero (I-TS1a).
// CO2 — captureOnly across N passes: `inputStarved == N`,
//       `outputUnderruns == 0` throughout (mutual exclusivity, FR-029a).
// CO3 — captureOnly == false, ring Running but short: unchanged existing
//       behaviour — `outputUnderruns` increments, `inputStarved` stays at
//       zero (regression guard on the untouched path).
// CO4 — a stale duplex carryover must NOT be flushed once captureOnly
//       becomes true: it is discarded, and only fresh zero bytes reach the
//       sink from that pass on.
// CO5 — captureOnly with no sink room: no crash, no counter fires (nothing
//       was actually emitted, so nothing was actually starved-for yet).

using namespace acfx::nucleo;

namespace {

constexpr int kBytesPerFrame = kChannels * static_cast<int>(sizeof(std::int16_t));

using TestRing = AudioRing<256, kChannels>;

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

// Mirrors nucleo-usb-in-path-test.cpp's FakeInSink exactly (see that file for
// the rationale); duplicated rather than shared because each test TU is
// compiled and linked independently and neither exposes the fake publicly.
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

bool allZero(const std::vector<std::uint8_t>& bytes) {
    for (const std::uint8_t byte : bytes) {
        if (byte != 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ============================================================================
// CO1 — captureOnly with room: zeroed silence, inputStarved += 1
// ============================================================================

TEST_CASE("CO1: captureOnly emits a room-bounded zero block and counts inputStarved once") {
    TestRing ring(4);  // Priming; never written to — must never be touched.
    REQUIRE(ring.state() == RingState::Priming);

    FakeInSink sink(UsbInPath::maxPayloadBytes());
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass = path.service(ring, sink, stats, /*captureOnly=*/true);

    CHECK(pass.flushedCarryover == false);
    CHECK(pass.framesRead == 0);           // the ring is never touched
    CHECK(pass.framesSubstituted == 0);
    CHECK(pass.bytesOffered == UsbInPath::maxPayloadBytes());
    CHECK(pass.bytesWritten == UsbInPath::maxPayloadBytes());

    REQUIRE(sink.received_.size() == static_cast<std::size_t>(UsbInPath::maxPayloadBytes()));
    CHECK(allZero(sink.received_));

    CHECK(stats.inputStarved == 1u);
    CHECK(stats.outputUnderruns == 0u);

    // The ring's own state is never consulted beyond the REQUIRE above: a
    // Priming ring reaching Running would need a write nothing here performs.
    CHECK(ring.state() == RingState::Priming);
}

// ============================================================================
// CO2 — N passes: inputStarved == N, outputUnderruns == 0 throughout
// ============================================================================

TEST_CASE("CO2: N captureOnly passes count inputStarved N times and never outputUnderruns") {
    TestRing ring(0);
    FakeInSink sink(UsbInPath::maxPayloadBytes());
    AudioTransportStats stats;
    UsbInPath path;

    constexpr int kPasses = 5;
    for (int pass = 0; pass < kPasses; ++pass) {
        const InServicePass result = path.service(ring, sink, stats, /*captureOnly=*/true);
        CHECK(result.bytesWritten == UsbInPath::maxPayloadBytes());
        CHECK(stats.outputUnderruns == 0u);
    }

    CHECK(stats.inputStarved == static_cast<std::uint32_t>(kPasses));
    CHECK(stats.outputUnderruns == 0u);
}

// ============================================================================
// CO3 — captureOnly == false regression guard: untouched existing behaviour
// ============================================================================

TEST_CASE("CO3: captureOnly=false with a short Running ring is unchanged: outputUnderruns "
          "only") {
    TestRing ring(0);
    pushFrames(
        ring, 2, [](int frame) { return static_cast<float>(frame + 1) / kInt16Scale; },
        [](int frame) { return -static_cast<float>(frame + 1) / kInt16Scale; });
    REQUIRE(ring.occupancy() == 2);

    FakeInSink sink(5 * kBytesPerFrame);  // room for more than the ring holds
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass = path.service(ring, sink, stats, /*captureOnly=*/false);

    CHECK(pass.framesRead == 5);
    CHECK(pass.framesSubstituted == 3);
    CHECK(stats.outputUnderruns == 1u);
    CHECK(stats.inputStarved == 0u);
}

// ============================================================================
// CO4 — a stale duplex carryover is discarded, never flushed, once
// captureOnly becomes true
// ============================================================================

TEST_CASE("CO4: a stale carryover is dropped, not flushed, on the first captureOnly pass") {
    TestRing ring(0);
    pushFrames(
        ring, 4, [](int frame) { return static_cast<float>(frame + 1) / kInt16Scale; },
        [](int frame) { return -static_cast<float>(frame + 1) / kInt16Scale; });
    REQUIRE(ring.occupancy() == 4);

    FakeInSink sink(4 * kBytesPerFrame);
    AudioTransportStats stats;
    UsbInPath path;

    // Pass 1 (duplex, captureOnly=false): a deliberate short accept leaves a
    // non-zero carryover behind, exactly like IN4's setup.
    sink.setAcceptLimit(6);
    const InServicePass duplexPass = path.service(ring, sink, stats, /*captureOnly=*/false);
    REQUIRE(duplexPass.bytesOffered == 16);
    REQUIRE(duplexPass.bytesWritten == 6);
    REQUIRE(path.pendingBytes() == 10);
    const std::size_t receivedBeforeCaptureOnly = sink.received_.size();

    // The host then closes OUT (or this pass is simply the first one to
    // observe captureOnly having become true). The stale 10 bytes must never
    // reach the sink; only fresh zero silence should follow.
    sink.setAcceptLimit(-1);  // accept everything, so nothing is left ambiguous
    const InServicePass captureOnlyPass = path.service(ring, sink, stats, /*captureOnly=*/true);

    CHECK(captureOnlyPass.flushedCarryover == false);
    CHECK(path.pendingBytes() == 0);
    CHECK(captureOnlyPass.bytesOffered == 4 * kBytesPerFrame);  // fresh room-bounded silence
    CHECK(captureOnlyPass.bytesWritten == 4 * kBytesPerFrame);

    REQUIRE(sink.received_.size() ==
            receivedBeforeCaptureOnly + static_cast<std::size_t>(captureOnlyPass.bytesWritten));
    const std::vector<std::uint8_t> appended(
        sink.received_.begin() + static_cast<std::ptrdiff_t>(receivedBeforeCaptureOnly),
        sink.received_.end());
    CHECK(allZero(appended));  // the stale non-zero carryover never appears

    CHECK(stats.inputStarved == 1u);
    CHECK(stats.outputUnderruns == 0u);  // the duplex pass drew a full ring; no shortfall
}

// ============================================================================
// CO5 — captureOnly with no sink room: bounded, no crash, no false count
// ============================================================================

TEST_CASE("CO5: captureOnly with zero sink room does nothing and counts nothing") {
    TestRing ring(0);
    FakeInSink sink(0);
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass = path.service(ring, sink, stats, /*captureOnly=*/true);

    CHECK(pass.bytesOffered == 0);
    CHECK(pass.bytesWritten == 0);
    CHECK(sink.writeCalls_ == 0);
    CHECK(stats.inputStarved == 0u);
    CHECK(stats.outputUnderruns == 0u);

    // A second zero-room pass must not somehow accumulate a count either.
    path.service(ring, sink, stats, /*captureOnly=*/true);
    CHECK(stats.inputStarved == 0u);
}

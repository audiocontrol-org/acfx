#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-out-path.h"

#include "nucleo-usb-out-test-support.h"

// Polled OUT path, part 2: everything that spans MORE THAN ONE PACKET.
//
// nucleo-usb-out-path-test.cpp covers one packet at a time. This file covers
// the two structures that only appear once a second packet's worth of bytes is
// in play, neither of which had any coverage when the path first landed:
//
//   * serviceOutFifo()'s per-call read bound. TinyUSB's OUT endpoint fifo is an
//     unframed BYTE fifo (tu_fifo_t has no item-size field:
//     src/common/tusb_fifo.h:119-132 in the pinned 0.21.0 tree), so how many
//     bytes one read asks for is the only framing control the adapter has.
//     Reading at most one wMaxPacketSize per call — and coming back on the next
//     service-loop pass for the rest — is what keeps FR-028a truncation landing
//     on a torn payload instead of on a whole backlog.
//
//   * consumePacket()'s chunk loop, which runs more than once only when a
//     caller hands over more than one maximum packet's worth of bytes. Its
//     de-interleave offset is in FRAMES but indexes SAMPLES, so it must be
//     scaled by kChannels; a single-packet test can never see that because the
//     offset is always zero on the only iteration that runs.
//
// Cases:
// OP9  — the read bound: one pass never consumes more than one maximum packet,
//        the backlog is reported rather than silently drained, and a torn
//        payload read on its own pass leaves the NEXT payload L/R aligned.
//        OP9's third case pins the limitation the headers state plainly: a tear
//        that is already merged with the following payload inside the fifo
//        cannot be realigned by any read size, and the path reports that
//        condition instead of hiding it.
// OP10 — the chunk loop: frame-by-frame L/R correctness ACROSS chunk
//        boundaries, and inputOverruns counted once per call, not per chunk.
// OP11 — a negative byte count is a caller error, not a transport condition.
//
// Clear-on-tear — what serviceOutFifo() does with the fifo AFTER a torn read —
// is the sibling suite nucleo-usb-out-flush-test.cpp (OP12-OP16); the shared
// simulated fifo and payload builders both suites use live in
// nucleo-usb-out-test-support.h.

using namespace acfx::nucleo;
using namespace nucleo_out_test;

namespace {

// Deliberately smaller than three maximum packets, so a single multi-chunk
// call overflows on more than one chunk.
constexpr int kSmallCapacity = 64;
using SmallRing = AudioRing<kSmallCapacity, kChannels>;

} // namespace

// ============================================================================
// OP9: the per-call read bound
// ============================================================================

TEST_CASE("OP9: one service pass reads at most one maximum packet, never the backlog") {
    // Three whole payloads queued behind each other — the state the fifo
    // reaches whenever the service loop misses a pass. A read that drains the
    // whole fifo would swallow all 588 bytes in one call; the bound must hold
    // each pass to 196 bytes and leave the rest for the next pass.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(kMaxPacketFrames));
    fifo.push(makePayload(kMaxPacketFrames, 100), bytesFor(kMaxPacketFrames));
    fifo.push(makePayload(kMaxPacketFrames, 200), bytesFor(kMaxPacketFrames));

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const std::vector<OutServicePass> passes = drainFifo(fifo, path, buffer, ring, stats);

    REQUIRE(passes.size() == 3);
    for (const OutServicePass& pass : passes) {
        CHECK(pass.bytesRead == UsbOutPath::maxPayloadBytes());
        CHECK(pass.framesConsumed == kMaxPacketFrames);
        CHECK(pass.chunks == 1);
        CHECK(pass.wasTruncated == false);
        CHECK(pass.framesDropped == 0);
    }

    // The backlog is REPORTED, not silently drained: it shrinks by exactly one
    // packet per pass and reaches zero only on the last one.
    CHECK(passes[0].backlogBytes == 2 * UsbOutPath::maxPayloadBytes());
    CHECK(passes[1].backlogBytes == UsbOutPath::maxPayloadBytes());
    CHECK(passes[2].backlogBytes == 0);

    CHECK(stats.malformedPayloads == 0u);
    CHECK(stats.inputOverruns == 0u);

    // Every frame of every payload, in order, with L/R intact across both pass
    // boundaries.
    const int total = 3 * kMaxPacketFrames;
    REQUIRE(ring.occupancy() == total);
    std::vector<float> left(static_cast<std::size_t>(total));
    std::vector<float> right(static_cast<std::size_t>(total));
    float* dst[kChannels] = {left.data(), right.data()};
    REQUIRE(ring.read(dst, total) == 0);

    for (int frame = 0; frame < total; ++frame) {
        const int base = 100 * (frame / kMaxPacketFrames);
        const auto sample = static_cast<std::int16_t>(base + (frame % kMaxPacketFrames) + 1);
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(expected(sample)));
        CHECK(right[static_cast<std::size_t>(frame)] ==
              doctest::Approx(expected(static_cast<std::int16_t>(-sample))));
    }
}

TEST_CASE("OP9: a torn payload read alone leaves the FOLLOWING payload L/R aligned") {
    // The FR-028a case as the service loop actually meets it: the torn payload
    // is what the fifo holds when the pass runs, so truncation lands on the
    // torn payload itself. The next payload arrives afterwards and is read from
    // a fresh, frame-aligned start.
    const std::vector<std::int16_t> torn = makePayload(kMaxPacketFrames, 0);
    const std::vector<std::int16_t> whole = makePayload(kMaxPacketFrames, 100);

    FakeOutFifo fifo;
    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    // 48 whole frames plus a 2-byte remainder.
    fifo.push(torn, bytesFor(48) + 2);
    const std::vector<OutServicePass> first = drainFifo(fifo, path, buffer, ring, stats);
    REQUIRE(first.size() == 1);
    CHECK(first[0].bytesRead == bytesFor(48) + 2);
    CHECK(first[0].framesConsumed == 48);
    CHECK(first[0].wasTruncated == true);
    CHECK(first[0].backlogBytes == 0);
    CHECK(stats.malformedPayloads == 1u);

    fifo.push(whole, bytesFor(kMaxPacketFrames));
    const std::vector<OutServicePass> second = drainFifo(fifo, path, buffer, ring, stats);
    REQUIRE(second.size() == 1);
    CHECK(second[0].framesConsumed == kMaxPacketFrames);
    CHECK(second[0].wasTruncated == false);
    CHECK(stats.malformedPayloads == 1u);

    const int total = 48 + kMaxPacketFrames;
    REQUIRE(ring.occupancy() == total);
    std::vector<float> left(static_cast<std::size_t>(total));
    std::vector<float> right(static_cast<std::size_t>(total));
    float* dst[kChannels] = {left.data(), right.data()};
    REQUIRE(ring.read(dst, total) == 0);

    for (int frame = 0; frame < 48; ++frame) {
        const auto sample = static_cast<std::int16_t>(frame + 1);
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(expected(sample)));
        CHECK(right[static_cast<std::size_t>(frame)] ==
              doctest::Approx(expected(static_cast<std::int16_t>(-sample))));
    }
    // The innocent payload: every frame L/R correct and in order, NOT shifted
    // by the discarded remainder.
    for (int frame = 0; frame < kMaxPacketFrames; ++frame) {
        const auto sample = static_cast<std::int16_t>(100 + frame + 1);
        const auto index = static_cast<std::size_t>(48 + frame);
        CHECK(left[index] == doctest::Approx(expected(sample)));
        CHECK(right[index] == doctest::Approx(expected(static_cast<std::int16_t>(-sample))));
    }
}

TEST_CASE("OP9: a tear already merged into the backlog is reported, not hidden") {
    // THE LIMITATION, PINNED. If the ISR appended the next payload behind a
    // torn one before the pass ran, the boundary between them is not in the
    // fifo at all — tu_fifo_t has no item-size field — so NO read size can put
    // it back. This case therefore asserts what the path does guarantee in that
    // state (nothing is silently dropped, the tear is still counted, and the
    // backlog is visible so the condition is diagnosable) and deliberately does
    // NOT assert L/R alignment across the merged boundary, because the headers
    // do not claim it. What the path DOES do about the merge — flush the fifo
    // so the misalignment cannot outlive it — is OP14/OP15 in the sibling flush
    // suite; here the fifo is already empty when the tear is detected, so the
    // flush discards nothing and none of the counts below move.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(48) + 2);
    fifo.push(makePayload(kMaxPacketFrames, 100), bytesFor(kMaxPacketFrames));

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const std::vector<OutServicePass> passes = drainFifo(fifo, path, buffer, ring, stats);

    REQUIRE(passes.size() == 2);
    // The condition is OBSERVABLE: after the first bounded read the fifo still
    // holds more, which is exactly the signal that payloads queued up behind us.
    CHECK(passes[0].backlogBytes > 0);

    // 390 bytes queued: 97 whole frames and a 2-byte remainder. Every whole
    // frame is consumed — the merge costs alignment, never audio.
    CHECK(passes[0].framesConsumed + passes[1].framesConsumed == 97);
    CHECK(stats.malformedPayloads == 1u);
    CHECK(ring.occupancy() == 97);
}

// ============================================================================
// OP10: the chunk loop
// ============================================================================

TEST_CASE("OP10: a multi-packet call de-interleaves every chunk from the right offset") {
    // consumePacket() stays total for any byte count, so a caller handing over
    // three packets' worth in one call must get all 147 frames back in order.
    // The loop's offset is in FRAMES and indexes SAMPLES: dropping the
    // kChannels factor, or accumulating framesConsumed wrong, shows up here as
    // wrong sample values from frame 49 onward and nowhere else.
    constexpr int kFrames = 3 * kMaxPacketFrames;

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(kFrames);
    const OutPacketResult result =
        path.consumePacket(payload.data(), bytesFor(kFrames), ring, stats);

    CHECK(result.framesConsumed == kFrames);
    CHECK(result.framesDropped == 0);
    CHECK(result.chunks == 3);
    CHECK(result.wasTruncated == false);
    CHECK(stats.inputOverruns == 0u);
    REQUIRE(ring.occupancy() == kFrames);

    std::vector<float> left(static_cast<std::size_t>(kFrames));
    std::vector<float> right(static_cast<std::size_t>(kFrames));
    float* dst[kChannels] = {left.data(), right.data()};
    REQUIRE(ring.read(dst, kFrames) == 0);

    for (int frame = 0; frame < kFrames; ++frame) {
        const auto sample = static_cast<std::int16_t>(frame + 1);
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(expected(sample)));
        CHECK(right[static_cast<std::size_t>(frame)] ==
              doctest::Approx(expected(static_cast<std::int16_t>(-sample))));
    }
}

TEST_CASE("OP10: inputOverruns counts once per CALL even when several chunks overflow") {
    // 147 frames into a 64-frame ring: chunk 1 fits, chunks 2 and 3 both
    // overflow. inputOverruns is an EVENT count per packet (transport-stats.h),
    // so it must read 1, not 2 and not 3 — the frame-level detail is in
    // framesDropped.
    constexpr int kFrames = 3 * kMaxPacketFrames;

    SmallRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(kFrames);
    const OutPacketResult result =
        path.consumePacket(payload.data(), bytesFor(kFrames), ring, stats);

    CHECK(result.chunks == 3);
    CHECK(result.framesConsumed == kFrames);
    CHECK(result.framesDropped == kFrames - kSmallCapacity);
    CHECK(stats.inputOverruns == 1u);
    REQUIRE(ring.occupancy() == kSmallCapacity);

    // AR3: the NEWEST frames survive, so the ring ends on the last frame of the
    // last chunk.
    std::vector<float> left(static_cast<std::size_t>(kSmallCapacity));
    std::vector<float> right(static_cast<std::size_t>(kSmallCapacity));
    float* dst[kChannels] = {left.data(), right.data()};
    REQUIRE(ring.read(dst, kSmallCapacity) == 0);
    CHECK(left[static_cast<std::size_t>(kSmallCapacity - 1)] ==
          doctest::Approx(expected(static_cast<std::int16_t>(kFrames))));
}

// ============================================================================
// OP11: a negative byte count
// ============================================================================

TEST_CASE("OP11: a negative byte count is an empty payload, not a malformed one") {
    // tud_audio_read() returns uint16_t, so a negative count can only come from
    // a caller's own arithmetic. It must not be counted in malformedPayloads,
    // which describes the HOST's bytes; and it must still reach the ring so the
    // promotion rule stays evaluated in exactly one place (AR7).
    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(4);
    REQUIRE(ring.state() == RingState::Priming);

    const OutPacketResult result =
        path.consumePacket(payload.data(), -bytesFor(1), ring, stats);

    CHECK(result.framesConsumed == 0);
    CHECK(result.framesDropped == 0);
    CHECK(result.chunks == 1);
    CHECK(result.wasTruncated == false);
    CHECK(stats.malformedPayloads == 0u);
    CHECK(stats.inputOverruns == 0u);
    CHECK(ring.occupancy() == 0);
    CHECK(ring.state() == RingState::Running);
}

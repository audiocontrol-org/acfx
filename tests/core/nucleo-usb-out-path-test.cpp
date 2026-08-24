#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-out-path.h"
#include "support/allocation-sentinel.h"

// Polled OUT path (host -> device) contract: FR-024, FR-025, FR-028, FR-028a.
//
// The OUT stream is an ADAPTIVE SINK. The host's SOF is the only sample clock
// (FR-024, D20); the device asserts no rate of its own and consumes whatever
// the host paces to it (FR-025). Everything below therefore exercises the path
// with the payload sizes the host is actually allowed to send — 0 to 49 stereo
// frames inclusive (FR-028) — plus the torn byte counts a byte FIFO with no
// frame framing can hand back (R13.3).
//
// The TinyUSB call itself (tud_audio_read() into a packet buffer) stays in
// nucleo-main.cpp; UsbOutPath::consumePacket() is the whole truncate-count-
// convert-write sequence, expressed over a plain byte count so it is testable
// on the host (adapters/nucleo/support is host-compilable by construction).
//
// Cases cover:
// OP1 — a zero-length payload is a legal packet: consumed, not counted malformed,
//       and it still reaches the ring so the ring's promotion rule sees it.
// OP2 — a 1-frame payload.
// OP3 — a full 49-frame payload (kMaxPacketFrames).
// OP4 — FR-028a: a torn byte count truncates to whole frames; the whole frames ARE
//       consumed and malformedPayloads increments EXACTLY ONCE per torn packet.
// OP5 — ring overflow is reported, not absorbed: inputOverruns increments and the
//       NEWEST frames survive (AudioRing AR3).
// OP6 — no code path assumes 48: every frame count in [0, 49] round-trips exactly,
//       and 48 is in no way distinguished from its neighbours.
// OP7 — L/R alignment survives truncation, which is the whole reason FR-028a
//       truncates instead of rejecting the packet.
// OP8 — the path allocates nothing (Constitution real-time safety).

using namespace acfx::nucleo;
using acfx::test::AllocationSentinel;

namespace {

// Capacity chosen so a single maximum packet fits comfortably and two do not:
// 2 * kMaxPacketFrames == 98 > 64, which is what OP5 needs to force an overflow
// without contriving a degenerate one-frame ring.
constexpr int kTestCapacity = 64;

using TestRing = AudioRing<kTestCapacity, kChannels>;

// Build an interleaved stereo payload of `frames` frames. Left samples count
// up from `base`, right samples are the left sample negated, so a de-interleave
// that crosses the channels is visible immediately rather than only in a
// magnitude comparison.
std::vector<std::int16_t> makePayload(int frames, std::int16_t base = 0) {
    std::vector<std::int16_t> payload(static_cast<std::size_t>(kChannels * frames));
    for (int frame = 0; frame < frames; ++frame) {
        const auto left = static_cast<std::int16_t>(base + frame + 1);
        payload[static_cast<std::size_t>(kChannels * frame)] = left;
        payload[static_cast<std::size_t>(kChannels * frame + 1)] =
            static_cast<std::int16_t>(-left);
    }
    return payload;
}

// The float value deinterleaveToFloat() must produce for int16 `sample`.
float expected(std::int16_t sample) noexcept {
    return static_cast<float>(sample) / kInt16Scale;
}

int bytesFor(int frames) noexcept {
    return frames * kChannels * static_cast<int>(sizeof(std::int16_t));
}

} // namespace

// ============================================================================
// OP1: a zero-length payload is a legal packet
// ============================================================================

TEST_CASE("OP1: a zero-length payload consumes nothing and is not malformed") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const OutPacketResult result = path.consumePacket(nullptr, 0, ring, stats);

    CHECK(result.framesConsumed == 0);
    CHECK(result.framesDropped == 0);
    CHECK(result.wasTruncated == false);
    CHECK(stats.malformedPayloads == 0u);
    CHECK(stats.inputOverruns == 0u);
    CHECK(ring.occupancy() == 0);
}

TEST_CASE("OP1: a zero-length payload still reaches the ring's promotion rule") {
    // AudioRing evaluates its Priming -> Running threshold at the END of every
    // write(), including a zero-frame one. An OUT path that short-circuited on
    // an empty packet would silently change that documented behaviour, so the
    // empty packet must still be handed to the ring rather than swallowed.
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    REQUIRE(ring.state() == RingState::Priming);
    (void)path.consumePacket(nullptr, 0, ring, stats);
    CHECK(ring.state() == RingState::Running);
}

// ============================================================================
// OP2 / OP3: the ends of the legal payload range
// ============================================================================

TEST_CASE("OP2: a 1-frame payload is converted, de-interleaved and written") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(1);
    const OutPacketResult result =
        path.consumePacket(payload.data(), bytesFor(1), ring, stats);

    CHECK(result.framesConsumed == 1);
    CHECK(result.framesDropped == 0);
    CHECK(result.wasTruncated == false);
    CHECK(stats.malformedPayloads == 0u);
    CHECK(ring.occupancy() == 1);

    float left[1] = {0.0f};
    float right[1] = {0.0f};
    float* dst[kChannels] = {left, right};
    CHECK(ring.read(dst, 1) == 0);
    CHECK(left[0] == doctest::Approx(expected(1)));
    CHECK(right[0] == doctest::Approx(expected(-1)));
}

TEST_CASE("OP3: a full 49-frame payload is consumed whole") {
    REQUIRE(kMaxPacketFrames == 49);

    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(kMaxPacketFrames);
    const OutPacketResult result =
        path.consumePacket(payload.data(), bytesFor(kMaxPacketFrames), ring, stats);

    CHECK(result.framesConsumed == kMaxPacketFrames);
    CHECK(result.framesDropped == 0);
    CHECK(stats.malformedPayloads == 0u);
    CHECK(stats.inputOverruns == 0u);
    CHECK(ring.occupancy() == kMaxPacketFrames);

    std::vector<float> left(static_cast<std::size_t>(kMaxPacketFrames));
    std::vector<float> right(static_cast<std::size_t>(kMaxPacketFrames));
    float* dst[kChannels] = {left.data(), right.data()};
    CHECK(ring.read(dst, kMaxPacketFrames) == 0);
    CHECK(left[0] == doctest::Approx(expected(1)));
    CHECK(right[0] == doctest::Approx(expected(-1)));
    CHECK(left[static_cast<std::size_t>(kMaxPacketFrames - 1)] ==
          doctest::Approx(expected(kMaxPacketFrames)));
    CHECK(right[static_cast<std::size_t>(kMaxPacketFrames - 1)] ==
          doctest::Approx(expected(-kMaxPacketFrames)));
}

// ============================================================================
// OP4 (FR-028a): torn payloads truncate to whole frames and are COUNTED
// ============================================================================

TEST_CASE("OP4: a torn payload truncates to whole frames and counts once") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    // 10 whole frames plus a 3-byte remainder: the largest tear a 4-byte
    // stereo frame admits.
    const std::vector<std::int16_t> payload = makePayload(kMaxPacketFrames);
    const OutPacketResult result =
        path.consumePacket(payload.data(), bytesFor(10) + 3, ring, stats);

    CHECK(result.framesConsumed == 10);
    CHECK(result.wasTruncated == true);
    CHECK(stats.malformedPayloads == 1u);
    CHECK(ring.occupancy() == 10);
}

TEST_CASE("OP4: every remainder size 1..3 truncates and counts exactly once") {
    for (int remainder = 1; remainder <= 3; ++remainder) {
        TestRing ring(0);
        AudioTransportStats stats;
        UsbOutPath path;

        const std::vector<std::int16_t> payload = makePayload(4);
        const OutPacketResult result =
            path.consumePacket(payload.data(), bytesFor(4) + remainder, ring, stats);

        CHECK(result.framesConsumed == 4);
        CHECK(result.wasTruncated == true);
        CHECK(stats.malformedPayloads == 1u);
        CHECK(ring.occupancy() == 4);
    }
}

TEST_CASE("OP4: malformedPayloads is an EVENT count, one per torn packet") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(2);
    for (int packet = 0; packet < 5; ++packet) {
        (void)path.consumePacket(payload.data(), bytesFor(2) + 1, ring, stats);
    }
    CHECK(stats.malformedPayloads == 5u);

    // A well-formed packet must not touch the counter.
    (void)path.consumePacket(payload.data(), bytesFor(2), ring, stats);
    CHECK(stats.malformedPayloads == 5u);
}

TEST_CASE("OP4: a tear shorter than one frame yields zero frames, still counted") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(1);
    const OutPacketResult result = path.consumePacket(payload.data(), 3, ring, stats);

    CHECK(result.framesConsumed == 0);
    CHECK(result.wasTruncated == true);
    CHECK(stats.malformedPayloads == 1u);
    CHECK(ring.occupancy() == 0);
}

// ============================================================================
// OP5: overflow is reported, never absorbed
// ============================================================================

TEST_CASE("OP5: a payload that overflows the ring increments inputOverruns") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    // Two maximum packets against a 64-frame ring: 98 frames offered, 34 must
    // be dropped, and AudioRing drops the OLDEST (AR3).
    const std::vector<std::int16_t> first = makePayload(kMaxPacketFrames, 0);
    const std::vector<std::int16_t> second = makePayload(kMaxPacketFrames, 100);

    const OutPacketResult a =
        path.consumePacket(first.data(), bytesFor(kMaxPacketFrames), ring, stats);
    CHECK(a.framesDropped == 0);
    CHECK(stats.inputOverruns == 0u);

    const OutPacketResult b =
        path.consumePacket(second.data(), bytesFor(kMaxPacketFrames), ring, stats);
    CHECK(b.framesConsumed == kMaxPacketFrames);
    CHECK(b.framesDropped == 2 * kMaxPacketFrames - kTestCapacity);
    CHECK(stats.inputOverruns == 1u);
    CHECK(ring.occupancy() == kTestCapacity);

    // AR3: the NEWEST frames survive. The last frame in the ring must be the
    // last frame of the second packet.
    std::vector<float> left(static_cast<std::size_t>(kTestCapacity));
    std::vector<float> right(static_cast<std::size_t>(kTestCapacity));
    float* dst[kChannels] = {left.data(), right.data()};
    CHECK(ring.read(dst, kTestCapacity) == 0);
    CHECK(left[static_cast<std::size_t>(kTestCapacity - 1)] ==
          doctest::Approx(expected(static_cast<std::int16_t>(100 + kMaxPacketFrames))));
}

TEST_CASE("OP5: inputOverruns is an event count, one per overflowing packet") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(kMaxPacketFrames);
    for (int packet = 0; packet < 4; ++packet) {
        (void)path.consumePacket(payload.data(), bytesFor(kMaxPacketFrames), ring, stats);
    }

    // Packet 0 fits (49 <= 64). Packets 1, 2 and 3 each overflow.
    CHECK(stats.inputOverruns == 3u);
    CHECK(ring.occupancy() == kTestCapacity);
}

// ============================================================================
// OP6: nothing on this path assumes 48
// ============================================================================

TEST_CASE("OP6: every frame count in [0, 49] round-trips exactly, 48 included") {
    for (int frames = 0; frames <= kMaxPacketFrames; ++frames) {
        TestRing ring(0);
        AudioTransportStats stats;
        UsbOutPath path;

        const std::vector<std::int16_t> payload = makePayload(frames);
        const OutPacketResult result =
            path.consumePacket(payload.data(), bytesFor(frames), ring, stats);

        CHECK(result.framesConsumed == frames);
        CHECK(result.framesDropped == 0);
        CHECK(result.wasTruncated == false);
        CHECK(ring.occupancy() == frames);
        CHECK(stats.malformedPayloads == 0u);
        CHECK(stats.inputOverruns == 0u);
    }
}

TEST_CASE("OP6: 48 is not distinguished from 47 or 49") {
    // The sizes either side of kBlockFrames behave identically in kind: same
    // consumed-equals-offered rule, same untouched counters. If any branch ever
    // special-cased the block size, one of these three would diverge.
    for (const int frames : {kBlockFrames - 1, kBlockFrames, kBlockFrames + 1}) {
        TestRing ring(0);
        AudioTransportStats stats;
        UsbOutPath path;

        const std::vector<std::int16_t> payload = makePayload(frames);
        const OutPacketResult result =
            path.consumePacket(payload.data(), bytesFor(frames), ring, stats);

        CHECK(result.framesConsumed == frames);
        CHECK(ring.occupancy() == frames);
        CHECK(stats.inputOverruns == 0u);
        CHECK(stats.malformedPayloads == 0u);
    }
}

// ============================================================================
// OP7: truncation preserves L/R alignment — the reason FR-028a truncates
// ============================================================================

TEST_CASE("OP7: a truncated payload leaves L/R alignment intact") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::int16_t> payload = makePayload(6);
    const OutPacketResult result =
        path.consumePacket(payload.data(), bytesFor(5) + 2, ring, stats);
    REQUIRE(result.framesConsumed == 5);

    std::vector<float> left(5);
    std::vector<float> right(5);
    float* dst[kChannels] = {left.data(), right.data()};
    CHECK(ring.read(dst, 5) == 0);

    for (int frame = 0; frame < 5; ++frame) {
        const auto sample = static_cast<std::int16_t>(frame + 1);
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(expected(sample)));
        CHECK(right[static_cast<std::size_t>(frame)] ==
              doctest::Approx(expected(static_cast<std::int16_t>(-sample))));
    }
}

// ============================================================================
// OP8: real-time safety
// ============================================================================

TEST_CASE("OP8: consumePacket allocates nothing") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    const std::vector<std::int16_t> payload = makePayload(kMaxPacketFrames);

    AllocationSentinel::reset();
    for (int iter = 0; iter < 100; ++iter) {
        for (int frames = 0; frames <= kMaxPacketFrames; ++frames) {
            (void)path.consumePacket(payload.data(), bytesFor(frames), ring, stats);
            (void)path.consumePacket(payload.data(), bytesFor(frames) + 1, ring, stats);
        }
    }

    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "consumePacket allocated ", allocations);
}

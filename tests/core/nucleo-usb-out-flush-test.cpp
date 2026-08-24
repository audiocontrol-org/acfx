#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-out-path.h"

#include "nucleo-usb-out-test-support.h"

// Polled OUT path, part 3: CLEAR-ON-TEAR — what serviceOutFifo() does to the
// fifo AFTER a read whose byte count was not a whole number of stereo frames.
//
// The trigger has no false positives: a well-formed run of USB audio payloads
// is always a whole number of 4-byte stereo frames, so a non-multiple-of-4 read
// is unambiguous evidence of a tear. On that signal the path consumes the whole
// frames it did get (FR-028a, unchanged), flushes the fifo so the next read
// provably restarts on a packet boundary, and COUNTS the discard — FR-032
// forbids throwing host audio away silently.
//
// WHAT THESE CASES DELIBERATELY DO NOT ASSERT, because it is not true. The
// flush does not repair frames already written to the ring, and it does not
// defend against a merged backlog. See the numbered analysis at the top of
// support/usb-out-path.h: because the read is bounded to 196 bytes (itself a
// multiple of 4) and tu_fifo_read_n returns min(queued, requested), a torn read
// is necessarily a read that emptied the fifo — so in the ordinary case the
// flush discards nothing, and the only way it discards anything is the ISR race
// modelled by FakeOutFifo::injectAfterNextRead(). OP16 pins that magnitude
// honestly rather than pretending the flush recovered audio.
//
// Cases:
// OP12 — a torn read flushes exactly once; a clean read never flushes.
// OP13 — the counted magnitude is exactly what available() reported before the
//        flush, in both frames and bytes, and it accumulates across events.
// OP14 — the worked merged-payload scenario end to end: the tear is counted,
//        the flush happens, and the payload AFTER it is L/R correct. The
//        straddling frames from the first pass are NOT asserted clean.
// OP15 — a chronically backlogged stream never trips the trigger at all. The
//        limitation, pinned so it cannot be quietly reworded into a guarantee.
// OP16 — a fifo that refuses to clear counts neither a flush nor its frames.

using namespace acfx::nucleo;
using namespace nucleo_out_test;

// ============================================================================
// OP12: the trigger fires on a tear and only on a tear
// ============================================================================

TEST_CASE("OP12: a torn read flushes the OUT fifo exactly once") {
    // 48 whole frames plus a 2-byte remainder: 194 bytes, not a multiple of 4.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(48) + 2);

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const OutServicePass pass = serviceOutFifo(fifo, path, buffer, ring, stats);

    CHECK(pass.bytesRead == bytesFor(48) + 2);
    CHECK(pass.wasTruncated == true);
    CHECK(pass.framesConsumed == 48);   // FR-028a is unchanged by the flush
    CHECK(pass.flushed == true);
    CHECK(fifo.clearCalls() == 1);      // once, not once per frame or per chunk
    CHECK(stats.malformedPayloads == 1u);
    CHECK(stats.inputFifoFlushes == 1u);
    CHECK(ring.occupancy() == 48);

    // A second, clean pass must not flush again — the flush is an edge, not a
    // sticky mode.
    fifo.push(makePayload(kMaxPacketFrames, 100), bytesFor(kMaxPacketFrames));
    const OutServicePass second = serviceOutFifo(fifo, path, buffer, ring, stats);
    CHECK(second.flushed == false);
    CHECK(fifo.clearCalls() == 1);
    CHECK(stats.inputFifoFlushes == 1u);
}

TEST_CASE("OP12: a clean whole-frame read never flushes") {
    // The over-eager-flush case. Every payload size in [0, 49] frames is legal
    // (FR-028) and none of them is evidence of anything, so none may flush —
    // including the empty read the idle service loop performs constantly, and
    // including a full backlog, which is ordinary jitter and NOT a tear.
    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    FakeOutFifo idle;
    const OutServicePass empty = serviceOutFifo(idle, path, buffer, ring, stats);
    CHECK(empty.bytesRead == 0);
    CHECK(empty.flushed == false);
    CHECK(idle.clearCalls() == 0);

    for (int frames = 0; frames <= kMaxPacketFrames; ++frames) {
        FakeOutFifo fifo;
        fifo.push(makePayload(frames), bytesFor(frames));
        const OutServicePass pass = serviceOutFifo(fifo, path, buffer, ring, stats);
        CHECK(pass.wasTruncated == false);
        CHECK(pass.flushed == false);
        CHECK(pass.flushedBytes == 0);
        CHECK(pass.flushedFrames == 0);
        CHECK(fifo.clearCalls() == 0);
    }

    // A three-packet backlog: bounded reads, a reported backlog, no flush.
    FakeOutFifo backlogged;
    for (int payload = 0; payload < 3; ++payload) {
        backlogged.push(makePayload(kMaxPacketFrames, static_cast<std::int16_t>(100 * payload)),
                        bytesFor(kMaxPacketFrames));
    }
    const std::vector<OutServicePass> passes =
        drainFifo(backlogged, path, buffer, ring, stats);
    REQUIRE(passes.size() == 3);
    CHECK(passes[0].backlogBytes == 2 * UsbOutPath::maxPayloadBytes());
    CHECK(backlogged.clearCalls() == 0);

    CHECK(stats.inputFifoFlushes == 0u);
    CHECK(stats.inputFifoFlushedFrames == 0u);
    CHECK(stats.malformedPayloads == 0u);
}

// ============================================================================
// OP13: the counted magnitude
// ============================================================================

TEST_CASE("OP13: the counted discard is exactly what the fifo reported before the flush") {
    // A tear, with a whole payload landing behind it in the window between the
    // read returning and the flush running (the ISR race — the only way the
    // fifo is non-empty at flush time). The flush throws that payload away, so
    // the counters must show 196 bytes / 49 frames gone, matching byte for byte
    // what the fifo reports it discarded.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(48) + 2);
    fifo.injectAfterNextRead(makePayload(kMaxPacketFrames, 100), bytesFor(kMaxPacketFrames));

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const OutServicePass pass = serviceOutFifo(fifo, path, buffer, ring, stats);

    REQUIRE(pass.flushed == true);
    CHECK(pass.flushedBytes == bytesFor(kMaxPacketFrames));
    CHECK(pass.flushedFrames == kMaxPacketFrames);
    // The magnitude the ADAPTER counted and the magnitude the FIFO actually
    // threw away are the same number, independently observed.
    CHECK(fifo.lastClearDiscardedBytes() == pass.flushedBytes);
    CHECK(stats.inputFifoFlushes == 1u);
    CHECK(stats.inputFifoFlushedFrames == static_cast<std::uint32_t>(kMaxPacketFrames));
    // The flush is what emptied the fifo, so nothing is left for the next pass.
    CHECK(pass.backlogBytes == 0);

    // A second event accumulates rather than replacing: the frame counter is a
    // lifetime sum, the event counter a lifetime count.
    fifo.push(makePayload(kMaxPacketFrames, 200), bytesFor(10) + 1);
    fifo.injectAfterNextRead(makePayload(20, 300), bytesFor(20));
    const OutServicePass second = serviceOutFifo(fifo, path, buffer, ring, stats);
    REQUIRE(second.flushed == true);
    CHECK(second.flushedFrames == 20);
    CHECK(stats.inputFifoFlushes == 2u);
    CHECK(stats.inputFifoFlushedFrames == static_cast<std::uint32_t>(kMaxPacketFrames + 20));
}

TEST_CASE("OP13: a flush that finds the fifo empty is counted as an event, not as frames") {
    // The ORDINARY case, and the reason the two counters have different units.
    // A torn read is necessarily a read that drained the fifo, so the flush
    // that follows it normally discards nothing at all. The event still
    // happened and is still counted; claiming frames here would be a fiction.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(48) + 3);

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const OutServicePass pass = serviceOutFifo(fifo, path, buffer, ring, stats);

    CHECK(pass.flushed == true);
    CHECK(pass.flushedBytes == 0);
    CHECK(pass.flushedFrames == 0);
    CHECK(stats.inputFifoFlushes == 1u);
    CHECK(stats.inputFifoFlushedFrames == 0u);
}

// ============================================================================
// OP14: the merged-payload scenario, end to end
// ============================================================================

TEST_CASE("OP14: after a merged tear is flushed, the NEXT payload is L/R correct") {
    // The worked example. fifo = A torn to 194 B, then a whole 196 B B.
    //   pass 1 reads 196 B: 196 % 4 == 0, so NO tear is visible. 49 frames go
    //          to the ring and frame 48 straddles the tear. Already corrupt,
    //          before any signal exists. This case does NOT assert those frames
    //          are clean, because they are not.
    //   pass 2 reads the remaining 194 B: the tear is visible, 48 frames are
    //          consumed (shifted, hence L/R swapped), 2 bytes are discarded and
    //          counted, and the fifo is flushed.
    // What IS guaranteed, and is asserted below: the tear is counted, the flush
    // happens, no whole frame is silently dropped, and the payload delivered
    // after the flush is byte-for-byte L/R correct.
    const std::vector<std::int16_t> a = makePayload(kMaxPacketFrames, 0);
    const std::vector<std::int16_t> b = makePayload(kMaxPacketFrames, 100);

    FakeOutFifo fifo;
    fifo.push(a, bytesFor(48) + 2);
    fifo.push(b, bytesFor(kMaxPacketFrames));

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const std::vector<OutServicePass> passes = drainFifo(fifo, path, buffer, ring, stats);

    REQUIRE(passes.size() == 2);
    CHECK(passes[0].bytesRead == UsbOutPath::maxPayloadBytes());
    CHECK(passes[0].wasTruncated == false);   // the tear is invisible here
    CHECK(passes[0].flushed == false);
    CHECK(passes[1].bytesRead == bytesFor(48) + 2);
    CHECK(passes[1].wasTruncated == true);    // and unambiguous here
    CHECK(passes[1].flushed == true);
    CHECK(fifo.clearCalls() == 1);
    CHECK(stats.malformedPayloads == 1u);
    CHECK(stats.inputFifoFlushes == 1u);

    // 390 bytes queued = 97 whole frames and a 2-byte remainder. Every whole
    // frame reached the ring: the merge costs alignment, never audio.
    CHECK(passes[0].framesConsumed + passes[1].framesConsumed == 97);
    REQUIRE(ring.occupancy() == 97);

    // Drain the damaged frames off, then deliver a fresh payload through the
    // flushed fifo. THIS is the assertion that matters: alignment going forward.
    std::vector<float> left;
    std::vector<float> right;
    REQUIRE(drainRing(ring, left, right, 97) == 0);

    const std::vector<std::int16_t> c = makePayload(kMaxPacketFrames, 200);
    fifo.push(c, bytesFor(kMaxPacketFrames));
    const OutServicePass after = serviceOutFifo(fifo, path, buffer, ring, stats);
    CHECK(after.bytesRead == bytesFor(kMaxPacketFrames));
    CHECK(after.wasTruncated == false);
    CHECK(after.flushed == false);
    REQUIRE(ring.occupancy() == kMaxPacketFrames);

    REQUIRE(drainRing(ring, left, right, kMaxPacketFrames) == 0);
    for (int frame = 0; frame < kMaxPacketFrames; ++frame) {
        const auto sample = static_cast<std::int16_t>(200 + frame + 1);
        const auto index = static_cast<std::size_t>(frame);
        CHECK(left[index] == doctest::Approx(expected(sample)));
        CHECK(right[index] == doctest::Approx(expected(static_cast<std::int16_t>(-sample))));
    }
}

// ============================================================================
// OP15: the limitation clear-on-tear does NOT cover
// ============================================================================

TEST_CASE("OP15: a backlog that never drains never trips the trigger") {
    // PINNED SO IT CANNOT BE QUIETLY REWORDED INTO A GUARANTEE. While the fifo
    // holds at least one maximum packet, every read returns a full 196 bytes —
    // a multiple of 4 — so a tear buried in that backlog is invisible and the
    // flush never fires, however long the misalignment lasts. The read bound
    // and backlogBytes are what make that state diagnosable; clear-on-tear is
    // not a defence against it and this case exists to say so.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(48) + 2);   // the tear
    for (int payload = 0; payload < 3; ++payload) {
        fifo.push(makePayload(kMaxPacketFrames, static_cast<std::int16_t>(100 * (payload + 1))),
                  bytesFor(kMaxPacketFrames));
    }

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    // Stop while at least one whole packet is still queued — i.e. never let the
    // backlog drain to the partial read that would expose the tear.
    int passes = 0;
    while (fifo.available() >= UsbOutPath::maxPayloadBytes()) {
        const OutServicePass pass = serviceOutFifo(fifo, path, buffer, ring, stats);
        CHECK(pass.bytesRead == UsbOutPath::maxPayloadBytes());
        CHECK(pass.wasTruncated == false);
        CHECK(pass.flushed == false);
        ++passes;
    }

    CHECK(passes == 3);
    CHECK(fifo.clearCalls() == 0);
    CHECK(stats.inputFifoFlushes == 0u);
    CHECK(stats.malformedPayloads == 0u);
    // Still queued, still misaligned, still undetected — and REPORTED, which is
    // the only thing this path claims about the condition.
    CHECK(fifo.available() == bytesFor(48) + 2);
}

// ============================================================================
// OP16: a fifo that refuses to clear
// ============================================================================

TEST_CASE("OP16: a refused clear counts neither a flush nor its frames") {
    // tud_audio_clear_ep_out_ff() returns false when its TU_VERIFY fails — the
    // audio function is not configured — and leaves the fifo untouched. Nothing
    // was discarded, so counting a flush or a discarded frame would be a
    // fiction, and FR-032 cuts both ways: no silent discard, and no invented
    // one either. The caller still sees the condition as wasTruncated == true
    // with flushed == false.
    FakeOutFifo fifo;
    fifo.push(makePayload(kMaxPacketFrames, 0), bytesFor(48) + 2);
    fifo.injectAfterNextRead(makePayload(kMaxPacketFrames, 100), bytesFor(kMaxPacketFrames));
    fifo.refuseClear();

    BigRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;
    StagingBuffer buffer = {};

    const OutServicePass pass = serviceOutFifo(fifo, path, buffer, ring, stats);

    CHECK(pass.wasTruncated == true);
    CHECK(pass.flushed == false);
    CHECK(pass.flushedBytes == 0);
    CHECK(pass.flushedFrames == 0);
    CHECK(fifo.clearCalls() == 1);              // it was attempted
    CHECK(stats.inputFifoFlushes == 0u);        // and it did not happen
    CHECK(stats.inputFifoFlushedFrames == 0u);
    CHECK(stats.malformedPayloads == 1u);       // the tear itself is still counted
    // The refusal left the bytes in place, so they are still there to be read.
    CHECK(pass.backlogBytes == bytesFor(kMaxPacketFrames));
}

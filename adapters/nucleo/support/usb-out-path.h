#pragma once

// Platform-independent OUT-path (host -> device) packet consumption for the
// Nucleo USB audio adapter (FR-024, FR-025, FR-028, FR-028a). No TinyUSB, no
// CMSIS, no board headers, no <cstdio> — this header compiles under the `test`
// preset with no toolchain file. Anything that cannot satisfy that constraint
// belongs in the shim (adapters/nucleo/usb-audio-service.h) instead.
//
// WHAT LIVES HERE AND WHAT DOES NOT.
// The TinyUSB calls are two lines and they stay in the shim: usb-audio-
// service.h owns a tiny adapter type whose `read()` is `tud_audio_read()` and
// whose `available()` is `tud_audio_available()`. Everything else — how many
// bytes to ask for, truncate, count, convert, de-interleave, write, and what
// to report about the leftover backlog — is ordinary logic with no platform
// dependency, so it lives here where the host test binary can reach it
// (adapters/nucleo/support is linked into acfx_core_tests as
// acfx_nucleo_support, FR-049).
//
// THE OUT STREAM IS AN ADAPTIVE SINK (FR-024, FR-025, D20).
// The host's SOF is the only sample clock. Nothing below asserts, expects, or
// enforces a rate: whatever arrives is consumed. In particular there is no
// nominal packet size anywhere in this file. A UAC2 48 kHz stereo stream paces
// mostly-48-frame packets, but 0 to 49 frames inclusive are all legal (FR-028,
// D21) and every one of them takes the same path. `kBlockFrames` is deliberately
// not referenced here at all — block size is the DSP's cadence (FR-030a) and the
// ring is what stops transport framing from reaching it.
//
// ============================================================================
// PACKET FRAMING: WHAT THIS PATH CAN AND CANNOT GUARANTEE. READ THIS.
// ============================================================================
// TinyUSB's OUT endpoint software FIFO is a plain BYTE fifo. In the pinned
// 0.21.0 tree `tu_fifo_t` (src/common/tusb_fifo.h:119-132) carries a buffer
// pointer, a depth in bytes and read/write indices — and NO item-size or
// packet-boundary field of any kind. `tud_audio_read()` is
// `tu_fifo_read_n()` on that fifo (src/class/audio/audio_device.c:448-451) and
// returns a BYTE count. There is therefore no API, anywhere in that tree, that
// can tell us where one USB payload ended and the next began.
//
// WHAT WE DO ABOUT IT: serviceOutFifo() reads AT MOST one wMaxPacketSize worth
// of bytes per call (UsbOutPath::maxPayloadBytes(), 196 = 49 frames x 2 ch x
// 2 B) and lets the service loop come back for the rest on its next pass. Under
// normal operation — the loop iterating far faster than the host's 1 ms SOF
// cadence — the fifo holds at most one payload, so a torn payload is read
// alone, FR-028a truncation lands on the torn payload itself, and the payload
// that follows it starts at byte 0 of the next read, still L/R aligned.
//
// WHAT WE CANNOT DO ABOUT IT, STATED PLAINLY: if the ISR has already appended a
// second payload behind a torn first one before we read, the boundary between
// them is genuinely unrecoverable — it does not exist in the data structure.
// A bounded read then cuts somewhere inside the merged bytes and everything
// after the tear is shifted by the 1-3 byte remainder, which for stereo means
// L and R swap. This path does NOT guarantee alignment in that case and no
// code here claims to. What it does instead is make the condition observable
// rather than silent: OutServicePass::backlogBytes reports what
// `tud_audio_available()` still holds after each pass (a value above one packet
// means the loop fell behind and alignment is at risk), and
// OutPacketResult::chunks is greater than 1 whenever a single call was handed
// more than one packet's worth of bytes.
//
// ----------------------------------------------------------------------------
// CLEAR-ON-TEAR: WHAT IT BUYS, AND WHAT IT DOES NOT. READ THIS TOO.
// ----------------------------------------------------------------------------
// A read whose byte count is not a whole multiple of the 4-byte stereo frame is
// UNAMBIGUOUS evidence of a tear — a whole payload is always a whole number of
// frames, so no well-formed run of payloads can produce such a count. On that
// signal serviceOutFifo() consumes the whole frames the read did contain
// (FR-028a, unchanged) and then FLUSHES the fifo, so the next read restarts on
// a real packet boundary.
//
// THIS DOES NOT ELIMINATE CORRUPTION, IT DOES NOT MAKE ALIGNMENT GUARANTEED,
// AND — MEASURED AGAINST THE PINNED FIFO'S ACTUAL SEMANTICS — IT DOES LESS
// THAN IT LOOKS LIKE IT DOES. Do not read more into it than the following.
//
// (1) WHEN THE SIGNAL CAN FIRE AT ALL. The read is bounded to 196 bytes, which
//     is itself a multiple of 4, and `tu_fifo_read_n()` returns
//     min(queued, requested) (src/common/tusb_fifo.c:505-515). A return that is
//     NOT a multiple of 4 is therefore necessarily a return SHORTER than 196,
//     which is necessarily a return that emptied the fifo. So at the instant a
//     tear becomes visible, the fifo is already empty — and FR-028a's
//     truncation, which threw the 1-3 byte remainder away as part of the read,
//     has ALREADY restored alignment for the next read. In that ordinary case
//     the flush discards zero bytes and changes nothing.
//
// (2) SO WHAT DOES IT ACTUALLY CATCH? Only the race: the ISR can append a
//     payload between the read returning and the flush running. Without the
//     flush that payload would be read next pass, whole and frame-aligned, and
//     it would be fine. With the flush it is discarded. Stated bluntly: on the
//     evidence in this tree, the non-zero-discard case of clear-on-tear is one
//     where the flush costs good audio rather than saving alignment. It buys a
//     structural guarantee — the next read provably begins on a packet
//     boundary, without relying on the read-bound argument in (1) holding for
//     every future fifo configuration — and it pays up to one OUT software fifo
//     (784 bytes = 196 frames, ~4 ms at 48 kHz) for it.
//
// (3) WHAT IT DOES NOT TOUCH AT ALL. The merged-backlog corruption. If the ISR
//     appended a payload B behind a torn payload A before the pass ran, the
//     first read takes 196 bytes spanning the tear: one frame straddles it and
//     the frames after it are shifted, and all of that reaches the ring BEFORE
//     any tear is detectable. No flush can retract it. Worse, a stream whose
//     backlog never drops below 196 bytes returns a full, 4-aligned 196 on
//     EVERY read, so the tear is never detected, the flush never fires, and the
//     shift persists for as long as the backlog does. Clear-on-tear is not a
//     defence against that; the read bound and the backlog high-water mark are
//     what make it visible.
//
// EVERY DISCARD IS COUNTED (FR-032). `stats.inputFifoFlushes` counts the
// flushes and `stats.inputFifoFlushedFrames` the frames they threw away. An
// uncounted discard is exactly the silent substitution FR-032 forbids, which is
// why the magnitude is read from the fifo BEFORE the flush rather than inferred
// after it.
//
// WHAT WE DELIBERATELY DO NOT DO: flush merely because the backlog exceeds one
// packet. A backlog is ordinary service-loop jitter, not evidence of anything,
// and flushing on it would convert a timing hiccup into a guaranteed audible
// dropout. Only a non-multiple-of-4 read triggers the flush, and that signal
// has no false positives.
//
// Real-time safety: no heap, no locks, no exceptions. The de-interleave scratch
// is a fixed member array sized for the 49-frame worst case.

#include <cstddef>
#include <cstdint>

#include "sample-format.h"
#include "transport-stats.h"

namespace acfx::nucleo {

// What one OUT packet did. Returned so the caller can observe the path without
// diffing counters, and so a future task can act on a drop (e.g. a diagnostic
// LED) without this header growing a policy it should not own.
struct OutPacketResult {
    // Whole stereo frames extracted from the payload and offered to the ring.
    // Equals the payload's frame count after FR-028a truncation.
    int framesConsumed = 0;

    // Frames the ring dropped to make room (AudioRing AR3 drops the OLDEST).
    // Zero on the normal path. This is a FRAME count; the matching statistic
    // `inputOverruns` is an EVENT count — see consumePacket().
    int framesDropped = 0;

    // De-interleave passes this call took. 1 on every normal packet, including
    // an empty one. A value above 1 means the caller handed over more than one
    // maximum packet's worth of bytes in a single call, which is exactly the
    // condition under which the file-header's framing limitation applies: the
    // bytes were all consumed, but the payload boundaries inside them were not
    // recoverable and L/R alignment across them is not guaranteed.
    int chunks = 0;

    // FR-028a: the payload's byte count was not a whole number of stereo
    // frames and a 1-3 byte remainder was discarded.
    bool wasTruncated = false;
};

// The polled OUT path. Holds only its de-interleave scratch; it owns no ring,
// no stats and no lifecycle, all of which are passed in per call so the shim
// keeps a single instance of each and this stays a pure transformation step.
class UsbOutPath {
public:
    // THE PER-CALL READ BOUND, and the largest payload one de-interleave pass
    // handles. One wMaxPacketSize: 49 frames x 2 channels x 2 bytes = 196.
    //
    // serviceOutFifo() asks the fifo for no more than this per call — see the
    // framing discussion in the file header for why the bound is the whole
    // point and not an arbitrary buffer size. The shim sizes its staging buffer
    // from this same function and static_asserts the two agree, which is what
    // welds the bound to the buffer across the platform seam.
    static constexpr int maxPayloadBytes() noexcept {
        return kMaxPacketFrames * kChannels * static_cast<int>(sizeof(std::int16_t));
    }

    // Consume one USB OUT payload: truncate to whole frames, count the
    // remainder, convert to float, de-interleave, and write to `ring`.
    //
    // `payload` points at `byteCount` bytes of interleaved 16-bit stereo PCM
    // as delivered by the host. `byteCount` is what the fifo read returned;
    // 0 is a legal, common value and is NOT an error (R13.3 records that a read
    // issued before the function is open also returns 0, which is why a zero
    // return must never be treated as a fault here).
    //
    // FR-028a — TRUNCATE, DO NOT REJECT. A byte count that is not a whole
    // number of stereo frames is truncated to the frames it does contain; the
    // whole frames are consumed and the 1-3 byte remainder is discarded and
    // COUNTED in `stats.malformedPayloads`. Rejecting the whole packet would
    // drop good frames, and — worse — consuming the torn remainder would swap
    // L and R for every subsequent frame in the stream. Note the scope of that
    // claim: truncation keeps the frames INSIDE this call aligned, and (given
    // serviceOutFifo()'s one-packet read bound) keeps the NEXT call's payload
    // aligned because that call starts at a fresh read. It cannot realign bytes
    // that were already merged into this call — see the file header.
    //
    // COUNTER SEMANTICS. `malformedPayloads` and `inputOverruns` are both
    // incremented at most ONCE per call, i.e. per packet, matching spec US2
    // AS2's "the oldest frames are dropped and `inputOverruns` increments" and
    // transport-stats.h's note that `malformedPayloads` is an event count. The
    // frame-level detail is not lost: it comes back in `framesDropped`.
    //
    // A zero-frame payload is still handed to `ring.write()` rather than
    // short-circuited. AudioRing evaluates its Priming -> Running threshold at
    // the end of EVERY write, including a zero-frame one (AR7), and that single
    // rule is what makes the promotion edge cases decidable; skipping the call
    // would quietly move that decision into this file.
    //
    // `Ring` is a template parameter rather than a concrete AudioRing so the
    // ring's capacity — an HIL-derived number pinned later (D23, FR-035) — does
    // not have to be spelled here.
    template <typename Ring>
    OutPacketResult consumePacket(const std::int16_t* payload,
                                  int byteCount,
                                  Ring& ring,
                                  AudioTransportStats& stats) noexcept {
        // A negative byte count cannot come from tud_audio_read() (it returns
        // uint16_t) and is a caller error, not a transport condition. This path
        // is noexcept and runs in the audio loop, so it cannot throw; treating
        // it as an empty payload is the only defined option, and it must NOT be
        // counted as malformed — malformedPayloads describes the host's bytes,
        // not our own arithmetic.
        const int bytes = (byteCount > 0) ? byteCount : 0;

        const PayloadFrameCount parsed = payloadToFrameCount(bytes);

        OutPacketResult result;
        result.wasTruncated = parsed.wasTruncated;
        if (parsed.wasTruncated) {
            ++stats.malformedPayloads;
        }

        float* channels[kChannels];
        for (int channel = 0; channel < kChannels; ++channel) {
            channels[channel] = scratch_[channel];
        }

        // Chunked so that a caller handing over more than one maximum packet
        // consumes all of it rather than overrunning the scratch or silently
        // clamping. serviceOutFifo() never does that — its read is bounded to
        // maxPayloadBytes() precisely so this loop runs exactly once per packet
        // — but consumePacket() is a public entry point over an arbitrary byte
        // count and must stay total for any of them. When the loop does run
        // more than once, `result.chunks` says so; see the file header for what
        // that costs in alignment terms.
        //
        // do/while, not while: a zero-frame payload must still reach
        // ring.write() exactly once (see the promotion note above).
        int remaining = parsed.frames;
        int offset = 0;
        bool overran = false;
        do {
            const int chunk = (remaining < kMaxPacketFrames) ? remaining : kMaxPacketFrames;

            deinterleaveToFloat(payload + static_cast<std::ptrdiff_t>(kChannels) * offset,
                                channels,
                                chunk);

            const int dropped = ring.write(channels, chunk);
            if (dropped > 0) {
                overran = true;
                result.framesDropped += dropped;
            }

            result.framesConsumed += chunk;
            ++result.chunks;
            offset += chunk;
            remaining -= chunk;
        } while (remaining > 0);

        if (overran) {
            ++stats.inputOverruns;
        }

        return result;
    }

private:
    // Non-interleaved, one contiguous run per channel, matching the
    // `float* const*` channel-pointer signature AudioRing::write() takes.
    // Sized for the 49-frame worst case (FR-028): fixed storage, no heap.
    float scratch_[kChannels][kMaxPacketFrames] = {};
};

// What one pass of the service loop did to the OUT fifo. `backlogBytes` is the
// observability the file header promises: it is what the fifo still held AFTER
// this pass, so a value above UsbOutPath::maxPayloadBytes() means payloads are
// queueing behind us and a tear in that backlog cannot be realigned. Note that
// after a flush it necessarily reads 0 — the flush is what emptied it.
struct OutServicePass {
    int bytesRead = 0;
    int framesConsumed = 0;
    int framesDropped = 0;
    int chunks = 0;
    int backlogBytes = 0;
    bool wasTruncated = false;

    // Clear-on-tear (see the file header). True when this pass detected a torn
    // read and the fifo's clear() reported success. False both when there was
    // no tear and when a clear() was attempted but refused — the fifo can only
    // refuse when the audio function is not configured, in which case it also
    // reported nothing queued, so a refusal never hides a discard.
    bool flushed = false;

    // What the fifo reported queued immediately BEFORE the flush, i.e. exactly
    // what the flush threw away. Both are zero on a pass that did not flush.
    // `flushedBytes` is the raw magnitude the fifo reported; `flushedFrames` is
    // the whole stereo frames inside it, which is what reaches the lifetime
    // counter. They differ by a 1-3 byte partial frame whenever the discarded
    // run was itself not frame-aligned.
    int flushedBytes = 0;
    int flushedFrames = 0;
};

// One service-loop pass over the OUT fifo: ONE bounded read, then consume it.
//
// `fifo` is anything providing
//     int read(std::int16_t* dst, int maxBytes) noexcept;   // bytes read
//     int available() noexcept;                             // bytes queued
//     bool clear() noexcept;                                // discard all
// which on the board is a three-line adapter over tud_audio_read() /
// tud_audio_available() / tud_audio_clear_ep_out_ff() (usb-audio-service.h) and
// in the host tests is a simulated unframed byte fifo. Templated rather than
// virtual: no allocation, no indirect call, and the shim's adapter inlines away
// entirely. `clear()` is part of the seam rather than a direct TinyUSB call
// from here for the same reason `read()` is: this header must keep compiling
// with no USB stack in sight, and a fake fifo in the host tests must be able to
// observe that the flush happened.
//
// EXACTLY ONE READ PER PASS, deliberately. Draining the whole fifo inside one
// call is what merges consecutive payloads into a single byte run and destroys
// the framing (file header). Looping ACROSS service-loop passes instead keeps
// each read to at most one payload, and the loop iterates far faster than the
// host's 1 ms cadence, so a transient backlog still clears promptly.
//
// Bounded work, no blocking: one bounded read plus one convert-and-write over
// at most 49 frames. That is what makes it safe to call from a tud_task()
// service loop whose USB servicing cadence depends on iterating promptly.
template <typename Fifo, typename Ring, std::size_t BufferSamples>
OutServicePass serviceOutFifo(Fifo& fifo,
                              UsbOutPath& path,
                              std::int16_t (&buffer)[BufferSamples],
                              Ring& ring,
                              AudioTransportStats& stats) noexcept {
    static_assert(BufferSamples * sizeof(std::int16_t) >=
                      static_cast<std::size_t>(UsbOutPath::maxPayloadBytes()),
                  "OUT staging buffer is smaller than one maximum packet");

    OutServicePass pass;
    pass.bytesRead = fifo.read(buffer, UsbOutPath::maxPayloadBytes());

    const OutPacketResult result = path.consumePacket(buffer, pass.bytesRead, ring, stats);
    pass.framesConsumed = result.framesConsumed;
    pass.framesDropped = result.framesDropped;
    pass.chunks = result.chunks;
    pass.wasTruncated = result.wasTruncated;

    // CLEAR-ON-TEAR. A read that was not a whole number of stereo frames is
    // unambiguous evidence of a tear — no well-formed run of payloads can
    // produce such a count — so flushing makes the next read provably begin on
    // a packet boundary rather than relying on the read bound to have drained
    // the fifo. Read the file header's three numbered points before changing
    // anything here: in the ordinary case this discards nothing, and in the one
    // case where it discards something it is discarding good audio. It does NOT
    // repair the frames already written above, and it is NOT a defence against
    // a merged backlog.
    if (result.wasTruncated) {
        // BEFORE the flush: this is the only moment the discard's magnitude
        // can be known, and FR-032 forbids discarding host audio silently.
        const int queued = fifo.available();
        const int discarded = (queued > 0) ? queued : 0;

        if (fifo.clear()) {
            pass.flushed = true;
            pass.flushedBytes = discarded;
            pass.flushedFrames = payloadToFrameCount(discarded).frames;
            ++stats.inputFifoFlushes;
            stats.inputFifoFlushedFrames +=
                static_cast<std::uint32_t>(pass.flushedFrames);
        }
        // No else-branch counting: a fifo that refuses to clear discarded
        // nothing, so counting a flush or its frames would be a fiction. The
        // refusal is still visible to the caller as `flushed == false` on a
        // pass whose `wasTruncated` is true.
    }

    // Read AFTER the drain (and after any flush), so it reports what is left
    // for the next pass rather than what was there before this one.
    pass.backlogBytes = fifo.available();
    return pass;
}

} // namespace acfx::nucleo

#pragma once

// Platform-independent OUT-path (host -> device) packet consumption for the
// Nucleo USB audio adapter (FR-024, FR-025, FR-028, FR-028a). No TinyUSB, no
// CMSIS, no board headers, no <cstdio> — this header compiles under the `test`
// preset with no toolchain file. Anything that cannot satisfy that constraint
// belongs in nucleo-main.cpp instead.
//
// WHAT LIVES HERE AND WHAT DOES NOT.
// The TinyUSB call is one line and it stays in the shim: nucleo-main.cpp owns
// `tud_audio_read(buffer, sizeof(buffer))`, which per research R13.3 returns a
// BYTE count out of an unframed byte FIFO. Everything downstream of that byte
// count — truncate, count, convert, de-interleave, write — is ordinary logic
// with no platform dependency, so it lives here where the host test binary can
// reach it (adapters/nucleo/support is linked into acfx_core_tests as
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

    // FR-028a: the payload's byte count was not a whole number of stereo
    // frames and a 1-3 byte remainder was discarded.
    bool wasTruncated = false;
};

// The polled OUT path. Holds only its de-interleave scratch; it owns no ring,
// no stats and no lifecycle, both of which are passed in per call so the shim
// keeps a single instance of each and this stays a pure transformation step.
class UsbOutPath {
public:
    // Largest payload one call can hand to the ring in a single de-interleave
    // pass. A byte count above this is not rejected — see consumePacket().
    static constexpr int maxPayloadBytes() noexcept {
        return kMaxPacketFrames * kChannels * static_cast<int>(sizeof(std::int16_t));
    }

    // Consume one USB OUT payload: truncate to whole frames, count the
    // remainder, convert to float, de-interleave, and write to `ring`.
    //
    // `payload` points at `byteCount` bytes of interleaved 16-bit stereo PCM
    // as delivered by the host. `byteCount` is what tud_audio_read() returned;
    // 0 is a legal, common value and is NOT an error (R13.3 records that a read
    // issued before the function is open also returns 0, which is why a zero
    // return must never be treated as a fault here).
    //
    // FR-028a — TRUNCATE, DO NOT REJECT. A byte count that is not a whole
    // number of stereo frames is truncated to the frames it does contain; the
    // whole frames are consumed and the 1-3 byte remainder is discarded and
    // COUNTED in `stats.malformedPayloads`. Rejecting the whole packet would
    // drop good frames, and — worse — consuming the torn remainder would swap
    // L and R for every subsequent frame in the stream, which is a permanent
    // channel-alignment fault from a transient one.
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

        // Chunked so a payload larger than one maximum packet — which the FIFO
        // can return when the service loop misses a poll, since it is a byte
        // FIFO with no packet boundaries (R13.3) — is consumed in full rather
        // than clamped to the scratch size. Clamping would silently discard
        // audio the host did send; a loop discards nothing and still needs no
        // buffer beyond the 49-frame worst case.
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

} // namespace acfx::nucleo

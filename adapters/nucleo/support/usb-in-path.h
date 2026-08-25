#pragma once

// Platform-independent IN-path (device -> host) packet production for the
// Nucleo USB audio adapter (T035; FR-026, FR-032, FR-038a). No TinyUSB, no
// CMSIS, no board headers, no <cstdio> — this header compiles under the
// `test` preset with no toolchain file. Anything that cannot satisfy that
// constraint belongs in the shim (adapters/nucleo/usb-audio-service.h)
// instead.
//
// T047 (US7, FR-029, FR-029a, D22) adds the `captureOnly` parameter to
// service(): the capture-only alt-setting state (host has opened the IN
// streaming interface while OUT sits at its zero-bandwidth alt, i.e. no
// playback stream open at all) is a shim-side condition and cannot be
// derived from anything the ring or sink expose, so the shim decides it
// (usb-audio-service.h's tud_audio_set_itf_cb / tud_audio_set_itf_close_ep_cb)
// and passes it in. See service()'s doc comment below for the branch itself.
//
// THE IN STREAM IS AN ASYNCHRONOUS SOURCE (FR-026, D20). There is no feedback
// endpoint (FR-027) and no local clock (FR-024): this path does not decide
// WHEN the host wants audio — TinyUSB's own IN software FIFO
// (CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ, drained by the SOF-paced ISR) is the
// only thing that knows that, and this path defers to it entirely by asking
// how much ROOM is free before ever touching the ring. That query is what
// paces this path to the host: a fast tud_task() poll loop that ran an
// unconditional fixed-size pull every pass would drain the output ring far
// faster than the DSP replenishes it, manufacturing underruns out of nothing.
// Gating on room instead means a pull only happens once the ISR has actually
// freed space by sending a previous packet, i.e. once per SOF in steady state
// — exactly FR-026's "one host-paced frame per SOF".
//
// HOW MUCH PER PASS (the design question T035's brief asks to answer
// explicitly): at most ONE maximum packet — kMaxPacketFrames (49) frames,
//196 bytes — bounded further by however much room the sink currently reports,
// whichever is smaller. This mirrors the OUT path's identical "one bounded
// unit of work per pass" discipline (support/usb-out-path.h's
// serviceOutFifo(): one read, never a drain-the-backlog loop) for the same
// real-time reason: the work per pass must stay bounded so tud_task()'s own
// servicing cadence is never starved by this path looping internally. A
// backlog — room reappearing while this path is mid-retry (see below) — is
// drained by the service loop coming back, not by looping in here.
//
// BACK-PRESSURE, HANDLED WITHOUT A SILENT DROP. tud_audio_write() (the
// pinned 0.21.0 tree's tu_fifo_write_n(), non-overwritable mode) may accept
// fewer bytes than offered if its own software FIFO fills between this path's
// room check and the write — the check and the write are not atomic against
// a second producer, and even though this adapter is the FIFO's only
// producer, the underlying primitive still reports a per-call written count
// that must be honoured rather than assumed. A partial accept only ever
// commits the FRONT (oldest) part of what was offered (tu_fifo_write_n_access_
// mode copies from the front of `data` up to the FIFO's remaining depth) — so
// naively continuing would silently drop the TAIL, which is the NEWEST audio,
// backwards from AR3's "drop the oldest, keep the newest" policy every other
// substitution in this adapter follows.
//
// This path instead RETAINS the unwritten tail and retries it, byte for
// byte, at the START of the next pass, before any new frames are pulled from
// the ring. No byte offered to the sink is ever discarded by this path: a
// short accept only adds latency to the tail's delivery, bounded by one
// packet (the retained remainder can never exceed what one pass just pulled).
// If the sink genuinely never drains — the failure this guards against, not
// the common case — the ring is not touched again until the carryover clears
// (the gate at the top of service() below), so the DSP's own writes into the
// output ring are what eventually overflow it, and that overflow is already
// FR-032-observable: it increments `outputOverruns` in
// support/dsp-block-path.h, the ring's own existing accounting. No new
// counter is introduced here for the sink's back-pressure itself, because
// nothing is actually lost at THIS layer to count — see the file's
// UsbInPath::service() doc comment for the full accounting story, including
// where `outputUnderruns` (the one counter this path DOES own) fires.
//
// Real-time safety: no heap, no locks, no exceptions. The ring-read scratch
// and the packet/carryover buffer are both fixed member arrays sized for the
// 49-frame worst case (FR-028).

#include <cstddef>
#include <cstdint>

#include "audio-format.h"
#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"

namespace acfx::nucleo {

// What one IN service pass did. Returned so a caller can observe the pass
// without diffing counters — `framesSubstituted` is the only field with a
// counter behind it (`outputUnderruns`, incremented inside service()); the
// rest is per-pass detail with no persistent counter, exactly as
// OutServicePass/BlockPassResult split "event counted" from "per-pass detail"
// in the OUT and DSP-block paths.
struct InServicePass {
    // True when this pass only retried a carryover left by a previous pass's
    // partial accept — no ring state was touched, no counter was
    // (re-)evaluated. False on a fresh pull, INCLUDING a pull that immediately
    // becomes a new carryover.
    bool flushedCarryover = false;

    // Frames pulled from the output ring this pass. 0 whenever the pass did
    // nothing — ring not Running, no room, or a carryover retry.
    int framesRead = 0;

    // The ring's own AR2 shortfall count for this pull (silence-filled
    // frames). Already reflected in `stats.outputUnderruns` below; kept here
    // too so a caller can see the magnitude, matching BlockPassResult's
    // framesSubstituted convention.
    int framesSubstituted = 0;

    // Bytes handed to the sink this pass — either the fresh conversion or the
    // retried carryover.
    int bytesOffered = 0;

    // Bytes the sink actually accepted. bytesOffered - bytesWritten is what
    // this path is now holding as the carryover for the NEXT pass (see
    // pendingBytes()).
    int bytesWritten = 0;
};

// The polled IN path. Holds its ring-read scratch and its packet/carryover
// buffer; it owns no ring, no sink and no stats, all of which are passed in
// per call so the shim keeps a single instance and this stays a pure step —
// the same shape as UsbOutPath and DspBlockPath.
class UsbInPath {
public:
    // THE PER-PASS WRITE BOUND: one wMaxPacketSize, 49 frames x 2 channels x
    // 2 bytes = 196. Restated here as a function (not merely a comment) so
    // the shim can static_assert it against tusb_config.h's
    // CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX, the same cross-check
    // usb-audio-service.h already does for the OUT side.
    static constexpr int maxPayloadBytes() noexcept {
        return kMaxPacketFrames * kChannels * static_cast<int>(sizeof(std::int16_t));
    }

    // T019 (US3, FR-010): the ACTIVE format's maximum payload — the per-pass
    // write bound service() must use once it knows which format is
    // selected. maxPayloadBytes() above is unchanged (196 B, 16-bit) and
    // every pre-T019 call site (IN1-5, CO1-5, the cold-drain test) keeps
    // reading exactly that value. For Pcm24 this returns 294 B, the same
    // worst case tusb_config.h's EP envelope is already sized to (research
    // R6/R7).
    static constexpr int maxPayloadBytes(AudioFormat format) noexcept {
        return kMaxPacketFrames * bytesPerFrame(format);
    }

    // Bytes currently retained from a prior pass's partial accept, awaiting
    // retry. Always in [0, maxPayloadBytes(format)) for whichever format
    // that pass actually offered — maxPayloadBytes() (196 B) for Pcm16,
    // maxPayloadBytes(AudioFormat::Pcm24) (294 B) for Pcm24. Exposed for
    // tests and diagnostics; service() is the only thing that changes it.
    int pendingBytes() const noexcept { return pendingBytes_; }

    // Service the IN endpoint once.
    //
    // `Ring` is AudioRing<Capacity, kChannels> (the output ring, T033's
    // g_outputRing on the far side of the shim): `RingState state() const;
    // int read(float* const*, int);`. `Sink` is anything providing
    //     int write(const std::int16_t* data, int len) noexcept;  // bytes accepted
    //     int writeAvailable() noexcept;                          // bytes of room
    //     int capacity() noexcept;                                // total FIFO bytes
    // which on the board is a two-line adapter over tud_audio_write() /
    // tud_audio_get_ep_in_ff()+tu_fifo_remaining() (usb-audio-service.h) and
    // in the host tests is a simulated byte sink. Templated rather than
    // virtual: no allocation, no indirect call, and the shim's adapter
    // inlines away.
    //
    // ------------------------------------------------------------------
    // ORDER OF OPERATIONS, AND WHY.
    //
    // 1. A CARRYOVER FROM A PRIOR PASS'S PARTIAL ACCEPT TAKES PRIORITY,
    //    UNCONDITIONALLY. It does not depend on the ring's state — those
    //    bytes are already-converted PCM that left the ring on an earlier
    //    pass and must reach the sink eventually, regardless of what the
    //    ring is doing meanwhile (including a mid-stream reset). Retrying it
    //    is the WHOLE of this path's back-pressure handling: see the file
    //    header for why a partial accept must never simply be extended with
    //    fresh data instead.
    //
    // 2. NO CARRYOVER: gate on `ring.state() == RingState::Running`, exactly
    //    the precondition AudioRing::read() itself documents ("the caller
    //    MUST consult state() first and draw nothing unless Running... the
    //    returned count is meaningless and must not be recorded as an
    //    underrun" while Priming or Stopped). FR-030d's transition table
    //    gives Priming and Stopped both "draws no blocks... underrun
    //    counted? No" — this path's analogue is drawing nothing and writing
    //    nothing to the sink, not manufacturing silence to keep the SOF fed.
    //    FR-031's silence-on-underflow duty is explicitly scoped to "In the
    //    Running state"; a startup-fill gap before Running is reached is
    //    exactly that state, not a Running-state shortfall, and is left
    //    unfed rather than force-read against the ring's own contract.
    //
    // 3. RUNNING: ask the sink how much ROOM it has (`writeAvailable()`)
    //    before pulling anything. This is the pacing mechanism described in
    //    the file header — with no feedback endpoint, the sink's own
    //    remaining capacity is the only thing that tracks the host's SOF
    //    cadence, and gating on it is what stops a fast poll loop from
    //    over-draining the ring. Zero room is the ordinary idle result (the
    //    ISR has not yet freed space since the last pass) and is NOT a
    //    fault, matching the OUT path's "a zero from either call is the
    //    normal idle result" convention.
    //
    // 4. THE PULL SIZE is room, capped at kMaxPacketFrames — never more than
    //    one packet regardless of how much room is free, for the bounded-
    //    work reason in the file header. FR-007 ADDS ONE MORE, NARROW CAP
    //    here: while this ring's Running run has never yet held more than
    //    its own startupFill() (i.e. the Priming -> Running promotion that
    //    just happened is the only reason it is Running at all — see the
    //    RUNNING branch below for why occupancy sits at EXACTLY
    //    startupFill() at that instant), the pull is additionally bounded by
    //    current occupancy, so this path never asks the ring for more than
    //    it has ever been given a chance to hold. That guard retires
    //    permanently, for this Running run, the first time occupancy is
    //    observed above startupFill() — a real, measured cushion — after
    //    which every shortfall is reported exactly as step 5 describes,
    //    with nothing hidden.
    //
    // 5. THE RING READ. AudioRing::read() ALWAYS supplies exactly the frames
    //    asked for, zero-filling any shortfall and returning the substituted
    //    count (AR2) — this is FR-031's "output underflow MUST emit
    //    silence" satisfied structurally by the ring itself. A shortfall
    //    increments `outputUnderruns` ONCE per pass (an event count, matching
    //    every other counter in this adapter — the frame-level detail is
    //    `framesSubstituted`), which is this path's half of US3 AS3.
    //
    // 6. CONVERSION. `interleaveToInt16()` (support/sample-format.h) already
    //    implements FR-038a in full — scale by 32768, round to nearest with
    //    ties away from zero, clamp to [-32768, 32767] BEFORE the narrowing
    //    cast — and is reused verbatim; nothing here reimplements that math.
    //
    // 7. THE WRITE, and the accept count is checked, not assumed. Any
    //    unwritten remainder becomes the next pass's carryover (step 1) —
    //    see the file header for why extending it with fresh data instead
    //    would be wrong.
    //
    // ------------------------------------------------------------------
    // `captureOnly` (T047; FR-029, FR-029a, D22): the shim's alt-setting
    // tracking (usb-audio-service.h's tud_audio_set_itf_cb /
    // tud_audio_set_itf_close_ep_cb) reports true exactly when the host has
    // opened the IN streaming interface while the OUT streaming interface
    // sits at its zero-bandwidth alt — i.e. capture-only: no playback stream
    // is open AT ALL, not merely idle. That is a materially different
    // condition from "the output ring is Running but momentarily empty"
    // (step 5 above, which counts `outputUnderruns`): here there is no
    // producer on the other end of the ring ever, so the ring's own state is
    // not consulted at all, and the two conditions are mutually exclusive by
    // construction (I-TS1a / FR-029a) — this branch returns before step 2's
    // ring-state gate is ever reached.
    //
    // D22's requirement is that a host opening capture alone gets
    // well-defined SILENCE, never a hang or stale audio, so this ACTIVELY
    // writes a room-bounded block of zeroed samples rather than leaving
    // whatever was last in the IN software FIFO. Any carryover retained from
    // a PRIOR (duplex) session is discarded unconditionally on entry to this
    // branch — those bytes belong to a session that is no longer running,
    // and letting them leak into capture-only silence would violate D22 as
    // surely as sending nothing at all.
    // T019 (US3, FR-010): `format` is threaded in as an EXPLICIT PARAMETER,
    // APPENDED after `captureOnly` (not inserted before it) so every
    // pre-T019 call site — including every positional `captureOnly=true`
    // call in IN1-5/CO1-5/the cold-drain test, none of which mention
    // AudioFormat — compiles and behaves exactly as before, defaulted to
    // Pcm16. Read ONCE at the top of the call into `bpf`, not per sample:
    // every per-sample loop below (interleaveToInt16() /
    // interleaveToInt24Packed()) carries no format branch of its own.
    template <typename Ring, typename Sink>
    InServicePass service(Ring& ring, Sink& sink, AudioTransportStats& stats,
                          bool captureOnly = false,
                          AudioFormat format = AudioFormat::Pcm16) noexcept {
        InServicePass pass;
        const int bpf = bytesPerFrame(format);

        if (captureOnly) {
            // Discard, never retry: a carryover left by a duplex session that
            // has since closed must not leak into capture-only silence.
            pendingBytes_ = 0;

            const int roomBytes = sink.writeAvailable();
            const int roomFrames = (roomBytes > 0) ? (roomBytes / bpf) : 0;
            const int framesToPull =
                (roomFrames < kMaxPacketFrames) ? roomFrames : kMaxPacketFrames;
            if (framesToPull <= 0) {
                // No room to emit silence into this pass; nothing was
                // actually starved-for-silence yet, so no counter fires
                // (mirrors step 3's "zero room is the ordinary idle result,
                // not a fault").
                return pass;
            }

            pass.bytesOffered = framesToPull * bpf;
            // `bpf` is always even (kChannels * subslotBytes, subslotBytes
            // in {2, 3}), so bpf/2 int16 slots per frame always covers
            // exactly bpf bytes for either format — no cast needed to zero
            // the buffer generically.
            const int sampleCount = framesToPull * (bpf / 2);
            for (int i = 0; i < sampleCount; ++i) {
                buffer_[i] = 0;
            }

            pass.bytesWritten = boundedWrite(sink, pass.bytesOffered);
            // One event per pass in which silence was actually offered,
            // exactly the `outputUnderruns` convention in step 5 — and NEVER
            // `outputUnderruns` itself (FR-029a / I-TS1a: one event bumps
            // exactly one counter).
            ++stats.inputStarved;
            return pass;
        }

        if (pendingBytes_ > 0) {
            pass.flushedCarryover = true;
            pass.bytesOffered = pendingBytes_;
            pass.bytesWritten = boundedWrite(sink, pendingBytes_);
            retainRemainder(pass.bytesOffered, pass.bytesWritten);
            return pass;
        }

        if (ring.state() != RingState::Running) {
            // Not (yet, or no longer) delivering steady audio: re-arm the
            // FR-007 startup-cushion guard below so the NEXT time this ring
            // enters Running — whether that is the very first stream open or
            // a later reopen/resume/reset (FR-051..FR-054, all of which
            // re-clear the ring to Priming/Stopped) — is protected as its
            // own fresh cold start, not treated as a continuation of a
            // cushion this run never actually built.
            coldStartPending_ = true;
            return pass;
        }

        const int roomBytes = sink.writeAvailable();
        const int roomFrames = (roomBytes > 0) ? (roomBytes / bpf) : 0;
        if (roomFrames <= 0) {
            return pass;
        }
        int framesToPull = (roomFrames < kMaxPacketFrames) ? roomFrames : kMaxPacketFrames;

        // FR-007 cold-drain fix. AudioRing::write() promotes Priming ->
        // Running the INSTANT occupancy first reaches startupFill() (audio-
        // ring.h: "the threshold is evaluated at the END of every write(),
        // and ONLY here") — at that instant occupancy sits at EXACTLY
        // startupFill(), no more, because the promotion fires on the very
        // write that crosses it. A room/packet-bounded pull the caller makes
        // before the ring has ever held more than that bare minimum would
        // therefore ask for more than the ring has EVER had a chance to
        // build, manufacturing a substitution out of the Priming->Running
        // transition itself — not a genuine shortfall — which is exactly the
        // "cold-buffer greedy-drain" FR-007 requires be eliminated.
        //
        // This guard is intentionally narrow and self-retiring, so it can
        // never become a general-purpose way to hide a real underrun (which
        // FR-030d/FR-031/FR-032, already fixed by the base feature, forbid
        // suppressing): the FIRST time occupancy is observed strictly above
        // startupFill() — i.e. the ring has built real cushion beyond the
        // bare promotion minimum, FR-007's "cushioned steady state" — the
        // guard retires permanently for this Running run. Every shortfall
        // after that point, and every shortfall on a ring whose startupFill()
        // was already exceeded on THIS very first observation (an
        // already-cushioned ring, or one with startupFill() == 0), is
        // reported exactly as before: silence-filled, `outputUnderruns`
        // incremented, nothing hidden.
        if (coldStartPending_) {
            if (ring.occupancy() > ring.startupFill()) {
                coldStartPending_ = false;
            } else if (framesToPull > ring.occupancy()) {
                // Defer to what is actually available rather than fabricate
                // silence for frames the ring was never given a chance to
                // hold; the remainder is served on the very next pass, once
                // the DSP's next write lands. Bounded, one-time added
                // startup latency — never more than one packet — is the
                // intended FR-007/FR-008 trade, not a spurious
                // `outputUnderruns`.
                framesToPull = ring.occupancy();
            }
        }

        // Do not fabricate silence for a phantom frame the output ring never
        // produced. TRACE-CONFIRMED root cause of the residual dropout (SWD
        // occupancy trace, steady state): the ring holds exactly one DSP block
        // (kBlockFrames == 48), but this pull's bound is kMaxPacketFrames (49)
        // — a BUFFER-SIZE constant, not the per-SOF rate. When the sink reports
        // room for 49, the room/packet-bounded pull asks the 48-frame ring for
        // a 49th frame and ring.read() fabricates one silence sample: the whole
        // ~0.2% steady-state glitch, one frame at a time (the DSP is never
        // behind — the input ring reads empty every pass because it keeps up,
        // so this is a pull/production GRANULARITY mismatch, not a shortfall).
        //
        // Cap the pull at what the ring holds — BUT only while the sink still
        // buffers a full packet for the wire, so a momentarily-empty ring is a
        // granularity artifact, not a wire underrun. When the sink is ALSO low,
        // a genuine underrun is imminent: leave the pull as-is so ring.read()
        // silence-fills AND counts it (FR-030d/FR-031/FR-032 observability),
        // and the `held == 0` unit-test sinks (capacity() == room) fall through
        // unchanged. Trace-verified: in steady state the sink holds ~100-149
        // frames, so this caps, and every steady-state substitution disappears.
        const int sinkHeldFrames = (sink.capacity() - roomBytes) / bpf;
        if (sinkHeldFrames >= kMaxPacketFrames && framesToPull > ring.occupancy()) {
            framesToPull = ring.occupancy();
        }

        float* channels[kChannels];
        for (int channel = 0; channel < kChannels; ++channel) {
            channels[channel] = scratch_[channel];
        }

        pass.framesRead = framesToPull;
        pass.framesSubstituted = ring.read(channels, framesToPull);
        if (pass.framesSubstituted > 0) {
            ++stats.outputUnderruns;
        }

        // The Pcm24 branch reinterprets `buffer_` as raw bytes: writing
        // through an unsigned-char-family pointer is always well defined
        // regardless of the pointee's declared type (the same exception
        // retainRemainder() below already relies on). The Pcm16 branch is
        // untouched and calls the exact same interleaveToInt16() as before
        // T019 — byte-identical, per the task brief.
        if (format == AudioFormat::Pcm24) {
            interleaveToInt24Packed(channels, reinterpret_cast<std::uint8_t*>(buffer_),
                                    framesToPull);
        } else {
            interleaveToInt16(channels, buffer_, framesToPull);
        }

        pass.bytesOffered = framesToPull * bpf;
        pass.bytesWritten = boundedWrite(sink, pass.bytesOffered);
        retainRemainder(pass.bytesOffered, pass.bytesWritten);

        return pass;
    }

private:
    // Compile-time WORST CASE across supported formats (packed-24, 3 B per
    // subslot): sizes buffer_ below so it never needs reallocation across a
    // live 16<->24 format change (FR-010). The byte count ACTUALLY USED per
    // pass is bpf (`bytesPerFrame(format)`), read once per service() call
    // above — this constant is for storage sizing only.
    static constexpr int kMaxBytesPerFrame = kChannels * 3;

    // Calls the sink and clamps a caller-error negative return (never
    // observed from tu_fifo_write_n(), which returns an unsigned count, but
    // this path is noexcept and must stay total) to 0 rather than letting a
    // negative remainder corrupt retainRemainder()'s arithmetic below.
    template <typename Sink>
    int boundedWrite(Sink& sink, int bytes) noexcept {
        const int written = sink.write(buffer_, bytes);
        return (written > 0) ? written : 0;
    }

    // Keeps whatever the sink did not accept at the FRONT of `buffer_`,
    // compacted, so the next pass's retry (or the next fresh conversion, once
    // the carryover clears) starts at index 0. `written` bytes at the front
    // of `buffer_` were already accepted and are dead; the `remainder` bytes
    // after them are the still-unsent NEWEST tail (see the file header for
    // why the tail, not the front, is what must survive).
    void retainRemainder(int offered, int written) noexcept {
        const int remainder = offered - written;
        if (remainder <= 0) {
            pendingBytes_ = 0;
            return;
        }
        if (written > 0) {
            auto* bytes = reinterpret_cast<std::uint8_t*>(buffer_);
            for (int i = 0; i < remainder; ++i) {
                bytes[i] = bytes[i + written];
            }
        }
        pendingBytes_ = remainder;
    }

    // Non-interleaved, one contiguous run per channel, matching the
    // `float* const*` channel-pointer signature AudioRing::read() takes.
    // Sized for the 49-frame worst case (FR-028): fixed storage, no heap.
    float scratch_[kChannels][kMaxPacketFrames] = {};

    // The interleaved staging AND carryover buffer, one and the same: a
    // fresh conversion (int16 OR packed-24 byte layout, per the active
    // format) is written here, offered to the sink, and whatever is not
    // accepted simply stays here (compacted to the front) as the next
    // pass's carryover. T019 (US3, FR-010): sized for one maximum packet at
    // the WORST-CASE format (kMaxBytesPerFrame, packed-24) rather than the
    // fixed 16-bit figure, so a live format change never needs a
    // reallocation — this is strictly an ENLARGEMENT (98 -> 147 int16
    // elements) of the pre-T019 array; every existing Pcm16 test only ever
    // touches the leading 196 B it already did.
    std::int16_t buffer_[kMaxPacketFrames * kMaxBytesPerFrame / 2] = {};

    int pendingBytes_ = 0;

    // FR-007 startup-cushion guard. Starts armed (true) so the FIRST Running
    // observation of this instance's life is protected; re-armed every time
    // service() observes the ring NOT Running (Stopped or Priming, including
    // after a lifecycle reset/reopen/resume) so each fresh Running run gets
    // its own cold-start protection; retired (false) permanently for a given
    // Running run the first time occupancy is observed strictly above the
    // ring's startupFill() — real cushion beyond the bare promotion minimum,
    // i.e. FR-007's "cushioned steady state" has been reached and every
    // shortfall from then on is a genuine fault, reported exactly as
    // FR-030d/FR-031/FR-032 already require. See the RUNNING branch of
    // service() for the full rationale.
    bool coldStartPending_ = true;
};

} // namespace acfx::nucleo

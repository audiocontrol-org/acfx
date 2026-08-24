#pragma once

// Platform-independent FIXED 48-frame block assembly for the Nucleo USB audio
// adapter (T033; FR-030a, FR-036a, FR-037, FR-032). No TinyUSB, no CMSIS, no
// board headers, no <cstdio> — this header compiles under the `test` preset
// with no toolchain file. Anything that cannot satisfy that constraint belongs
// in the shim (adapters/nucleo/dsp-block-service.h) instead.
//
// This is the CONSUMER half of the ring; support/usb-out-path.h is the
// producer half. Between them sits AudioRing, and the reason it sits there is
// the single property this file exists to hold:
//
//   FR-030a — THE PACKET CADENCE STOPS AT THE RING.
//   The host paces 0 to 49 frames per packet (FR-028, D21). The DSP runs a
//   fixed 48-frame block (FR-036a). Those two numbers are independent, and the
//   ring is what keeps them so. Nothing below reads a packet size, and
//   kMaxPacketFrames is deliberately not referenced in this file at all: a
//   49-frame packet changes ring OCCUPANCY and nothing else. There is no
//   headroom term, no "round up to the packet", and no path by which a
//   transport-side length reaches process().
//
// THE ONLY PLATFORM-INDEPENDENT DEPENDENCY OUTSIDE support/ IS THE DSP CORE.
// FR-037 names acfx::AudioBlock specifically, so this header includes
// "dsp/audio-block.h" from core/. That is not a seam violation — core/ is the
// platform-independent DSP spine (Constitution IV; dependencies point inward)
// — but it does mean a consumer must have acfx_core on its include path.
// Both consumers do: the firmware targets link acfx_core (cmake/acfx-effect-
// targets.cmake's acfx_add_effect_nucleo) and so does acfx_core_tests
// (tests/CMakeLists.txt).
//
// Real-time safety: no heap, no locks, no exceptions, and no unbounded work.
// The block scratch is a fixed member array of exactly kBlockFrames frames.
//
// WHAT IS NOT HERE. The block TIMER (DWT CYCCNT -> worstBlockMicros, FR-034 /
// T036) is not implemented here and must not be inferred from the absence of
// a hook: its seam is the single effect.process() call in runOneBlock(), which
// T036 wraps. blocksProcessed IS maintained here, because "a block ran" is a
// fact only this file knows.

#include "dsp/audio-block.h"

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"

namespace acfx::nucleo {

// What one pass of the block path did. Returned so a caller can observe the
// pass without diffing counters — every field below is ALREADY reflected in
// the AudioTransportStats record runOneBlock() was handed, so discarding this
// result loses no accounting.
struct BlockPassResult {
    // False means no block was available to run this pass: the input ring was
    // not Running, or it held fewer than kBlockFrames frames. Neither is an
    // error and neither increments a counter (FR-030d).
    bool blockProcessed = false;

    // Frames the input ring could not supply and filled with silence (AR2).
    // Always 0 on the normal path — see the gate in runOneBlock(). A FRAME
    // count; the matching statistic `inputUnderruns` is an EVENT count.
    int framesSubstituted = 0;

    // Frames the output ring dropped to make room (AR3 drops the OLDEST).
    // A FRAME count; the matching `outputOverruns` is an EVENT count.
    int framesDropped = 0;
};

// The fixed-block DSP path. Holds only its block scratch; it owns no ring, no
// effect, no stats and no lifecycle — all are passed in per call, so the shim
// keeps a single instance of each and this stays a pure step.
class DspBlockPath {
public:
    // Run at most ONE 48-frame block: draw it from `input`, hand it to
    // `effect` in place, publish it to `output`.
    //
    // ------------------------------------------------------------------
    // 1. WHEN DOES A BLOCK RUN?  Only when `input.state() == Running` AND
    //    `input.occupancy() >= kBlockFrames`. Both halves are load-bearing.
    //
    //    The state half is the ring's own contract, not a choice made here:
    //    AR7 says "the consumer checks state() and draws no block unless
    //    Running", and calling read() while Priming or Stopped is a CALLER
    //    ERROR whose substitution count "must not be recorded as an underrun".
    //    FR-030d's transition table says the same in the other direction —
    //    Stopped and Priming both "draw no blocks", and no underrun is counted
    //    in either. Priming is not an error state; it is the startup fill
    //    (FR-030b) doing its job, and drawing there would manufacture the
    //    burst of underruns FR-030b exists to prevent.
    //
    //    The occupancy half is what makes this path's "no substitution"
    //    property structural rather than hoped for. This adapter's consumer
    //    has NO independent clock: it is a free-running polled loop (D26), so
    //    unlike a callback-driven adapter it is never *obliged* to produce a
    //    block at a particular instant. Running a short block anyway would
    //    spin the effect at loop speed over a starved ring, inflating
    //    blocksProcessed (the FR-034 rate denominator) with blocks that are
    //    mostly manufactured silence and overrunning the output ring with
    //    them. Waiting instead costs nothing and loses nothing: the frames
    //    stay in the ring until the block is genuinely there. Real starvation
    //    is still counted — it just lands where it actually becomes audible,
    //    at the IN endpoint, as `outputUnderruns` when the output ring runs
    //    dry (T035, US3 AS3).
    //
    // 2. WHAT ABOUT A SHORT READ?  AudioRing::read() zero-fills any shortfall
    //    and returns the count substituted (AR2). The gate above makes that
    //    return structurally 0 here — but this path records it rather than
    //    assuming it, because FR-032 forbids an uncounted substitution and an
    //    assumption is not a count. If the ring ever reports a shortfall, the
    //    block still runs (its frames are already consumed; discarding them
    //    would be a second, uncounted loss) and `inputUnderruns` increments
    //    ONCE for the block — an event count, matching the OUT path's
    //    inputOverruns convention and transport-stats.h's note on event vs
    //    frame counts. There is no double-count risk: AudioRing owns no
    //    counters at all (AR4), so this is the single increment site.
    //
    // 3. WHAT IF THE OUTPUT RING IS FULL?  The effect has already produced 48
    //    frames, so something must give. `output.write()` drops the OLDEST
    //    frames it holds (AR3, D24) — the newest audio is what the IN endpoint
    //    is about to need — and returns the count dropped, which increments
    //    `outputOverruns` ONCE for this block (US3 AS4). The frame-level
    //    detail comes back in BlockPassResult::framesDropped. Nothing is
    //    dropped silently, which is the whole of FR-032.
    //
    // 4. HOW MANY BLOCKS PER PASS?  Exactly one, never a drain-while-available
    //    loop. Two reasons, both real-time. First, bounded work: this runs
    //    inside the tud_task() service loop, whose USB servicing cadence
    //    depends on iterating promptly (D26 — one execution context, so there
    //    is no second thread to absorb a stall), and an unbounded loop would
    //    starve exactly the servicing that refills the ring. The OUT path
    //    bounds itself the same way and for the same reason (serviceOutFifo()'s
    //    ONE READ PER PASS). Second, a burst drain would convert a transient
    //    input backlog into a *counted output drop*: processing the whole ring
    //    at loop speed hands the output ring far more than the IN endpoint can
    //    take at its 1 ms cadence, so the surplus would be dropped as an
    //    overrun. Backlogs are drained by coming back here, not by looping
    //    inside; the loop iterates far faster than a block arrives.
    // ------------------------------------------------------------------
    //
    // `InputRing` / `OutputRing` / `Effect` are template parameters rather
    // than concrete types: the rings' capacities are HIL-derived numbers
    // pinned later (D23, FR-035) and so must not be spelled here, and the
    // effect type is a firmware-time compile definition (ACFX_EFFECT_TYPE)
    // that does not exist on the host at all. Duck-typed rather than virtual:
    // no allocation, no indirect call, and the shim's instances inline away.
    //   InputRing:  RingState state() const; int occupancy() const;
    //               int read(float* const*, int);
    //   OutputRing: int write(const float* const*, int);
    //   Effect:     void process(acfx::AudioBlock&);
    template <typename InputRing, typename OutputRing, typename Effect>
    BlockPassResult runOneBlock(InputRing& input,
                                OutputRing& output,
                                Effect& effect,
                                AudioTransportStats& stats) noexcept {
        BlockPassResult result;

        // (1) The lifecycle + availability gate. Draws nothing, consumes
        // nothing, counts nothing.
        if (input.state() != RingState::Running) {
            return result;
        }
        if (input.occupancy() < kBlockFrames) {
            return result;
        }

        float* channels[kChannels];
        for (int channel = 0; channel < kChannels; ++channel) {
            channels[channel] = scratch_[channel];
        }

        // (2) Exactly kBlockFrames — 48 — every time. Never a packet size.
        result.framesSubstituted = input.read(channels, kBlockFrames);
        if (result.framesSubstituted > 0) {
            ++stats.inputUnderruns;
        }

        // FR-037: non-interleaved float* per channel, in an acfx::AudioBlock,
        // processed IN PLACE — the same shape adapters/daisy/daisy-main.cpp
        // exemplifies. `channels` points at this object's fixed scratch, so
        // the effect's output overwrites its input and is what gets published
        // below. No copy, no heap, no interleaving on this side of the ring.
        //
        // T036's DWT block timer brackets THIS call and nothing else.
        acfx::AudioBlock block(channels, kChannels, kBlockFrames);
        effect.process(block);

        result.blockProcessed = true;

        // FR-034: the rate denominator, incremented exactly once per block
        // that actually ran (TS2). Deliberately after process(), so a block is
        // counted when it has been processed rather than when it was merely
        // attempted.
        ++stats.blocksProcessed;

        // (3) Publish. A full output ring drops its oldest frames and says so.
        result.framesDropped = output.write(channels, kBlockFrames);
        if (result.framesDropped > 0) {
            ++stats.outputOverruns;
        }

        return result;
    }

private:
    // Non-interleaved, one contiguous run per channel, matching the
    // `float* const*` channel-pointer signatures AudioRing and AudioBlock both
    // take. Sized for EXACTLY one block — kBlockFrames, never kMaxPacketFrames
    // (FR-036b/D28). Fixed storage, no heap (AR5, D16).
    float scratch_[kChannels][kBlockFrames] = {};
};

// The block size is the DSP's, not the transport's. Restated as a build-time
// guard for the same reason effect-instance.h restates it: a future edit that
// collapses the two constants, or that reaches for kMaxPacketFrames here to
// "harmonize" them, must fail the BUILD rather than quietly let transport
// framing cross the ring.
static_assert(kBlockFrames == 48,
              "FR-036a: the DSP block is exactly 48 frames; there is no "
              "headroom term");
static_assert(kBlockFrames != kMaxPacketFrames,
              "FR-030a/FR-036b: the DSP block size and the transport packet "
              "bound are DIFFERENT quantities on purpose — the ring is what "
              "keeps them independent");

}  // namespace acfx::nucleo

#pragma once

// Platform-independent single-producer / single-consumer audio ring for the
// Nucleo USB audio adapter (FR-030, FR-030a, FR-030b, FR-030c, FR-030d,
// FR-031, FR-032, FR-046, FR-051). No TinyUSB, no CMSIS, no board headers,
// no <cstdio> — this header compiles under the `test` preset with no
// toolchain file. Anything that cannot satisfy that constraint belongs in
// nucleo-main.cpp instead.
//
// The ring is a passive buffer that REPORTS facts and never hides them
// (AR4): every dropped or substituted frame comes back as a return value so
// the caller can increment the matching AudioTransportStats counter. The ring
// owns no counters of its own — see transport-stats.h for the record and
// contracts/nucleo-support.md § "Audio ring" for the normative contract.
//
// Storage is a fixed member array sized by the CapacityFrames template
// parameter: no heap, no locks, and — the constructor aside — no exceptions,
// so every member below the constructor is callable from the audio path
// (AR5, AR6). Single-producer / single-consumer is sufficient here under the
// single-execution-context assumption (D26, FR-046); a lock-free
// multi-producer structure would buy nothing and cost clarity.

#include <stdexcept>

#include "sample-format.h"

namespace acfx::nucleo {

// The ring's lifecycle (FR-030d, I-AR6). Three states, not two: Stopped is
// NOT a synonym for Priming. Priming means a stream is open and the producer
// is actively filling; Stopped means no streaming alt-setting is open and
// nothing is arriving. Collapsing them would leave suspend (FR-051) nowhere
// to land and would make "empty and filling" indistinguishable from "empty
// because the host went away" — the very ambiguity AR7 exists to remove.
enum class RingState { Stopped, Priming, Running };

// A statically sized SPSC ring of non-interleaved float frames.
//
// `CapacityFrames` is deliberately a template parameter WITH NO DEFAULT, and
// `startupFillFrames` a constructor argument with no default: both are
// derived from HIL measurement per research R5 and pinned in Phase 13 (D23,
// FR-035). A default for either would be an invented number wearing the
// costume of a decision. They differ in kind, which is why they are supplied
// differently: capacity sizes the fixed member array and so must be known at
// compile time (AR5); the fill threshold is a policy value compared against
// occupancy at run time, and a constructor argument lets the host tests sweep
// it (0, 48, capacity) without instantiating a fresh template per value.
template <int CapacityFrames, int Channels = kChannels>
class AudioRing {
    static_assert(CapacityFrames > 0, "AudioRing capacity must be positive");
    static_assert(Channels > 0, "AudioRing must carry at least one channel");

public:
    // `startupFillFrames` is the occupancy at which Priming becomes Running.
    //
    // Throws std::invalid_argument if it is negative or exceeds
    // CapacityFrames: such a ring could never reach Running and would prime
    // forever, SILENTLY. Construction is startup work, never the audio path,
    // so throwing here is safe and is the fail-loud behaviour this codebase
    // requires over a silent clamp (AR6). This is the sole member permitted
    // to throw.
    explicit AudioRing(int startupFillFrames) : startupFill_(startupFillFrames) {
        if (startupFillFrames < 0) {
            throw std::invalid_argument(
                "AudioRing: startupFillFrames must not be negative");
        }
        if (startupFillFrames > CapacityFrames) {
            throw std::invalid_argument(
                "AudioRing: startupFillFrames exceeds CapacityFrames; such a ring "
                "could never reach Running and would prime forever");
        }
        clearStorage();
    }

    // Push `frames` frames from `src`, which holds `Channels` contiguous
    // float buffers of at least `frames` samples each.
    //
    // Overflow drops the OLDEST frames, never the newest (AR3, D24): the
    // incoming audio is what the consumer is about to need, so discarding it
    // in favour of stale audio already in the ring would add latency on top
    // of the loss. Returns the number of frames dropped — 0 on the normal
    // path. `frames <= 0` is a valid no-op that still evaluates the promotion
    // threshold below.
    int write(const float* const* src, int frames) noexcept {
        const int incoming = (frames > 0) ? frames : 0;

        // Frames that cannot fit: those already held plus those arriving,
        // minus what the ring can hold. Non-positive means no overflow.
        const int overflow = count_ + incoming - CapacityFrames;
        const int dropped = (overflow > 0) ? overflow : 0;

        // A single write larger than the whole ring drops its own leading
        // frames too — only its newest CapacityFrames can survive, and they
        // are the ones AR3 says to keep.
        const int accepted = (incoming < CapacityFrames) ? incoming : CapacityFrames;
        const int srcOffset = incoming - accepted;

        // The remainder of the drop comes off the FRONT of the ring: the
        // oldest frames it holds.
        const int dropFromRing = dropped - srcOffset;
        if (dropFromRing > 0) {
            tail_ = advance(tail_, dropFromRing);
            count_ -= dropFromRing;
        }

        int slot = advance(tail_, count_);
        for (int frame = 0; frame < accepted; ++frame) {
            for (int channel = 0; channel < Channels; ++channel) {
                storage_[channel][slot] = src[channel][srcOffset + frame];
            }
            slot = advance(slot, 1);
        }
        count_ += accepted;

        // AR7 / the contract's normative transition table: the threshold is
        // evaluated at the END of every write(), and ONLY here. Fixing the
        // check to one place is what makes the edge cases decidable by
        // reading one rule — startupFill() == 0 (promoted by the first write,
        // including a zero-frame one), a write that overshoots the threshold,
        // and a write while Stopped (which does NOT promote) all fall out of
        // this single line. Reads never change state.
        if (state_ == RingState::Priming && count_ >= startupFill_) {
            state_ = RingState::Running;
        }

        return dropped;
    }

    // Draw exactly `frames` frames into `dst`, which holds `Channels`
    // contiguous float buffers of at least `frames` samples each.
    //
    // ALWAYS writes exactly `frames` frames — never a short write, never
    // uninitialised memory. Any shortfall is filled with SILENCE and the
    // count of substituted frames returned (AR2).
    //
    // The caller MUST consult state() first and draw nothing unless Running
    // (AR7). Lifecycle is deliberately NOT encoded in this return value: "not
    // ready" is a property of the transport, not an outcome of a read.
    // Calling read() while Priming or Stopped is therefore a caller error;
    // the ring still behaves predictably, but the returned count is
    // meaningless and must not be recorded as an underrun.
    int read(float* const* dst, int frames) noexcept {
        const int requested = (frames > 0) ? frames : 0;
        const int available = (count_ < requested) ? count_ : requested;

        for (int frame = 0; frame < available; ++frame) {
            for (int channel = 0; channel < Channels; ++channel) {
                dst[channel][frame] = storage_[channel][tail_];
            }
            tail_ = advance(tail_, 1);
        }
        count_ -= available;

        const int substituted = requested - available;
        for (int frame = available; frame < requested; ++frame) {
            for (int channel = 0; channel < Channels; ++channel) {
                dst[channel][frame] = 0.0f;
            }
        }

        // No state change on read. In particular Running is NEVER demoted by
        // starvation (AR7): once primed, an empty ring is a genuine fault and
        // stays visible as one. Silently dropping back to Priming would mask
        // sustained drift behind a "still starting up" reading — the same
        // masking the no-re-centring guarantee (AR8) forbids.
        return substituted;
    }

    RingState state() const noexcept { return state_; }

    // Always in [0, capacity()], on every path (AR1). No re-centring: the
    // ring never drops or duplicates frames to steer this value toward a
    // target, so drift stays visible (AR8, FR-030c).
    int occupancy() const noexcept { return count_; }

    int capacity() const noexcept { return CapacityFrames; }

    int startupFill() const noexcept { return startupFill_; }

    // Stream open, resume, bus reset (AR9, FR-052, FR-053, FR-054): clear the
    // contents and return to Priming, so each of those restarts from a
    // defined state rather than draining a stale partial ring.
    void reset() noexcept {
        clearStorage();
        tail_ = 0;
        count_ = 0;
        state_ = RingState::Priming;
    }

    // Suspend (AR9, FR-051): clear the contents and enter Stopped.
    void stop() noexcept {
        clearStorage();
        tail_ = 0;
        count_ = 0;
        state_ = RingState::Stopped;
    }

    // Neither reset() nor stop() touches any counter — lifecycle events must
    // not erase the transport history the diagnostics report (AR9). That is
    // structurally guaranteed here: the ring owns no counters at all (AR4).

private:
    // Step an index forward by `delta` frames, wrapping at capacity.
    // `delta` is always in [0, CapacityFrames] at every call site, so one
    // conditional subtraction suffices — no modulo, no division on the audio
    // path.
    static int advance(int index, int delta) noexcept {
        const int stepped = index + delta;
        return (stepped >= CapacityFrames) ? (stepped - CapacityFrames) : stepped;
    }

    void clearStorage() noexcept {
        for (int channel = 0; channel < Channels; ++channel) {
            for (int frame = 0; frame < CapacityFrames; ++frame) {
                storage_[channel][frame] = 0.0f;
            }
        }
    }

    // Non-interleaved: one contiguous run per channel, matching the
    // `float* const*` channel-pointer signatures the DSP core uses.
    float storage_[Channels][CapacityFrames] = {};

    int tail_ = 0;   // index of the oldest held frame
    int count_ = 0;  // frames currently held; occupancy()

    const int startupFill_;
    RingState state_ = RingState::Priming;  // construction ALWAYS yields
                                            // Priming, even when
                                            // startupFill_ == 0 (AR7).
};

} // namespace acfx::nucleo

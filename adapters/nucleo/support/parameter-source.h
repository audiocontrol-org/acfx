#pragma once

// The parameter-source seam for the Nucleo USB audio adapter (FR-039,
// FR-040, D2, D3) plus its first implementation, MidiParameterSource<N>.
// No TinyUSB, no CMSIS, no board headers — this header compiles under the
// `test` preset with no toolchain file, same constraint as parameter-shadow.h
// and midi-cc-map.h.
//
// A ParameterSource is any type T for which this is valid:
//
//     void T::poll(ParameterShadow<N>& shadow) noexcept;
//
// Called once per audio block, BEFORE shadow.flush(). Implementations WRITE
// slots via shadow.set(); they NEVER call setParameter themselves (PSRC5) —
// that stays flush()'s job, which is what keeps application to exactly once
// per dirty parameter per block (PS4).
//
// PSRC1 — one seam, two source kinds: a sampled-state source (ADC, encoder)
// and an event-driven source (USB MIDI, below) both satisfy poll(shadow).
// They converge on the shadow DATA, not on a shared execution model.
//
// MidiParameterSource<N> is the event-driven half: onControlChange() is
// called by the shim as raw (cc, value) pairs arrive off the wire (USB MIDI
// packet decoding is the shim's job — T045 — which is what keeps this type
// host-testable, holding no USB/TinyUSB headers at all). Incoming pairs are
// buffered into a small FIXED-CAPACITY ring; poll() drains the ring once per
// block, resolves each CC through mapCcToParam(cc, N), and dead-bands
// (PSRC2) against the last normalized value WRITTEN to that slot — mirroring
// daisy-main.cpp's kKnobDeadband / lastKnob precedent, except here the
// dead-band is an exact-equality check rather than a tolerance band, because
// MIDI values are already quantized to 128 discrete levels: two identical
// wire values always normalize to the identical bit pattern, so "changed"
// and "not bit-identical" coincide.

#include <array>
#include <cstdint>
#include <optional>

#include "midi-cc-map.h"
#include "parameter-shadow.h"

namespace acfx::nucleo {

template <int N>
class MidiParameterSource {
public:
    // -1.0f is a sentinel: no valid MIDI-derived normalized value
    // (0/127 .. 127/127) is negative, so it guarantees the first real value
    // for a slot always writes (see poll()'s dead-band check below).
    MidiParameterSource() noexcept { lastValue_.fill(-1.0f); }

    // Buffer one incoming CC update, lock-free and allocation-free (PSRC3).
    // Bounded ring of kCapacity slots (D26: single execution context today,
    // but the contract calls for a bounded lock-free buffer regardless of
    // who ends up calling this — a plain head/tail index ring satisfies
    // that without atomics). FULL-BUFFER POLICY: drop the incoming update
    // and keep what is already queued — a dropped intermediate CC costs
    // nothing because PS2 (last-write-wins) means only the most recent
    // value per parameter is ever applied; queueing is just how that most
    // recent value reaches poll(), never a promise every wire event survives.
    void onControlChange(std::uint8_t cc, std::uint8_t value) noexcept {
        const std::size_t next = (head_ + 1) % kCapacity;
        if (next == tail_) {
            // Ring full: drop this update, keep what is already queued.
            return;
        }
        pending_[head_] = CcUpdate{cc, value};
        head_ = next;
    }

    // Drain all pending CC updates and write dead-banded slots (PSRC1,
    // PSRC3). Called once per audio block, before shadow.flush().
    void poll(ParameterShadow<N>& shadow) noexcept {
        while (tail_ != head_) {
            const CcUpdate update = pending_[tail_];
            tail_ = (tail_ + 1) % kCapacity;

            const std::optional<int> index = mapCcToParam(update.cc, N);
            if (!index.has_value()) {
                // Unmapped CC: silently discarded (MC1).
                continue;
            }

            const std::size_t slot = static_cast<std::size_t>(*index);
            const float norm = static_cast<float>(update.value) / 127.0f;
            if (norm != lastValue_[slot]) {
                shadow.set(*index, norm);
                lastValue_[slot] = norm;
            }
        }
    }

private:
    struct CcUpdate {
        std::uint8_t cc;
        std::uint8_t value;
    };

    // Fixed capacity, sized generously for a block's worth of CC traffic;
    // one slot is always kept empty to distinguish full from empty with
    // plain head/tail indices.
    static constexpr std::size_t kCapacity = 32;

    std::array<CcUpdate, kCapacity> pending_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;

    // Last normalized value WRITTEN to each slot (see the constructor for
    // the sentinel rationale).
    std::array<float, N> lastValue_{};
};

}  // namespace acfx::nucleo

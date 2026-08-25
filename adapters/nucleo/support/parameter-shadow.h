#pragma once

// Platform-independent per-ParamId shadow block for the Nucleo USB audio
// adapter (FR-041, FR-042, FR-043, FR-044, D25). No TinyUSB, no CMSIS, no
// board headers — this header compiles under the `test` preset with no
// toolchain file. Anything that cannot satisfy that constraint belongs in
// nucleo-main.cpp instead.
//
// ParameterShadow<N> decouples a parameter source (MIDI CC, a future UI, USB
// control messages) from the effect's setParameter(): any number of set()
// calls within one audio block collapse to a single value per slot
// (last-write-wins, PS2) with no cross-parameter eviction (PS3), and
// flush() applies each dirty slot's final value exactly once before
// clearing its flag (PS4). Bounded by construction at N — an out-of-range
// index is a silent no-op, never an overflow (PS1). N == 0 is valid: both
// set() and flush() degenerate to no-ops (PS5).
//
// Scope note (FR-044): correct for state-valued parameters only. An
// event-valued control (momentary trigger, tap tempo) would need its own
// mechanism — collapsing such a control to a last value would destroy the
// signal. acfx's parameter model is normalized continuous values only, so
// nothing here is affected.

#include <array>
#include <cstdint>

#include "dsp/param-id.h"

namespace acfx::nucleo {

template <int N>
class ParameterShadow {
public:
    // Write a slot from any parameter source. Idempotent, last-write-wins
    // (PS2). Out-of-range index (< 0 or >= N) is a silent no-op (PS1) — it
    // dirties nothing and never reaches setParameter.
    void set(int index, float normalized) noexcept {
        if (index < 0 || index >= N) {
            return;
        }
        const auto slot = static_cast<std::size_t>(index);
        values_[slot] = normalized;
        dirty_[slot] = true;
    }

    // Walk the dirty flags once per audio block, applying each to the
    // effect, then clear (PS4). `apply` is invoked exactly once per dirty
    // slot, in slot order, as apply(acfx::ParamId{i}, value) — never for a
    // clean slot. A fast-moving control on one slot never displaces another
    // slot's single pending update (PS3). With N == 0 this loop runs zero
    // times (PS5).
    template <class ApplyFn>
    void flush(ApplyFn&& apply) noexcept {
        for (int i = 0; i < N; ++i) {
            const auto slot = static_cast<std::size_t>(i);
            if (dirty_[slot]) {
                apply(ParamId{static_cast<std::uint8_t>(i)}, values_[slot]);
                dirty_[slot] = false;
            }
        }
    }

    // Whether slot `index` has a value pending application. Out-of-range
    // index (including any index when N == 0) returns false rather than
    // faulting.
    bool dirty(int index) const noexcept {
        if (index < 0 || index >= N) {
            return false;
        }
        return dirty_[static_cast<std::size_t>(index)];
    }

    static constexpr int size() noexcept { return N; }

private:
    std::array<float, N> values_{};
    std::array<bool, N> dirty_{};
};

} // namespace acfx::nucleo

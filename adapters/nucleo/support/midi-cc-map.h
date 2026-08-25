#pragma once

// MIDI CC -> parameter-index resolution for the Nucleo USB adapter
// (contract: MIDI CC mapping, midi-cc-map.h; I-MC1, I-MC2, I-MC3). No
// TinyUSB, no CMSIS, no board headers -- this header compiles under the
// `test` preset with no toolchain file, same constraint as sample-format.h.

#include <cstdint>
#include <optional>

namespace acfx::nucleo {

// One CC-to-parameter-index binding. `daisy-main.cpp`'s `boundKnobs()` binds
// physical controls to parameter indices generically (never by effect-
// specific enum); this is the same shape applied to MIDI CCs.
struct CcBinding {
    std::uint8_t cc;
    std::uint8_t paramIndex;
};

// The concrete CC convention was open question 7; it is now settled to match
// the desktop workbench's existing default MIDI map (see
// adapters/workbench/workbench-app.cpp, WorkbenchComponent's constructor):
//   CC 74 -> parameter index 0
//   CC 71 -> parameter index 1
// This is the only convention the project has established so far. Omni
// channel: like the workbench, this table carries no MIDI channel and
// mapCcToParam() takes none -- channel filtering is not part of the
// established convention.
//
// Extending the convention (when the operator sets one for further
// parameters) is a TABLE EDIT: add a `CcBinding{cc, paramIndex}` entry below
// the marker. Do not invent CC numbers beyond what the operator has decided.
inline constexpr CcBinding kCcBindings[] = {
    CcBinding{74, 0},
    CcBinding{71, 1},
    CcBinding{72, 2},    // CC72 -> mix (dry/wet)
    CcBinding{76, 19},   // CC76 -> lofi_rate
    CcBinding{77, 20},   // CC77 -> lofi_bits
    // -- Add further CC -> parameter-index bindings above this line. --
};

// Resolve a CC number to a parameter index, or none. Pure and stateless
// (I-MC3): scans the fixed table above for a binding whose `cc` matches.
// If found, the resolved index is bounded by `paramCount` (I-MC2) -- an
// index >= paramCount is never returned, so it can never reach
// setParameter. An unmapped CC, or a mapped CC whose index has fallen out
// of range, both resolve to std::nullopt (I-MC1).
inline std::optional<int> mapCcToParam(std::uint8_t cc, int paramCount) noexcept {
    for (const CcBinding& binding : kCcBindings) {
        if (binding.cc == cc) {
            const int paramIndex = binding.paramIndex;
            if (paramIndex < paramCount) {
                return paramIndex;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace acfx::nucleo

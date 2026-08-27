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
    // Feedback-filter base settings (the fixed point the cutoff/res LFOs sweep
    // around). fb_mode is discrete lowpass/highpass/bandpass (index 0/1/2 <- CC
    // value scaled by paramCount in the effect).
    CcBinding{70, 3},    // CC70 -> fb_cutoff
    CcBinding{73, 4},    // CC73 -> fb_resonance
    CcBinding{75, 5},    // CC75 -> fb_mode
    // Modulation (ModulatedDelayEffect indices 6..18). CC84 skipped: it is the
    // standard Portamento-Control CC. See modulated-delay-params.h for units.
    CcBinding{78, 6},    // CC78 -> delay_mod_rate   (delay-line LFO)
    CcBinding{79, 7},    // CC79 -> delay_mod_depth
    CcBinding{80, 8},    // CC80 -> delay_mod_shape
    CcBinding{81, 9},    // CC81 -> cutoff_mod_rate  (feedback-cutoff LFO)
    CcBinding{82, 10},   // CC82 -> cutoff_mod_depth
    CcBinding{83, 11},   // CC83 -> cutoff_mod_shape
    CcBinding{85, 12},   // CC85 -> res_mod_rate     (feedback-resonance LFO)
    CcBinding{86, 13},   // CC86 -> res_mod_depth
    CcBinding{87, 14},   // CC87 -> res_mod_shape
    CcBinding{88, 15},   // CC88 -> wow_rate         (slow tape pitch drift)
    CcBinding{89, 16},   // CC89 -> wow_depth
    CcBinding{90, 17},   // CC90 -> flutter_rate     (fast tape pitch drift)
    CcBinding{91, 18},   // CC91 -> flutter_depth
    CcBinding{92, 21},   // CC92 -> reverb algorithm (reverse-reverb: room/hall/plate)
    CcBinding{93, 42},   // CC93 -> reverb algorithm (breathing-canyon: Cathedral/Chamber/Swirl/Plate/Cascade)
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

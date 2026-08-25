#pragma once

#include <cstdint>
#include <cstring>

#include "primitives/filters/svf-primitive.h"
#include "primitives/modulation/lfo.h"

// Small, effect-state-free helpers factored out of ModulatedDelayEffect
// purely to keep modulated-delay-effect.h under the repo's per-file line
// budget (Constitution VII / check-portability.sh's <=500-line HARD gate).
// Mirrors the modulated-delay-params.h split (own comment there: "extracted
// ... to keep both files under the Constitution VII per-file line budget").
//
// Scoped to its own namespace, not bare acfx::, so generic names like
// toMode/toShape/floatBits/bitsFloat never collide with anything else at
// acfx:: scope as the core grows (same collision concern
// modulated-delay-params.h's own header comment states for its enum).
//
// Platform independence (Constitution IV): standard library only.

namespace acfx::modulated_delay_detail {

// Bit-reinterpret helpers backing ModulatedDelayEffect's lock-free
// float<->uint32_t pending-parameter exchange (setParameter() publishes via
// floatBits(); pendingValue() reads back via bitsFloat()). Pure, no effect
// state.
inline std::uint32_t floatBits(float f) noexcept {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
inline float bitsFloat(std::uint32_t u) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

// Discrete-parameter index -> enum mappings for the kMode / *ModShape
// params (denormalized float index in, concrete enum out).
inline SvfMode toMode(float index) noexcept {
    switch (static_cast<int>(index)) {
    case 1:  return SvfMode::highpass;
    case 2:  return SvfMode::bandpass;
    case 0:
    default: return SvfMode::lowpass;
    }
}

inline LfoShape toShape(int index) noexcept {
    switch (index) {
    case 1:  return LfoShape::triangle;
    case 2:  return LfoShape::saw;
    case 3:  return LfoShape::random;
    case 0:
    default: return LfoShape::sine;
    }
}

} // namespace acfx::modulated_delay_detail

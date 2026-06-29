// measurement-support.h
// Shared test helpers for the measurement-infrastructure doctest suite.
// Include from any measurement-*-test.cpp that uses these helpers.
// All helpers live in namespace acfx::meastest as inline functions or
// inline constexpr constants so multiple TUs can include this without
// ODR violations.  Do NOT add "using namespace ..." in this header.

#pragma once

#include "effects/svf/svf-effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"

namespace acfx::meastest {

// Configure fx as a lowpass at cutoffHz with zero resonance via the SVF
// parameter table.  Pending parameter edits are consumed on the audio thread
// at the first process() call inside capture().
inline void configureLowpass(acfx::SvfEffect& fx, double cutoffHz) {
    fx.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},
                    acfx::normalize(acfx::SvfEffect::kParams[acfx::SvfEffect::kCutoff],
                                    static_cast<float>(cutoffHz)));
    fx.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance}, 0.0f);
    const float modeIndex =
        static_cast<float>(static_cast<int>(acfx::SvfMode::lowpass));
    fx.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},
                    acfx::normalize(acfx::SvfEffect::kParams[acfx::SvfEffect::kMode],
                                    modeIndex));
}

} // namespace acfx::meastest

#pragma once

#include "effects/svf/svf-effect.h"

// Internal (NON-ABI) definition of the opaque handle, shared by the two ABI
// translation units: the audio capability (svf-web-abi.cpp) and the analysis
// capability (svf-web-analysis.cpp). The C header keeps `SvfHandle` opaque; only
// these adapter sources see the layout.
//
// Beyond the real acfx::SvfEffect, the handle records the prepared sample rate
// and the normalized parameter values last published via svf_set_param. That
// lets the analysis functions rebuild an at-rest impulse and reconstruct the
// filter's exact coefficients WITHOUT reading the effect's private state and
// WITHOUT disturbing the live filter — every analysis result is still computed
// from the real SvfEffect / its real impulse (Principle VII).
struct SvfHandle {
    acfx::SvfEffect effect;
    double sampleRate = 48000.0;
    // paramNorm[i] is meaningful only when paramSet[i]; otherwise the analysis
    // uses the effect's descriptor default (exactly as the live effect does).
    float paramNorm[3] = {0.0f, 0.0f, 0.0f};
    bool paramSet[3] = {false, false, false};
};

// saturation-effect-test-support.h
// Shared test helpers for the SaturationEffect runtime/RT doctest suite
// (tests/core/saturation-effect-rt-test.cpp). Split out of the original
// saturation-effect-test.cpp's anonymous-namespace helper block so the
// runtime test file can stay under the per-file governance byte envelope
// without duplicating this logic. All helpers live in namespace
// acfx::sattest as inline functions / inline constexpr constants so
// multiple TUs can include this without ODR violations, mirroring
// measurement-support.h's convention. Do NOT add "using namespace ..." in
// this header.

#pragma once

#include <cstddef>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/saturation/saturation-core.h"    // SaturationVoicing / SaturationQuality
#include "effects/saturation/saturation-effect.h"  // SaturationEffect (T015/T016)

namespace acfx::sattest {

inline constexpr float  kPi          = 3.14159265358979323846f;
inline constexpr float  kSampleRateF = 48000.0f;
inline constexpr double kSampleRateD = 48000.0;

// Shared sine-stimulus window: 1 kHz fundamental over 100 integer cycles at
// 48 kHz (matches the anti-leakage window contract in measurement-support.h,
// and the same window every other saturation-*-test.cpp file in this suite
// uses -- every harmonic lands exactly on a Goertzel bin).
inline constexpr double      kFundamentalHz = 1000.0;
inline constexpr std::size_t kNumSamples    = 4800;  // 100 cycles * 48 samples/cycle
inline constexpr float       kAmplitude     = 0.5f;  // moderate: exercises the shaper without full clip

// Convert a desired PLAIN-units value for a SaturationEffect parameter into
// the normalized 0..1 value setParameter() expects, via the shared
// descriptor table -- mirrors svf-test.cpp's MonoDriver / measurement-
// support.h's configureLowpass idiom. Never hand-roll the normalize math
// here: the descriptor table (min/max/skew) is the single source of truth
// (FR-009), and exact ranges are a T015 tuning decision this test must not
// assume beyond what data-model.md fixes (tone/mix/bias) or documents as
// "e.g." (drive/output).
inline float normFor(acfx::SaturationEffect::Param p, float plainValue) {
    return acfx::normalize(acfx::SaturationEffect::kParams[p], plainValue);
}

// A fully-configured mono SaturationEffect driver, one sample per
// process() call. Configuration is published via setParameter() (as if
// from a non-audio/control thread) in the constructor; it is applied by the
// FIRST process() call this driver makes, since applyPending() (per the
// SvfEffect idiom) runs at the top of process() and consumes every pending
// edit in one shot. Mirrors svf-test.cpp's MonoDriver exactly.
struct MonoEffectDriver {
    acfx::SaturationEffect fx;
    float scratch = 0.0f;

    MonoEffectDriver(float driveDb, float biasPlain, float tonePlain, float mixPlain, float outputDb,
                      acfx::SaturationVoicing voicing = acfx::SaturationVoicing::softClip,
                      acfx::SaturationQuality quality = acfx::SaturationQuality::adaa) {
        fx.prepare(acfx::ProcessContext{kSampleRateD, 1, 1});
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kVoicing},
                         normFor(acfx::SaturationEffect::kVoicing, static_cast<float>(static_cast<int>(voicing))));
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kQuality},
                         normFor(acfx::SaturationEffect::kQuality, static_cast<float>(static_cast<int>(quality))));
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kDrive}, normFor(acfx::SaturationEffect::kDrive, driveDb));
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kBias}, normFor(acfx::SaturationEffect::kBias, biasPlain));
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kTone}, normFor(acfx::SaturationEffect::kTone, tonePlain));
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kMix}, normFor(acfx::SaturationEffect::kMix, mixPlain));
        fx.setParameter(acfx::ParamId{acfx::SaturationEffect::kOutput}, normFor(acfx::SaturationEffect::kOutput, outputDb));
    }

    float operator()(float in) noexcept {
        scratch = in;
        float* chans[1] = {&scratch};
        acfx::AudioBlock block(chans, 1, 1);
        fx.process(block);
        return scratch;
    }
};

} // namespace acfx::sattest

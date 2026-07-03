#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-parameters.h"
#include "support/allocation-sentinel.h"

// T037 -- the no-heap-allocation-in-process() invariant (SC-013, FR-018) for
// ProgramDependentSaturationEffect::process(), split out of no-allocation-test.cpp
// (mirrors compressor-no-allocation-test.cpp's split off the same shared file)
// to keep every no-allocation suite within the ~300-500-line module budget
// (Constitution VII, FR-028). Uses the shared thread-local allocation sentinel;
// the count is captured out of the measured region before any assertion macro
// runs.

using namespace acfx;
using acfx::test::AllocationSentinel;
using PdsEffect = ProgramDependentSaturationEffect;

namespace {

// Normalize a PLAIN-units value for a ProgramDependentSaturationEffect
// parameter via the shared descriptor table, mirroring
// compressor-no-allocation-test.cpp's paramNorm / program-dependent-
// saturation-effect-test.cpp's normFor. Never hand-roll the normalize math
// here: the descriptor table (min/max/skew) is the single source of truth
// (FR-016). Discrete params (detection/dynamicPreset/externalSidechain/
// stereoLink/*Curve) take the bucket INDEX as `plainValue`.
float paramNorm(PdsEffect::Param p, float plainValue) {
    return normalize(PdsEffect::kParams[p], plainValue);
}

// SC-013 sweep configuration -- one row per axis-combination the no-allocation
// invariant must hold across (targets x curves x topologies x presets x
// sidechain/link configs). NOT a full cartesian product; an orthogonal design
// mirroring compressor-no-allocation-test.cpp's kSc012Sweep: every axis VALUE
// named in the task (each of the four modulation targets; each of the three
// curves; feedForward/feedBack; external-sidechain on/off; perChannel/linked;
// scHpf on/off; the opto/tapeComp presets) appears at least once below, plus a
// stress row combining every axis at once and a preset-then-manual-override
// row proving the override semantics stay allocation-free too.
//
// Rows with `preset == none` drive the four modulation targets directly
// (driveDepth/biasDepth/toneDepth/mixDepth + curves); rows with a non-none
// preset instead publish `kDynamicPreset` (which writes the documented
// matrix, program-dependent-saturation-presets.h) and leave the axis fields
// below unused UNLESS `overrideToneAfterPreset` is set, in which case
// kToneDepth/kToneCurve are ALSO published (applied AFTER the preset per
// applyPending()'s documented "preset first, then individual overrides win"
// order) -- this is also what drives the newBlock() per-block-tone REAL path
// (a non-zero tone depth), since every preset here ships toneDepth == 0.
struct PdsSweepConfig {
    const char* label;
    float driveDepth;
    ModCurve driveCurve;
    float biasDepth;
    ModCurve biasCurve;
    float toneDepth;
    ModCurve toneCurve;
    float mixDepth;
    ModCurve mixCurve;
    PdsDetection detection;
    PdsEffect::DynamicPreset preset;
    bool overrideToneAfterPreset;
    bool externalSidechain;
    float scHpfHz;
    PdsEffect::StereoLink stereoLink;
};

constexpr PdsSweepConfig kSc013Sweep[] = {
    // driveDepth+linear, feedForward, no preset, hpfOff, perChannel, internalKey.
    {"drive+0.5/linear/feedForward/hpfOff/perChannel/internalKey",
     0.5f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear,
     PdsDetection::feedForward, PdsEffect::DynamicPreset::none, false, false, 0.0f,
     PdsEffect::StereoLink::perChannel},
    // biasDepth+logarithmic, feedBack, hpfOn, linked, externalKey.
    {"bias-0.5/logarithmic/feedBack/hpfOn/linked/externalKey",
     0.0f, ModCurve::linear, -0.5f, ModCurve::logarithmic, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear,
     PdsDetection::feedBack, PdsEffect::DynamicPreset::none, false, true, 200.0f,
     PdsEffect::StereoLink::linked},
    // toneDepth+exponential (exercises newBlock's real per-block SVF-recompute
    // path, not the zero-depth skip guard), feedForward, hpfOff, perChannel,
    // internalKey.
    {"tone+0.6/exponential/feedForward/hpfOff/perChannel/internalKey",
     0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.6f, ModCurve::exponential, 0.0f, ModCurve::linear,
     PdsDetection::feedForward, PdsEffect::DynamicPreset::none, false, false, 0.0f,
     PdsEffect::StereoLink::perChannel},
    // mixDepth+linear, feedBack, hpfOn, linked, externalKey.
    {"mix-0.4/linear/feedBack/hpfOn/linked/externalKey",
     0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear, -0.4f, ModCurve::linear,
     PdsDetection::feedBack, PdsEffect::DynamicPreset::none, false, true, 150.0f,
     PdsEffect::StereoLink::linked},
    // Stress: every target modulated with a different curve at once, feedBack,
    // hpfOn, linked, externalKey.
    {"stress: all 4 targets/mixed curves/feedBack/hpfOn/linked/externalKey",
     0.7f, ModCurve::logarithmic, 0.3f, ModCurve::exponential, -0.4f, ModCurve::linear, 0.2f,
     ModCurve::logarithmic, PdsDetection::feedBack, PdsEffect::DynamicPreset::none, false, true, 100.0f,
     PdsEffect::StereoLink::linked},
    // opto preset (pure), hpfOff, perChannel, internalKey.
    {"preset=opto/hpfOff/perChannel/internalKey",
     0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear,
     PdsDetection::feedForward, PdsEffect::DynamicPreset::opto, false, false, 0.0f,
     PdsEffect::StereoLink::perChannel},
    // tapeComp preset (pure), hpfOff, perChannel, internalKey.
    {"preset=tapeComp/hpfOff/perChannel/internalKey",
     0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.0f, ModCurve::linear,
     PdsDetection::feedForward, PdsEffect::DynamicPreset::tapeComp, false, false, 0.0f,
     PdsEffect::StereoLink::perChannel},
    // opto preset + a manual tone-depth override (US9 override semantics: the
    // preset writes toneDepth==0, the override below applies AFTER and must
    // win) + externalKey + linked + hpfOn -- combines preset, override, and
    // sidechain/link/HPF in one row.
    {"preset=opto + tone override/externalKey/linked/hpfOn",
     0.0f, ModCurve::linear, 0.0f, ModCurve::linear, 0.5f, ModCurve::exponential, 0.0f, ModCurve::linear,
     PdsDetection::feedForward, PdsEffect::DynamicPreset::opto, true, true, 300.0f,
     PdsEffect::StereoLink::linked},
};

} // namespace

// T037 -- the no-heap-allocation-in-process() invariant for
// ProgramDependentSaturationEffect (SC-013, FR-018). Mirrors the
// CompressorEffect pattern (compressor-no-allocation-test.cpp): prepare() and
// every setParameter() publish + the ONE process() call that consumes them
// (applyPending() only runs at the top of process(), per program-dependent-
// saturation-effect.h's documented thread-ownership) all run OUTSIDE the
// sentinel scope -- that priming call is control-thread configuration, not
// the measured region. Only the REPEATED process() calls after that (with no
// further parameter edits in between) are asserted allocation-free, across
// the kSc013Sweep rows above and, per row, using EITHER the keyless
// process(io) overload or the external-key process(io, sidechain) overload
// per that row's `externalSidechain` flag (FR-012), which also drives
// newBlock()'s per-block tone path since it runs inside process() (Decision 4).
TEST_CASE("ProgramDependentSaturationEffect::process allocates nothing across the "
          "SC-013 configuration sweep") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kBlockSize = 64;
    constexpr int kNumChannels = 2;

    for (const PdsSweepConfig& cfg : kSc013Sweep) {
        PdsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, kNumChannels});

        // A representative engaged static drive so the saturation path is
        // actually active (not sitting at an identity default) in every row.
        fx.setParameter(ParamId{PdsEffect::kDrive}, paramNorm(PdsEffect::kDrive, 12.0f));

        if (cfg.preset == PdsEffect::DynamicPreset::none) {
            // Direct axis edits (control thread -- may allocate).
            fx.setParameter(ParamId{PdsEffect::kDriveDepth},
                            paramNorm(PdsEffect::kDriveDepth, cfg.driveDepth));
            fx.setParameter(ParamId{PdsEffect::kDriveCurve},
                            paramNorm(PdsEffect::kDriveCurve,
                                      static_cast<float>(static_cast<int>(cfg.driveCurve))));
            fx.setParameter(ParamId{PdsEffect::kBiasDepth},
                            paramNorm(PdsEffect::kBiasDepth, cfg.biasDepth));
            fx.setParameter(ParamId{PdsEffect::kBiasCurve},
                            paramNorm(PdsEffect::kBiasCurve,
                                      static_cast<float>(static_cast<int>(cfg.biasCurve))));
            fx.setParameter(ParamId{PdsEffect::kToneDepth},
                            paramNorm(PdsEffect::kToneDepth, cfg.toneDepth));
            fx.setParameter(ParamId{PdsEffect::kToneCurve},
                            paramNorm(PdsEffect::kToneCurve,
                                      static_cast<float>(static_cast<int>(cfg.toneCurve))));
            fx.setParameter(ParamId{PdsEffect::kMixDepth},
                            paramNorm(PdsEffect::kMixDepth, cfg.mixDepth));
            fx.setParameter(ParamId{PdsEffect::kMixCurve},
                            paramNorm(PdsEffect::kMixCurve,
                                      static_cast<float>(static_cast<int>(cfg.mixCurve))));
            fx.setParameter(ParamId{PdsEffect::kDetection},
                            paramNorm(PdsEffect::kDetection,
                                      static_cast<float>(static_cast<int>(cfg.detection))));
        } else {
            // Preset FIRST (US9): writes the documented matrix (drive/bias/
            // tone/mix depth+curve, detection, detector, ballistics, timing).
            fx.setParameter(ParamId{PdsEffect::kDynamicPreset},
                            paramNorm(PdsEffect::kDynamicPreset,
                                      static_cast<float>(static_cast<int>(cfg.preset))));
            if (cfg.overrideToneAfterPreset) {
                // A same-block individual edit published AFTER the preset --
                // must OVERRIDE it on the next process() (US9 semantics).
                fx.setParameter(ParamId{PdsEffect::kToneDepth},
                                paramNorm(PdsEffect::kToneDepth, cfg.toneDepth));
                fx.setParameter(ParamId{PdsEffect::kToneCurve},
                                paramNorm(PdsEffect::kToneCurve,
                                          static_cast<float>(static_cast<int>(cfg.toneCurve))));
            }
        }

        fx.setParameter(ParamId{PdsEffect::kExternalSidechain},
                        paramNorm(PdsEffect::kExternalSidechain, cfg.externalSidechain ? 1.0f : 0.0f));
        fx.setParameter(ParamId{PdsEffect::kScHpf}, paramNorm(PdsEffect::kScHpf, cfg.scHpfHz));
        fx.setParameter(ParamId{PdsEffect::kStereoLink},
                        paramNorm(PdsEffect::kStereoLink,
                                  static_cast<float>(static_cast<int>(cfg.stereoLink))));

        // The priming process() call that consumes every pending edit above
        // via applyPending() -- control-thread configuration, OUTSIDE the
        // sentinel scope (mirrors compressor-no-allocation-test.cpp).
        std::vector<float> primeLeft(static_cast<std::size_t>(kBlockSize), 0.1f);
        std::vector<float> primeRight(static_cast<std::size_t>(kBlockSize), 0.1f);
        float* primeChans[kNumChannels] = {primeLeft.data(), primeRight.data()};
        AudioBlock primeBlock(primeChans, kNumChannels, kBlockSize);
        fx.process(primeBlock);

        // Sidechain block for the external-key overload (FR-012) -- a
        // pre-sized buffer allocated outside the sentinel scope, like the
        // main-channel buffers below.
        std::vector<float> scLeft(static_cast<std::size_t>(kBlockSize), 0.05f);
        std::vector<float> scRight(static_cast<std::size_t>(kBlockSize), 0.05f);
        float* scChans[kNumChannels] = {scLeft.data(), scRight.data()};
        AudioBlock sidechain(scChans, kNumChannels, kBlockSize);

        std::vector<float> left(static_cast<std::size_t>(kBlockSize));
        std::vector<float> right(static_cast<std::size_t>(kBlockSize));
        for (int i = 0; i < kBlockSize; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            const float s = 0.7f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
            left[static_cast<std::size_t>(i)] = s;
            right[static_cast<std::size_t>(i)] = 0.9f * s; // slightly different per-channel level
        }
        float* chans[kNumChannels] = {left.data(), right.data()};

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            AudioBlock block(chans, kNumChannels, kBlockSize);
            // Vary the per-block amplitude so newBlock()'s per-block tone
            // envelope (Decision 4) actually moves block-to-block, not just
            // process()'s per-sample drive/bias/mix modulation.
            const float scale = (i % 2 == 0) ? 1.0f : 0.4f;
            for (int ch = 0; ch < kNumChannels; ++ch)
                for (int n = 0; n < kBlockSize; ++n)
                    chans[ch][n] = ((ch == 0) ? left[static_cast<std::size_t>(n)]
                                               : right[static_cast<std::size_t>(n)]) * scale;

            if (cfg.externalSidechain)
                fx.process(block, sidechain);
            else
                fx.process(block);
        }
        const std::size_t allocations = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocations == 0, "config=", cfg.label, " allocated ", allocations);
    }
}

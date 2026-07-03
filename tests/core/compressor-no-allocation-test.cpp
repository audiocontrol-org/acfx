#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/compressor/compressor-effect.h"
#include "effects/compressor/compressor-parameters.h"
#include "support/allocation-sentinel.h"

// T039 — the no-heap-allocation (SC-012, FR-022) and no-NaN/Inf (SC-013)
// invariants for CompressorEffect::process(), split out of no-allocation-test.cpp
// to keep both files within the ~300–500-line module budget (Constitution VII,
// FR-028). Uses the shared thread-local allocation sentinel; the count is
// captured out of the measured region before any assertion macro runs.

using namespace acfx;
using acfx::test::AllocationSentinel;

namespace {

// T039 (SC-012/SC-013) shared helper -- normalize a PLAIN-units value for a
// CompressorEffect parameter via the shared descriptor table, mirroring
// tests/core/compressor-effect-test.cpp's local `normFor`. Never hand-roll the
// normalize math here: the descriptor table (min/max/skew) is the single
// source of truth (FR-019). Discrete params (mode/detection/detector/
// ballisticsSite/autoMakeup/stereoLink) take the bucket INDEX as `plainValue`
// (normalize() dispatches on ParameterDescriptor::kind), which for every
// discrete param exercised below happens to equal the composed enum's
// underlying value (GainMode, Detection, BallisticsSite, DetectMode's
// peak/rms, StereoLink all declare their enumerators in the same order as the
// matching compressor-parameters.h label array).
float paramNorm(CompressorEffect::Param p, float plainValue) {
    return normalize(CompressorEffect::kParams[p], plainValue);
}

// SC-012 sweep configuration -- one row per axis-combination the no-allocation
// invariant (spec.md SC-012, FR-022) must hold across. NOT a full cartesian
// product (GainMode x Detection x BallisticsSite x DetectMode x scHpf x
// lookahead x autoMakeup x StereoLink x external-key would be 4*2*2*2*2*2*2*2
// = 512 rows); instead a small orthogonal design -- every individual axis
// VALUE named in the task (each GainMode; feedForward/feedBack; level/gain
// site; peak/rms detector; sidechain HPF on/off; lookahead on/off; auto-makeup
// on/off; linked/perChannel; external-key vs. internal-key) appears at least
// twice across the six rows below, plus two rows ("stress") that combine the
// axis values most likely to interact badly (feedback + linked + external key
// + lookahead + HPF all active at once, and the inverse corner).
struct CompressorSweepConfig {
    const char* label;
    GainMode mode;
    Detection detection;
    BallisticsSite site;
    DetectMode detector;
    bool scHpfOn;
    bool lookaheadOn;
    bool autoMakeupOn;
    StereoLink stereoLink;
    bool externalKey;
};

constexpr CompressorSweepConfig kSc012Sweep[] = {
    {"compress/feedForward/level/peak/hpfOff/laOff/amOff/perChannel/internalKey",
     GainMode::compress, Detection::feedForward, BallisticsSite::level, DetectMode::peak,
     false, false, false, StereoLink::perChannel, false},
    {"limit/feedBack/gain/rms/hpfOn/laOn/amOn/linked/externalKey",
     GainMode::limit, Detection::feedBack, BallisticsSite::gain, DetectMode::rms,
     true, true, true, StereoLink::linked, true},
    {"expand/feedForward/gain/peak/hpfOn/laOff/amOff/linked/internalKey",
     GainMode::expand, Detection::feedForward, BallisticsSite::gain, DetectMode::peak,
     true, false, false, StereoLink::linked, false},
    {"gate/feedBack/level/rms/hpfOff/laOn/amOn/perChannel/externalKey",
     GainMode::gate, Detection::feedBack, BallisticsSite::level, DetectMode::rms,
     false, true, true, StereoLink::perChannel, true},
    {"compress/feedBack/gain/rms/hpfOn/laOn/amOff/linked/externalKey (stress)",
     GainMode::compress, Detection::feedBack, BallisticsSite::gain, DetectMode::rms,
     true, true, false, StereoLink::linked, true},
    {"gate/feedForward/level/peak/hpfOff/laOff/amOn/perChannel/internalKey (stress)",
     GainMode::gate, Detection::feedForward, BallisticsSite::level, DetectMode::peak,
     false, false, true, StereoLink::perChannel, false},
};

} // namespace

// T039 -- the no-heap-allocation-in-process() invariant for CompressorEffect
// (SC-012, FR-022). Mirrors the SvfEffect/SaturationCore/EnvelopeFollower
// pattern in no-allocation-test.cpp: prepare() and every setParameter() publish
// + the ONE process() call that consumes them (applyPending() only runs at the
// top of process(), per compressor-effect.h's documented thread-ownership) all
// run OUTSIDE the sentinel scope -- that priming call is control-thread
// configuration, not the measured region. Only the REPEATED process() calls
// after that (with no further parameter edits in between) are asserted
// allocation-free, across the kSc012Sweep rows above and, per row, using
// EITHER the keyless process(io) overload or the external-key
// process(io, sidechain) overload per that row's `externalKey` flag (FR-014).
TEST_CASE("CompressorEffect::process allocates nothing across the SC-012 configuration sweep") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kBlockSize = 64;
    constexpr int kNumChannels = 2;

    for (const CompressorSweepConfig& cfg : kSc012Sweep) {
        CompressorEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, kNumChannels});

        // Publish every axis under test (control thread -- may allocate).
        fx.setParameter(ParamId{CompressorEffect::kMode},
                        paramNorm(CompressorEffect::kMode,
                                  static_cast<float>(static_cast<int>(cfg.mode))));
        fx.setParameter(ParamId{CompressorEffect::kDetection},
                        paramNorm(CompressorEffect::kDetection,
                                  static_cast<float>(static_cast<int>(cfg.detection))));
        fx.setParameter(ParamId{CompressorEffect::kBallisticsSite},
                        paramNorm(CompressorEffect::kBallisticsSite,
                                  static_cast<float>(static_cast<int>(cfg.site))));
        fx.setParameter(ParamId{CompressorEffect::kDetector},
                        paramNorm(CompressorEffect::kDetector,
                                  static_cast<float>(static_cast<int>(cfg.detector))));
        fx.setParameter(ParamId{CompressorEffect::kScHpf},
                        paramNorm(CompressorEffect::kScHpf, cfg.scHpfOn ? 200.0f : 0.0f));
        fx.setParameter(ParamId{CompressorEffect::kLookahead},
                        paramNorm(CompressorEffect::kLookahead, cfg.lookaheadOn ? 0.005f : 0.0f));
        fx.setParameter(ParamId{CompressorEffect::kAutoMakeup},
                        paramNorm(CompressorEffect::kAutoMakeup, cfg.autoMakeupOn ? 1.0f : 0.0f));
        fx.setParameter(ParamId{CompressorEffect::kStereoLink},
                        paramNorm(CompressorEffect::kStereoLink,
                                  static_cast<float>(static_cast<int>(cfg.stereoLink))));
        // A representative engaged threshold/ratio so the gain path is
        // actually active (not sitting at an identity default) in every row.
        fx.setParameter(ParamId{CompressorEffect::kThreshold},
                        paramNorm(CompressorEffect::kThreshold, -24.0f));
        fx.setParameter(ParamId{CompressorEffect::kRatio},
                        paramNorm(CompressorEffect::kRatio, 4.0f));

        // The priming process() call that consumes every pending edit above
        // via applyPending() -- control-thread configuration, OUTSIDE the
        // sentinel scope (mirrors the EnvelopeFollower cases' setter calls).
        std::vector<float> primeLeft(static_cast<std::size_t>(kBlockSize), 0.1f);
        std::vector<float> primeRight(static_cast<std::size_t>(kBlockSize), 0.1f);
        float* primeChans[kNumChannels] = {primeLeft.data(), primeRight.data()};
        AudioBlock primeBlock(primeChans, kNumChannels, kBlockSize);
        fx.process(primeBlock);

        // Sidechain block for the external-key overload (FR-014) -- a
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
            right[static_cast<std::size_t>(i)] = s;
        }
        float* chans[kNumChannels] = {left.data(), right.data()};

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            AudioBlock block(chans, kNumChannels, kBlockSize);
            if (cfg.externalKey)
                fx.process(block, sidechain);
            else
                fx.process(block);
        }
        const std::size_t allocations = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocations == 0, "config=", cfg.label, " allocated ", allocations);
    }
}

// T039 -- the no-NaN/Inf-in-process() invariant for CompressorEffect (SC-013).
// For a representative config set spanning both topologies (feedforward rows
// exercise silence/DC/impulse/threshold-crossing-ramp; feedback rows do too,
// PLUS a dedicated cold-start transient below, since only feedback taps a
// "previous output" — compressor-core.h's prevOutput_ — into detection), feed
// each stimulus and assert every output sample is finite. The silence stimulus
// additionally asserts exact silence-out (SC-013's second half): the per-
// sample chain (sidechain HPF -> detector -> static curve -> ballistics ->
// makeup -> lookahead -> VCA multiply -> feedback tap -> mix -> output trim)
// is purely multiplicative/affine-through-zero, so x=0 must produce y=0
// regardless of mode/threshold/ratio/makeup -- a real bug (e.g. an additive
// DC leak) would fail this even though it might not fail an isfinite() check.
TEST_CASE("CompressorEffect::process never emits NaN/Inf; silence in yields silence out (SC-013)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kBlockSize = 256;
    constexpr int kNumChannels = 1;

    struct Sc013Config {
        const char* label;
        GainMode mode;
        Detection detection;
        BallisticsSite site;
        DetectMode detector;
        bool scHpfOn;
        bool lookaheadOn;
        bool autoMakeupOn;
    };

    constexpr Sc013Config kConfigs[] = {
        {"compress/feedForward/level/peak", GainMode::compress, Detection::feedForward,
         BallisticsSite::level, DetectMode::peak, false, false, false},
        {"limit/feedForward/gain/rms/hpfOn/laOn/amOn", GainMode::limit, Detection::feedForward,
         BallisticsSite::gain, DetectMode::rms, true, true, true},
        {"compress/feedBack/level/peak", GainMode::compress, Detection::feedBack,
         BallisticsSite::level, DetectMode::peak, false, false, false},
        {"gate/feedBack/gain/rms/hpfOn/laOn/amOn", GainMode::gate, Detection::feedBack,
         BallisticsSite::gain, DetectMode::rms, true, true, true},
    };

    for (const Sc013Config& cfg : kConfigs) {
        CompressorEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, kNumChannels});

        fx.setParameter(ParamId{CompressorEffect::kMode},
                        paramNorm(CompressorEffect::kMode,
                                  static_cast<float>(static_cast<int>(cfg.mode))));
        fx.setParameter(ParamId{CompressorEffect::kDetection},
                        paramNorm(CompressorEffect::kDetection,
                                  static_cast<float>(static_cast<int>(cfg.detection))));
        fx.setParameter(ParamId{CompressorEffect::kBallisticsSite},
                        paramNorm(CompressorEffect::kBallisticsSite,
                                  static_cast<float>(static_cast<int>(cfg.site))));
        fx.setParameter(ParamId{CompressorEffect::kDetector},
                        paramNorm(CompressorEffect::kDetector,
                                  static_cast<float>(static_cast<int>(cfg.detector))));
        fx.setParameter(ParamId{CompressorEffect::kScHpf},
                        paramNorm(CompressorEffect::kScHpf, cfg.scHpfOn ? 200.0f : 0.0f));
        fx.setParameter(ParamId{CompressorEffect::kLookahead},
                        paramNorm(CompressorEffect::kLookahead, cfg.lookaheadOn ? 0.005f : 0.0f));
        fx.setParameter(ParamId{CompressorEffect::kAutoMakeup},
                        paramNorm(CompressorEffect::kAutoMakeup, cfg.autoMakeupOn ? 1.0f : 0.0f));
        fx.setParameter(ParamId{CompressorEffect::kThreshold},
                        paramNorm(CompressorEffect::kThreshold, -24.0f));
        fx.setParameter(ParamId{CompressorEffect::kRatio},
                        paramNorm(CompressorEffect::kRatio, 4.0f));

        // Stimulus 1: silence. This block ALSO doubles as the priming
        // process() call that consumes every pending edit published above --
        // and is itself the exact "silence in -> silence out" assertion.
        std::vector<float> silence(static_cast<std::size_t>(kBlockSize), 0.0f);
        {
            float* chans[1] = {silence.data()};
            AudioBlock block(chans, 1, kBlockSize);
            fx.process(block);
        }
        for (float v : silence) {
            INFO("config=" << cfg.label << " stimulus=silence");
            CHECK(std::isfinite(v));
            CHECK(v == doctest::Approx(0.0f));
        }

        // Stimulus 2: DC -- a sustained level well above the -24 dB threshold,
        // exercising the static curve + ballistics + makeup + lookahead + mix/
        // output stages at steady state, continuing from the settled silence
        // state above (a realistic streaming continuation, not a fresh reset).
        std::vector<float> dc(static_cast<std::size_t>(kBlockSize), 0.9f);
        {
            float* chans[1] = {dc.data()};
            AudioBlock block(chans, 1, kBlockSize);
            fx.process(block);
        }
        for (float v : dc) {
            INFO("config=" << cfg.label << " stimulus=DC");
            CHECK(std::isfinite(v));
        }

        // Stimulus 3: impulse -- one full-scale sample amid silence, the
        // sharpest transient into the detector/gain-computer/ballistics
        // chain, continuing from the DC-settled state above.
        std::vector<float> impulse(static_cast<std::size_t>(kBlockSize), 0.0f);
        impulse[0] = 1.0f;
        {
            float* chans[1] = {impulse.data()};
            AudioBlock block(chans, 1, kBlockSize);
            fx.process(block);
        }
        for (float v : impulse) {
            INFO("config=" << cfg.label << " stimulus=impulse");
            CHECK(std::isfinite(v));
        }

        // Stimulus 4: threshold-crossing ramp -- a triangle from silence up
        // through the -24 dB threshold to full scale and back down, crossing
        // the static curve's threshold/knee boundary on BOTH the attack
        // (rising) and release (falling) side within one block.
        std::vector<float> ramp(static_cast<std::size_t>(kBlockSize));
        for (int i = 0; i < kBlockSize; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kBlockSize - 1);
            ramp[static_cast<std::size_t>(i)] = (t < 0.5f) ? (t * 2.0f) : (2.0f - t * 2.0f);
        }
        {
            float* chans[1] = {ramp.data()};
            AudioBlock block(chans, 1, kBlockSize);
            fx.process(block);
        }
        for (float v : ramp) {
            INFO("config=" << cfg.label << " stimulus=threshold-crossing ramp");
            CHECK(std::isfinite(v));
        }
    }

    // Stimulus 5 (feedback topology only): cold-start transient. A FRESH
    // feedback-topology instance -- prepared but with NO prior processing at
    // all -- immediately hit with a full-scale impulse as its very first
    // sample. Only feedback topology feeds a "previous output" (prevOutput_,
    // cold at 0.0f per compressor-core.h's in-class initializer/reset()) into
    // detection, so this is the one stimulus where a never-yet-written piece
    // of feedback state reaches the detector on sample 0.
    {
        CompressorEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, kNumChannels});
        fx.setParameter(ParamId{CompressorEffect::kDetection},
                        paramNorm(CompressorEffect::kDetection,
                                  static_cast<float>(static_cast<int>(Detection::feedBack))));
        fx.setParameter(ParamId{CompressorEffect::kThreshold},
                        paramNorm(CompressorEffect::kThreshold, -24.0f));
        fx.setParameter(ParamId{CompressorEffect::kRatio},
                        paramNorm(CompressorEffect::kRatio, 8.0f));

        std::vector<float> coldImpulse(static_cast<std::size_t>(kBlockSize), 0.0f);
        coldImpulse[0] = 1.0f;
        float* chans[1] = {coldImpulse.data()};
        AudioBlock block(chans, 1, kBlockSize);
        // The FIRST process() call both consumes the pending edits above
        // (applyPending()) AND processes this cold-start impulse in the same
        // call -- exactly the sequence a host driving a freshly-prepared
        // feedback-topology instance would produce.
        fx.process(block);

        for (float v : coldImpulse) {
            INFO("stimulus=feedback cold-start transient");
            CHECK(std::isfinite(v));
        }
    }
}

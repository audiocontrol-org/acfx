#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-presets.h"

// T038 -- SC-014 numerical-safety suite. Asserts NO NaN/Inf output for a
// battery of pathological STIMULI x pathological CONFIGS, and that every
// modulated parameter stays inside SaturationCore's valid range (the core
// CLAMPS drive'/bias'/tone'/mix' per FR-010) so the effect output stays
// BOUNDED -- never a runaway -- even at extreme depth + feedback.
//
// The observable proxy for "modulated parameter stayed in range" is a bounded
// output: FR-010's clamps keep the composed SaturationCore's loudness bounding
// engaged, so a diverging/unbounded output would be the only externally
// visible symptom of a clamp having been defeated. This suite therefore checks
// std::isfinite AND |y| <= kBound on every emitted sample.
//
// References: spec.md SC-014, FR-010, FR-021, and the "Edge cases" section
// (silence / DC / impulse / level step / feedback cold start / extreme depth /
// short-tau at low fs / degenerate ref window / response-curve extremes);
// core/effects/program-dependent-saturation/program-dependent-saturation-
// {effect,core}.h (the drive/bias/tone/mix clamps, the feedback cold-start
// prevOutput_==0, the degenerate-ref-window divide guard); core/primitives/
// dynamics/envelope-follower.h (the -120 dB detection floor). Conventions
// mirror program-dependent-saturation-{effect,presets}-test.cpp (normFor,
// finite-output checks) and saturation-effect-rt-test.cpp (kBounded sanity).

using namespace acfx;

namespace {

using DynamicPreset = ProgramDependentSaturationEffect::DynamicPreset;
using Param = ProgramDependentSaturationEffect::Param;
using ConfigFn = std::function<void(ProgramDependentSaturationEffect&)>;
using CoreConfigFn = std::function<void(ProgramDependentSaturationCore&)>;

// Generous sanity bound (matches saturation-effect-rt-test.cpp's kBoundedOutput
// convention): the composed shape is bounded to +-1, the emphasis/tone SVF
// stages are low-resonance, and output makeup stays at unity (0 dB) in every
// config below, so |y| far below 8x full-scale catches only a genuinely
// diverging / clamp-defeated output, not ordinary transient ringing.
constexpr float kBound = 8.0f;

// Convert a PLAIN-units value into the normalized 0..1 setParameter() expects,
// via the shared descriptor table (never hand-roll normalize math -- FR-016).
float normFor(Param p, float plainValue) {
    return normalize(ProgramDependentSaturationEffect::kParams[p], plainValue);
}

// The pathological stimulus battery (spec.md "Edge cases"). Each is generated
// on demand at (sampleIndex i, block length n) so the same set drives both the
// block-based effect path and the sample-based core path.
enum class Stim { silence, dcPos, dcNeg, impulse, levelStep, square, denormal };

constexpr Stim kStims[] = {Stim::silence, Stim::dcPos,     Stim::dcNeg, Stim::impulse,
                           Stim::levelStep, Stim::square, Stim::denormal};

const char* stimName(Stim s) {
    switch (s) {
    case Stim::silence:   return "silence";
    case Stim::dcPos:     return "dc+1";
    case Stim::dcNeg:     return "dc-1";
    case Stim::impulse:   return "impulse";
    case Stim::levelStep: return "level-step";
    case Stim::square:    return "square";
    case Stim::denormal:  return "denormal";
    }
    return "?";
}

// One pathological sample. impulse: full-scale spike at i==0. levelStep: a hard
// silence -> full-scale -> silence step (thirds). square: full-scale +-1 at a
// fixed 16-sample half-period. denormal: a value ~1e-30 that flushes to a
// denormal/underflow range on most FPUs.
float stimSample(Stim s, int i, int n) {
    switch (s) {
    case Stim::silence:   return 0.0f;
    case Stim::dcPos:     return 1.0f;
    case Stim::dcNeg:     return -1.0f;
    case Stim::impulse:   return (i == 0) ? 1.0f : 0.0f;
    case Stim::levelStep: return (i >= n / 3 && i < (2 * n) / 3) ? 1.0f : 0.0f;
    case Stim::square:    return ((i / 16) % 2 == 0) ? 1.0f : -1.0f;
    case Stim::denormal:  return 1e-30f;
    }
    return 0.0f;
}

struct Scan {
    bool  finite = true;
    float maxAbs = 0.0f;
};

Scan scan(const std::vector<float>& buf) {
    Scan out;
    for (float v : buf) {
        if (!std::isfinite(v))
            out.finite = false;
        const float a = std::fabs(v);
        if (a > out.maxAbs)
            out.maxAbs = a;
    }
    return out;
}

// Drive one CONFIG across the whole stimulus battery through the FULL effect
// wrapper. A fresh effect is prepared per stimulus (clean config x stimulus
// isolation), then the stimulus is regenerated and processed across several
// blocks so the detector envelope and (in feedback configs) the feedback tap
// evolve toward steady state -- a single block would miss a slow runaway.
void effectBattery(const char* cfgName, double sampleRate, int blockSize, int numChannels,
                   const ConfigFn& configure, float bound = kBound) {
    constexpr int kBlocks = 8;
    for (Stim s : kStims) {
        ProgramDependentSaturationEffect fx;
        fx.prepare(ProcessContext{sampleRate, blockSize, numChannels});
        configure(fx);

        std::vector<std::vector<float>> chans(static_cast<std::size_t>(numChannels),
                                              std::vector<float>(static_cast<std::size_t>(blockSize)));
        std::vector<float*> ptrs(static_cast<std::size_t>(numChannels));

        for (int b = 0; b < kBlocks; ++b) {
            for (int ch = 0; ch < numChannels; ++ch) {
                for (int i = 0; i < blockSize; ++i)
                    chans[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)] =
                        stimSample(s, i, blockSize);
                ptrs[static_cast<std::size_t>(ch)] = chans[static_cast<std::size_t>(ch)].data();
            }
            AudioBlock block(ptrs.data(), numChannels, blockSize);
            fx.process(block);

            for (int ch = 0; ch < numChannels; ++ch) {
                const Scan r = scan(chans[static_cast<std::size_t>(ch)]);
                INFO("config=" << cfgName << " stim=" << stimName(s) << " block=" << b
                               << " ch=" << ch << " maxAbs=" << r.maxAbs);
                CHECK(r.finite);
                CHECK(r.maxAbs <= bound);
            }
        }
    }
}

// Drive one CONFIG across the battery through the bare per-channel CORE
// (process(x, key)), sample by sample. Used for the CORE-only surfaces the
// effect wrapper does not expose as parameters: the degenerate ref window
// (setRefWindow) and the direct feedback cold start. key == x so an
// external-key config still detects on the stimulus.
void coreBattery(const char* cfgName, double sampleRate, int n,
                 const CoreConfigFn& configure, float bound = kBound) {
    for (Stim s : kStims) {
        ProgramDependentSaturationCore core;
        core.prepare(static_cast<float>(sampleRate));
        configure(core);
        // Two passes: cold state, then a warmed continuation, so a slow
        // feedback/envelope drift shows up.
        for (int pass = 0; pass < 4; ++pass) {
            float worst = 0.0f;
            bool  finite = true;
            for (int i = 0; i < n; ++i) {
                const float x = stimSample(s, i, n);
                const float y = core.process(x, x);
                if (!std::isfinite(y))
                    finite = false;
                const float a = std::fabs(y);
                if (a > worst)
                    worst = a;
            }
            INFO("core-config=" << cfgName << " stim=" << stimName(s) << " pass=" << pass
                                << " maxAbs=" << worst);
            CHECK(finite);
            CHECK(worst <= bound);
        }
    }
}

// Set all four modulation depths to one signed value and all four curves to one
// curve index (0=linear, 1=log, 2=exp) -- the "all four targets at +-1
// simultaneously, all curves" extreme (spec.md "Extreme depth").
void setAllDepths(ProgramDependentSaturationEffect& fx, float depth, float curveIndex) {
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveDepth},
                    normFor(ProgramDependentSaturationEffect::kDriveDepth, depth));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kBiasDepth},
                    normFor(ProgramDependentSaturationEffect::kBiasDepth, depth));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kToneDepth},
                    normFor(ProgramDependentSaturationEffect::kToneDepth, depth));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kMixDepth},
                    normFor(ProgramDependentSaturationEffect::kMixDepth, depth));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveCurve},
                    normFor(ProgramDependentSaturationEffect::kDriveCurve, curveIndex));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kBiasCurve},
                    normFor(ProgramDependentSaturationEffect::kBiasCurve, curveIndex));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kToneCurve},
                    normFor(ProgramDependentSaturationEffect::kToneCurve, curveIndex));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kMixCurve},
                    normFor(ProgramDependentSaturationEffect::kMixCurve, curveIndex));
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Pathological STIMULI x the DEFAULT config all stay finite + bounded
// (SC-014 -- silence / DC / impulse / level step / full-scale square / denormal
// -- the "no input produces NaN/Inf" baseline; the -120 dB detection floor stops
// -inf propagating from the silent/denormal cases).
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: pathological stimuli through the default config stay finite and bounded") {
    effectBattery("default", 48000.0, 128, 1, [](ProgramDependentSaturationEffect&) {});
}

// ---------------------------------------------------------------------------
// TEST 2: EXTREME depths -- all four targets at +1 AND at -1, every curve --
// crossed with the whole stimulus battery (SC-014 "extreme depth"; FR-010: the
// clamps keep drive'/bias'/tone'/mix' in range so the output stays bounded).
// A moderate base drive engages the nonlinearity; feedBack detection combines
// the extreme-depth + feedback worst case (spec.md "combine worst-case").
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: extreme modulation depth (all targets, both signs, every curve) "
          "stays finite and bounded") {
    for (float sign : {1.0f, -1.0f}) {
        for (float curve : {0.0f, 1.0f, 2.0f}) { // linear / log / exp
            for (float detection : {0.0f, 1.0f}) { // feedForward / feedBack
                char name[96];
                std::snprintf(name, sizeof(name), "extreme depth=%+.0f curve=%.0f detection=%.0f",
                              static_cast<double>(sign), static_cast<double>(curve),
                              static_cast<double>(detection));
                effectBattery(name, 48000.0, 128, 1,
                              [sign, curve, detection](ProgramDependentSaturationEffect& fx) {
                                  fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDrive},
                                                  normFor(ProgramDependentSaturationEffect::kDrive, 12.0f));
                                  fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDetection},
                                                  normFor(ProgramDependentSaturationEffect::kDetection,
                                                          detection));
                                  setAllDepths(fx, sign, curve);
                              });
            }
        }
    }
}

// ---------------------------------------------------------------------------
// TEST 3: Very short attack/release (0.1 ms attack / 1 ms release -- the
// descriptor minima) at a LOW MCU sample rate (8000 Hz), with modulation
// engaged (SC-014 "low-sample-rate short-tau"; spec.md edge case: the
// EnvelopeFollower coeff guards keep coefficients finite and bounded to [0,1)).
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: very short attack/release at a low sample rate stays finite and bounded") {
    effectBattery("short-tau @ 8kHz", 8000.0, 64, 1, [](ProgramDependentSaturationEffect& fx) {
        fx.setParameter(ParamId{ProgramDependentSaturationEffect::kAttack},
                        normFor(ProgramDependentSaturationEffect::kAttack, 0.0001f));   // s (min)
        fx.setParameter(ParamId{ProgramDependentSaturationEffect::kRelease},
                        normFor(ProgramDependentSaturationEffect::kRelease, 0.001f));    // s (min)
        fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDrive},
                        normFor(ProgramDependentSaturationEffect::kDrive, 18.0f));
        setAllDepths(fx, 1.0f, 2.0f); // full depth, exp curve: hardest on the coeffs
    });
}

// ---------------------------------------------------------------------------
// TEST 4: scHpf at both extremes of its range -- near 0 (just above the bypass
// threshold) and at its max (500 Hz) -- crossed with the battery (SC-014;
// spec.md: a degenerate cutoff never destabilizes the SVF -- applyScHpf clamps).
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: scHpf near 0 and near its max stays finite and bounded") {
    for (float hz : {0.5f, 500.0f}) {
        char name[48];
        std::snprintf(name, sizeof(name), "scHpf=%.1fHz", static_cast<double>(hz));
        effectBattery(name, 48000.0, 128, 1, [hz](ProgramDependentSaturationEffect& fx) {
            fx.setParameter(ParamId{ProgramDependentSaturationEffect::kScHpf},
                            normFor(ProgramDependentSaturationEffect::kScHpf, hz));
            fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDrive},
                            normFor(ProgramDependentSaturationEffect::kDrive, 12.0f));
            setAllDepths(fx, 1.0f, 1.0f);
        });
    }
}

// ---------------------------------------------------------------------------
// TEST 5: Every named dynamic preset (none/opto/variMu/tapeComp) crossed with
// the whole stimulus battery (SC-014 "in any configuration"). opto/variMu use
// feedBack detection, so this also exercises the presets' feedback paths.
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: every dynamic preset stays finite and bounded across the stimulus battery") {
    const DynamicPreset presets[] = {DynamicPreset::none, DynamicPreset::opto,
                                     DynamicPreset::variMu, DynamicPreset::tapeComp};
    const char* names[] = {"preset none", "preset opto", "preset variMu", "preset tapeComp"};
    for (std::size_t p = 0; p < 4; ++p) {
        const DynamicPreset preset = presets[p];
        effectBattery(names[p], 48000.0, 128, 1, [preset](ProgramDependentSaturationEffect& fx) {
            fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDynamicPreset},
                            normFor(ProgramDependentSaturationEffect::kDynamicPreset,
                                    static_cast<float>(static_cast<int>(preset))));
        });
    }
}

// ---------------------------------------------------------------------------
// TEST 6: Combined worst case -- extreme depth (all targets, full) + exp curves
// + feedBack detection + short-tau + scHpf max + a preset base override, in
// LINKED STEREO at a low sample rate, across the whole battery (SC-014 "combine
// a few worst-case configs"). This is the single harshest configuration.
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: the combined worst-case config (extreme depth + feedback + short-tau + "
          "scHpf max, linked stereo) stays finite and bounded") {
    effectBattery("worst-case (linked stereo)", 8000.0, 64, 2,
                  [](ProgramDependentSaturationEffect& fx) {
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDynamicPreset},
                                      normFor(ProgramDependentSaturationEffect::kDynamicPreset,
                                              static_cast<float>(static_cast<int>(DynamicPreset::variMu))));
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDrive},
                                      normFor(ProgramDependentSaturationEffect::kDrive, 36.0f));
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDetection},
                                      normFor(ProgramDependentSaturationEffect::kDetection, 1.0f));
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kAttack},
                                      normFor(ProgramDependentSaturationEffect::kAttack, 0.1f));
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kRelease},
                                      normFor(ProgramDependentSaturationEffect::kRelease, 1.0f));
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kScHpf},
                                      normFor(ProgramDependentSaturationEffect::kScHpf, 500.0f));
                      fx.setParameter(ParamId{ProgramDependentSaturationEffect::kStereoLink},
                                      normFor(ProgramDependentSaturationEffect::kStereoLink, 1.0f)); // linked
                      setAllDepths(fx, 1.0f, 2.0f);
                  });
}

// ---------------------------------------------------------------------------
// TEST 7: Feedback COLD START (spec.md AC-2 / SC-014) -- a freshly prepared &
// reset core in feedback detection: prevOutput_ == 0 is a defined floor, so the
// FIRST sample is finite and the run stays bounded, at extreme depth. Driven at
// the bare CORE (process(x, key)) so the very first sample is observable.
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: feedback cold start -- the first sample is finite and the run stays bounded") {
    ProgramDependentSaturationCore core;
    core.prepare(48000.0f);
    core.setStaticDrive(48.0f); // max base drive
    core.setDepth(ModTarget::drive, 1.0f);
    core.setDepth(ModTarget::bias, 1.0f);
    core.setDepth(ModTarget::mix, 1.0f);
    core.setCurve(ModTarget::drive, ModCurve::exponential);
    core.setDetection(Detection::feedBack);
    core.setBallistics(Ballistics::decoupled);
    core.setAttack(0.0001f);
    core.setRelease(0.001f);
    core.reset(); // prevOutput_ -> 0: the defined feedback cold-start floor

    // First sample: full-scale, straight into the feedback topology from a cold
    // prevOutput_ == 0.
    const float y0 = core.process(1.0f, 1.0f);
    INFO("cold-start first sample y0=" << y0);
    CHECK(std::isfinite(y0));
    CHECK(std::fabs(y0) <= kBound);

    // Continue -- the feedback loop must stay finite + bounded (no runaway), the
    // externally visible proof the drive'/bias'/mix' clamps stay engaged.
    float worst = std::fabs(y0);
    bool  finite = std::isfinite(y0);
    for (int i = 1; i < 4096; ++i) {
        const float x = ((i / 16) % 2 == 0) ? 1.0f : -1.0f; // full-scale square
        const float y = core.process(x, x);
        if (!std::isfinite(y))
            finite = false;
        worst = std::fmax(worst, std::fabs(y));
    }
    INFO("cold-start run worst=" << worst);
    CHECK(finite);
    CHECK(worst <= kBound);
}

// ---------------------------------------------------------------------------
// TEST 8: Degenerate / narrow reference window (spec.md edge case) -- the
// effect wrapper does not expose the ref window, so this drives the CORE
// directly. A zero-width window (lo == hi) and an INVERTED window (hi < lo) must
// both be tolerated: detectNorm() guards the divide to a bounded norm of 0, so
// no divide-by-zero / NaN reaches the output, across the whole battery.
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: degenerate reference windows (zero-width and inverted) stay finite/bounded") {
    coreBattery("ref-window zero-width", 48000.0, 512, [](ProgramDependentSaturationCore& core) {
        core.setStaticDrive(18.0f);
        core.setDepth(ModTarget::drive, 1.0f);
        core.setDepth(ModTarget::bias, 1.0f);
        core.setRefWindow(-30.0f, -30.0f); // zero width -> denom == 0, guarded to norm 0
    });
    coreBattery("ref-window inverted", 48000.0, 512, [](ProgramDependentSaturationCore& core) {
        core.setStaticDrive(18.0f);
        core.setDepth(ModTarget::drive, 1.0f);
        core.setDepth(ModTarget::mix, 1.0f);
        core.setDetection(Detection::feedBack);
        core.setRefWindow(0.0f, -60.0f); // hi < lo -> denom < 0, guarded to norm 0
    });
}

// ---------------------------------------------------------------------------
// TEST 9: reset() mid-stream leaves the effect in a defined state -- after
// processing pathological blocks, a reset() followed by more processing yields
// finite output (SC-014 / FR-018). Uses the harshest wrapper config so reset()
// is exercised over a fully-modulated, feedback-engaged state.
// ---------------------------------------------------------------------------

TEST_CASE("T038/SC-014: reset() mid-stream leaves a defined state (finite output after reset)") {
    constexpr int kBlockSize = 128;
    ProgramDependentSaturationEffect fx;
    fx.prepare(ProcessContext{48000.0, kBlockSize, 1});
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDrive},
                    normFor(ProgramDependentSaturationEffect::kDrive, 36.0f));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDetection},
                    normFor(ProgramDependentSaturationEffect::kDetection, 1.0f)); // feedBack
    setAllDepths(fx, 1.0f, 2.0f);

    std::vector<float> buf(static_cast<std::size_t>(kBlockSize));
    auto processSquare = [&]() {
        for (int i = 0; i < kBlockSize; ++i)
            buf[static_cast<std::size_t>(i)] = ((i / 16) % 2 == 0) ? 1.0f : -1.0f;
        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    };

    // Drive several blocks so the feedback tap / detector are in a nontrivial
    // running state, then reset() MID-STREAM.
    for (int b = 0; b < 6; ++b)
        processSquare();
    fx.reset();

    // After reset(), continued processing must remain finite + bounded -- the
    // reset left a DEFINED (not NaN-poisoned) state.
    for (int b = 0; b < 6; ++b) {
        processSquare();
        const Scan r = scan(buf);
        INFO("post-reset block=" << b << " maxAbs=" << r.maxAbs);
        CHECK(r.finite);
        CHECK(r.maxAbs <= kBound);
    }
}

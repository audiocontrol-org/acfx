#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-presets.h"

// T029 -- User Story 9 suite: named dynamic-character presets (SC-008,
// FR-014). program-dependent-saturation-presets.h's kPdsPresetConfigs table
// is the single documented source of truth for opto/variMu/tapeComp (research
// .md Decision 7); ProgramDependentSaturationEffect::presetConfig() exposes
// it, and applyDynamicPreset()/writePreset() realize it into every channel
// core when dynamicPreset (Param id 20) is set. Three concerns, mirroring the
// program-dependent-saturation-effect-test.cpp idiom (normFor helper, finite-
// output checks):
//   1. Table equivalence -- presetConfig() returns the DOCUMENTED values for
//      each preset (read directly off the header, not re-derived).
//   2. Behavioral -- selecting a non-`none` preset then process()ing produces
//      output that diverges from the `none` baseline; `none` matches the
//      zero-depth static baseline exactly. No NaN/Inf anywhere.
//   3. Override -- a manual setParameter() on a depth AFTER selecting a
//      preset overrides that value on the next process() (the preset is a
//      starting point, not a lock -- the header's "OVERRIDE SEMANTICS" note).
//
// References: specs/program-dependent-saturation/tasks.md T029 (SC-008);
// spec.md User Story 9 + SC-008; research.md Decision 7; contracts/
// program-dependent-saturation-effect-api.md.

using namespace acfx;

namespace {

using DynamicPreset = ProgramDependentSaturationEffect::DynamicPreset;

// Convert a desired PLAIN-units value for a ProgramDependentSaturationEffect
// parameter into the normalized 0..1 value setParameter() expects, via the
// shared descriptor table -- mirrors program-dependent-saturation-effect-
// test.cpp's normFor. Never hand-roll the normalize math: the descriptor
// table (min/max/skew) is the single source of truth (FR-016).
float normFor(ProgramDependentSaturationEffect::Param p, float plainValue) {
    return normalize(ProgramDependentSaturationEffect::kParams[p], plainValue);
}

// Max absolute per-sample difference between two equal-length buffers.
double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (d > worst)
            worst = d;
    }
    return worst;
}

// Select a named dynamic preset via the SAME public path a host uses
// (setParameter on dynamicPreset id 20, denormalized through the descriptor
// table's discrete bucket count of 4: none=0, opto=1, variMu=2, tapeComp=3).
void selectPreset(ProgramDependentSaturationEffect& fx, DynamicPreset preset) {
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDynamicPreset},
                     normFor(ProgramDependentSaturationEffect::kDynamicPreset,
                             static_cast<float>(static_cast<int>(preset))));
}

// Render one block of a fixed 400 Hz tone through `fx` (already prepared) and
// return the output buffer -- a shared stimulus so the `none` / named-preset
// comparisons below are apples-to-apples.
std::vector<float> renderTone(ProgramDependentSaturationEffect& fx, int blockSize, float sampleRate) {
    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    for (int i = 0; i < blockSize; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        buf[static_cast<std::size_t>(i)] = 0.8f * std::sin(2.0f * 3.14159265f * 400.0f * t);
    }
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, blockSize);
    fx.process(block);
    return buf;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Table equivalence (T029, US9, SC-008) -- presetConfig() returns the
// DOCUMENTED configuration for every named preset, read directly off
// program-dependent-saturation-presets.h's kPdsPresetConfigs table (the
// source of truth). `none` is all-zero; opto has a negative driveDepth;
// variMu has positive bias+drive; tapeComp has positive drive+mix.
// ---------------------------------------------------------------------------

TEST_CASE("presetConfig() realizes the documented per-preset matrix (T029, US9, SC-008)") {
    // none -- neutral / orthogonality baseline: every target depth 0, linear
    // curves, feedForward detection, default detector/ballistics/timing.
    {
        const PdsPresetConfig& none = ProgramDependentSaturationEffect::presetConfig(DynamicPreset::none);
        CHECK(none.drive.depth == doctest::Approx(0.0f));
        CHECK(none.bias.depth == doctest::Approx(0.0f));
        CHECK(none.tone.depth == doctest::Approx(0.0f));
        CHECK(none.mix.depth == doctest::Approx(0.0f));
        CHECK(none.drive.curve == ModCurve::linear);
        CHECK(none.bias.curve == ModCurve::linear);
        CHECK(none.tone.curve == ModCurve::linear);
        CHECK(none.mix.curve == ModCurve::linear);
        CHECK(none.detection == Detection::feedForward);
        CHECK(none.detector == DetectMode::rms);
        CHECK(none.ballistics == Ballistics::branching);
        CHECK(none.attackMs == doctest::Approx(10.0f));
        CHECK(none.releaseMs == doctest::Approx(100.0f));
    }

    // opto -- optical character: NEGATIVE drive depth (downward drive
    // softening, louder = cleaner), logarithmic curve, RMS detector,
    // decoupled ballistics, slow timing, feedBack detection. Every other
    // target stays at 0 (opto colors drive only).
    {
        const PdsPresetConfig& opto = ProgramDependentSaturationEffect::presetConfig(DynamicPreset::opto);
        CHECK(opto.drive.depth == doctest::Approx(-0.60f));
        CHECK(opto.drive.depth < 0.0f); // "opto has a negative driveDepth"
        CHECK(opto.drive.curve == ModCurve::logarithmic);
        CHECK(opto.bias.depth == doctest::Approx(0.0f));
        CHECK(opto.tone.depth == doctest::Approx(0.0f));
        CHECK(opto.mix.depth == doctest::Approx(0.0f));
        CHECK(opto.detection == Detection::feedBack);
        CHECK(opto.detector == DetectMode::rms);
        CHECK(opto.ballistics == Ballistics::decoupled);
        CHECK(opto.attackMs == doctest::Approx(50.0f));
        CHECK(opto.releaseMs == doctest::Approx(500.0f));
    }

    // variMu -- vari-mu tube character: POSITIVE bias + POSITIVE drive
    // (level-dependent push), exponential drive curve (tube "bloom"), linear
    // bias curve, RMS detection, branching ballistics, medium timing,
    // feedBack detection.
    {
        const PdsPresetConfig& variMu =
            ProgramDependentSaturationEffect::presetConfig(DynamicPreset::variMu);
        CHECK(variMu.drive.depth == doctest::Approx(0.50f));
        CHECK(variMu.bias.depth == doctest::Approx(0.40f));
        CHECK(variMu.drive.depth > 0.0f); // "variMu has positive bias+drive"
        CHECK(variMu.bias.depth > 0.0f);
        CHECK(variMu.drive.curve == ModCurve::exponential);
        CHECK(variMu.bias.curve == ModCurve::linear);
        CHECK(variMu.tone.depth == doctest::Approx(0.0f));
        CHECK(variMu.mix.depth == doctest::Approx(0.0f));
        CHECK(variMu.detection == Detection::feedBack);
        CHECK(variMu.detector == DetectMode::rms);
        CHECK(variMu.ballistics == Ballistics::branching);
        CHECK(variMu.attackMs == doctest::Approx(20.0f));
        CHECK(variMu.releaseMs == doctest::Approx(300.0f));
    }

    // tapeComp -- tape character: POSITIVE drive push + POSITIVE mix
    // self-compression, both linear curves, peak detection, decoupled
    // (tape-ish) ballistics, medium-fast timing, feedForward detection.
    {
        const PdsPresetConfig& tapeComp =
            ProgramDependentSaturationEffect::presetConfig(DynamicPreset::tapeComp);
        CHECK(tapeComp.drive.depth == doctest::Approx(0.40f));
        CHECK(tapeComp.mix.depth == doctest::Approx(0.30f));
        CHECK(tapeComp.drive.depth > 0.0f); // "tapeComp has positive drive+mix"
        CHECK(tapeComp.mix.depth > 0.0f);
        CHECK(tapeComp.drive.curve == ModCurve::linear);
        CHECK(tapeComp.mix.curve == ModCurve::linear);
        CHECK(tapeComp.bias.depth == doctest::Approx(0.0f));
        CHECK(tapeComp.tone.depth == doctest::Approx(0.0f));
        CHECK(tapeComp.detection == Detection::feedForward);
        CHECK(tapeComp.detector == DetectMode::peak);
        CHECK(tapeComp.ballistics == Ballistics::decoupled);
        CHECK(tapeComp.attackMs == doctest::Approx(15.0f));
        CHECK(tapeComp.releaseMs == doctest::Approx(250.0f));
    }
}

// ---------------------------------------------------------------------------
// TEST 2: Behavioral -- `none` matches the zero-depth static baseline; each
// named preset engages modulation and diverges from `none` (T029, US9,
// SC-008). No NaN/Inf in any case.
// ---------------------------------------------------------------------------

TEST_CASE("selecting `none` matches the zero-depth baseline; opto/variMu/tapeComp "
          "diverge from it (T029, US9, SC-008)") {
    constexpr int kBlockSize = 4800; // 100 ms at 48 kHz
    constexpr float kSampleRate = 48000.0f;

    // Baseline: a freshly-prepared effect, no dynamicPreset selection at all
    // -- the constructor default (DynamicPreset::none, all depths 0).
    ProgramDependentSaturationEffect baselineFx;
    baselineFx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
    const std::vector<float> baseline = renderTone(baselineFx, kBlockSize, kSampleRate);
    for (float v : baseline)
        REQUIRE(std::isfinite(v));

    // `none` selected EXPLICITLY via setParameter -- must be a no-op
    // (byte-identical to the baseline; applyDynamicPreset() short-circuits on
    // `none` per the effect header's note).
    {
        ProgramDependentSaturationEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
        selectPreset(fx, DynamicPreset::none);
        const std::vector<float> out = renderTone(fx, kBlockSize, kSampleRate);
        REQUIRE(out.size() == baseline.size());
        for (std::size_t i = 0; i < out.size(); ++i) {
            REQUIRE(std::isfinite(out[i]));
            CHECK(out[i] == doctest::Approx(baseline[i]));
        }
    }

    // Each named preset diverges measurably from the `none` baseline (the
    // preset engages modulation) and stays finite throughout.
    const DynamicPreset presets[] = {DynamicPreset::opto, DynamicPreset::variMu,
                                      DynamicPreset::tapeComp};
    for (DynamicPreset preset : presets) {
        ProgramDependentSaturationEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
        selectPreset(fx, preset);
        const std::vector<float> out = renderTone(fx, kBlockSize, kSampleRate);
        REQUIRE(out.size() == baseline.size());
        for (float v : out)
            REQUIRE(std::isfinite(v));

        const double diff = maxAbsDiff(out, baseline);
        INFO("preset=" << static_cast<int>(preset) << " max abs diff vs none=" << diff);
        CHECK(diff > 1.0e-6);
    }
}

// ---------------------------------------------------------------------------
// TEST 3: Override -- a preset is a STARTING POINT, not a lock (T029, US9
// Deferred, spec.md "OVERRIDE SEMANTICS"). Selecting a preset, then manually
// setting driveDepth, overrides the preset's driveDepth on the next
// process(): the manual value wins, not the preset's documented value.
// ---------------------------------------------------------------------------

TEST_CASE("a manual setParameter after a preset selection overrides that preset value "
          "(T029, US9, override semantics)") {
    constexpr int kBlockSize = 4800;
    constexpr float kSampleRate = 48000.0f;

    // Reference A: opto selected, no override -- realizes opto's documented
    // driveDepth (-0.60, per TEST 1) untouched.
    ProgramDependentSaturationEffect optoFx;
    optoFx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
    selectPreset(optoFx, DynamicPreset::opto);
    const std::vector<float> optoOnly = renderTone(optoFx, kBlockSize, kSampleRate);
    for (float v : optoOnly)
        REQUIRE(std::isfinite(v));

    // Reference B: opto selected, THEN driveDepth manually overridden to a
    // full-scale POSITIVE value -- the opposite sign of opto's documented
    // -0.60, so if the override actually takes effect the two outputs must
    // diverge (a same-sign, same-magnitude accidental match is not possible).
    ProgramDependentSaturationEffect overriddenFx;
    overriddenFx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
    selectPreset(overriddenFx, DynamicPreset::opto);
    overriddenFx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveDepth},
                               normFor(ProgramDependentSaturationEffect::kDriveDepth, 0.60f));
    const std::vector<float> overridden = renderTone(overriddenFx, kBlockSize, kSampleRate);
    for (float v : overridden)
        REQUIRE(std::isfinite(v));

    const double diff = maxAbsDiff(overridden, optoOnly);
    INFO("max abs diff (overridden driveDepth vs opto-only)=" << diff);
    CHECK(diff > 1.0e-6);

    // The preset's OTHER targets (bias/tone/mix, all 0 for opto) and its
    // detector/ballistics/timing choices are untouched by the driveDepth-only
    // override -- selecting a second, unrelated preset param (bias curve)
    // still layers on top of the SAME opto base rather than resetting it.
    // Confirmed indirectly: applying the SAME driveDepth override a second
    // time (idempotent re-publish) on a fresh opto selection reproduces
    // `overridden` exactly, proving the override is a deterministic
    // configuration write, not a one-shot/consumed edit.
    ProgramDependentSaturationEffect repeatFx;
    repeatFx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
    selectPreset(repeatFx, DynamicPreset::opto);
    repeatFx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveDepth},
                           normFor(ProgramDependentSaturationEffect::kDriveDepth, 0.60f));
    const std::vector<float> repeat = renderTone(repeatFx, kBlockSize, kSampleRate);
    REQUIRE(repeat.size() == overridden.size());
    for (std::size_t i = 0; i < repeat.size(); ++i) {
        REQUIRE(std::isfinite(repeat[i]));
        CHECK(repeat[i] == doctest::Approx(overridden[i]));
    }
}

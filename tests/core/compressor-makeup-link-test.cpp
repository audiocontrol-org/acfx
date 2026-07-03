#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/compressor/compressor-core.h"
#include "effects/compressor/compressor-effect.h"
#include "primitives/dynamics/gain-computer.h"

// T035 (US11 — makeup / auto-makeup / mix / output-trim, SC-009) and
// T037 (US12 — stereo/multichannel linking, SC-010) authored against the
// SHIPPED implementation in core/effects/compressor/compressor-core.h and
// core/effects/compressor/compressor-effect.h. Written to the code as
// implemented (research Decision 5 for makeup; FR-017 for linking), NOT to a
// spec-idealized expectation — see the auto-makeup case below for the one place
// the implemented relation diverges from spec SC-009's "below-threshold ≈ unity"
// wording, documented in-line so the divergence is an explicit, reviewed choice.
//
// Every CompressorCore case drives the per-channel kernel directly with a
// constant (DC) key so the level detector settles to a fixed dB and the settled
// gain is deterministic; the makeup/mix/output assertions are then EXACT ratios
// (they cancel the settled gain-reduction entirely), so the tolerances are
// tight. The CompressorEffect cases exercise the wrapper's stereo-link paths.

using acfx::AudioBlock;
using acfx::CompressorCore;
using acfx::CompressorEffect;
using acfx::GainComputer;
using acfx::GainMode;
using acfx::ParamId;
using acfx::ProcessContext;
using acfx::StereoLink;

namespace {

// -------------------------------------------------------------------------
// Named tolerances (SVF-reference idiom: named, documented bounds, no magic
// numbers inline). All are RELATIVE unless the name says "abs".
// -------------------------------------------------------------------------

// Makeup / output-trim ratios and the auto-makeup relation cancel the settled
// gain-reduction exactly and differ only by one dB->linear conversion, so they
// match to float rounding. A few parts in 1e4 is comfortably tight.
constexpr double kMakeupRatioRelTol = 1e-4;
constexpr double kAutoMakeupRelTol  = 1e-4;
constexpr double kOutputTrimRelTol  = 1e-4;

// Unity passthrough (mix=0; expand/gate no-inflation): output should equal the
// dry input. mix=0's blend law (0*comp + 1*x) is bit-exact in IEEE, so this can
// be very tight; a small absolute floor absorbs any settling residue.
constexpr double kUnityAbsTol = 1e-5;

// Parallel-mix blend law verified against the effect's OWN wet value.
constexpr double kMixLawRelTol = 1e-5;

// Stereo linking: the SAME shared linear gain is applied to every channel, so
// the per-channel applied gain (out/in) is bit-identical up to the division's
// single rounding. This is the strict equal-gain criterion.
constexpr double kLinkedGainRelTol = 1e-5;

// perChannel: a well-below-threshold channel receives dbToLin(0) == 1 exactly
// (grDb=0, makeup 0), so its applied gain is unity to float rounding.
constexpr double kPerChannelUnityAbsTol = 1e-6;

// Degenerate one-channel linked vs perChannel: the two paths are algebraically
// identical for a single channel, so their outputs match to float rounding.
constexpr double kDegenerateAbsTol = 1e-6;

// A loud, clearly-compressed channel must show meaningful attenuation; this is
// the "obviously reducing" floor, not a precise gain value.
constexpr double kClearAttenCeil = 0.9; // applied gain must be < this

// -------------------------------------------------------------------------
// Helpers.
// -------------------------------------------------------------------------

double dbToLin(double db) noexcept { return std::pow(10.0, db * 0.05); }

// Drive a core with a constant DC key/input for n samples; return the settled
// output sample. Keyless path (key == x), matching CompressorEffect's internal
// sidechain. Long settle so the dB-domain detector converges.
double settleDC(CompressorCore& core, float x, int n) noexcept {
    float y = 0.0f;
    for (int i = 0; i < n; ++i)
        y = core.process(x, x);
    return static_cast<double>(y);
}

// A CompressorCore prepared at 48 kHz with no lookahead, left at its shipped
// defaults (compress, threshold -18 dB, ratio 4, knee 6, mix 1, output 0 dB,
// makeup 0, autoMakeup off, feedforward). Callers tweak only what they test.
constexpr float kSampleRate = 48000.0f;
constexpr int   kSettle     = 48000; // 1 s — many attack time-constants.

void prepareDefault(CompressorCore& core) noexcept { core.prepare(kSampleRate, 0); }

// Normalized value that denormalizes to a discrete parameter's plain bucket.
float normFor(CompressorEffect::Param p, float plainBucket) noexcept {
    return acfx::normalize(CompressorEffect::kParams[p], plainBucket);
}

} // namespace

// ===========================================================================
// T035 — makeup / auto-makeup / mix / output-trim (US11, SC-009).
// ===========================================================================

// SC-009 / US11 Scenario 1: manual makeup of M dB raises the settled output by
// exactly M dB relative to makeup = 0. Two cores fed the identical DC key have
// an identical gain-reduction trajectory (feedforward — makeup never re-enters
// detection), so the settled output ratio is dbToLin(M) with the reduction
// cancelled out.
TEST_CASE("CompressorCore manual makeup raises settled output by M dB (T035, SC-009)") {
    constexpr float kMakeupDb = 6.0f;
    constexpr float kDC = 0.5f; // -6 dBFS: above the -18 dB threshold -> compressed.

    CompressorCore base;
    prepareDefault(base);
    const double outBase = settleDC(base, kDC, kSettle);

    CompressorCore made;
    prepareDefault(made);
    made.setMakeup(kMakeupDb);
    const double outMade = settleDC(made, kDC, kSettle);

    REQUIRE(outBase > 0.0);
    const double ratio = outMade / outBase;
    CHECK(ratio == doctest::Approx(dbToLin(kMakeupDb)).epsilon(kMakeupRatioRelTol));
}

// Auto-makeup — IMPLEMENTED RELATION (research Decision 5). The kernel folds the
// cached effective makeup into the linear gain UNCONDITIONALLY:
//     detectGainLin() returns dbToLin(grDb + makeupEffectiveDb)
// and for compress, autoMakeup on, makeupEffectiveDb = -computeGainDb(0 dBFS).
// A below-threshold signal gets grDb = 0 (unity region), so the settled output
// is  input * dbToLin(-computeGainDb(0 dBFS))  — the auto-makeup gain IS applied
// to a signal that received no reduction. This is NOT ≈ unity.
//
// NB: spec SC-009 / US11 Scenario 2 word this as "below-threshold ≈ unity". The
// shipped code does not do that — it multiplies the below-threshold signal by
// the full auto-makeup gain. Per the task's explicit instruction this test
// asserts the IMPLEMENTED relation, and the divergence from the spec wording is
// flagged here for review (it is the one place code and spec disagree).
TEST_CASE("CompressorCore auto-makeup folds makeup into below-threshold gain (T035, SC-009)") {
    constexpr float kDC = 0.01f; // -40 dBFS: below threshold-knee (< -21) -> grDb == 0.

    // Reference: the auto-makeup magnitude the kernel caches for the default
    // curve. GainComputer's own defaults match CompressorCore's (compress,
    // -18 dB, ratio 4, knee 6), so computeGainDb(0) reproduces the cached value.
    GainComputer ref;
    const double autoMakeupDb = -static_cast<double>(ref.computeGainDb(0.0f));
    REQUIRE(autoMakeupDb > 0.0); // sanity: default curve reduces at 0 dBFS.

    CompressorCore core;
    prepareDefault(core);
    core.setAutoMakeup(true);
    const double out = settleDC(core, kDC, kSettle);

    // The implemented relation: out == input * dbToLin(-computeGainDb(0)).
    const double ratio = out / static_cast<double>(kDC);
    CHECK(ratio == doctest::Approx(dbToLin(autoMakeupDb)).epsilon(kAutoMakeupRelTol));
    // And it is emphatically NOT unity — the auto-makeup DOES inflate the
    // no-reduction signal (documents the divergence as an assertion).
    CHECK(ratio > 1.5);
}

// Auto-makeup is 0 for the downward (expand / gate) modes (FR-016, research
// Decision 5): updateMakeup() forces makeupEffectiveDb = 0 there, so an
// above-threshold (unity-region) signal is passed through un-inflated even with
// autoMakeup on. Observable: settled output == dry input.
TEST_CASE("CompressorCore auto-makeup is 0 in expand/gate modes (T035)") {
    constexpr float kDC = 0.5f; // -6 dBFS: ABOVE threshold -> expand/gate unity region.

    for (const GainMode mode : {GainMode::expand, GainMode::gate}) {
        CompressorCore core;
        prepareDefault(core);
        core.setMode(mode);
        core.setAutoMakeup(true);
        const double out = settleDC(core, kDC, kSettle);
        // No inflation: the above-threshold signal is unchanged (grDb = 0,
        // makeupEffectiveDb forced to 0 -> gain = 1).
        CHECK(out == doctest::Approx(static_cast<double>(kDC)).epsilon(kUnityAbsTol));
    }
}

// SC-009 / US11 Scenario 3: mix = 0 is dry passthrough; mix = 1 is fully
// compressed (wet); a middle mix is the parallel blend  mix*wet + (1-mix)*dry.
// Verified against the effect's OWN settled wet value so the assertion is
// independent of the detector's absolute convergence.
TEST_CASE("CompressorCore mix blends dry and wet (T035, SC-009)") {
    constexpr float kDC = 0.5f;

    // mix = 1 (default): fully compressed wet value.
    CompressorCore wetCore;
    prepareDefault(wetCore);
    const double wet = settleDC(wetCore, kDC, kSettle);

    // mix = 0: dry passthrough — bit-exact (blend law 0*comp + 1*x).
    CompressorCore dryCore;
    prepareDefault(dryCore);
    dryCore.setMix(0.0f);
    const double dry = settleDC(dryCore, kDC, kSettle);
    CHECK(dry == doctest::Approx(static_cast<double>(kDC)).epsilon(kUnityAbsTol));

    // Sanity: wet actually differs from dry (the signal IS being compressed).
    REQUIRE(std::fabs(wet - static_cast<double>(kDC)) > 1e-3);

    // mix = 0.5: the exact parallel blend of the effect's own wet and dry.
    CompressorCore midCore;
    prepareDefault(midCore);
    midCore.setMix(0.5f);
    const double mid = settleDC(midCore, kDC, kSettle);
    const double expectedMid = 0.5 * wet + 0.5 * static_cast<double>(kDC);
    CHECK(mid == doctest::Approx(expectedMid).epsilon(kMixLawRelTol));
}

// Output trim scales the final sample by dbToLin(output dB). Two cores identical
// except for the output trim yield a settled ratio of exactly dbToLin(G).
TEST_CASE("CompressorCore output trim scales by dbToLin(output) (T035)") {
    constexpr float kOutputDb = 6.0f;
    constexpr float kDC = 0.5f;

    CompressorCore base;
    prepareDefault(base);
    const double out0 = settleDC(base, kDC, kSettle);

    CompressorCore trimmed;
    prepareDefault(trimmed);
    trimmed.setOutput(kOutputDb);
    const double outG = settleDC(trimmed, kDC, kSettle);

    REQUIRE(out0 > 0.0);
    const double ratio = outG / out0;
    CHECK(ratio == doctest::Approx(dbToLin(kOutputDb)).epsilon(kOutputTrimRelTol));
}

// ===========================================================================
// T037 — stereo / multichannel linking (US12, SC-010).
// ===========================================================================

// SC-010 / US12 Scenario 1: LINKED detection drives ONE common gain (from the
// cross-channel max key) applied to every channel, so a transient in L alone
// ducks BOTH channels by the SAME gain — the stereo image stays put. With the
// shipped defaults (mix=1, output 0 dB, lookahead 0) applyGain reduces to
// out = x * gLin, so the per-channel applied gain is out/in, and the two must be
// equal (they are literally the same shared gLin).
TEST_CASE("CompressorEffect linked stereo applies the SAME gain to both channels (T037, SC-010)") {
    constexpr int kN = 8000; // >> attack time-constant: gLin is well engaged.
    constexpr float kLoud  = 0.8f;  // -1.9 dBFS: well above threshold in L.
    constexpr float kQuiet = 0.002f; // -54 dBFS: far below threshold in R.

    CompressorEffect fx; // default stereoLink = linked.
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kN, 2});

    std::vector<float> left(static_cast<std::size_t>(kN), kLoud);
    std::vector<float> right(static_cast<std::size_t>(kN), kQuiet);
    std::array<float*, 2> chans{left.data(), right.data()};
    AudioBlock io(chans.data(), 2, kN);
    fx.process(io);

    // out[ch] = in[ch] * gLin (same gLin both channels). Recover applied gains.
    const double gainL = static_cast<double>(left[static_cast<std::size_t>(kN - 1)]) / kLoud;
    const double gainR = static_cast<double>(right[static_cast<std::size_t>(kN - 1)]) / kQuiet;

    // Strict equal-gain criterion: the SAME gain is applied to L and R.
    CHECK(gainR == doctest::Approx(gainL).epsilon(kLinkedGainRelTol));
    // The loud transient really is reducing gain (both channels ducked).
    CHECK(gainL < kClearAttenCeil);
}

// SC-010 / US12: perChannel detection is independent — only the loud channel is
// attenuated; the quiet, below-threshold channel is essentially unaffected
// (grDb = 0, makeup 0 -> unity gain).
TEST_CASE("CompressorEffect perChannel stereo attenuates only the affected channel (T037, SC-010)") {
    constexpr int kN = 8000;
    constexpr float kLoud  = 0.8f;
    constexpr float kQuiet = 0.002f;

    CompressorEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kN, 2});
    fx.setParameter(ParamId{static_cast<std::uint8_t>(CompressorEffect::kStereoLink)},
                    normFor(CompressorEffect::kStereoLink, 0.0f)); // perChannel bucket.

    std::vector<float> left(static_cast<std::size_t>(kN), kLoud);
    std::vector<float> right(static_cast<std::size_t>(kN), kQuiet);
    std::array<float*, 2> chans{left.data(), right.data()};
    AudioBlock io(chans.data(), 2, kN);
    fx.process(io);

    const double gainL = static_cast<double>(left[static_cast<std::size_t>(kN - 1)]) / kLoud;
    const double gainR = static_cast<double>(right[static_cast<std::size_t>(kN - 1)]) / kQuiet;

    CHECK(gainL < kClearAttenCeil);                                  // L compressed.
    CHECK(gainR == doctest::Approx(1.0).epsilon(kPerChannelUnityAbsTol)); // R untouched.
}

// Edge case (spec.md "Mono input to a linked/stereo config"): linking over one
// channel degenerates to per-channel (the max over one channel is itself). No
// crash; the linked and perChannel single-channel outputs are identical.
TEST_CASE("CompressorEffect linked over one channel degenerates to per-channel (T037)") {
    constexpr int kN = 4000;
    constexpr float kLoud = 0.6f; // above threshold -> compressed.

    // Linked, one channel (default stereoLink = linked).
    CompressorEffect linked;
    linked.prepare(ProcessContext{static_cast<double>(kSampleRate), kN, 1});
    std::vector<float> a(static_cast<std::size_t>(kN), kLoud);
    std::array<float*, 1> chA{a.data()};
    AudioBlock ioA(chA.data(), 1, kN);
    linked.process(ioA);

    // perChannel, one channel — same input.
    CompressorEffect perCh;
    perCh.prepare(ProcessContext{static_cast<double>(kSampleRate), kN, 1});
    perCh.setParameter(ParamId{static_cast<std::uint8_t>(CompressorEffect::kStereoLink)},
                       normFor(CompressorEffect::kStereoLink, 0.0f));
    std::vector<float> b(static_cast<std::size_t>(kN), kLoud);
    std::array<float*, 1> chB{b.data()};
    AudioBlock ioB(chB.data(), 1, kN);
    perCh.process(ioB);

    // Sane output: finite and actually attenuated (loud -> compressed).
    const double outLinked = static_cast<double>(a[static_cast<std::size_t>(kN - 1)]);
    CHECK(std::isfinite(outLinked));
    CHECK(std::fabs(outLinked) < kLoud); // reduced below the dry input.

    // Degenerate equivalence: linked == perChannel, sample for sample.
    for (int i = 0; i < kN; ++i) {
        CHECK(static_cast<double>(a[static_cast<std::size_t>(i)]) ==
              doctest::Approx(static_cast<double>(b[static_cast<std::size_t>(i)]))
                  .epsilon(kDegenerateAbsTol));
    }
}

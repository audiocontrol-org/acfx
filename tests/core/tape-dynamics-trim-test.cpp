#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/tape-dynamics/tape-dynamics-core.h"
#include "effects/tape-dynamics/tape-dynamics-effect.h"

// T027 -- User Story 6 / contract E7 acceptance: the FULL trim acceptance
// suite for the optional explicit trim stage T026 wired up in
// TapeDynamicsCore/-Effect (per-channel EnvelopeFollower + a shared
// GainComputer; trim.enabled/attack/release/amount are the only user-facing
// trim macros -- threshold/ratio/knee are a fixed internal curve, see
// tape-dynamics-core.h's applyToTrim()). tape-dynamics-core-test.cpp already
// carries ONE focused disabled/enabled check from T026; this file is the
// complete US6 suite:
//   CASE 1 (E7, the load-bearing invariant) -- trim DISABLED is BIT-EXACT
//     the magnetics-only core, for a spread of attack/release/amount values,
//     both through TapeDynamicsCore<Factor> directly and through
//     TapeDynamicsEffect (black-box, setParameter()).
//   CASE 2 -- trim ENABLED applies envelope-driven gain reduction on a loud
//     sustained tone that grows monotonically with trim.amount; amount=0 is
//     provably (not just approximately) bit-exact trim-disabled.
//   CASE 3 -- attack/release ballistics: a fast trim.attack/release reaches
//     its settled gain reduction / recovers to unity sooner (fewer samples)
//     than a slow one, after a level step.
//
// Measurement technique for CASE 3 (documented once, reused by both
// TEST_CASEs below): a trim.enabled=false run and a trim.enabled=true run
// driven with the IDENTICAL input have a bit-identical PRE-TRIM wet signal
// (CASE 1 proves exactly this), so the ratio of a windowed-RMS envelope of
// the enabled output to the disabled output, at each sample, is EXACTLY the
// trim stage's instantaneous linear gain multiplier -- no need to reach into
// TapeDynamicsCore's private EnvelopeFollower/GainComputer state. The window
// (kWindowSamples, one sin^2 half-cycle) removes the raw per-sample peak
// detector's intra-cycle ripple (EnvelopeFollower's peak mode detects |x|
// directly every sample, not a held peak -- see
// core/primitives/dynamics/envelope-follower.h) while adding negligible lag
// relative to the ballistics under test. Because the follower runs in the
// DECIBEL domain, most of its multi-time-constant climb from the -120 dB
// cold-start floor toward the steady-state level happens BEFORE the
// linear-domain ratio moves measurably -- the ratio only responds once the
// dB envelope nears the fixed -18 dB threshold's knee. This was measured
// directly with a throwaway host probe (not part of this suite) before
// choosing the specific attack/release values and crossing thresholds below,
// so the asserted orderings and margins are not guesses: the probe measured
// onset/recovery around 1023/10434 samples (2 ms vs 40 ms attack) and
// 206/2062 samples (5 ms vs 100 ms release) -- roughly 10x apart in both
// cases -- against which this file's 2x assertion margins are conservative.

using namespace acfx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr float kFreq = 240.0f;               // 48000/240 = 200 samples/period exactly
constexpr int kWindowSamples = 100;           // one sin^2 half-cycle at kFreq
constexpr float kLoudAmp = 0.8f;              // well above the -18 dB trim threshold
constexpr float kQuietAmp = 0.05f;            // well below threshold - knee/2 (-21 dB)

// Common non-trim macro state shared by every case in this file (mirrors
// tape-dynamics-core-test.cpp's T026 configureCommon): moderate drive so the
// magnetics are meaningfully engaged without dominating the trim's own
// signature.
void configureCommon(TapeDynamicsCore<8>& core) {
    core.prepare(kFs, 1);
    core.setDrive(6.0f);
    core.setSaturation(1.0f);
    core.setWidth(1.0f);
    core.setSolver(Solver::rk4);
    core.setMix(1.0f);
    core.setOutput(0.0f);
}

// A sinusoid whose peak amplitude STEPS from `ampBefore` to `ampAfter` at
// `stepSample` -- the "level step" stimulus CASE 3 needs to observe
// attack/release onset and recovery timing. `stepSample = 0` (ampBefore
// irrelevant) drives a plain constant-amplitude tone, used by CASEs 1/2.
std::vector<float> steppedTone(int totalSamples, int stepSample, float ampBefore, float ampAfter) {
    std::vector<float> out(static_cast<std::size_t>(totalSamples));
    const double w = 2.0 * kPi * static_cast<double>(kFreq) / kFs;
    for (int n = 0; n < totalSamples; ++n) {
        const float amp = (n < stepSample) ? ampBefore : ampAfter;
        out[static_cast<std::size_t>(n)] =
            amp * static_cast<float>(std::sin(w * static_cast<double>(n)));
    }
    return out;
}

std::vector<float> drive(TapeDynamicsCore<8>& core, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n)
        out[n] = core.processSample(in[n], 0);
    return out;
}

bool allFinite(const std::vector<float>& v) {
    for (float s : v)
        if (!std::isfinite(s))
            return false;
    return true;
}

// Late-window RMS (mirrors tape-dynamics-compression-test.cpp's
// measureOutputLevelDb() late-window technique): averages the tail half of
// `v`, past any onset transient, into one steady-state RMS figure.
double tailRms(const std::vector<float>& v) {
    const std::size_t start = v.size() / 2;
    double sumSq = 0.0;
    for (std::size_t n = start; n < v.size(); ++n)
        sumSq += static_cast<double>(v[n]) * static_cast<double>(v[n]);
    return std::sqrt(sumSq / static_cast<double>(v.size() - start));
}

// Causal sliding-window RMS (window = `windowLen` samples), O(N) via a
// running sum of squares. A MEASUREMENT tool for this test file only -- not
// part of, and never substituted for, the DSP path under test.
std::vector<float> windowedRms(const std::vector<float>& v, int windowLen) {
    std::vector<float> out(v.size(), 0.0f);
    double sumSq = 0.0;
    for (std::size_t n = 0; n < v.size(); ++n) {
        sumSq += static_cast<double>(v[n]) * static_cast<double>(v[n]);
        if (n >= static_cast<std::size_t>(windowLen)) {
            const std::size_t old = n - static_cast<std::size_t>(windowLen);
            sumSq -= static_cast<double>(v[old]) * static_cast<double>(v[old]);
        }
        const std::size_t count = std::min(n + 1, static_cast<std::size_t>(windowLen));
        out[n] = static_cast<float>(std::sqrt(sumSq / static_cast<double>(count)));
    }
    return out;
}

// Elementwise ratio of two windowed-RMS envelopes -- the trim stage's
// instantaneous linear gain multiplier (see the file-header measurement-
// technique note). Guarded against a near-zero denominator (both signals
// start at exactly 0: x[0] = amp*sin(0) = 0) with a floor far below any
// signal level this file drives.
std::vector<float> gainRatio(const std::vector<float>& enabledRms, const std::vector<float>& disabledRms) {
    constexpr float kFloor = 1.0e-9f;
    std::vector<float> ratio(enabledRms.size());
    for (std::size_t n = 0; n < ratio.size(); ++n)
        ratio[n] = enabledRms[n] / std::max(kFloor, disabledRms[n]);
    return ratio;
}

// First absolute index >= `start` at which `v[n] <= threshold` (attack/onset
// search), or -1 if never reached in range.
int firstIndexAtOrBelow(const std::vector<float>& v, float threshold, std::size_t start) {
    for (std::size_t n = start; n < v.size(); ++n)
        if (v[n] <= threshold)
            return static_cast<int>(n);
    return -1;
}

// First absolute index >= `start` at which `v[n] >= threshold`
// (release/recovery search), or -1 if never reached in range.
int firstIndexAtOrAbove(const std::vector<float>& v, float threshold, std::size_t start) {
    for (std::size_t n = start; n < v.size(); ++n)
        if (v[n] >= threshold)
            return static_cast<int>(n);
    return -1;
}

// Convert a desired PLAIN-units value for a TapeDynamicsEffect parameter into
// the normalized 0..1 setParameter() expects (mirrors tape-dynamics-effect-
// test.cpp's normFor).
float normFor(TapeDynamicsEffect::Param p, float plainValue) {
    return normalize(TapeDynamicsEffect::kParams[p], plainValue);
}

// A spread of attack/release/amount combos spanning the extremes and
// midpoints of each macro's documented range (tape-dynamics-parameters.h:
// attack 0.001..0.5 s, release 0.001..1 s, amount 0..1) -- deliberately not
// just the default triple, so a bug that only leaks through at, say, a large
// amount or a fast attack cannot hide behind one "boring" configuration.
struct TrimCombo {
    float attackSeconds;
    float releaseSeconds;
    float amount;
};

constexpr std::array<TrimCombo, 6> kBitExactCombos = {{
    {0.001f, 0.001f, 0.0f},
    {0.001f, 1.0f, 1.0f},
    {0.5f, 0.001f, 1.0f},
    {0.5f, 1.0f, 0.0f},
    {0.05f, 0.1f, 0.5f},
    {0.001f, 0.5f, 0.75f},
}};

} // namespace

// ---------------------------------------------------------------------------
// CASE 1 -- E7: trim.enabled = false is BIT-EXACT the magnetics-only core,
// across a spread of attack/release/amount values. This is the load-bearing
// invariant: applyTrim() must early-return before touching ANY trim state
// when disabled (tape-dynamics-core.h), so nothing about trim's ballistics
// or depth may leak into the output while it is off.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsCore<8> trim disabled is bit-exact the magnetics-only core across attack/release/amount (E7, T027)") {
    constexpr int kN = 2048;
    const auto in = steppedTone(kN, 0, kLoudAmp, kLoudAmp);

    TapeDynamicsCore<8> baseline;
    configureCommon(baseline);
    baseline.setTrimEnabled(false);
    const auto expected = drive(baseline, in);
    CHECK(allFinite(expected));

    for (const TrimCombo& combo : kBitExactCombos) {
        INFO("attack=" << combo.attackSeconds << " release=" << combo.releaseSeconds
                        << " amount=" << combo.amount);
        TapeDynamicsCore<8> core;
        configureCommon(core);
        core.setTrimEnabled(false); // the invariant under test
        core.setTrimAttack(combo.attackSeconds);
        core.setTrimRelease(combo.releaseSeconds);
        core.setTrimAmount(combo.amount);
        const auto actual = drive(core, in);

        REQUIRE(actual.size() == expected.size());
        bool identical = true;
        for (std::size_t n = 0; identical && n < actual.size(); ++n)
            identical = (actual[n] == expected[n]);
        CHECK(identical);
    }
}

TEST_CASE("TapeDynamicsEffect trim disabled is bit-exact the magnetics-only path (E7, T027, black-box)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kN = 1024;

    auto reference = steppedTone(kN, 0, kLoudAmp, kLoudAmp);
    {
        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kN, 1});
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, 6.0f));
        // trim.enabled left at its descriptor default (off).
        float* chans[1] = {reference.data()};
        AudioBlock block(chans, 1, kN);
        fx.process(block); // reference.data() now holds the trim-off OUTPUT
    }
    CHECK(allFinite(reference));

    for (const TrimCombo& combo : kBitExactCombos) {
        INFO("attack=" << combo.attackSeconds << " release=" << combo.releaseSeconds
                        << " amount=" << combo.amount);
        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kN, 1});
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, 6.0f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kTrimEnabled},
                         normFor(TapeDynamicsEffect::kTrimEnabled, 0.0f)); // off (the invariant under test)
        fx.setParameter(ParamId{TapeDynamicsEffect::kTrimAttack},
                         normFor(TapeDynamicsEffect::kTrimAttack, combo.attackSeconds));
        fx.setParameter(ParamId{TapeDynamicsEffect::kTrimRelease},
                         normFor(TapeDynamicsEffect::kTrimRelease, combo.releaseSeconds));
        fx.setParameter(ParamId{TapeDynamicsEffect::kTrimAmount},
                         normFor(TapeDynamicsEffect::kTrimAmount, combo.amount));

        auto buf = steppedTone(kN, 0, kLoudAmp, kLoudAmp);
        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kN);
        fx.process(block); // applyPending() consumes every edit above, THEN processes the whole block

        REQUIRE(buf.size() == reference.size());
        bool identical = true;
        for (std::size_t n = 0; identical && n < buf.size(); ++n)
            identical = (buf[n] == reference[n]);
        CHECK(identical);
    }
}

// ---------------------------------------------------------------------------
// CASE 2 -- trim ENABLED applies envelope-driven gain reduction on a loud
// sustained tone, and the reduction's DEPTH scales with trim.amount:
// amount=0 is provably bit-exact trim-disabled; larger amount reduces the
// steady-state level further. Fixed attack=5ms/release=50ms so all three
// amount settings reach the SAME settled ballistics state within kN samples.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsCore<8> trim gain reduction on a loud tone scales monotonically with trim.amount (T027)") {
    constexpr int kN = 8000;
    const auto in = steppedTone(kN, 0, kLoudAmp, kLoudAmp);

    TapeDynamicsCore<8> disabled;
    configureCommon(disabled);
    disabled.setTrimEnabled(false);
    const auto disabledOut = drive(disabled, in);
    CHECK(allFinite(disabledOut));
    const double rmsDisabled = tailRms(disabledOut);
    REQUIRE(rmsDisabled > 0.0);

    auto enabledRun = [&](float amount) {
        TapeDynamicsCore<8> core;
        configureCommon(core);
        core.setTrimEnabled(true);
        core.setTrimAttack(0.005f);
        core.setTrimRelease(0.05f);
        core.setTrimAmount(amount);
        return drive(core, in);
    };

    SUBCASE("amount = 0 is bit-exact trim-disabled") {
        // computeGainDb(level) * 0.0f == 0.0f for every finite `level`
        // (GainComputer guards a non-finite level to 0.0f too), and
        // dbToGain(0.0f) == std::pow(10.0f, 0.0f) == 1.0f exactly, so
        // wet * 1.0f == wet bit-for-bit (IEEE-754 multiply-by-1 is exact).
        // A stronger, EXACT claim than "~no reduction" -- verified directly.
        const auto out = enabledRun(0.0f);
        CHECK(allFinite(out));
        REQUIRE(out.size() == disabledOut.size());
        bool identical = true;
        for (std::size_t n = 0; identical && n < out.size(); ++n)
            identical = (out[n] == disabledOut[n]);
        CHECK(identical);
    }

    SUBCASE("amount = 0.5 and amount = 1.0 reduce level, more reduction at larger amount") {
        const auto half = enabledRun(0.5f);
        const auto full = enabledRun(1.0f);
        CHECK(allFinite(half));
        CHECK(allFinite(full));
        const double rmsHalf = tailRms(half);
        const double rmsFull = tailRms(full);
        INFO("rmsDisabled=" << rmsDisabled << " rmsHalf=" << rmsHalf << " rmsFull=" << rmsFull);

        // Named margin: 2% -- comfortably above float/RMS-measurement noise
        // (CASE 1's bit-exact check already rules out float noise as an
        // explanation), well under the ~21% (amount=0.5) / further ~21%
        // (amount=1.0 vs amount=0.5) reductions actually measured by the
        // host probe that shaped this suite, so a real regression to "no
        // reduction" or "no scaling" fails loudly without the test being
        // fragile to tuning-pass drift.
        constexpr double kMarginFrac = 0.02;
        CHECK(rmsHalf < (1.0 - kMarginFrac) * rmsDisabled);
        CHECK(rmsFull < (1.0 - kMarginFrac) * rmsHalf);
    }
}

// ---------------------------------------------------------------------------
// CASE 3 -- attack/release ballistics: a fast trim.attack/release reaches
// its settled gain reduction / recovers to unity sooner (fewer samples) than
// a slow one, after a level step. See the file-header note for the
// windowed-RMS-ratio measurement technique and why crossing thresholds are
// evaluated well past a short warm-up window rather than from sample 0.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsCore<8> trim attack ballistics: fast attack reaches settled gain reduction sooner (T027)") {
    constexpr int kN = 12000; // comfortably >> 6x either attack time constant at kFs
    const auto in = steppedTone(kN, 0, kLoudAmp, kLoudAmp);

    TapeDynamicsCore<8> disabled;
    configureCommon(disabled);
    disabled.setTrimEnabled(false);
    const auto disabledRms = windowedRms(drive(disabled, in), kWindowSamples);

    auto onsetIndex = [&](float attackSeconds) {
        TapeDynamicsCore<8> core;
        configureCommon(core);
        core.setTrimEnabled(true);
        core.setTrimAttack(attackSeconds);
        core.setTrimRelease(1.0f); // slow enough to stay out of the attack measurement's way
        core.setTrimAmount(1.0f);
        const auto out = drive(core, in);
        CHECK(allFinite(out));
        const auto enabledRms = windowedRms(out, kWindowSamples);
        const auto ratio = gainRatio(enabledRms, disabledRms);

        // Settled ratio = mean over the last 10% of the run.
        const std::size_t tailStart = ratio.size() - ratio.size() / 10;
        double sum = 0.0;
        for (std::size_t n = tailStart; n < ratio.size(); ++n)
            sum += static_cast<double>(ratio[n]);
        const double settled = sum / static_cast<double>(ratio.size() - tailStart);
        // 90% of the way from unity (no reduction yet) down to the settled
        // ratio -- a threshold DERIVED from each run's own settled value, so
        // the comparison between attack settings is apples-to-apples.
        const float threshold = static_cast<float>(1.0 - 0.9 * (1.0 - settled));
        // Search starts at kWindowSamples: before the window fills, both the
        // enabled and disabled RMS are near-zero startup artifacts
        // (x[0] = amp*sin(0) = 0), not a ballistics measurement.
        const int idx = firstIndexAtOrBelow(ratio, threshold, static_cast<std::size_t>(kWindowSamples));
        INFO("attackSeconds=" << attackSeconds << " settledRatio=" << settled
                               << " threshold=" << threshold << " onsetIndex=" << idx);
        REQUIRE(idx >= 0);
        return idx;
    };

    const int fastOnset = onsetIndex(0.002f); // 2 ms
    const int slowOnset = onsetIndex(0.04f);  // 40 ms
    INFO("fastOnset=" << fastOnset << " slowOnset=" << slowOnset);

    // Named margin: fast attack must reach its settled reduction in under
    // HALF the samples the slow attack needs. The host probe that shaped
    // this suite measured ~1023 samples (fast) vs ~10434 (slow) -- roughly
    // 10x apart -- so a 2x bar is comfortably conservative against
    // measurement/tuning-pass drift while still failing hard on a genuine
    // attack-ballistics regression (e.g. attack wired to a fixed constant).
    CHECK(fastOnset < slowOnset / 2);
}

TEST_CASE("TapeDynamicsCore<8> trim release ballistics: fast release recovers to unity sooner (T027)") {
    constexpr int kWarmupSamples = 6000;
    constexpr int kPostSamples = 30000; // comfortably >> 6x either release time constant
    constexpr int kN = kWarmupSamples + kPostSamples;
    const auto in = steppedTone(kN, kWarmupSamples, kLoudAmp, kQuietAmp);

    TapeDynamicsCore<8> disabled;
    configureCommon(disabled);
    disabled.setTrimEnabled(false);
    const auto disabledRms = windowedRms(drive(disabled, in), kWindowSamples);

    auto recoveryIndex = [&](float releaseSeconds) {
        TapeDynamicsCore<8> core;
        configureCommon(core);
        core.setTrimEnabled(true);
        core.setTrimAttack(0.005f); // fast enough to fully settle within kWarmupSamples
        core.setTrimRelease(releaseSeconds);
        core.setTrimAmount(1.0f);
        const auto out = drive(core, in);
        CHECK(allFinite(out));
        const auto enabledRms = windowedRms(out, kWindowSamples);
        const auto ratio = gainRatio(enabledRms, disabledRms);

        const double r0 = static_cast<double>(ratio[static_cast<std::size_t>(kWarmupSamples - 1)]);
        const std::size_t tailStart = ratio.size() - static_cast<std::size_t>(kPostSamples) / 10;
        double sum = 0.0;
        for (std::size_t n = tailStart; n < ratio.size(); ++n)
            sum += static_cast<double>(ratio[n]);
        const double settled = sum / static_cast<double>(ratio.size() - tailStart);
        // 90% of the way from the warmed-up reduced ratio back up toward the
        // recovered (unity) ratio.
        const float threshold = static_cast<float>(r0 + 0.9 * (settled - r0));
        const int idx = firstIndexAtOrAbove(
            ratio, threshold, static_cast<std::size_t>(kWarmupSamples + kWindowSamples));
        INFO("releaseSeconds=" << releaseSeconds << " r0=" << r0 << " settledRatio=" << settled
                                << " threshold=" << threshold << " recoveryIndex(absolute)=" << idx);
        REQUIRE(idx >= 0);
        return idx - kWarmupSamples;
    };

    const int fastRecovery = recoveryIndex(0.005f); // 5 ms
    const int slowRecovery = recoveryIndex(0.1f);   // 100 ms
    INFO("fastRecovery=" << fastRecovery << " slowRecovery=" << slowRecovery);

    // Named margin: mirrors the attack CASE's 2x bar. The host probe
    // measured ~206 samples (fast) vs ~2062 (slow) -- ~10x apart -- so 2x is
    // comfortably conservative while still failing hard on a genuine
    // release-ballistics regression.
    CHECK(fastRecovery < slowRecovery / 2);
}

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "effects/tape-dynamics/tape-dynamics-core.h"

using namespace acfx;

namespace {

// Drive a full sinusoid through a prepared core on channel 0 and collect the
// output. Bounded, deterministic; the caller sets drive/mix beforehand.
std::vector<float> driveSine(TapeDynamicsCore<8>& core, int numSamples,
                             float freqHz, double sampleRate, float amp) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(numSamples));
    const double twoPiFOverFs =
        2.0 * 3.14159265358979323846 * static_cast<double>(freqHz) / sampleRate;
    for (int n = 0; n < numSamples; ++n) {
        const float x =
            amp * static_cast<float>(std::sin(twoPiFOverFs * static_cast<double>(n)));
        out.push_back(core.processSample(x, 0));
    }
    return out;
}

bool allFinite(const std::vector<float>& v) {
    for (float s : v)
        if (!std::isfinite(s))
            return false;
    return true;
}

float maxAbs(const std::vector<float>& v) {
    float m = 0.0f;
    for (float s : v)
        m = std::max(m, std::fabs(s));
    return m;
}

} // namespace

TEST_CASE("TapeDynamicsCore<8> — composed OS+JA signal path (T015)") {
    constexpr double kFs = 48000.0;
    constexpr int kN = 4096;
    constexpr float kFreq = 220.0f;

    SUBCASE("output is finite under drive") {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(18.0f);      // +18 dB into the magnetics
        core.setSaturation(1.0f);
        core.setWidth(1.0f);
        core.setSolver(Solver::rk4);
        core.setMix(1.0f);
        core.setOutput(0.0f);

        const auto y = driveSine(core, kN, kFreq, kFs, 0.5f);
        CHECK(allFinite(y));
        CHECK(maxAbs(y) > 0.0f);
    }

    SUBCASE("drive > 0 produces a nonlinear (saturated) output that differs "
            "from the input") {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(18.0f);
        core.setSaturation(1.0f);
        core.setWidth(1.0f);
        core.setSolver(Solver::rk4);
        core.setMix(1.0f);
        core.setOutput(0.0f);

        // Feed a strong sine; after settling, the magnetization output must not
        // be a pure scaled copy of the input — nonlinearity introduces harmonic
        // content, so the normalized wet shape deviates from the input shape.
        const float amp = 0.9f;
        const auto y = driveSine(core, kN, kFreq, kFs, amp);
        CHECK(allFinite(y));

        // Compare late-window RMS of wet vs the input sine RMS. A purely linear
        // path would keep the wet/in ratio constant sample-to-sample; the JA
        // curve compresses peaks, so the wet crest factor differs from a sine.
        const double twoPiFOverFs =
            2.0 * 3.14159265358979323846 * static_cast<double>(kFreq) / kFs;
        double sumSqIn = 0.0, sumSqWet = 0.0;
        double peakWet = 0.0;
        int count = 0;
        for (int n = kN / 2; n < kN; ++n) {
            const double xin =
                static_cast<double>(amp) * std::sin(twoPiFOverFs * n);
            sumSqIn += xin * xin;
            const double w = static_cast<double>(y[static_cast<std::size_t>(n)]);
            sumSqWet += w * w;
            peakWet = std::max(peakWet, std::fabs(w));
            ++count;
        }
        const double rmsIn = std::sqrt(sumSqIn / count);
        const double rmsWet = std::sqrt(sumSqWet / count);
        REQUIRE(rmsIn > 0.0);
        REQUIRE(rmsWet > 0.0);
        // Sine crest factor = sqrt(2) ~= 1.414. A saturated wet signal has a
        // LOWER crest factor (peaks compressed relative to RMS). Assert the wet
        // crest factor is meaningfully below the pure-sine value — proof the
        // path is nonlinear, not a scaled passthrough.
        const double wetCrest = peakWet / rmsWet;
        CHECK(wetCrest < 1.40);
    }

    SUBCASE("mix = 0 is a dry passthrough (latency-aligned) regardless of drive") {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(24.0f);   // heavy drive...
        core.setMix(0.0f);      // ...but fully dry
        core.setOutput(0.0f);

        const float amp = 0.7f;
        const auto y = driveSine(core, 512, kFreq, kFs, amp);
        // mix=0 -> y = dryAligned (output 0 dB -> unity). The dry path is now
        // DELAY-COMPENSATED to the wet path's group delay (dry/wet mix-comb fix)
        // so mix in (0,1) blends time-aligned signals. For Oversampler<8> that
        // delay is 2*45*7/8 = 78.75 samples, read fractionally from a zero-
        // initialized ring, so y[n] = 0.25*x[n-78] + 0.75*x[n-79] (x[k<0] = 0).
        // Asserting exactly the aligned dry is a stronger contract than the old
        // undelayed comb; tolerance is float-store rounding only.
        const double twoPiFOverFs =
            2.0 * 3.14159265358979323846 * static_cast<double>(kFreq) / kFs;
        auto xAt = [&](int n) {
            return (n >= 0) ? amp * static_cast<float>(std::sin(twoPiFOverFs * n)) : 0.0f;
        };
        constexpr int kDelayInt = 78;
        constexpr float kDelayFrac = 0.75f;
        bool exact = true;
        for (int n = 0; n < 512; ++n) {
            const float expected =
                (1.0f - kDelayFrac) * xAt(n - kDelayInt) + kDelayFrac * xAt(n - kDelayInt - 1);
            if (std::fabs(y[static_cast<std::size_t>(n)] - expected) > 1.0e-6f) {
                exact = false;
                break;
            }
        }
        CHECK(exact);
    }

    SUBCASE("JA memory: rising vs falling field give different magnetization "
            "(hysteresis loop)") {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(12.0f);
        core.setSaturation(1.0f);
        core.setWidth(1.0f);   // k=1 -> a genuinely open loop
        core.setSolver(Solver::rk4);
        core.setMix(1.0f);
        core.setOutput(0.0f);

        // Sweep the field UP to a mid level, then DOWN back through the SAME
        // level. A hysteretic (memory-bearing) system returns a different output
        // at the same instantaneous input on the rising vs the falling branch.
        float risingAtHalf = 0.0f;
        float fallingAtHalf = 0.0f;
        const int steps = 200;
        const float target = 0.5f;
        for (int n = 0; n <= steps; ++n) {   // ramp up 0 -> target
            const float x = target * static_cast<float>(n) / steps;
            const float wet = core.processSample(x, 0);
            if (n == steps / 2)
                risingAtHalf = wet;
        }
        for (int n = steps; n >= 0; --n) {   // ramp down target -> 0
            const float x = target * static_cast<float>(n) / steps;
            const float wet = core.processSample(x, 0);
            if (n == steps / 2)
                fallingAtHalf = wet;
        }
        CHECK(std::isfinite(risingAtHalf));
        CHECK(std::isfinite(fallingAtHalf));
        // The loop opens: same field (x = target/2) yields different output on
        // the two branches. Non-trivial difference proves stored JA memory.
        CHECK(std::fabs(risingAtHalf - fallingAtHalf) > 1.0e-4f);
    }

    SUBCASE("reset() restores determinism") {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(15.0f);
        core.setSaturation(1.0f);
        core.setWidth(1.0f);
        core.setMix(1.0f);

        const auto first = driveSine(core, 1024, kFreq, kFs, 0.6f);
        core.reset();
        const auto second = driveSine(core, 1024, kFreq, kFs, 0.6f);
        bool identical = first.size() == second.size();
        for (std::size_t i = 0; identical && i < first.size(); ++i)
            identical = (first[i] == second[i]);
        CHECK(identical);
    }
}

// T026 -- optional explicit envelope-driven trim (US6, FR-011, contract E7).
// A focused check (the full US6 suite is T027): the disabled path must stay
// bit-exact the magnetics-only core no matter what trim.attack/release/amount
// are set to, and the enabled path must measurably reduce a loud, sustained
// signal's level relative to the disabled path.
TEST_CASE("TapeDynamicsCore<8> — optional trim: disabled is bit-exact, enabled reduces level (T026)") {
    constexpr double kFs = 48000.0;
    constexpr int kN = 4096;
    constexpr float kFreq = 220.0f;
    constexpr float kAmp = 0.8f; // well above the trim curve's -18 dBFS threshold

    auto configureCommon = [](auto& core) {
        core.prepare(kFs, 1);
        core.setDrive(6.0f);
        core.setSaturation(1.0f);
        core.setWidth(1.0f);
        core.setSolver(Solver::rk4);
        core.setMix(1.0f);
        core.setOutput(0.0f);
    };

    SUBCASE("trim.enabled = false is bit-exact the magnetics-only path, "
            "regardless of trim.attack/release/amount (E7)") {
        TapeDynamicsCore<8> baseline;
        configureCommon(baseline);
        // trim left at its defaults (disabled) -- the magnetics-only path.
        const auto expected = driveSine(baseline, kN, kFreq, kFs, kAmp);

        TapeDynamicsCore<8> withTrimConfigured;
        configureCommon(withTrimConfigured);
        // Set every trim knob to an aggressive, obviously-audible value --
        // but leave trim.enabled at its default (false).
        withTrimConfigured.setTrimAttack(0.001f);
        withTrimConfigured.setTrimRelease(0.5f);
        withTrimConfigured.setTrimAmount(1.0f);
        const auto actual = driveSine(withTrimConfigured, kN, kFreq, kFs, kAmp);

        REQUIRE(expected.size() == actual.size());
        bool identical = true;
        for (std::size_t i = 0; identical && i < expected.size(); ++i)
            identical = (expected[i] == actual[i]);
        CHECK(identical);
    }

    SUBCASE("trim.enabled = true with amount = 1 measurably reduces a loud "
            "sustained signal's level vs trim disabled") {
        TapeDynamicsCore<8> disabled;
        configureCommon(disabled);
        const auto disabledOut = driveSine(disabled, kN, kFreq, kFs, kAmp);

        TapeDynamicsCore<8> enabled;
        configureCommon(enabled);
        enabled.setTrimEnabled(true);
        enabled.setTrimAttack(0.001f);  // fast attack: settle quickly within kN samples
        enabled.setTrimRelease(0.05f);
        enabled.setTrimAmount(1.0f);    // full curve depth
        const auto enabledOut = driveSine(enabled, kN, kFreq, kFs, kAmp);

        CHECK(allFinite(disabledOut));
        CHECK(allFinite(enabledOut));

        // Compare late-window RMS (past the envelope follower's attack
        // settling) so the measurement reflects steady-state gain reduction,
        // not the attack transient.
        auto rmsFrom = [](const std::vector<float>& v, int start) {
            double sumSq = 0.0;
            int count = 0;
            for (std::size_t i = static_cast<std::size_t>(start); i < v.size(); ++i) {
                sumSq += static_cast<double>(v[i]) * static_cast<double>(v[i]);
                ++count;
            }
            return std::sqrt(sumSq / count);
        };
        const double rmsDisabled = rmsFrom(disabledOut, kN / 2);
        const double rmsEnabled = rmsFrom(enabledOut, kN / 2);
        INFO("rmsDisabled=" << rmsDisabled << " rmsEnabled=" << rmsEnabled);
        REQUIRE(rmsDisabled > 0.0);
        // Named tolerance: the enabled path must sit meaningfully (>1%) below
        // the disabled path -- comfortably above any float-rounding floor,
        // proving the trim stage measurably attenuates a loud signal.
        CHECK(rmsEnabled < 0.99 * rmsDisabled);
    }
}

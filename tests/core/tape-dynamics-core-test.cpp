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

    SUBCASE("mix = 0 is a dry passthrough regardless of drive") {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(24.0f);   // heavy drive...
        core.setMix(0.0f);      // ...but fully dry
        core.setOutput(0.0f);

        const float amp = 0.7f;
        const auto y = driveSine(core, 512, kFreq, kFs, amp);
        // mix=0 -> y = dry = x exactly (output 0 dB -> unity), for every sample.
        const double twoPiFOverFs =
            2.0 * 3.14159265358979323846 * static_cast<double>(kFreq) / kFs;
        bool exact = true;
        for (int n = 0; n < 512; ++n) {
            const float xin =
                amp * static_cast<float>(std::sin(twoPiFOverFs * n));
            if (std::fabs(y[static_cast<std::size_t>(n)] - xin) > 1.0e-6f) {
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

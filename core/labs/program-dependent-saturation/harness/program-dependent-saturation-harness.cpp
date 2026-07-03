// program-dependent-saturation-harness.cpp
// Host-only harness that drives acfx::ProgramDependentSaturationCore and
// prints evidence to stdout:
//   (a) ORTHOGONALITY: with all modulation depths at zero, process a tone
//       through ProgramDependentSaturationCore and SaturationCore at matching
//       static parameters; print the max abs difference (should be ~0).
//   (b) THD-vs-LEVEL: with a positive driveDepth (feedforward modulation),
//       process a sine at several input levels (-40, -20, -6 dBFS); print THD
//       at each level to show level-dependent saturation modulation.
//   (c) STEP RESPONSE: a level step (quiet -> loud -> quiet); print the output
//       RMS trajectory at several time points to show attack/release engaging.
//
// Like core/labs/saturation/harness/saturation-harness.cpp, this harness
// drives measurements through the SHIPPED measurement infrastructure
// (tests/core/measurement-support.h -- meastest::harmonicSignature /
// meastest::thd) rather than reimplementing a second spectral engine.
// Every header pulled in is header-only, so this stays a single-TU host
// executable with no test-target linkage -- only the tests/ include root
// (see CMakeLists.txt).
//
// Include roots: core/ and tests/ (see the acfx_lab_program_dependent_saturation_harness
// CMake target). Compile with -std=c++20. May allocate, loop, printf. Never
// included by portable code (C-1 gate in scripts/check-portability.sh).

#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/saturation/saturation-core.h"
#include "effects/saturation/saturation-voicings.h"
#include "core/measurement-support.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace acfx;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float  kPi          = 3.14159265358979323846f;
constexpr float  kSampleRate  = 48000.0f;
constexpr double kSampleRateD = 48000.0;

// Stimulus parameters
constexpr double      kFundHz     = 1000.0;
constexpr std::size_t kNumSamples = 4800;  // 100 integer cycles @ 48 kHz
constexpr int         kNumHarm    = 6;     // harmonics 1..6, all < Nyquist
constexpr float       kAmplitude  = 0.5f;
constexpr float       kDrive      = 6.0f;  // dB
constexpr float       kBias       = 0.0f;
constexpr float       kTone       = 0.0f;
constexpr float       kMix        = 1.0f;

// Settling window for transient response (analogous to saturation-harness)
constexpr int kTransientSamples = 16000;

// Attack/release for step response test (ms)
constexpr float kAttackMs  = 10.0f;
constexpr float kReleaseMs = 100.0f;

} // namespace

// ---------------------------------------------------------------------------
// Configure a SaturationCore to match static parameters
// ---------------------------------------------------------------------------
void configureSaturationCore(acfx::SaturationCore& core,
                             float drive,
                             float bias,
                             float tone,
                             float mix) {
    core.prepare(kSampleRate);
    core.setDrive(std::pow(10.0f, drive / 20.0f));  // dB -> linear
    core.setBias(bias);
    core.setTone(tone);
    core.setMix(mix);
    core.setOutput(1.0f);
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(SaturationQuality::adaa);
}

// ---------------------------------------------------------------------------
// Configure a ProgramDependentSaturationCore to match static parameters
// ---------------------------------------------------------------------------
void configurePdSaturationCore(acfx::ProgramDependentSaturationCore& core,
                               float drive,
                               float bias,
                               float tone,
                               float mix) {
    core.prepare(kSampleRate);
    core.setStaticDrive(drive);  // stored in dB
    core.setStaticBias(bias);
    core.setStaticTone(tone);
    core.setStaticMix(mix);
    core.setOutput(1.0f);
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(SaturationQuality::adaa);

    // Set all modulation depths to zero for orthogonality test
    core.setDepth(ModTarget::drive, 0.0f);
    core.setDepth(ModTarget::bias, 0.0f);
    core.setDepth(ModTarget::tone, 0.0f);
    core.setDepth(ModTarget::mix, 0.0f);

    // Default detector/ballistics (will be configured per test)
    core.setDetectorMode(DetectMode::rms);
    core.setBallistics(Ballistics::branching);
    core.setAttack(kAttackMs / 1000.0f);
    core.setRelease(kReleaseMs / 1000.0f);
    core.setDetection(Detection::feedForward);
}

// ---------------------------------------------------------------------------
// ORTHOGONALITY TEST: ProgramDependentSaturationCore with depths=0 should
// match SaturationCore at the same static parameters.
// ---------------------------------------------------------------------------
void runOrthogonalityTest() {
    std::printf("=== ORTHOGONALITY TEST ===\n");
    std::printf("(depths=0, static parameters only)\n\n");

    // Warm up both cores through transients
    acfx::SaturationCore satCore;
    configureSaturationCore(satCore, kDrive, kBias, kTone, kMix);

    acfx::ProgramDependentSaturationCore pdCore;
    configurePdSaturationCore(pdCore, kDrive, kBias, kTone, kMix);

    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float x = kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
        (void)satCore.process(x);
        (void)pdCore.process(x, x);
    }

    // Capture steady-state output and measure difference
    std::vector<float> satOut(kNumSamples, 0.0f);
    std::vector<float> pdOut(kNumSamples, 0.0f);

    for (std::size_t i = 0; i < kNumSamples; ++i) {
        const float t = static_cast<float>(kTransientSamples + static_cast<int>(i)) / kSampleRate;
        const float x = kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
        satOut[i] = satCore.process(x);
        pdOut[i] = pdCore.process(x, x);
    }

    // Compute max absolute difference
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < kNumSamples; ++i) {
        const float diff = std::abs(satOut[i] - pdOut[i]);
        if (diff > maxDiff)
            maxDiff = diff;
    }

    std::printf("orthogonality maxdiff: %.6f\n", static_cast<double>(maxDiff));
    std::printf("(expecting < 1e-5 for bit-identical behavior)\n\n");
}

// ---------------------------------------------------------------------------
// THD-vs-LEVEL TEST: with driveDepth > 0 (feedforward modulation), sweep
// input level and print THD at each level.
// ---------------------------------------------------------------------------
void runThdVsLevelTest() {
    std::printf("=== THD-vs-LEVEL TEST ===\n");
    std::printf("(feedforward driveDepth = 1.0, modulating static drive)\n\n");

    // Input levels (dBFS) to test
    const std::array<float, 3> testLevelsDb = {-40.0f, -20.0f, -6.0f};

    std::printf("  level (dBFS) | THD\n");
    std::printf("  -----------+---------\n");

    for (float levelDb : testLevelsDb) {
        // Convert dBFS to linear amplitude
        const float amplitude = std::pow(10.0f, levelDb / 20.0f);

        acfx::ProgramDependentSaturationCore core;
        configurePdSaturationCore(core, kDrive, kBias, kTone, kMix);

        // Enable feedforward drive modulation (depth = 1.0 means full swing)
        core.setDepth(ModTarget::drive, 1.0f);
        core.setDetection(Detection::feedForward);

        // Warm up through transient
        for (int i = 0; i < kTransientSamples; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            const float x = amplitude * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
            (void)core.process(x, x);
        }

        // Capture steady-state output
        std::vector<float> out(kNumSamples, 0.0f);
        for (std::size_t i = 0; i < kNumSamples; ++i) {
            const float t = static_cast<float>(kTransientSamples + static_cast<int>(i)) / kSampleRate;
            const float x = amplitude * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
            out[i] = core.process(x, x);
        }

        // Measure THD
        const double thdValue = acfx::measure::thd(
            acfx::span<const float>(out), kFundHz, kSampleRateD, 5);

        const char* thdStr = std::isnan(thdValue) ? "unmeasurable" : "";
        if (std::isnan(thdValue)) {
            std::printf("  %6.1f       | %s\n", static_cast<double>(levelDb), thdStr);
        } else {
            std::printf("  %6.1f       | %.4f\n", static_cast<double>(levelDb), thdValue);
        }
    }

    std::printf("\n");
}

// ---------------------------------------------------------------------------
// STEP RESPONSE TEST: apply a level step and print output RMS trajectory
// to observe attack/release envelope engagement.
// ---------------------------------------------------------------------------
void runStepResponseTest() {
    std::printf("=== STEP RESPONSE TEST ===\n");
    std::printf("(level step: quiet @ 0ms -> loud @ 1000ms -> quiet @ 2000ms)\n");
    std::printf("(feedforward detection, attack=%.0f ms, release=%.0f ms)\n\n",
                kAttackMs, kReleaseMs);

    acfx::ProgramDependentSaturationCore core;
    configurePdSaturationCore(core, kDrive, kBias, kTone, kMix);

    // Enable feedforward drive modulation
    core.setDepth(ModTarget::drive, 0.8f);
    core.setDetection(Detection::feedForward);

    // Step response: quiet -> loud -> quiet
    const float quietLevel = 0.01f;   // -40 dBFS
    const float loudLevel  = 0.5f;    // -6 dBFS
    const int  quietPhase  = 48000;   // 1000 ms @ 48 kHz
    const int  loudPhase   = 48000;   // 1000 ms @ 48 kHz
    const int  finalQuiet  = 48000;   // 1000 ms @ 48 kHz

    std::vector<float> output;
    output.reserve(quietPhase + loudPhase + finalQuiet);

    // Quiet phase
    for (int i = 0; i < quietPhase; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float x = quietLevel * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
        output.push_back(core.process(x, x));
    }

    // Loud phase
    for (int i = 0; i < loudPhase; ++i) {
        const float t = static_cast<float>(quietPhase + i) / kSampleRate;
        const float x = loudLevel * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
        output.push_back(core.process(x, x));
    }

    // Final quiet phase
    for (int i = 0; i < finalQuiet; ++i) {
        const float t = static_cast<float>(quietPhase + loudPhase + i) / kSampleRate;
        const float x = quietLevel * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
        output.push_back(core.process(x, x));
    }

    // Sample at key points: start, step-up, during loud, step-down, final
    const std::array<int, 5> samplePoints = {
        0,                      // quiet start
        quietPhase - 1000,      // end of quiet
        quietPhase + 1000,      // early in loud phase (attack ramping)
        quietPhase + loudPhase - 1000,  // end of loud phase
        quietPhase + loudPhase + 1000   // early in final quiet (release ramping)
    };

    std::printf("  time (ms)  | RMS level\n");
    std::printf("  -----------+----------\n");

    for (int sampleIdx : samplePoints) {
        if (sampleIdx < 0 || sampleIdx >= static_cast<int>(output.size()))
            continue;

        // Compute RMS over a 48-sample window (1 ms at 48 kHz) centered at sampleIdx
        const int windowRadius = 24;
        const int start = std::max(0, sampleIdx - windowRadius);
        const int end   = std::min(static_cast<int>(output.size()) - 1, sampleIdx + windowRadius);

        double sumSq = 0.0;
        for (int i = start; i <= end; ++i) {
            const double x = static_cast<double>(output[i]);
            sumSq += x * x;
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(end - start + 1));

        const float timeMs = static_cast<float>(sampleIdx) / kSampleRate * 1000.0f;
        std::printf("  %6.1f     | %.6f\n", static_cast<double>(timeMs), rms);
    }

    std::printf("\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== acfx Program-Dependent Saturation Lab Harness ===\n");
    std::printf("sample rate: %.0f Hz   fundamental: %.0f Hz   amplitude: %.2f\n",
                kSampleRateD, kFundHz, static_cast<double>(kAmplitude));
    std::printf("window: %zu measurement samples (%d integer cycles), %d warm-up samples\n\n",
                kNumSamples, static_cast<int>(kFundHz * static_cast<double>(kNumSamples) / kSampleRateD),
                kTransientSamples);

    runOrthogonalityTest();
    runThdVsLevelTest();
    runStepResponseTest();

    std::printf("=== Done ===\n");

    return 0;
}

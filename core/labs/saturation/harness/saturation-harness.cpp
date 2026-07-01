// saturation-harness.cpp
// Host-only harness that drives acfx::SaturationCore through all four named
// voicings (softClip/tape/console/tubePreamp) and prints evidence to stdout:
//   (a) Per-voicing harmonic signatures -- absolute Goertzel amplitude at each
//       of the first 6 harmonics (f0 = 1000 Hz, integer-cycle window
//       N = 4800), each voicing settled through its own transient first.
//   (b) Naive-vs-ADAA aliasing comparison for the softClip voicing at 10 kHz:
//       inharmonic (aliased) power for each quality arm and the reduction
//       ratio/dB.
//   (c) OPTIONAL: --csv flag for a machine-readable CSV dump of the per-
//       voicing harmonic spectra (default: human-readable tables only).
//
// Unlike core/labs/waveshaping/harness/waveshaping-harness.cpp (which is
// self-contained per the SVF-harness precedent), this harness drives its
// measurements through the SHIPPED measurement infrastructure
// (tests/core/measurement-support.h -- meastest::harmonicSignature /
// meastest::aliasingMeasure) rather than reimplementing a second spectral
// engine, per spec.md User Story 5's acceptance scenario 1 ("emits ...
// evidence ... using the shared measurement infrastructure"). Every header
// this pulls in (measurement-support.h + support/measurement/*.h) is header-
// only, so this stays a single-TU host executable with no test-target
// linkage -- only the tests/ include root (see CMakeLists.txt).
//
// Include roots: core/ and tests/ (see the acfx_lab_saturation_harness CMake
// target). Compile with -std=c++20. May allocate, loop, printf. Never
// included by portable code (C-1 gate in scripts/check-portability.sh).

#include "labs/saturation/saturation-core.h"
#include "labs/saturation/saturation-voicings.h"
#include "core/measurement-support.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// meastest:: lives in namespace acfx::meastest (measurement-support.h); this
// using-directive matches the convention already used by the test suites
// that consume it (e.g. saturation-voicings-test.cpp, saturation-aliasing-
// test.cpp), so `meastest::...` and `SaturationCore`/`SaturationVoicing`
// resolve unqualified below.
using namespace acfx;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float  kPi          = 3.14159265358979323846f;
constexpr float  kSampleRate  = 48000.0f;
constexpr double kSampleRateD = 48000.0;

// --- Harmonic-signature stimulus (mirrors saturation-voicings-test.cpp's
// captureVoicingSignature exactly, so the harness reproduces the same
// evidence the test suite already asserts against). ---
constexpr double      kFundHz     = 1000.0;
constexpr std::size_t kNumSamples = 4800;  // 100 integer cycles @ 48 kHz
constexpr int         kNumHarm    = 6;     // harmonics 1..6, all < Nyquist
constexpr float       kAmplitude  = 0.5f;  // moderate: exercises every shape class
constexpr float       kDrive      = 3.0f;  // fixed across voicings
constexpr float       kBias       = 0.0f;  // default: bias is a separate user control
constexpr float       kFullWetMix = 1.0f;  // isolate the wet (voicing-colored) path
// Settling window: bounded by the composed chain's slowest LTI stage (the
// tone-tilt highpass at tone=0, tau ~= 1592 samples @ 48 kHz -- see
// saturation-voicings-test.cpp for the full derivation). 16000 (~10*tau)
// leaves comfortable headroom above every stage's time constant.
constexpr int kTransientSamples = 16000;

// --- Aliasing comparison (mirrors saturation-aliasing-test.cpp TEST 1
// exactly): softClip's near-flat emphasis pair does not roll off the
// aliased band the way tape's darker post-de-emphasis would, so it is the
// voicing that best exposes naive-vs-ADAA aliasing differences. ---
constexpr acfx::SaturationVoicing kAliasVoicing   = acfx::SaturationVoicing::softClip;
constexpr double                  kAliasFundHz    = 10000.0;
constexpr std::size_t             kAliasNumSamples = 4800;  // 1000 integer cycles @ 48 kHz
constexpr float                   kAliasDrive     = 4.0f;
constexpr float                   kAliasAmplitude = 1.0f;

} // namespace

// ---------------------------------------------------------------------------
// Voicing table (name + enumerator, in kVoicingLabels order).
// ---------------------------------------------------------------------------

namespace {

struct VoicingEntry {
    acfx::SaturationVoicing voicing;
    const char*             name;
};

constexpr std::array<VoicingEntry, 4> kAllVoicings = {{
    {acfx::SaturationVoicing::softClip,   "softClip"},
    {acfx::SaturationVoicing::tape,       "tape"},
    {acfx::SaturationVoicing::console,    "console"},
    {acfx::SaturationVoicing::tubePreamp, "tubePreamp"},
}};

// ---------------------------------------------------------------------------
// Build a fully-configured SaturationCore for one voicing (mirrors
// saturation-voicings-test.cpp / saturation-aliasing-test.cpp's
// configureCore() exactly, so this harness reproduces the same setup order
// the test suite already exercises).
// ---------------------------------------------------------------------------
void configureCore(acfx::SaturationCore& core,
                    acfx::SaturationVoicing voicing,
                    float drive,
                    float bias,
                    float mix,
                    acfx::SaturationQuality quality = acfx::SaturationQuality::adaa) {
    core.prepare(kSampleRate);
    core.setVoicing(voicing);
    core.setQuality(quality);
    core.setDrive(drive);
    core.setBias(bias);
    core.setTone(0.0f);
    core.setMix(mix);
    core.setOutput(1.0f);
}

// ---------------------------------------------------------------------------
// captureVoicingSignature -- warm the composed chain through its transient,
// then capture kNumSamples of steady-state output and reduce it to a
// meastest::HarmonicSignature via the shared measurement infrastructure.
// Phase continues across the warm-up/capture boundary to avoid a
// discontinuity artifact (matches saturation-voicings-test.cpp).
// ---------------------------------------------------------------------------
meastest::HarmonicSignature captureVoicingSignature(acfx::SaturationVoicing voicing) {
    acfx::SaturationCore core;
    configureCore(core, voicing, kDrive, kBias, kFullWetMix);

    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        (void)core.process(kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t));
    }

    std::vector<float> outBuf(kNumSamples);
    for (std::size_t i = 0; i < kNumSamples; ++i) {
        const float t = static_cast<float>(kTransientSamples + static_cast<int>(i)) / kSampleRate;
        const float x = kAmplitude * std::sin(2.0f * kPi * static_cast<float>(kFundHz) * t);
        outBuf[i] = core.process(x);
    }

    return meastest::harmonicSignature(
        acfx::span<const float>(outBuf), kFundHz, kSampleRateD, kNumHarm);
}

// Prints a harmonic signature in human-readable table format.
void printHarmonicSignature(const char* name, const meastest::HarmonicSignature& sig) {
    std::printf("  %-14s |", name);
    for (int h = 1; h <= kNumHarm; ++h) {
        const double freq = kFundHz * static_cast<double>(h);
        if (freq >= kSampleRateD * 0.5) {
            std::printf("   ------  |");
        } else {
            std::printf("   %6.4f  |", sig.at(h));
        }
    }
    std::printf("\n");
}

// Dumps a harmonic signature in CSV format:
// voicing,harmonic_index,frequency_hz,magnitude
void printHarmonicSignatureCSV(const char* name, const meastest::HarmonicSignature& sig) {
    for (int h = 1; h <= kNumHarm; ++h) {
        const double freq = kFundHz * static_cast<double>(h);
        if (freq >= kSampleRateD * 0.5) {
            continue;  // Skip out-of-band harmonics
        }
        std::printf("%s,%d,%.1f,%g\n", name, h, freq, sig.at(h));
    }
}

// ---------------------------------------------------------------------------
// Naive vs ADAA aliasing comparison (softClip voicing at 10 kHz, drive = 4).
// Mirrors saturation-aliasing-test.cpp TEST 1's stimulus and configuration.
// ---------------------------------------------------------------------------
void runAliasingComparison() {
    std::printf("\n=== Naive vs ADAA aliasing comparison ===\n");
    std::printf("voicing: softClip   f0: %.0f Hz   drive: %.1f   window: %zu samples (%d cycles)\n",
                kAliasFundHz, static_cast<double>(kAliasDrive),
                kAliasNumSamples, static_cast<int>(kAliasFundHz * static_cast<double>(kAliasNumSamples) / kSampleRateD));
    std::printf("Aliased images at non-harmonic bins (see saturation-aliasing-test.cpp comment).\n\n");

    acfx::SaturationCore naiveCore;
    configureCore(naiveCore, kAliasVoicing, kAliasDrive, 0.0f, kFullWetMix, acfx::SaturationQuality::naive);
    const meastest::AliasingMeasure naive = meastest::aliasingMeasure(
        [&naiveCore](float x) { return naiveCore.process(x); },
        kAliasFundHz, kSampleRateD, kAliasNumSamples, kAliasAmplitude);

    acfx::SaturationCore adaaCore;
    configureCore(adaaCore, kAliasVoicing, kAliasDrive, 0.0f, kFullWetMix, acfx::SaturationQuality::adaa);
    const meastest::AliasingMeasure adaa = meastest::aliasingMeasure(
        [&adaaCore](float x) { return adaaCore.process(x); },
        kAliasFundHz, kSampleRateD, kAliasNumSamples, kAliasAmplitude);

    std::printf("  arm    | totalPower | harmPower  | inharmonic\n");
    std::printf("  -------+------------+------------+------------\n");
    std::printf("  naive  |  %8.5f  |  %8.5f  |  %9.6f\n",
                naive.totalPower, naive.harmonicPower, naive.inharmonicPower);
    std::printf("  ADAA   |  %8.5f  |  %8.5f  |  %9.6f\n",
                adaa.totalPower, adaa.harmonicPower, adaa.inharmonicPower);

    std::printf("\n");
    if (naive.inharmonicPower > 1.0e-12) {
        const double ratio = adaa.inharmonicPower / naive.inharmonicPower;
        if (ratio >= 1.0) {
            // Report a regression honestly rather than hiding it.
            std::printf("  ADAA/naive inharmonic ratio: %.4f  (no reduction -- ADAA regressed)\n",
                        ratio);
            std::printf("  naive inharmonic: %.6f  ADAA inharmonic: %.6f\n",
                        naive.inharmonicPower, adaa.inharmonicPower);
        } else {
            // Cap reported dB to avoid an absurd figure when ADAA inharmonic
            // power is near zero (ratio << 1 makes 1/ratio extremely large).
            const bool nearZero = (ratio < 1.0e-6);
            const double dBred  = nearZero ? 60.0 : 10.0 * std::log10(1.0 / ratio);
            if (nearZero) {
                std::printf("  ADAA/naive inharmonic ratio: %.4e  (>%.0f dB reduction)\n",
                            ratio, dBred);
            } else {
                std::printf("  ADAA/naive inharmonic ratio: %.4f  (%.1f dB reduction)\n",
                            ratio, dBred);
            }
            // Floor the bound so the stated claim is always <= the measured
            // reduction (rounding to nearest can state a bound ABOVE what
            // was measured).
            const double claimedBound = std::floor(dBred < 3.0 ? 0.0 : dBred);
            std::printf("  [ADAA reduces aliased energy by > %.0f dB for this stimulus]\n",
                        claimedBound);
        }
    } else {
        std::printf("  Naive inharmonic power negligible; no meaningful comparison.\n");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Parse --csv flag for machine-readable output.
    bool outputCSV = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--csv") == 0) {
            outputCSV = true;
            break;
        }
    }

    // Compute all per-voicing harmonic signatures (independent of output format).
    std::array<meastest::HarmonicSignature, kAllVoicings.size()> sigs;
    for (std::size_t i = 0; i < kAllVoicings.size(); ++i) {
        sigs[i] = captureVoicingSignature(kAllVoicings[i].voicing);
    }

    if (outputCSV) {
        // Machine-readable CSV: voicing, harmonic_index, frequency_hz, magnitude
        std::printf("voicing,harmonic_index,frequency_hz,magnitude\n");
        for (std::size_t i = 0; i < kAllVoicings.size(); ++i) {
            printHarmonicSignatureCSV(kAllVoicings[i].name, sigs[i]);
        }
    } else {
        // Human-readable table format (default).
        std::printf("=== acfx Saturation Lab Harness ===\n");
        std::printf("sample rate: %.0f Hz   drive: %.1f   fundamental: %.0f Hz   amplitude: %.1f\n",
                    kSampleRateD, static_cast<double>(kDrive), kFundHz, static_cast<double>(kAmplitude));
        std::printf("window: %zu measurement samples (%d integer cycles), %d warm-up samples\n\n",
                    kNumSamples, static_cast<int>(kFundHz * static_cast<double>(kNumSamples) / kSampleRateD),
                    kTransientSamples);

        std::printf("=== Per-voicing harmonic signatures (absolute Goertzel amplitudes) ===\n");
        std::printf("  (f0 = %.0f Hz; each cell is the amplitude at that harmonic)\n\n", kFundHz);
        std::printf("  %-14s |   f0    |   2f0   |   3f0   |   4f0   |   5f0   |   6f0   |\n", "voicing");
        std::printf("  %-14s +---------+---------+---------+---------+---------+---------+\n",
                    "--------------");

        for (std::size_t i = 0; i < kAllVoicings.size(); ++i) {
            printHarmonicSignature(kAllVoicings[i].name, sigs[i]);
        }

        std::printf("\n  softClip/tape/console are odd-only shapes (softKnee/tanh/arctan):\n");
        std::printf("  odd harmonics (3f0, 5f0) non-zero, even harmonics (2f0, 4f0, 6f0) ~0.\n");
        std::printf("  tubePreamp (diodeCurve) is the only asymmetric voicing: it shows\n");
        std::printf("  non-zero even harmonics (2f0, 4f0) in addition to odd content.\n");
        std::printf("  See core/labs/saturation/saturation-voicings.h for each voicing's\n");
        std::printf("  documented shape + pre-/post-emphasis rationale.\n");

        runAliasingComparison();

        std::printf("\n=== Done ===\n");
    }

    return 0;
}

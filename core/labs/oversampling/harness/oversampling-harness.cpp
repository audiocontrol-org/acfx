// oversampling-harness.cpp
// Host-only harness that drives acfx::Oversampler<Factor> and prints evidence
// to stdout:
//   (a) Transparency under identity — Oversampler<4> with an identity
//       nonlinearity, passband magnitude (Goertzel, normalized ~1.0) at a
//       handful of in-band frequencies. A linear-phase composite should
//       reproduce the input's magnitude at every passband frequency; only the
//       (constant, latencySamples()) delay should differ.
//   (b) Aliasing reduction — a self-contained hard-clip nonlinearity driven
//       naively (base rate, no oversampling) vs through Oversampler<2/4/8>,
//       using the SHARED measurement infrastructure
//       (tests/core/measurement-support.h — meastest::aliasingMeasure) per
//       the lab README's "Measured evidence" section, so this reproduces the
//       same evidence the oversampler test suites already assert against.
//   (c) Per-factor latencySamples() — the integer group delay each factor
//       reports, for host PDC.
//   (d) OPTIONAL: --csv flag for a machine-readable CSV dump of all three
//       sections (default: human-readable tables only).
//
// CONSOLIDATION NOTE (harmonic-analysis T023): Measurement routed exclusively
// through the shared host/analysis toolkit (acfx::measure::GoertzelAnalyzer +
// meastest::aliasingMeasure); no self-contained spectral engine or duplicate
// aliasing implementation exists in this harness.
//
// Mirrors core/labs/saturation/harness/saturation-harness.cpp: unlike the
// SVF/waveshaping harnesses (self-contained per the SVF-harness precedent),
// this harness drives its measurements through the shared measurement
// infrastructure rather than reimplementing a second spectral engine.  Every
// header this pulls in (measurement-support.h + support/measurement/*.h) is
// header-only, so this stays a single-TU host executable with no test-target
// linkage — only the tests/ include root (see the acfx_lab_oversampling_harness
// CMake target).
//
// Include roots: core/ and tests/. Compile with -std=c++20. May allocate,
// loop, printf. Never included by portable code (C-OS-LAB gate in
// scripts/check-portability.sh).

#include "primitives/oversampling/oversampler.h"
#include "core/measurement-support.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// meastest:: lives in namespace acfx::meastest (measurement-support.h); this
// using-directive matches the convention already used by
// core/labs/saturation/harness/saturation-harness.cpp, so `meastest::...` /
// `acfx::measure::...` / `Oversampler` resolve unqualified below.
using namespace acfx;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr double kPi          = 3.141592653589793238462643383279502884;
constexpr float  kSampleRate  = 48000.0f;
constexpr double kSampleRateD = 48000.0;

// --- Identity eval (transparency stimulus): a pure passthrough at the
// oversampled rate — the composite Oversampler<Factor>::process becomes a
// linear-phase FIR cascade with no nonlinearity, so any passband deviation
// from unity magnitude is filter ripple, not distortion. ---
constexpr auto kIdentityEval = [](float x) noexcept -> float { return x; };

// --- Self-contained hard-clip nonlinearity (aliasing stimulus): no effects/
// dependency, matches the "hard-clip/tanh nonlinearity" the task calls for.
// Scales by kAliasDrive then hard-clips to +-1, generating strong high-order
// harmonics so the naive (un-oversampled) arm produces clearly measurable
// aliasing at kAliasFundHz. ---
constexpr float kAliasDrive = 4.0f;
constexpr auto  kHardClipEval = [](float x) noexcept -> float {
    const float y = kAliasDrive * x;
    if (y > 1.0f) return 1.0f;
    if (y < -1.0f) return -1.0f;
    return y;
};

// --- Transparency stimulus: integer-cycle window at every tested frequency
// (freqHz * kTransparencyN / kSampleRateD is a whole number for every entry
// below, since each freqHz is a multiple of 100 and kTransparencyN/kSampleRateD
// == 1/10) so the Goertzel readout stays leakage-free. ---
constexpr std::size_t kTransparencyWarmup = 1000;  // >> cascade FIR length; settles fully
constexpr std::size_t kTransparencyN      = 4800;  // 100 ms @ 48 kHz
constexpr std::array<double, 6> kTransparencyFreqsHz = {
    100.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0
};

// --- Aliasing comparison stimulus (mirrors saturation-harness.cpp /
// waveshaping-harness.cpp's naive-vs-mitigated convention: f0 = 10 kHz,
// N = 4800 = 1000 integer cycles @ 48 kHz, near-full-scale amplitude). ---
constexpr double      kAliasFundHz     = 10000.0;
constexpr std::size_t kAliasN          = 4800;
constexpr float        kAliasAmplitude = 1.0f;

} // namespace

// ---------------------------------------------------------------------------
// Transparency: passband magnitude of Oversampler<4> under the identity eval.
// ---------------------------------------------------------------------------

namespace {

template <int Factor>
double measureTransparencyMagnitude(double freqHz) {
    acfx::Oversampler<Factor> os;
    os.init(kSampleRate);

    // Warm the FIR cascade's delay lines before measuring (kTransparencyWarmup
    // is far longer than the cascade's finite impulse response, so the
    // composite is fully settled by the time the measurement window starts).
    for (std::size_t i = 0; i < kTransparencyWarmup; ++i) {
        const double t = static_cast<double>(i) / kSampleRateD;
        const float  x = static_cast<float>(std::sin(2.0 * kPi * freqHz * t));
        (void)os.process(x, kIdentityEval);
    }

    std::vector<float> out(kTransparencyN);
    for (std::size_t i = 0; i < kTransparencyN; ++i) {
        const double t = static_cast<double>(kTransparencyWarmup + i) / kSampleRateD;
        const float  x = static_cast<float>(std::sin(2.0 * kPi * freqHz * t));
        out[i] = os.process(x, kIdentityEval);
    }

    // Magnitude readout is delay-insensitive (Goertzel single-bin magnitude
    // ignores phase), so the composite's constant group delay (latencySamples())
    // does not need to be compensated for here.
    return acfx::measure::GoertzelAnalyzer{freqHz, kSampleRateD}
        .analyze(acfx::span<const float>(out))
        .magnitude;
}

// ---------------------------------------------------------------------------
// Aliasing reduction: naive hard-clip vs Oversampler<Factor>-wrapped hard-clip.
// ---------------------------------------------------------------------------

meastest::AliasingMeasure measureNaiveAliasing() {
    return meastest::aliasingMeasure(
        kHardClipEval, kAliasFundHz, kSampleRateD, kAliasN, kAliasAmplitude);
}

template <int Factor>
meastest::AliasingMeasure measureOversampledAliasing() {
    acfx::Oversampler<Factor> os;
    os.init(kSampleRate);
    return meastest::aliasingMeasure(
        [&os](float x) { return os.process(x, kHardClipEval); },
        kAliasFundHz, kSampleRateD, kAliasN, kAliasAmplitude);
}

// ---------------------------------------------------------------------------
// Latency: Oversampler<Factor>::latencySamples() (base-rate group delay).
// ---------------------------------------------------------------------------

template <int Factor>
int measureLatency() {
    acfx::Oversampler<Factor> os;
    os.init(kSampleRate);
    return os.latencySamples();
}

// ---------------------------------------------------------------------------
// Printing helpers
// ---------------------------------------------------------------------------

void printAliasingRow(const char* label, const meastest::AliasingMeasure& m) {
    std::printf("  %-7s|  %8.5f  |  %8.5f  |  %9.6f\n",
                label, m.totalPower, m.harmonicPower, m.inharmonicPower);
}

// Reports the inharmonic-power reduction of `arm` relative to `naive`, using
// the same capped-dB convention as saturation-harness.cpp / waveshaping-
// harness.cpp: a regression is reported honestly rather than hidden, and the
// reported dB bound is floored so the stated claim never overstates the
// measured reduction.
void printReduction(const char* label, const meastest::AliasingMeasure& naive,
                    const meastest::AliasingMeasure& arm) {
    if (naive.inharmonicPower <= 1.0e-12) {
        std::printf("  %-7s naive inharmonic power negligible; no meaningful comparison.\n", label);
        return;
    }
    const double ratio = arm.inharmonicPower / naive.inharmonicPower;
    if (ratio >= 1.0) {
        std::printf("  %-7s inharmonic ratio vs naive: %.4f  (no reduction -- regressed)\n",
                    label, ratio);
        return;
    }
    const bool   nearZero = (ratio < 1.0e-6);
    const double dBred    = nearZero ? 60.0 : 10.0 * std::log10(1.0 / ratio);
    if (nearZero) {
        std::printf("  %-7s inharmonic ratio vs naive: %.4e  (>%.0f dB reduction)\n",
                    label, ratio, dBred);
    } else {
        std::printf("  %-7s inharmonic ratio vs naive: %.4f  (%.1f dB reduction)\n",
                    label, ratio, dBred);
    }
    const double claimedBound = std::floor(dBred < 3.0 ? 0.0 : dBred);
    std::printf("  %-7s [reduces aliased energy by > %.0f dB for this stimulus]\n",
                label, claimedBound);
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

    // --- Transparency evidence (Factor 4 with identity eval). ---
    std::array<double, kTransparencyFreqsHz.size()> transparencyMags{};
    for (std::size_t i = 0; i < kTransparencyFreqsHz.size(); ++i) {
        transparencyMags[i] = measureTransparencyMagnitude<4>(kTransparencyFreqsHz[i]);
    }

    // --- Aliasing-reduction evidence (naive vs 2x/4x/8x). ---
    const meastest::AliasingMeasure naive = measureNaiveAliasing();
    const meastest::AliasingMeasure over2 = measureOversampledAliasing<2>();
    const meastest::AliasingMeasure over4 = measureOversampledAliasing<4>();
    const meastest::AliasingMeasure over8 = measureOversampledAliasing<8>();

    // --- Latency evidence. ---
    const int lat2 = measureLatency<2>();
    const int lat4 = measureLatency<4>();
    const int lat8 = measureLatency<8>();

    if (outputCSV) {
        // Machine-readable CSV: section, factor, frequency_hz, metric, value.
        std::printf("section,factor,frequency_hz,metric,value\n");
        for (std::size_t i = 0; i < kTransparencyFreqsHz.size(); ++i) {
            std::printf("transparency,4,%.1f,magnitude,%g\n",
                        kTransparencyFreqsHz[i], transparencyMags[i]);
        }
        auto printAliasingCSV = [](const char* factorLabel, const meastest::AliasingMeasure& m) {
            std::printf("aliasing,%s,%.1f,totalPower,%g\n",      factorLabel, kAliasFundHz, m.totalPower);
            std::printf("aliasing,%s,%.1f,harmonicPower,%g\n",   factorLabel, kAliasFundHz, m.harmonicPower);
            std::printf("aliasing,%s,%.1f,inharmonicPower,%g\n", factorLabel, kAliasFundHz, m.inharmonicPower);
        };
        printAliasingCSV("naive", naive);
        printAliasingCSV("2",     over2);
        printAliasingCSV("4",     over4);
        printAliasingCSV("8",     over8);
        std::printf("latency,2,,latencySamples,%d\n", lat2);
        std::printf("latency,4,,latencySamples,%d\n", lat4);
        std::printf("latency,8,,latencySamples,%d\n", lat8);
    } else {
        std::printf("=== acfx Oversampling Lab Harness ===\n");
        std::printf("sample rate: %.0f Hz\n\n", kSampleRateD);

        std::printf("=== Transparency under identity (Oversampler<4>) ===\n");
        std::printf("  (Goertzel passband magnitude; ~1.0 == transparent, delay = latencySamples() only)\n\n");
        std::printf("  freq (Hz) |");
        for (double f : kTransparencyFreqsHz) {
            std::printf(" %8.0f |", f);
        }
        std::printf("\n  magnitude |");
        for (double m : transparencyMags) {
            std::printf(" %8.5f |", m);
        }
        std::printf("\n\n");

        std::printf("=== Naive vs oversampled aliasing comparison ===\n");
        std::printf("nonlinearity: hard-clip (drive %.1f)   f0: %.0f Hz   window: %zu samples (%d cycles)\n",
                    static_cast<double>(kAliasDrive), kAliasFundHz, kAliasN,
                    static_cast<int>(kAliasFundHz * static_cast<double>(kAliasN) / kSampleRateD));
        std::printf("Aliased images fold to non-harmonic bins when kAliasFundHz's harmonics exceed Nyquist.\n\n");

        std::printf("  arm    | totalPower | harmPower  | inharmonic\n");
        std::printf("  -------+------------+------------+------------\n");
        printAliasingRow("naive", naive);
        printAliasingRow("2x",    over2);
        printAliasingRow("4x",    over4);
        printAliasingRow("8x",    over8);
        std::printf("\n");
        printReduction("2x", naive, over2);
        printReduction("4x", naive, over4);
        printReduction("8x", naive, over8);

        std::printf("\n=== Per-factor latency (latencySamples(), base-rate samples) ===\n");
        std::printf("  Factor 2: %3d   Factor 4: %3d   Factor 8: %3d\n", lat2, lat4, lat8);
        std::printf("  (analytic composite group delay = 90 * (1 - 1/Factor) base samples; see\n");
        std::printf("  core/primitives/oversampling/oversampler.h for the derivation.)\n");

        std::printf("\n=== Done ===\n");
    }

    return 0;
}

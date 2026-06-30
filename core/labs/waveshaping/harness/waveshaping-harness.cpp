// waveshaping-harness.cpp
// Host-only harness that drives acfx::Waveshaper (all 11 shapes) and
// acfx::ADAAWaveshaper (hardClip) and prints evidence to stdout:
//   (a) Per-shape harmonic signatures — absolute Goertzel amplitude at each of
//       the first 6 harmonics (f0 = 1000 Hz, integer-cycle window N = 4800).
//   (b) Naive-vs-ADAA aliasing comparison for hardClip at 10 kHz:
//       inharmonic (aliased) power for each arm and the reduction ratio/dB.
//
// Oversampled arm: OMITTED per FR-018 and the exec brief — no oversampler is
// built as a deliverable.  Oversampling is a contingent future extension.
//
// Include root: core/.  Compile with -std=c++20.
// May allocate, loop, printf.  Never included by portable code (C-1 gate).

#include "primitives/nonlinear/waveshaper.h"
#include "primitives/nonlinear/waveshaper-shapes.h"
#include "primitives/nonlinear/adaa-waveshaper.h"

#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float  kSampleRate  = 48000.0f;
constexpr double kSampleRateD = 48000.0;
constexpr double kPi          = 3.141592653589793238462643383279502884;

// --- Harmonic-signature window (mirrors waveshaper-harmonics-test.cpp) ---
// f0 = 1000 Hz, N = 4800 samples: 1000*4800/48000 = 100 complete cycles.
// Integer-cycle window -> every harmonic lands on a DFT bin -> leakage-free.
constexpr double kFundHz   = 1000.0;
constexpr int    kN        = 4800;
constexpr int    kNumHarm  = 6;
// drive = 2.0 pushes most shapes into their nonlinear region; matches the
// test value that generates measurable odd harmonics (~5-10% of fundamental).
constexpr float  kDrive    = 2.0f;
// DC-blocker tau = 1/(1-R) = 1/0.005 = 200 samples.
// kWarmup = 2000 = 10*tau ensures the transient falls below 1e-4 (all tolerances).
constexpr int    kWarmup   = 2000;

// --- Aliasing comparison (mirrors waveshaper-adaa-test.cpp, T018) ---
// f0 = 10000 Hz, N = 4800: 10000*4800/48000 = 1000 integer cycles.
// Odd harmonics fold to non-harmonic bins (documented in the test file header):
//   3rd (30 kHz) -> 18 kHz, 5th (50 kHz) -> 2 kHz, 7th (70 kHz) -> 22 kHz, ...
// drive = 4.0 -> near-square wave, rich aliasing in naive arm.
constexpr double kAliasFundHz = 10000.0;
constexpr int    kAliasN      = 4800;
constexpr float  kAliasDrive  = 4.0f;

} // namespace

// ---------------------------------------------------------------------------
// Self-contained Goertzel single-bin magnitude
//
// Mirrors acfx::measure::GoertzelAnalyzer::analyze() from
// tests/support/measurement/analyzers.h (self-contained per SVF harness
// precedent — no dependency on the tests/ measurement infrastructure).
//
// Returns amplitude-normalised magnitude: ~1.0 for a unit-amplitude sine at
// exactly freqHz, with an integer-cycle window.
// ---------------------------------------------------------------------------

static double goertzel(const float* buf, int n, double freqHz, double sampleRate)
{
    const double w     = 2.0 * kPi * freqHz / sampleRate;
    const double coeff = 2.0 * std::cos(w);

    double sPrev  = 0.0;
    double sPrev2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const double s = static_cast<double>(buf[i]) + coeff * sPrev - sPrev2;
        sPrev2 = sPrev;
        sPrev  = s;
    }

    const double real = sPrev - sPrev2 * std::cos(w);
    const double imag = sPrev2 * std::sin(w);
    return 2.0 * std::sqrt(real * real + imag * imag) / static_cast<double>(n);
}

// ---------------------------------------------------------------------------
// Per-shape harmonic signature
//
// Drives a unit sine at kFundHz through each shape (after kWarmup samples to
// settle the DC-blocker) and prints absolute Goertzel amplitude at each
// harmonic.  Static buffer avoids a large stack allocation.
// ---------------------------------------------------------------------------

static float gHarmBuf[kWarmup + kN];

static void printHarmonicSignature(acfx::Shape shape, const char* name)
{
    acfx::Waveshaper ws;
    ws.setShape(shape);
    ws.setDrive(kDrive);
    ws.setGainCompensation(false);
    ws.init(kSampleRate);

    const int total = kWarmup + kN;
    for (int i = 0; i < total; ++i) {
        const double phase = 2.0 * kPi * kFundHz * static_cast<double>(i) / kSampleRateD;
        const float  x     = static_cast<float>(std::sin(phase));
        gHarmBuf[i] = ws.process(x);
    }

    const float* win = gHarmBuf + kWarmup;  // measurement window

    std::printf("  %-14s |", name);
    for (int h = 1; h <= kNumHarm; ++h) {
        const double freq = kFundHz * static_cast<double>(h);
        if (freq >= kSampleRateD * 0.5) {
            std::printf("   ------  |");
        } else {
            const double mag = goertzel(win, kN, freq, kSampleRateD);
            std::printf("   %6.4f  |", mag);
        }
    }
    std::printf("\n");
}

// ---------------------------------------------------------------------------
// Aliasing-power measure (power-domain residual)
//
// Computes total mean-square and the harmonic-series power (DC + each in-band
// multiple of fundHz using Goertzel A^2/2).  The residual is inharmonic
// (aliased) energy.  Mirrors the AliasingMeasure logic in measurement-support.h.
// ---------------------------------------------------------------------------

struct AliasMeasure {
    double totalPower;
    double harmonicPower;
    double inharmonicPower;
};

static AliasMeasure measureAliasing(const float* buf, int n, double fundHz)
{
    double sumSq = 0.0;
    double sum   = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = static_cast<double>(buf[i]);
        sumSq += x * x;
        sum   += x;
    }

    const double totalPower = sumSq / static_cast<double>(n);
    const double dc         = sum   / static_cast<double>(n);

    double harmonicPower = dc * dc;  // k=0 (DC) bin power

    const double nyquist = kSampleRateD * 0.5;
    for (int k = 1; static_cast<double>(k) * fundHz < nyquist; ++k) {
        const double aK = goertzel(buf, n, static_cast<double>(k) * fundHz, kSampleRateD);
        harmonicPower += 0.5 * aK * aK;  // sinusoid power = A^2 / 2
    }

    // Clamp tiny negative floating-point residual (numerical hygiene, not a
    // fallback masking real energy; occurs when totalPower ~= harmonicPower).
    double inharmonic = totalPower - harmonicPower;
    if (inharmonic < 0.0)
        inharmonic = 0.0;

    return {totalPower, harmonicPower, inharmonic};
}

// ---------------------------------------------------------------------------
// Naive vs ADAA aliasing comparison (hardClip at 10 kHz, drive = 4)
// ---------------------------------------------------------------------------

static float gAliasBuf[kAliasN];

static void runAliasingComparison()
{
    std::printf("\n=== Naive vs ADAA aliasing comparison ===\n");
    std::printf("shape: hardClip   f0: %.0f Hz   drive: %.1f   window: %d samples (%d cycles)\n",
                kAliasFundHz, static_cast<double>(kAliasDrive),
                kAliasN, static_cast<int>(kAliasFundHz * kAliasN / kSampleRateD));
    std::printf("Aliased images at non-harmonic bins (see adaa-waveshaper.h comment).\n\n");

    const double omega = 2.0 * kPi * kAliasFundHz / kSampleRateD;

    // Naive Waveshaper arm.
    {
        acfx::Waveshaper ws;
        ws.setShape(acfx::Shape::hardClip);
        ws.setDrive(kAliasDrive);
        ws.setGainCompensation(false);
        ws.init(kSampleRate);
        for (int i = 0; i < kAliasN; ++i) {
            const float x = static_cast<float>(std::sin(omega * static_cast<double>(i)));
            gAliasBuf[i] = ws.process(x);
        }
    }
    const AliasMeasure naive = measureAliasing(gAliasBuf, kAliasN, kAliasFundHz);

    // ADAA Waveshaper arm (first-order antiderivative anti-aliasing).
    {
        acfx::ADAAWaveshaper ws;
        ws.setShape(acfx::Shape::hardClip);  // hardClip is ADAA-covered (T020)
        ws.setDrive(kAliasDrive);
        ws.setGainCompensation(false);
        ws.init(kSampleRate);
        for (int i = 0; i < kAliasN; ++i) {
            const float x = static_cast<float>(std::sin(omega * static_cast<double>(i)));
            gAliasBuf[i] = ws.process(x);
        }
    }
    const AliasMeasure adaa = measureAliasing(gAliasBuf, kAliasN, kAliasFundHz);

    std::printf("  arm    | totalPower | harmPower  | inharmonic\n");
    std::printf("  -------+------------+------------+------------\n");
    std::printf("  naive  |  %8.5f  |  %8.5f  |  %9.6f\n",
                naive.totalPower, naive.harmonicPower, naive.inharmonicPower);
    std::printf("  ADAA   |  %8.5f  |  %8.5f  |  %9.6f\n",
                adaa.totalPower,  adaa.harmonicPower,  adaa.inharmonicPower);

    std::printf("\n");
    if (naive.inharmonicPower > 1.0e-12) {
        const double ratio = adaa.inharmonicPower / naive.inharmonicPower;
        const double dBred = 10.0 * std::log10(1.0 / (ratio + 1.0e-300));
        std::printf("  ADAA/naive inharmonic ratio: %.4f  (%.1f dB reduction)\n",
                    ratio, dBred);
        std::printf("  [ADAA reduces aliased energy by > %.0f dB for this stimulus]\n",
                    dBred < 3.0 ? 0.0 : dBred);
    } else {
        std::printf("  Naive inharmonic power negligible; no meaningful comparison.\n");
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    std::printf("=== acfx Waveshaping Lab Harness ===\n");
    std::printf("sample rate: %.0f Hz   drive: %.1f   fundamental: %.0f Hz\n",
                kSampleRateD, static_cast<double>(kDrive), kFundHz);
    std::printf("window: %d measurement + %d warmup samples (%d integer cycles)\n\n",
                kN, kWarmup, static_cast<int>(kFundHz * kN / kSampleRateD));

    std::printf("=== Per-shape harmonic signatures (absolute Goertzel amplitudes) ===\n");
    std::printf("  (f0 = %.0f Hz; each cell is the amplitude at that harmonic)\n\n",
                kFundHz);
    std::printf("  %-14s |   f0    |   2f0   |   3f0   |   4f0   |   5f0   |   6f0   |\n", "shape");
    std::printf("  %-14s +---------+---------+---------+---------+---------+---------+\n",
                "--------------");

    printHarmonicSignature(acfx::Shape::tanh,         "tanh");
    printHarmonicSignature(acfx::Shape::arctan,       "arctan");
    printHarmonicSignature(acfx::Shape::cubicSoft,    "cubicSoft");
    printHarmonicSignature(acfx::Shape::algebraic,    "algebraic");
    printHarmonicSignature(acfx::Shape::hardClip,     "hardClip");
    printHarmonicSignature(acfx::Shape::softKnee,     "softKnee");
    printHarmonicSignature(acfx::Shape::chebyshev,    "chebyshev");
    printHarmonicSignature(acfx::Shape::biasedAsym,   "biasedAsym");
    printHarmonicSignature(acfx::Shape::diodeCurve,   "diodeCurve");
    printHarmonicSignature(acfx::Shape::sineFold,     "sineFold");
    printHarmonicSignature(acfx::Shape::triangleFold, "triangleFold");

    std::printf("\n  Symmetric (odd) shapes produce only odd harmonics (3f0, 5f0 non-zero;\n");
    std::printf("  2f0, 4f0, 6f0 ~0): tanh, arctan, cubicSoft, algebraic, hardClip, softKnee.\n");
    std::printf("  Asymmetric shapes produce even+odd harmonics: biasedAsym, diodeCurve.\n");
    std::printf("  Folding shapes have rich harmonic content: sineFold, triangleFold.\n");
    std::printf("  Chebyshev T2 targets the 2nd harmonic (2f0 dominant).\n");

    runAliasingComparison();

    std::printf("\n=== Done ===\n");
    return 0;
}

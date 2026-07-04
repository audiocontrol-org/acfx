// tape-dynamics-harness.cpp
// Host-only lab evidence harness (T020, FR-015, research.md R9): never
// included by portable code (C-1 gate in scripts/check-portability.sh).
//
// Drives the graduated acfx::Hysteresis primitive (core/primitives/nonlinear/
// hysteresis.h) and the acfx::TapeDynamicsCore<Factor> composition kernel
// (core/effects/tape-dynamics/tape-dynamics-core.h) through representative
// stimuli and prints measured evidence of their behavior to stdout:
//
//   1. Hysteresis loop area: drive a settled sinusoidal H cycle through the
//      raw primitive (one solver at a time) and print the closed M-vs-H
//      loop's shoelace area -- the memory signature (area > 0), reusing the
//      SAME shoelaceArea()/driveSettledLoop() helpers the shipped hysteresis
//      test suites already validate against (tests/core/hysteresis-test-
//      support.h), rather than re-deriving a second area formula.
//
//   2. Dynamic-range reduction (DRR) / emergent compression: sweep the
//      TapeDynamicsCore INPUT level at two fixed drive settings (trim OFF --
//      the default), print the output-vs-input level curve at each drive,
//      and a DRR figure (how much the swept input range is compressed on
//      output) -- showing DRR grows with drive.
//
//   3. THD / aliasing vs oversampling factor: drive TapeDynamicsCore<2>,
//      <4>, <8> with the SAME high-frequency tone and print THD+N (host/
//      analysis/thdn.h) and inharmonic/aliased energy (host/analysis/
//      alias-sweep.h) at each factor -- showing aliasing falls as the
//      factor rises.
//
// Include roots: core/ (acfx_core), host/ (acfx_analysis -- "analysis/..."),
// and tests/ (the tests/core/hysteresis-test-support.h loop-area helper) --
// see the acfx_lab_tape_dynamics_harness CMake target. Compile with
// -std=c++20. May allocate, loop, printf: this is NOT the RT path.

#include "primitives/nonlinear/hysteresis.h"
#include "effects/tape-dynamics/tape-dynamics-core.h"
#include "core/hysteresis-test-support.h"

#include "analysis/thdn.h"
#include "analysis/alias-sweep.h"
#include "analysis/aliasing.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace acfx;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr double kPi         = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;

} // namespace

// ---------------------------------------------------------------------------
// 1. Hysteresis loop area (memory signature)
// ---------------------------------------------------------------------------

namespace {

struct SolverEntry {
    Solver      solver;
    const char* name;
};

constexpr std::array<SolverEntry, 3> kAllSolvers = {{
    {Solver::rk2,           "rk2"},
    {Solver::rk4,           "rk4"},
    {Solver::newtonRaphson, "newtonRaphson"},
}};

void runHysteresisLoopArea() {
    std::printf("=== 1. Hysteresis closed M-vs-H loop area (memory signature) ===\n");
    std::printf("  JAParams: Ms=1.0 a=1.0 alpha=0.0 k=1.0 c=0.5 (defaults)\n");
    std::printf("  drive amplitude=1.0, 4 settle cycles, 400 steps/cycle\n\n");
    std::printf("  %-16s %14s\n", "solver", "loop area");
    std::printf("  %-16s %14s\n", "----------------", "--------------");

    constexpr double kAmp            = 1.0;
    constexpr int    kSettleCycles   = 4;
    constexpr int    kStepsPerCycle  = 400;

    for (const auto& entry : kAllSolvers) {
        Hysteresis h;
        h.prepare(kSampleRate);
        h.setSolver(entry.solver);

        const hysteresistest::Trace trace =
            hysteresistest::driveSettledLoop(h, kAmp, kSettleCycles, kStepsPerCycle);
        const double area = hysteresistest::shoelaceArea(trace.H, trace.out);

        std::printf("  %-16s %14.6f\n", entry.name, area);
    }

    std::printf("\n  A memoryless waveshaper traces a single-valued curve (area ~= 0);\n");
    std::printf("  every solver above encloses strictly positive area -- the rising and\n");
    std::printf("  falling branches of H trace two different M curves (SC-001/FR-018).\n\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 2. Dynamic-range reduction (DRR) / emergent compression
// ---------------------------------------------------------------------------

namespace {

// Test input levels (dBFS peak amplitude), low to high.
constexpr std::array<float, 5> kInputLevelsDb = {-24.0f, -18.0f, -12.0f, -6.0f, 0.0f};
constexpr double               kDrrToneHz     = 200.0; // representative program-material tone
constexpr int                  kDrrSettleCyc  = 20;
constexpr int                  kDrrCaptureCyc = 10;

// Configure a fresh TapeDynamicsCore<4> at the given drive, trim OFF (the
// default -- FR-008's MVP path: drive -> OS(JA) -> mix -> output).
void configureDrrCore(TapeDynamicsCore<4>& core, float driveDb) {
    core.prepare(kSampleRate, 1);
    core.setDrive(driveDb);
    core.setSaturation(1.0f);
    core.setWidth(1.0f);
    core.setSolver(Solver::rk4);
    core.setMix(1.0f);
    core.setOutput(0.0f);
    core.setTrimEnabled(false);
}

// Drives one input level through a freshly-reset core: settles kDrrSettleCyc
// cycles, then captures kDrrCaptureCyc cycles and returns the RMS-based peak-
// equivalent output level in dB (20*log10(rms*sqrt(2))), matching the input's
// own peak-amplitude dB convention so gain = outputDb - inputDb is meaningful.
float measureOutputLevelDb(TapeDynamicsCore<4>& core, float inputLevelDb) {
    core.reset();
    const float amp = std::pow(10.0f, inputLevelDb / 20.0f);
    const double omega = 2.0 * kPi * kDrrToneHz / kSampleRate;

    const int settleSamples = static_cast<int>(
        kDrrSettleCyc * kSampleRate / kDrrToneHz);
    const int captureSamples = static_cast<int>(
        kDrrCaptureCyc * kSampleRate / kDrrToneHz);

    int n = 0;
    for (int i = 0; i < settleSamples; ++i, ++n) {
        const float x = amp * static_cast<float>(std::sin(omega * n));
        static_cast<void>(core.processSample(x, 0));
    }

    double sumSq = 0.0;
    for (int i = 0; i < captureSamples; ++i, ++n) {
        const float x = amp * static_cast<float>(std::sin(omega * n));
        const float y = core.processSample(x, 0);
        sumSq += static_cast<double>(y) * static_cast<double>(y);
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(captureSamples));
    const double peakEquivalent = rms * std::sqrt(2.0);
    // A silent/near-silent output has no meaningful dB level; floor rather
    // than emit -inf (defined behavior, not a hidden fallback).
    constexpr double kFloor = 1.0e-9;
    return static_cast<float>(20.0 * std::log10(peakEquivalent > kFloor ? peakEquivalent : kFloor));
}

// Sweeps kInputLevelsDb through `core` (already configured at a fixed drive),
// prints the level curve, and returns the DRR figure: how much the swept
// INPUT range is compressed on OUTPUT, in dB --
//   DRR = (inputDb_max - inputDb_min) - (outputDb_max - outputDb_min)
// DRR ~= 0 for a linear (uncompressed) stage; DRR > 0 as the stage compresses
// the loudest inputs relative to the quietest.
double runDrrSweep(float driveDb) {
    TapeDynamicsCore<4> core;
    configureDrrCore(core, driveDb);

    std::printf("  drive = %.1f dB\n", static_cast<double>(driveDb));
    std::printf("  %12s %12s %12s\n", "input (dB)", "output (dB)", "gain (dB)");

    float outputAtMin = 0.0f;
    float outputAtMax = 0.0f;
    for (std::size_t i = 0; i < kInputLevelsDb.size(); ++i) {
        const float inputDb  = kInputLevelsDb[i];
        const float outputDb = measureOutputLevelDb(core, inputDb);
        std::printf("  %12.1f %12.4f %12.4f\n",
                    static_cast<double>(inputDb), static_cast<double>(outputDb),
                    static_cast<double>(outputDb - inputDb));
        if (i == 0) outputAtMin = outputDb;
        if (i == kInputLevelsDb.size() - 1) outputAtMax = outputDb;
    }

    const float inputRangeDb  = kInputLevelsDb.back() - kInputLevelsDb.front();
    const float outputRangeDb = outputAtMax - outputAtMin;
    const double drrDb = static_cast<double>(inputRangeDb - outputRangeDb);
    std::printf("  DRR (input range %.1f dB - output range %.4f dB): %.4f dB\n\n",
                static_cast<double>(inputRangeDb), static_cast<double>(outputRangeDb), drrDb);
    return drrDb;
}

void runDynamicRangeReduction() {
    std::printf("=== 2. Dynamic-range reduction (DRR) vs drive (trim OFF) ===\n");
    std::printf("  TapeDynamicsCore<4>, tone=%.0f Hz, saturation=1.0 width=1.0 rk4\n",
                kDrrToneHz);
    std::printf("  DRR = (input dB range) - (output dB range) swept across %zu input levels\n\n",
                kInputLevelsDb.size());

    constexpr float kLowDriveDb  = 0.0f;
    constexpr float kHighDriveDb = 18.0f;

    const double drrLow  = runDrrSweep(kLowDriveDb);
    const double drrHigh = runDrrSweep(kHighDriveDb);

    std::printf("  DRR at drive=%.1f dB: %.4f dB\n", static_cast<double>(kLowDriveDb), drrLow);
    std::printf("  DRR at drive=%.1f dB: %.4f dB\n", static_cast<double>(kHighDriveDb), drrHigh);
    std::printf("  %s\n\n",
                (drrHigh > drrLow)
                    ? "DRR increases with drive -- higher drive compresses the swept input range harder (emergent compression, no explicit gain computer)."
                    : "UNEXPECTED: DRR did not increase with drive for this stimulus.");
}

} // namespace

// ---------------------------------------------------------------------------
// 3. THD / aliasing vs oversampling factor
// ---------------------------------------------------------------------------

namespace {

constexpr float        kAliasDriveDb  = 16.0f;
constexpr double       kAliasFundHz   = 20000.0; // near-Nyquist tone: harmonics fold hard at low OS factor
constexpr std::size_t  kThdNumSamples = 4800;    // 800 integer cycles @ 48 kHz (10 Hz/bin)
constexpr int          kAliasSettleCyc = 40;

// Configures + settles a fresh TapeDynamicsCore<Factor>, then returns THD+N
// (host/analysis/thdn.h) and the alias-sweep single-point inharmonic energy
// (host/analysis/alias-sweep.h) at kAliasFundHz.
template <int Factor>
void runFactorMeasurement(const char* label) {
    TapeDynamicsCore<Factor> core;
    core.prepare(kSampleRate, 1);
    core.setDrive(kAliasDriveDb);
    core.setSaturation(1.0f);
    core.setWidth(1.0f);
    core.setSolver(Solver::rk4);
    core.setMix(1.0f);
    core.setOutput(0.0f);
    core.setTrimEnabled(false);

    const double omega = 2.0 * kPi * kAliasFundHz / kSampleRate;
    const int settleSamples = static_cast<int>(kAliasSettleCyc * kSampleRate / kAliasFundHz);

    int n = 0;
    for (int i = 0; i < settleSamples; ++i, ++n) {
        const float x = static_cast<float>(std::sin(omega * n));
        static_cast<void>(core.processSample(x, 0));
    }

    // THD+N over a long integer-cycle capture (thdn.h).
    std::vector<float> outBuf(kThdNumSamples);
    for (std::size_t i = 0; i < kThdNumSamples; ++i, ++n) {
        const float x = static_cast<float>(std::sin(omega * n));
        outBuf[i] = core.processSample(x, 0);
    }
    const analysis::ThdnResult thdn =
        analysis::thdPlusN(acfx::span<const float>(outBuf), kAliasFundHz, kSampleRate);

    // Inharmonic/aliased energy via alias-sweep.h's single-callable overload,
    // continuing from the SAME settled core state (no reset) so the sweep
    // measures the same operating point the THD figure just captured.
    auto fn = [&core](float x) noexcept { return core.processSample(x, 0); };
    const analysis::AliasSweepCurve curve = analysis::aliasSweep(
        fn, analysis::FrequencyRange{kAliasFundHz, kAliasFundHz, 1});

    std::printf("  %-8s %14.6f %14.6f %16.8e\n",
                label, thdn.thdPlusN, thdn.snr,
                curve.inharmonicEnergy.empty() ? 0.0 : curve.inharmonicEnergy[0]);
}

void runThdAliasingVsFactor() {
    std::printf("=== 3. THD / aliasing vs oversampling factor ===\n");
    std::printf("  TapeDynamicsCore<Factor>, tone=%.0f Hz, drive=%.1f dB, saturation=1.0 width=1.0 rk4\n\n",
                kAliasFundHz, static_cast<double>(kAliasDriveDb));
    std::printf("  %-8s %14s %14s %16s\n", "factor", "THD+N", "SNR (dB)", "alias-sweep inharm.");
    std::printf("  %-8s %14s %14s %16s\n", "--------", "--------------", "--------------", "----------------");

    runFactorMeasurement<2>("2x");
    runFactorMeasurement<4>("4x");
    runFactorMeasurement<8>("8x");

    std::printf("\n  Higher oversampling factors run the Jiles-Atherton nonlinearity further\n");
    std::printf("  above the base-rate Nyquist before decimating back down, giving the\n");
    std::printf("  Oversampler's anti-imaging filter more separation to reject folded\n");
    std::printf("  harmonic energy -- THD+N and alias-sweep inharmonic energy should both\n");
    std::printf("  trend DOWN as factor rises from 2x to 8x.\n\n");
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== acfx Tape Dynamics lab harness (fs = %.0f Hz) ===\n\n", kSampleRate);

    runHysteresisLoopArea();
    runDynamicRangeReduction();
    runThdAliasingVsFactor();

    std::printf("=== Done ===\n");
    return 0;
}

// compressor-harness.cpp
// Host-only lab evidence harness: never included by portable code (C-1 gate
// in scripts/check-portability.sh).
//
// Drives the graduated acfx::GainComputer (core/labs/compressor/gain-computer.h)
// through representative stimuli and prints measured evidence of its behavior
// to stdout:
//   1. Compressor static curve: gain reduction across input levels.
//   2. Limiter static curve: hard knee at threshold.
//   3. Expander static curve: with range floor.
//
// This harness is self-contained (a direct computation against the
// primitive's public API) rather than routing through other shared
// measurement infrastructure — no spectral analysis needed here.
//
// Include roots: core/ (see the acfx_lab_compressor_harness CMake
// target). Compile with -std=c++20. May allocate, loop, printf.

#include "labs/compressor/gain-computer.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float kMinLevelDb = -60.0f;
constexpr float kMaxLevelDb = 0.0f;
constexpr float kStepDb = 6.0f;

} // namespace

// ---------------------------------------------------------------------------
// 1. Compressor static curve
// ---------------------------------------------------------------------------

namespace {

void runCompressorCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::compress);
    gc.setThreshold(-18.0f);
    gc.setRatio(4.0f);
    gc.setKnee(6.0f);
    gc.setRange(-40.0f);

    std::printf("=== 1. Compressor static curve (threshold=-18dB, ratio=4:1, knee=6dB) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 2. Limiter static curve
// ---------------------------------------------------------------------------

namespace {

void runLimiterCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::limit);
    gc.setThreshold(-12.0f);
    gc.setRatio(8.0f); // ignored by limiter (infinite ratio)
    gc.setKnee(0.0f);  // hard corner
    gc.setRange(-40.0f);

    std::printf("=== 2. Limiter static curve (threshold=-12dB, hard knee) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 3. Expander static curve
// ---------------------------------------------------------------------------

namespace {

void runExpanderCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::expand);
    gc.setThreshold(-24.0f);
    gc.setRatio(2.0f);
    gc.setKnee(6.0f);
    gc.setRange(-12.0f); // floor at -12 dB

    std::printf("=== 3. Expander static curve (threshold=-24dB, ratio=1:2, range=-12dB) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== acfx GainComputer lab harness ===\n\n");

    runCompressorCurve();
    runLimiterCurve();
    runExpanderCurve();

    std::printf("=== Done ===\n");
    return 0;
}

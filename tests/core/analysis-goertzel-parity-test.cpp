// analysis-goertzel-parity-test.cpp
// T012 -- harmonic-analysis feature, User Story 1: RED test for the RETAINED
// exact single-bin Goertzel known-bin path (contracts/analysis-engine-api.md
// "retained exact known-bin path (FR-007/010)"; FR-007, FR-010, US1).
//
// The design's key point (research.md, spec FR-010): the windowed FFT is for
// BROADBAND/breadth; the EXACT, leakage-free single-bin Goertzel is RETAINED
// for known-bin regression tests. The FFT MUST NOT replace Goertzel in the
// known-bin path. This test asserts the engine's `acfx::analysis::goertzelBin`
// entry point is a THIN WRAPPER over the shipped
// `acfx::measure::GoertzelAnalyzer` (analyzers.h) -- not a re-derivation, and
// not routed through the windowed FFT:
//
//   - PARITY: goertzelBin(in, freqHz, sampleRate) returns EXACTLY (within a
//     tiny epsilon -- bit-for-bit modulo double rounding through the call
//     boundary) the same amplitude as calling
//     GoertzelAnalyzer{freqHz, sampleRate}.analyze(in) directly on the same
//     buffer. Any re-derivation (e.g. reading the value off the windowed FFT
//     spectrum instead) would disagree at the leakage/coherent-gain level,
//     far beyond this epsilon.
//   - KNOWN-BIN ACCURACY: on an integer-cycle pure tone of known amplitude at
//     a known bin, goertzelBin reproduces that amplitude within a tight
//     leakage-free analytic tolerance.
//   - ABSENT BIN: at a different integer-cycle bin where no energy was
//     injected, goertzelBin returns ~0.
//
// host/analysis/analysis-engine.h does not export goertzelBin yet at RED
// time -- this test is expected to FAIL TO BUILD until T013 lands it. Do NOT
// implement goertzelBin to make this test pass; that is a separate task.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "analysis/analysis-engine.h"  // UNDER TEST: acfx::analysis::goertzelBin (does not exist at RED time)
#include "analysis/analyzers.h"        // acfx::measure::GoertzelAnalyzer -- the shipped reference implementation
#include "analysis/stimulus.h"         // acfx::measure::SineGenerator
#include "dsp/span.h"

using acfx::measure::GoertzelAnalyzer;
using acfx::measure::SineGenerator;

namespace {

// Parity epsilon: goertzelBin must agree with a direct GoertzelAnalyzer call
// on the identical buffer to within double-rounding noise, proving it is the
// SAME computation reached through a thin wrapper -- not a re-derivation.
constexpr double kParityEpsilon = 1.0e-12;

// Leakage-free known-bin tolerance: an integer-cycle pure tone's Goertzel
// readout at its own bin matches its analytic amplitude to within float
// round-trip + double-accumulation error, several orders of magnitude
// tighter than any windowed/leakage-affected measurement. 1e-4 is generous
// relative to that floor yet still fails a wrong scale factor or a
// mislocated bin.
constexpr double kKnownBinTolerance = 1.0e-4;

// Absent-bin ceiling: at an integer-cycle bin carrying no injected energy,
// the exact (rectangular, unwindowed) Goertzel readout is leakage-free, so
// only float round-trip residual remains -- several orders of magnitude
// below any real signal.
constexpr double kAbsentBinCeiling = 1.0e-4;

} // namespace

TEST_CASE("goertzelBin: parity with direct GoertzelAnalyzer on the same buffer (FR-007/010, US1)") {
    // Integer-cycle: f=1000, fs=48000, N=4800 => 100 cycles of the fundamental.
    constexpr std::size_t kN          = 4800;
    constexpr double      kFreqHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr float       kAmplitude  = 0.75f;

    std::vector<float> in(kN, 0.0f);
    SineGenerator{kFreqHz, kSampleRate, kAmplitude, 0.0}.fill(acfx::span<float>{in});

    const double viaEngine =
        acfx::analysis::goertzelBin(acfx::span<const float>(in), kFreqHz, kSampleRate);
    const double viaDirect =
        GoertzelAnalyzer{kFreqHz, kSampleRate}.analyze(acfx::span<const float>(in)).magnitude;

    // Exact parity (within double-rounding epsilon): the retained path is a
    // thin wrapper over the shipped analyzer, not a re-derivation.
    CHECK(std::abs(viaEngine - viaDirect) < kParityEpsilon);
}

TEST_CASE("goertzelBin: integer-cycle known-amplitude tone -> leakage-free known-bin amplitude (FR-007, US1)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kFreqHz     = 1000.0;
    constexpr double      kSampleRate = 48000.0;
    constexpr float       kAmplitude  = 0.75f;

    std::vector<float> in(kN, 0.0f);
    SineGenerator{kFreqHz, kSampleRate, kAmplitude, 0.0}.fill(acfx::span<float>{in});

    const double magnitude =
        acfx::analysis::goertzelBin(acfx::span<const float>(in), kFreqHz, kSampleRate);

    CHECK(magnitude == doctest::Approx(static_cast<double>(kAmplitude)).epsilon(kKnownBinTolerance));
}

TEST_CASE("goertzelBin: non-present integer-cycle bin -> ~0 (leakage-free, FR-007, US1)") {
    constexpr std::size_t kN          = 4800;
    constexpr double      kToneHz     = 1000.0;   // 100 cycles over kN @ kSampleRate
    constexpr double      kAbsentHz   = 3000.0;   // 300 cycles -- a different, uninjected bin
    constexpr double      kSampleRate = 48000.0;
    constexpr float       kAmplitude  = 0.75f;

    std::vector<float> in(kN, 0.0f);
    SineGenerator{kToneHz, kSampleRate, kAmplitude, 0.0}.fill(acfx::span<float>{in});

    const double magnitude =
        acfx::analysis::goertzelBin(acfx::span<const float>(in), kAbsentHz, kSampleRate);

    CHECK(magnitude < kAbsentBinCeiling);
}

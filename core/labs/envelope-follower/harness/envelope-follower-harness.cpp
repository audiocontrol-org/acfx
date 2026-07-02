// envelope-follower-harness.cpp
// Host-only diagnostic harness that exercises acfx::EnvelopeFollower through
// its public API: init, configuration setters (mode/ballistics/domain/
// attack/release/hold), and process() on a short signal stream.
//
// This is a stub; a later task fills in real measurement infrastructure
// (e.g., envelope capture, steady-state analysis) and real evidence emission
// (modeled after saturation-harness.cpp's measurement-support.h integration).
//
// Unlike core/labs/saturation/harness/saturation-harness.cpp (which drives
// measurements through tests/core/measurement-support.h), this harness is
// currently self-contained and prints a single diagnostic line. A future task
// will integrate with the shared measurement infrastructure.
//
// Include roots: core/ and tests/ (see the acfx_lab_envelope_follower_harness
// CMake target). Compile with -std=c++20. May allocate, loop, printf. Never
// included by portable code (C-1 gate in scripts/check-portability.sh).

#include "primitives/dynamics/envelope-follower.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr int   kNumSamples = 480;  // 10 ms @ 48 kHz

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // Construct and initialize the envelope follower.
    acfx::EnvelopeFollower follower;
    follower.init(kSampleRate);

    // Configure: peak detection, branching ballistics, linear domain,
    // 10 ms attack, 100 ms release.
    follower.setMode(acfx::DetectMode::peak);
    follower.setBallistics(acfx::Ballistics::branching);
    follower.setDomain(acfx::DetectDomain::linear);
    follower.setAttack(0.010f);
    follower.setRelease(0.100f);

    // Process a short ramp (0.0 -> 1.0 -> 0.0) and accumulate envelope output.
    float envelope_sum = 0.0f;
    constexpr float kHalf = static_cast<float>(kNumSamples) / 2.0f;
    for (int i = 0; i < kNumSamples; ++i) {
        // Simple ramp: rise for first half, fall for second half.
        float x = (i < kNumSamples / 2) ?
                      static_cast<float>(i) / kHalf :
                      1.0f - (static_cast<float>(i) - kHalf) / kHalf;
        float env = follower.process(x);
        envelope_sum += env;
    }

    std::printf("envelope-follower harness stub (sum=%.4f)\n", envelope_sum);

    return 0;
}

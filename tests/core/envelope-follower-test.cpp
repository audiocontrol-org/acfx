#include <doctest/doctest.h>

#include <cmath>

#include "primitives/dynamics/envelope-follower.h"

// T009 (US1) — acfx::EnvelopeFollower interface contract + edge-case tests.
//
// Written against specs/envelope-followers/contracts/envelope-follower-api.md
// and specs/envelope-followers/spec.md (US1, Edge Cases, FR-018, SC-008).
// The branching/decoupled smoothers and RMS/peakHold detectors may still be
// stubs at the time this file lands (see envelope-follower.h TODOs); these
// cases focus on invariants that must hold regardless of that internal
// state: default-config usability, silence -> exact 0, reset clears state,
// finite output under adversarial input, peak rectification, and the
// non-positive-fs / non-positive-time guards (FR-018).

using acfx::Ballistics;
using acfx::DetectDomain;
using acfx::DetectMode;
using acfx::EnvelopeFollower;

namespace {
constexpr float kSampleRate = 48000.0f;
} // namespace

// ---------------------------------------------------------------------------
TEST_CASE("EnvelopeFollower - default config after init() is a working peak follower") {
    // Assumptions: init() alone (no setters) yields peak, branching, non-smooth,
    // linear — a usable peak follower (US1).
    EnvelopeFollower env;
    env.init(kSampleRate);

    constexpr float kLevel = 0.5f;
    constexpr float kTol   = 1e-2f;

    float last = 0.0f;
    for (int n = 0; n < 4800; ++n) { // 100 ms — ample settling time
        last = env.process(kLevel);
        REQUIRE(std::isfinite(last));
    }

    // Peak of a constant/DC signal is its absolute value.
    CHECK(std::fabs(last - kLevel) < kTol);
}

// ---------------------------------------------------------------------------
TEST_CASE("EnvelopeFollower - silence settles to exactly zero (linear domain)") {
    // Edge Cases: "Silence / zero input: linear envelope must settle to exactly
    // 0 with no NaN/Inf." SC-008.
    EnvelopeFollower env;
    env.init(kSampleRate);
    env.setDomain(DetectDomain::linear);

    for (int n = 0; n < 1000; ++n) {
        const float out = env.process(0.0f);
        REQUIRE(std::isfinite(out));
        CHECK(out == 0.0f);
    }
}

// ---------------------------------------------------------------------------
TEST_CASE("EnvelopeFollower - reset() clears runtime state back to 0") {
    // FR-010: reset() clears env/meanSquare/y1/heldPeak/holdCounter to 0 —
    // silence right after reset must read 0, not a residual decaying value.
    EnvelopeFollower env;
    env.init(kSampleRate);

    // Drive a loud, sustained signal so the envelope rises well above 0.
    float driven = 0.0f;
    for (int n = 0; n < 2400; ++n) {
        driven = env.process(1.0f);
    }
    REQUIRE(std::isfinite(driven));
    REQUIRE(driven > 0.0f);

    env.reset();

    // First sample after reset is silence; the envelope must already read 0,
    // with no carried-over state producing a gradual decay instead.
    const float afterReset = env.process(0.0f);
    REQUIRE(std::isfinite(afterReset));
    CHECK(afterReset == 0.0f);
}

// ---------------------------------------------------------------------------
TEST_CASE("EnvelopeFollower - no NaN/Inf on adversarial input in peak mode") {
    // SC-008: no input (silence, DC, impulse, low-sample-rate short-tau)
    // produces NaN/Inf in any configuration.
    EnvelopeFollower env;
    env.init(kSampleRate);
    env.setMode(DetectMode::peak);

    // Positive DC.
    for (int n = 0; n < 100; ++n) {
        REQUIRE(std::isfinite(env.process(1.0f)));
    }
    // Negative DC.
    for (int n = 0; n < 100; ++n) {
        REQUIRE(std::isfinite(env.process(-1.0f)));
    }
    // Impulse then silence.
    REQUIRE(std::isfinite(env.process(1.0f)));
    for (int n = 0; n < 1000; ++n) {
        REQUIRE(std::isfinite(env.process(0.0f)));
    }
    // A few samples at a very high level.
    for (int n = 0; n < 10; ++n) {
        REQUIRE(std::isfinite(env.process(1.0e6f)));
    }
}

// ---------------------------------------------------------------------------
TEST_CASE("EnvelopeFollower - peak mode rectifies an alternating +A/-A signal") {
    // DC input rule generalizes: peak -> |x|. An alternating square wave of
    // amplitude A must settle near A, not oscillate down toward 0 — this is
    // the rectification behavior a peak detector must exhibit.
    EnvelopeFollower env;
    env.init(kSampleRate);
    env.setMode(DetectMode::peak);

    constexpr float kAmplitude = 0.7f;
    constexpr float kTol       = 1e-2f;

    float last = 0.0f;
    for (int n = 0; n < 4800; ++n) {
        const float x = (n % 2 == 0) ? kAmplitude : -kAmplitude;
        last          = env.process(x);
        REQUIRE(std::isfinite(last));
    }

    CHECK(std::fabs(last - kAmplitude) < kTol);
    CHECK(last > kTol); // must not have collapsed toward 0
}

// ---------------------------------------------------------------------------
TEST_CASE("EnvelopeFollower - guards non-positive sample rate (FR-018)") {
    // FR-018: sampleRate <= 0 is guarded to a defined finite state; process()
    // must never emit NaN/Inf afterward.
    EnvelopeFollower env;
    env.init(-1.0f);

    for (int n = 0; n < 100; ++n) {
        REQUIRE(std::isfinite(env.process(0.5f)));
    }
}

TEST_CASE("EnvelopeFollower - guards non-positive attack/release times (FR-018)") {
    // FR-018: setters guard against degenerate (negative/zero) time constants
    // so every coefficient stays within [0, 1) and no process() output is
    // NaN/Inf.
    EnvelopeFollower env;
    env.init(kSampleRate);
    env.setAttack(-5.0f);
    env.setRelease(0.0f);

    for (int n = 0; n < 100; ++n) {
        REQUIRE(std::isfinite(env.process(0.5f)));
    }
}

// measurement-stimulus-test.cpp
// Doctest cases for FR-001: stimulus generator correctness (T004).

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

#include "support/measurement/stimulus.h"
#include "dsp/audio-block.h"

using namespace acfx::measure;

TEST_CASE("ImpulseGenerator: out[0]==amplitude, all others==0") {
    constexpr std::size_t N = 64;
    std::vector<float> buf(N, -99.0f);

    ImpulseGenerator gen;
    gen.amplitude = 0.5f;
    gen.fill(acfx::span<float>(buf));

    CHECK(buf[0] == doctest::Approx(0.5f));
    for (std::size_t i = 1; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(buf[i] == doctest::Approx(0.0f));
    }
}

TEST_CASE("StepGenerator: every sample equals level") {
    constexpr std::size_t N = 64;
    std::vector<float> buf(N, -99.0f);

    StepGenerator gen;
    gen.level = 0.25f;
    gen.fill(acfx::span<float>(buf));

    for (std::size_t i = 0; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(buf[i] == doctest::Approx(0.25f));
    }
}

TEST_CASE("SineGenerator: samples match closed-form formula") {
    constexpr std::size_t N         = 64;
    constexpr double      freqHz    = 440.0;
    constexpr double      sampleRate = 44100.0;
    constexpr float       amplitude = 0.75f;
    constexpr double      phase     = std::numbers::pi / 4.0;

    std::vector<float> buf(N, 0.0f);

    SineGenerator gen;
    gen.freqHz     = freqHz;
    gen.sampleRate = sampleRate;
    gen.amplitude  = amplitude;
    gen.phase      = phase;
    gen.fill(acfx::span<float>(buf));

    const double omega = 2.0 * std::numbers::pi * freqHz / sampleRate;

    for (std::size_t n = 0; n < N; ++n) {
        // Mirror the generator's internal computation exactly:
        // it casts amplitude to float before multiplying (float * double -> double),
        // then casts the double result back to float for storage.
        const double s         = static_cast<double>(static_cast<float>(amplitude))
                                 * std::sin(omega * static_cast<double>(n) + phase);
        const float expected_f = static_cast<float>(s);

        INFO("sample index = " << n);
        CHECK(std::abs(buf[n] - expected_f) < 1e-5f);
    }
}

TEST_CASE("NoiseGenerator: same seed produces identical buffers") {
    constexpr std::size_t N         = 64;
    constexpr float       amplitude = 0.8f;

    // Two SEPARATE generators with the same seed (mirrors the sibling
    // "different seeds" test). This asserts exactly the named contract —
    // determinism from the seed — rather than relying on a single instance's
    // per-call re-seed behavior (AUDIT-20260629-03).
    NoiseGenerator gen1;
    gen1.amplitude = amplitude;
    gen1.seed      = 0xABCD1234u;

    NoiseGenerator gen2;
    gen2.amplitude = amplitude;
    gen2.seed      = 0xABCD1234u;

    std::vector<float> buf1(N, 0.0f);
    std::vector<float> buf2(N, 0.0f);

    gen1.fill(acfx::span<float>(buf1));
    gen2.fill(acfx::span<float>(buf2));

    for (std::size_t i = 0; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(buf1[i] == buf2[i]);
    }
}

TEST_CASE("NoiseGenerator: all samples within [-amplitude, amplitude]") {
    constexpr std::size_t N         = 64;
    constexpr float       amplitude = 0.8f;

    NoiseGenerator gen;
    gen.amplitude = amplitude;
    gen.seed      = 0xABCD1234u;

    std::vector<float> buf(N, 0.0f);
    gen.fill(acfx::span<float>(buf));

    for (std::size_t i = 0; i < N; ++i) {
        INFO("sample index = " << i);
        CHECK(std::abs(buf[i]) <= amplitude + 1e-6f);
    }
}

TEST_CASE("NoiseGenerator: different seeds produce different sequences") {
    constexpr std::size_t N = 64;

    std::vector<float> buf1(N, 0.0f);
    std::vector<float> buf2(N, 0.0f);

    NoiseGenerator gen1;
    gen1.seed = 0x1111u;
    gen1.fill(acfx::span<float>(buf1));

    NoiseGenerator gen2;
    gen2.seed = 0x2222u;
    gen2.fill(acfx::span<float>(buf2));

    bool anyDifference = false;
    for (std::size_t i = 0; i < N; ++i) {
        if (buf1[i] != buf2[i]) {
            anyDifference = true;
            break;
        }
    }
    CHECK(anyDifference);
}

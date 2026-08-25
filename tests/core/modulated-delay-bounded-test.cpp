#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/modulated-delay/modulated-delay-effect.h"
#include "support/allocation-sentinel.h"

// Task 4 — the templated, bounded, heap-free ModulatedDelayEffect.
// Covers:
//   * float clean-path NUMERICAL IDENTITY against a golden vector captured on
//     the pre-template effect (design Decisions 12a): the strongest guard that
//     the heap->static refactor preserved today's behavior bit-for-bit.
//   * the no-heap-allocation invariant on a small float instance (T066 sentinel).
//   * compile + prepare of the Nucleo int16 policy <14400, std::int16_t, 2> and
//     the heap-allocated <96000, float, 8> default (Ruling A: never a stack local).

using namespace acfx;
using acfx::test::AllocationSentinel;

namespace {

// The golden-capture parameters (must match scratchpad/golden-capture.cpp, whose
// output is committed at tests/core/golden/modulated-delay-default.txt).
template <typename Fx>
void setGoldenParams(Fx& fx) {
    auto setP = [&](typename Fx::Param p, float plain) {
        fx.setParameter(ParamId{p}, normalize(Fx::kParams[p], plain));
    };
    setP(Fx::kDelayTime,     0.003f);
    setP(Fx::kFeedback,      0.6f);
    setP(Fx::kMix,           0.7f);
    setP(Fx::kDelayModRate,  3.0f);
    setP(Fx::kDelayModDepth, 0.1f);
    setP(Fx::kDelayModShape, 0.0f);
}

std::vector<std::uint32_t> loadGolden(const char* path) {
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.is_open(), "cannot open golden vector: ", path);
    std::vector<std::uint32_t> bits;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        bits.push_back(static_cast<std::uint32_t>(std::stoul(line, nullptr, 16)));
    }
    return bits;
}

}  // namespace

// ---------------------------------------------------------------------------
// Numerical identity: the refactored float effect reproduces the pre-template
// golden BIT-EXACTLY. <32768, float, 1> is a safe stack local (~128 KB) whose
// capacity comfortably exceeds the exercised delay; BoundedDelayLine<float>
// stores the identical values regardless of capacity, so the reads match.
// ---------------------------------------------------------------------------
TEST_CASE("templated float default reproduces the pre-template golden bit-exactly") {
    const std::vector<std::uint32_t> golden = loadGolden(ACFX_MODULATED_DELAY_GOLDEN);
    REQUIRE(golden.size() == 512u);

    ModulatedDelayEffect<32768, float, 1> fx;
    fx.prepare(ProcessContext{48000.0, 1, 1});
    setGoldenParams(fx);

    // Warmup: 4800 samples of silence settle the delay-time smoother and advance
    // the LFO phase (identical to the capture procedure).
    for (int n = 0; n < 4800; ++n) {
        float s = 0.0f;
        float* ch[1] = {&s};
        AudioBlock block(ch, 1, 1);
        fx.process(block);
    }

    // Captured window: unit impulse then zeros; compare each output bit pattern.
    for (int n = 0; n < 512; ++n) {
        float s = (n == 0) ? 1.0f : 0.0f;
        float* ch[1] = {&s};
        AudioBlock block(ch, 1, 1);
        fx.process(block);
        const std::uint32_t got = std::bit_cast<std::uint32_t>(s);
        CHECK_MESSAGE(got == golden[static_cast<std::size_t>(n)],
                      "sample ", n, " differs from golden");
    }
}

// ---------------------------------------------------------------------------
// No-heap-allocation invariant on a small float instance (T066 sentinel).
// ---------------------------------------------------------------------------
TEST_CASE("templated float delay is heap-free through process()") {
    ModulatedDelayEffect<16384, float, 2> fx;
    fx.prepare(ProcessContext{48000.0, 64, 2});

    std::vector<float> l(64, 0.2f), r(64, -0.2f);
    float* ch[2] = {l.data(), r.data()};

    AllocationSentinel::reset();
    for (int i = 0; i < 32; ++i) {
        AudioBlock block(ch, 2, 64);
        fx.process(block);
        // setParameter on the audio thread must also be allocation-free.
        fx.setParameter(ParamId{ModulatedDelayEffect<16384, float, 2>::kCutoff},
                        (i % 2 == 0) ? 0.25f : 0.75f);
    }
    CHECK(AllocationSentinel::allocations() == 0);
}

// ---------------------------------------------------------------------------
// The Nucleo int16 policy compiles, satisfies the Effect concept, and
// prepare()+process() run heap-free on a small stack-safe instance (~57.6 KB).
// ---------------------------------------------------------------------------
TEST_CASE("Nucleo int16 policy <14400, int16, 2> compiles, prepares, runs heap-free") {
    using NucleoDelay = ModulatedDelayEffect<14400, std::int16_t, 2>;
    static_assert(acfx::Effect<NucleoDelay>,
                  "the int16 instantiation must satisfy the Effect contract");

    NucleoDelay fx;
    fx.prepare(ProcessContext{48000.0, 48, 2});

    std::vector<float> l(48, 0.1f), r(48, -0.1f);
    float* ch[2] = {l.data(), r.data()};

    AllocationSentinel::reset();
    for (int i = 0; i < 8; ++i) {
        AudioBlock block(ch, 2, 48);
        fx.process(block);
    }
    CHECK(AllocationSentinel::allocations() == 0);

    for (int i = 0; i < 48; ++i) {
        CHECK(std::isfinite(l[static_cast<std::size_t>(i)]));
        CHECK(std::isfinite(r[static_cast<std::size_t>(i)]));
    }
}

// ---------------------------------------------------------------------------
// The all-defaulted default <96000, float, 8> (~3 MB) must COMPILE and
// prepare() — heap-allocated, never on the stack (Ruling A).
// ---------------------------------------------------------------------------
TEST_CASE("default ModulatedDelayEffect<> compiles and prepares (heap-allocated)") {
    static_assert(acfx::Effect<ModulatedDelayEffect<>>,
                  "the all-defaulted instantiation must satisfy the Effect contract");

    auto fx = std::make_unique<ModulatedDelayEffect<>>();
    fx->prepare(ProcessContext{48000.0, 64, 2});

    std::vector<float> l(64, 0.05f), r(64, -0.05f);
    float* ch[2] = {l.data(), r.data()};
    AudioBlock block(ch, 2, 64);
    fx->process(block);

    for (int i = 0; i < 64; ++i) {
        CHECK(std::isfinite(l[static_cast<std::size_t>(i)]));
        CHECK(std::isfinite(r[static_cast<std::size_t>(i)]));
    }
}

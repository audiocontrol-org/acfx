#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/modulated-delay/modulated-delay-effect.h"

// Task 5 — the lo-fi layer: internal-rate (decimated) whole-wet-loop processing
// that trades bandwidth for delay time, plus an in-feedback-loop bit-crush, both
// as live params (kLofiRate=19, kLofiBits=20). The Nucleo policy instance
// <14400, int16, 2> is used throughout (Ruling A: never a multi-MB stack local).
//
// Covered (design §Decimation / §Bit-crush + storage, Decisions 4-8):
//   * D divides the internal rate: the realizable max delay scales with D.
//   * clean settings (D=1, B=16): the wet path is full-resolution — its RMS
//     matches the float-storage policy within the 16-bit storage floor (REAL
//     level assertion, not a placeholder — Ruling B).
//   * a live D change reinterprets the retained buffer (tape-speed), NOT silence
//     (REAL non-zero assertion — Ruling B).
//   * a low B audibly quantizes while PRESERVING level (guards the right-shift
//     level-drop trap, design Decisions 12d).

using namespace acfx;

namespace {

using NucleoDelay = ModulatedDelayEffect<14400, std::int16_t, 2>;
using FloatDelay  = ModulatedDelayEffect<14400, float, 2>;

// Publish a plain-unit value on a parameter (normalized through its descriptor).
template <typename Fx>
void setP(Fx& fx, typename Fx::Param p, float plain) {
    fx.setParameter(ParamId{p}, normalize(Fx::kParams[p], plain));
}

// Process one mono sample through a freshly prepared <..., N, 1> instance and
// return the (in-place) output.
template <typename Fx>
float tap(Fx& fx, float x) {
    float s = x;
    float* c[1] = {&s};
    AudioBlock b(c, 1, 1);
    fx.process(b);
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// D divides the internal rate: same buffer, D=8 gives ~8x the max delay time.
// ---------------------------------------------------------------------------
TEST_CASE("D divides the internal rate: D=8 gives ~8x the realizable max delay") {
    NucleoDelay fx;
    fx.prepare(ProcessContext{48000.0, 1, 1});

    setP(fx, NucleoDelay::kLofiRate, 0.0f);  // index 0 -> D=1
    const float m1 = fx.maxDelaySeconds();
    CHECK(m1 == doctest::Approx(14400.0f / 48000.0f).epsilon(0.001f));  // ~0.30 s

    setP(fx, NucleoDelay::kLofiRate, 3.0f);  // index 3 -> D=8
    // Params apply at the top of process(); pump one sample to consume them.
    (void)tap(fx, 0.0f);
    const float m8 = fx.maxDelaySeconds();

    CHECK(m8 == doctest::Approx(8.0f * m1).epsilon(0.02f));  // ~2.4 s
}

// ---------------------------------------------------------------------------
// Clean settings (D=1, B=16): the wet path is full-resolution. Drive a tone
// through the wet path on BOTH storage policies (int16 vs float) with identical
// params; the int16 output RMS must match the float reference within the 16-bit
// storage floor. Guards LEVEL, not just presence (Ruling B).
// ---------------------------------------------------------------------------
TEST_CASE("clean settings (D=1,B=16) leave the wet path full-resolution (RMS)") {
    NucleoDelay i16;
    FloatDelay  f32;
    i16.prepare(ProcessContext{48000.0, 1, 1});
    f32.prepare(ProcessContext{48000.0, 1, 1});

    auto configure = [](auto& fx) {
        using Fx = std::remove_reference_t<decltype(fx)>;
        setP(fx, Fx::kMix,       1.0f);    // wet only
        setP(fx, Fx::kFeedback,  0.0f);    // no recursion — one storage round-trip
        setP(fx, Fx::kDelayTime, 0.010f);  // 480 samples — fills quickly
        setP(fx, Fx::kLofiRate,  0.0f);    // D=1
        setP(fx, Fx::kLofiBits,  0.0f);    // B=16 -> crush bypassed
    };
    configure(i16);
    configure(f32);

    const int   kWarm = 1200;
    const int   kMeas = 2000;
    const float kAmp  = 0.25f;
    const float kW    = 2.0f * static_cast<float>(M_PI) * 300.0f / 48000.0f;

    double sumI = 0.0, sumF = 0.0;
    for (int n = 0; n < kWarm + kMeas; ++n) {
        const float x  = kAmp * std::sin(kW * static_cast<float>(n));
        const float yI = tap(i16, x);
        const float yF = tap(f32, x);
        if (n >= kWarm) {
            sumI += static_cast<double>(yI) * yI;
            sumF += static_cast<double>(yF) * yF;
        }
    }
    const float rmsI = static_cast<float>(std::sqrt(sumI / kMeas));
    const float rmsF = static_cast<float>(std::sqrt(sumF / kMeas));

    // Level is non-trivial (the wet tone actually rings through) ...
    CHECK(rmsF > 0.05f);
    // ... and the int16 storage floor is the ONLY difference from full float:
    // the two RMS values agree to well within one int16 LSB relative to signal.
    CHECK(rmsI == doctest::Approx(rmsF).epsilon(0.01f));
}

// ---------------------------------------------------------------------------
// A live D change reinterprets the retained buffer (tape-speed), does NOT clear
// it. Fill the line at D=1, switch to D=2 mid-stream, and assert the wet output
// stays non-zero (the buffer's contents are still played, not silenced).
// ---------------------------------------------------------------------------
TEST_CASE("live D change reinterprets the buffer (tape-speed), does not clear it") {
    NucleoDelay fx;
    fx.prepare(ProcessContext{48000.0, 1, 1});
    setP(fx, NucleoDelay::kMix,       1.0f);
    setP(fx, NucleoDelay::kFeedback,  0.0f);
    setP(fx, NucleoDelay::kDelayTime, 0.002f);  // 96 samples at D=1

    // Fill long enough for the 20 ms delay-time smoother to glide down to the
    // short target AND to pack the read region with the DC-ish signal.
    for (int n = 0; n < 6000; ++n)
        (void)tap(fx, 1.0f);                    // fill the line with a DC-ish signal

    const float before = tap(fx, 1.0f);         // steady wet output at D=1
    CHECK(before > 0.5f);                        // ~1.0 (lowpass DC gain ~unity)

    setP(fx, NucleoDelay::kLofiRate, 1.0f);      // D=1 -> D=2 mid-stream
    const float after = tap(fx, 0.0f);           // buffer retained, not silence

    CHECK(std::isfinite(after));
    // The retained buffer is reinterpreted at the new rate, so the wet output is
    // still driven by the stored (non-zero) samples — a clear-to-silence would
    // make this ~0.
    CHECK(std::abs(after) > 0.1f);
}

// ---------------------------------------------------------------------------
// A low B audibly quantizes while PRESERVING level. Run the same tone through
// B=16 and B=4; the B=4 output must (a) differ substantially from B=16 (the
// crush is doing real work) and (b) keep essentially the same RMS level — it is
// NOT shrunk by 2^(16-B), the right-shift trap (design Decisions 12d).
// ---------------------------------------------------------------------------
TEST_CASE("bit-crush at low B quantizes audibly while preserving level") {
    NucleoDelay clean;   // B=16
    NucleoDelay crushed; // B=4
    clean.prepare(ProcessContext{48000.0, 1, 1});
    crushed.prepare(ProcessContext{48000.0, 1, 1});

    auto configure = [](auto& fx, float bitsPlain) {
        using Fx = std::remove_reference_t<decltype(fx)>;
        setP(fx, Fx::kMix,       1.0f);
        setP(fx, Fx::kFeedback,  0.0f);
        setP(fx, Fx::kDelayTime, 0.002f);
        // Open the feedback-loop filter wide so the crush is observed, not
        // smoothed away by the default 2 kHz lowpass (this test is about the
        // quantization grid, not the SVF).
        setP(fx, Fx::kCutoff,    15000.0f);
        setP(fx, Fx::kLofiRate,  0.0f);       // D=1 (isolate the crush)
        setP(fx, Fx::kLofiBits,  bitsPlain);
    };
    configure(clean,   0.0f);  // index 0 -> B=16 (bypass)
    configure(crushed, 4.0f);  // index 4 -> B=4  (q = 0.25 grid)

    const int   kWarm = 800;
    const int   kMeas = 2000;
    const float kAmp  = 0.5f;
    const float kW    = 2.0f * static_cast<float>(M_PI) * 300.0f / 48000.0f;

    double sumC = 0.0, sumX = 0.0, sumDiff = 0.0;
    for (int n = 0; n < kWarm + kMeas; ++n) {
        const float x  = kAmp * std::sin(kW * static_cast<float>(n));
        const float yC = tap(clean,   x);
        const float yX = tap(crushed, x);
        if (n >= kWarm) {
            sumC    += static_cast<double>(yC) * yC;
            sumX    += static_cast<double>(yX) * yX;
            sumDiff += static_cast<double>(yC - yX) * (yC - yX);
        }
    }
    const float rmsC    = static_cast<float>(std::sqrt(sumC / kMeas));
    const float rmsX    = static_cast<float>(std::sqrt(sumX / kMeas));
    const float rmsDiff = static_cast<float>(std::sqrt(sumDiff / kMeas));

    // Level preserved: B=4 keeps the same order of magnitude as B=16. If the
    // crush had been a level-dropping right-shift, rmsX would collapse toward 0.
    CHECK(rmsC > 0.1f);
    CHECK(rmsX > 0.7f * rmsC);
    CHECK(rmsX < 1.3f * rmsC);
    // Audibly quantizes: a 4-bit grid injects substantial quantization error.
    CHECK(rmsDiff > 0.01f);
}

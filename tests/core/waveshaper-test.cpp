#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "labs/waveshaping/waveshaper.h"
#include "core/measurement-support.h"
#include "support/allocation-sentinel.h"

// T007 -- Waveshaper wrapper: signal-chain ordering, silence, DC removal,
// gain compensation direction, and reset invariants.
// References: FR-007/008/009, SC-002/005.
//
// Expected RED state: Waveshaper::process, all setters, and reset() are
// declared in core/labs/waveshaping/waveshaper.h (T005) but their bodies
// are not yet implemented (T010/T011 pending).  The link step will fail
// with undefined references naming acfx::Waveshaper symbols.  This is the
// correct RED; compile errors in THIS translation unit must be fixed before
// committing.

using namespace acfx;
using acfx::test::AllocationSentinel;

namespace {

constexpr float kPi         = 3.14159265358979323846f;
constexpr float kSampleRate = 48000.0f;

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Signal-chain order -- u = drive*x + bias -> shape(u) -> dcBlock -> gainComp
//
// For the very first sample of a freshly init()'d shaper, the DC-blocker
// state is zero (xPrev=0, yPrev=0):
//   dcBlock(y) = y - 0 + kDcR * 0 = y   (transparent at n=0)
// With gainComp off, process(x) at n=0 == shape(drive*x + bias) exactly.
//
// hardClip(u) = clamp(u, -1.0f, 1.0f); in-range values pass through exactly,
// giving analytic ground-truth values for the assertions.
// ---------------------------------------------------------------------------

TEST_CASE("signal-chain order: drive*x+bias applied before shape") {
    SUBCASE("drive scales x before bias is added (distinguishes correct from wrong order)") {
        // drive=2, bias=0.3, x=0.1:
        //   correct:  u = 2*0.1 + 0.3 = 0.5  -> hardClip(0.5) = 0.5  (analytic)
        //   wrong  :  u = 2*(0.1+0.3) = 0.8  -> hardClip(0.8) = 0.8  (wrong)
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(2.0f);
        ws.setBias(0.3f);
        ws.setGainCompensation(false);

        const float out = ws.process(0.1f);
        CHECK(out == doctest::Approx(0.5f).epsilon(1e-5));
    }

    SUBCASE("drive pushes input into clipping region") {
        // drive=2, bias=0, x=0.6:
        //   u = 2*0.6 = 1.2  -> hardClip(1.2) = 1.0  (clipped; analytic truth)
        //   without drive:      hardClip(0.6) = 0.6   (not clipped)
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(2.0f);
        ws.setBias(0.0f);
        ws.setGainCompensation(false);

        const float out = ws.process(0.6f);
        CHECK(out == doctest::Approx(1.0f).epsilon(1e-5));
    }

    SUBCASE("dcBlock is transparent at n=0 with unity drive and zero bias") {
        // u = 1*0.7 + 0 = 0.7  -> hardClip(0.7) = 0.7  -> dcBlock(0.7) = 0.7
        // (dcBlock transparent at n=0: y = x - 0 + kDcR*0 = x)
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(1.0f);
        ws.setBias(0.0f);
        ws.setGainCompensation(false);

        const float out = ws.process(0.7f);
        CHECK(out == doctest::Approx(0.7f).epsilon(1e-5));
    }
}

// ---------------------------------------------------------------------------
// TEST 2: Silence in -> silence out (FR-017, SC-005)
//
// With bias=0 and all-zero input the DC-blocker state stays zero throughout:
//   y[n] = 0 - 0 + R*0 = 0  for all n
// Output is exactly 0.0f for every sample; any symmetric shape evaluates
// shape(0) = 0 exactly (hardClip(0) = clamp(0,-1,1) = 0).
// ---------------------------------------------------------------------------

TEST_CASE("silence-in silence-out: all-zero input with zero bias yields zero output") {
    Waveshaper ws;
    ws.init(kSampleRate);
    ws.setShape(Shape::hardClip);
    ws.setEvaluation(Evaluation::closedForm);
    ws.setDrive(1.0f);
    ws.setBias(0.0f);
    ws.setGainCompensation(false);

    for (int i = 0; i < 1000; ++i) {
        const float out = ws.process(0.0f);
        CHECK(out == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// TEST 3: Asymmetric bias -> DC-free steady-state output (FR-007/008, SC-002)
//
// A nonzero bias shifts the operating point, causing the shaped output to
// carry a non-zero mean (DC + even harmonics).  The wrapper's DC-blocker
// (one-pole HP, R=kDcR=0.995) must remove this DC.
//
// Analytic settling bound: tau = 1/(1-R) = 200 samples.  After
// kTransientSamples=2000 (~= 10*tau) the envelope has decayed by
// R^2000 ~= e^-10 ~= 4.5e-5 of its initial value.  Shape output is bounded
// to 1.0, so residual DC < 4.5e-5.  Tolerance 0.001 gives a 22x margin.
// ---------------------------------------------------------------------------

TEST_CASE("asymmetric bias: DC-blocker drives steady-state output mean to near zero") {
    Waveshaper ws;
    ws.init(kSampleRate);
    ws.setShape(Shape::hardClip);
    ws.setEvaluation(Evaluation::closedForm);
    ws.setDrive(1.0f);
    ws.setBias(0.5f);
    ws.setGainCompensation(false);

    constexpr double kFundamentalHz    = 1000.0;
    constexpr int    kTransientSamples = 2000;  // ~10*tau; DC decays by > e^-10
    constexpr int    kMeasureSamples   = 4800;  // 100 integer cycles (1000 Hz @ 48 kHz)

    // Phase 1: warm up — let the DC-blocker settle over the transient window.
    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        ws.process(std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    // Phase 2: capture steady-state output into a buffer.
    std::vector<float> outBuf(static_cast<std::size_t>(kMeasureSamples));
    for (int i = 0; i < kMeasureSamples; ++i) {
        const float t = static_cast<float>(kTransientSamples + i) / kSampleRate;
        outBuf[static_cast<std::size_t>(i)] =
            ws.process(std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t));
    }

    // Measure DC offset using the shared measurement helper (T006).
    const double dc = meastest::dcOffset(span<const float>(outBuf));

    // Analytic bound: DC-blocker completely rejects DC in steady state.
    // Residual < R^2000 ~= 4.5e-5; tolerance 0.001 is a 22x safety margin.
    CHECK(std::abs(dc) < 0.001);
}

// ---------------------------------------------------------------------------
// TEST 4: Gain-compensation direction toward unity (FR-006, SC-005)
//
// With a high drive and a low-amplitude sine (linear region of tanh so
// tanh(drive*x) ~= drive*x), gainComp OFF leaves the output amplitude
// approximately drive * input_amplitude.  gainComp ON should pull the output
// amplitude back toward input_amplitude (the "unity" direction).
//
// We assert DIRECTION only, not a specific compensation value; the exact law
// is finalized in T011.  The assertion is robust to any monotone compensation
// scheme that moves output toward the input amplitude.
// ---------------------------------------------------------------------------

TEST_CASE("gain-compensation ON moves output RMS closer to input amplitude than OFF") {
    constexpr double      kFundamentalHz = 1000.0;
    constexpr float       kDrive         = 4.0f;
    constexpr float       kAmplitude     = 0.05f;  // low-level: tanh ~linear here
    constexpr std::size_t kNumSamples    = 4800;   // 100 integer cycles @ 48 kHz

    auto measureRms = [&](bool gainComp) {
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::tanh);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(kDrive);
        ws.setBias(0.0f);
        ws.setGainCompensation(gainComp);

        double sumSq = 0.0;
        for (std::size_t i = 0; i < kNumSamples; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            const float x = kAmplitude *
                std::sin(2.0f * kPi * static_cast<float>(kFundamentalHz) * t);
            const float y = ws.process(x);
            sumSq += static_cast<double>(y) * static_cast<double>(y);
        }
        return std::sqrt(sumSq / static_cast<double>(kNumSamples));
    };

    const double rmsOff   = measureRms(false);
    const double rmsOn    = measureRms(true);
    const double inputRms = static_cast<double>(kAmplitude) / std::sqrt(2.0);

    // With drive=4, kAmplitude=0.05 (linear region):
    //   rmsOff ~= kDrive * inputRms = 4 * inputRms  >>  inputRms
    // With gainComp ON, rmsOn should be pulled back toward inputRms.
    // Directional assertion: gainComp ON is strictly closer to the input RMS.
    CHECK(std::abs(rmsOn - inputRms) < std::abs(rmsOff - inputRms));
}

// ---------------------------------------------------------------------------
// TEST 5: No stale state on reset (FR-009)
//
// After reset(), the Waveshaper must produce the same output as a freshly
// init()'d instance given the same subsequent inputs.  reset() clears only
// xPrev and yPrev (DC-block state) to zero; shape, drive, bias, and gainComp
// MUST be preserved.  If any parameter were inadvertently cleared, fresh and
// ws would use different settings and their outputs would diverge.
// ---------------------------------------------------------------------------

TEST_CASE("reset clears DC-block state and preserves shape/drive/bias parameters") {
    constexpr float kDrive = 1.5f;
    constexpr float kBias  = 0.1f;

    auto configure = [&](Waveshaper& ws) {
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(kDrive);
        ws.setBias(kBias);
        ws.setGainCompensation(false);
    };

    // Reference: freshly init()'d shaper with zero DC-block state.
    Waveshaper fresh;
    configure(fresh);

    // Dirty shaper: accumulate DC-block history, then call reset().
    Waveshaper ws;
    configure(ws);
    for (int i = 0; i < 500; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        ws.process(0.3f * std::sin(2.0f * kPi * 440.0f * t));
    }

    // reset() clears DC-block state; shape/drive/bias MUST be preserved.
    ws.reset();

    // Both shapers now share identical state and parameters.  Subsequent
    // outputs for the same inputs must be bit-identical.
    for (int i = 0; i < 100; ++i) {
        const float t        = static_cast<float>(i) / kSampleRate;
        const float x        = 0.3f * std::sin(2.0f * kPi * 440.0f * t);
        const float outFresh = fresh.process(x);
        const float outWs    = ws.process(x);
        REQUIRE(outWs == outFresh);
    }
}

// ---------------------------------------------------------------------------
// TEST 6 (T014): setShape runtime switch — no stale state, all enum members
//   dispatchable (US2 contract: complete catalog dispatch).
//
// Three sub-assertions:
//   A. Every Shape enum member is dispatchable (loop over all 11); output is
//      finite and bounded for representative inputs.
//   B. After setShape + reset (DC cleared), the FIRST sample output equals the
//      new shape's transfer exactly.  DC-blocker is transparent at n=0
//      (y[0] = x[0] − 0 + R·0 = x[0]).  Verifies no old-shape residue.
//   C. DC-block state converges after the switch: after kWarmup >> tau=200
//      samples of the same input, ws (switched from hardClip→tanh) and ref
//      (always tanh) produce outputs within the analytic convergence bound
//      (|diff| ≤ R^kWarmup · max_initial_diff ≈ 3e-7 · 2; tol=0.001 is 6700×).
// ---------------------------------------------------------------------------

TEST_CASE("setShape runtime switch: no stale state, all enum members dispatchable (T014/US2)") {
    const Shape kAllShapes[] = {
        Shape::tanh, Shape::arctan, Shape::cubicSoft, Shape::algebraic,
        Shape::hardClip, Shape::softKnee, Shape::chebyshev, Shape::biasedAsym,
        Shape::diodeCurve, Shape::sineFold, Shape::triangleFold
    };

    SUBCASE("A: every Shape enum member is dispatchable — output finite and bounded") {
        // Drive=1.0, inputs in [-0.9, 0.9] keep chebyshev(u,2)=2u²-1 in [-1,1].
        // All other shapes are bounded over all finite u (biasedAsym range ≈ [-1.5, 0.6]).
        for (Shape s : kAllShapes) {
            Waveshaper ws;
            ws.init(kSampleRate);
            ws.setShape(s);
            ws.setDrive(1.0f);
            ws.setBias(0.0f);
            ws.setGainCompensation(false);

            const float inputs[] = {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f};
            for (float x : inputs) {
                const float out = ws.process(x);
                CHECK_MESSAGE(std::isfinite(out),
                    "shape=", static_cast<int>(s), " x=", x);
                CHECK_MESSAGE(std::abs(out) <= 3.0f,
                    "shape=", static_cast<int>(s), " x=", x);
            }
        }
    }

    SUBCASE("B: setShape+reset immediately applies new shape (DC-transparent at n=0)") {
        // After setShape(tanh) + reset(), dcXPrev_=dcYPrev_=0.
        // At n=0: dcBlock(y) = y - 0 + R*0 = y (transparent).
        // Therefore process(x) = tanh(drive*x+bias) exactly.
        // Use x=2 so hardClip(2)=1.0 and tanh(2)≈0.964 are clearly distinct.

        constexpr float kX = 2.0f;  // hardClip(2)=1.0, tanh(2)≈0.9640

        // Reference: fresh tanh shaper at n=0
        Waveshaper ref;
        ref.init(kSampleRate);
        ref.setShape(Shape::tanh);
        ref.setDrive(1.0f);
        ref.setBias(0.0f);
        ref.setGainCompensation(false);
        const float refOut = ref.process(kX);  // = tanh(2.0) ≈ 0.9640

        // Test shaper: start with hardClip, accumulate DC history, switch, reset.
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setDrive(1.0f);
        ws.setBias(0.0f);
        ws.setGainCompensation(false);
        for (int i = 0; i < 200; ++i) {
            ws.process(0.5f * std::sin(2.0f * kPi * 440.0f * static_cast<float>(i) / kSampleRate));
        }

        ws.setShape(Shape::tanh);
        ws.reset();  // clears DC state only; shape=tanh preserved (FR-009)

        const float wsOut = ws.process(kX);  // DC-transparent at n=0 post-reset

        // After setShape(tanh)+reset: output must match fresh tanh shaper exactly.
        CHECK(wsOut == doctest::Approx(refOut).epsilon(1.0e-5f));
        // Verify it is NOT hardClip's output (hardClip(2)=1.0, tanh(2)≈0.964 != 1.0).
        CHECK(wsOut != doctest::Approx(1.0f).epsilon(1.0e-4f));
    }

    SUBCASE("C: DC-block state converges after shape switch — no prior-shape residue") {
        // ws: hardClip for 500 samples → setShape(tanh) → kWarmup more samples.
        // ref: always tanh, zero initial DC state → kWarmup samples (same inputs).
        // After kWarmup=3000 >> tau=200 both DC states have converged:
        //   |ws_dc − ref_dc| ≤ R^3000 · max_initial_diff ≈ e^-15 · 2 ≈ 6e-7.
        // Tolerance 0.001 is ≈ 1700× the analytic bound.
        constexpr int   kWarmup = 3000;
        constexpr int   kCheck  = 100;
        constexpr float kAmp    = 0.3f;
        constexpr float kFreq   = 440.0f;

        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setDrive(1.0f);
        ws.setBias(0.0f);
        ws.setGainCompensation(false);
        for (int i = 0; i < 500; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            ws.process(kAmp * std::sin(2.0f * kPi * kFreq * t));
        }
        ws.setShape(Shape::tanh);  // switch mid-stream

        Waveshaper ref;
        ref.init(kSampleRate);
        ref.setShape(Shape::tanh);
        ref.setDrive(1.0f);
        ref.setBias(0.0f);
        ref.setGainCompensation(false);

        // Both process the SAME kWarmup inputs (starting from sample offset 500).
        for (int i = 0; i < kWarmup; ++i) {
            const float t = static_cast<float>(500 + i) / kSampleRate;
            const float x = kAmp * std::sin(2.0f * kPi * kFreq * t);
            ws.process(x);
            ref.process(x);
        }

        // After convergence: output difference bounded by R^3000 * max_initial_diff.
        constexpr float kTol = 0.001f;
        for (int i = 0; i < kCheck; ++i) {
            const float t      = static_cast<float>(500 + kWarmup + i) / kSampleRate;
            const float x      = kAmp * std::sin(2.0f * kPi * kFreq * t);
            const float wsOut  = ws.process(x);
            const float refOut = ref.process(x);
            CHECK(std::abs(wsOut - refOut) < kTol);
        }
    }
}

// ---------------------------------------------------------------------------
// TEST 7: RT-safety — Waveshaper::process() allocates zero bytes on the heap
//         (FR-011, FR-020; T011; research.md Decision 7)
//
// The audio path (process()) MUST be allocation-free: no heap alloc, no locks,
// bounded work.  The allocation sentinel intercepts global operator new/delete
// on the calling thread.  Any allocation inside process() increments the count.
//
// Shapes exercised: tanh, hardClip, cubicSoft (the full US1 catalog).
// gainComp ON and OFF are both tested to confirm the multiply branch is also
// allocation-free.  100 samples per shape/config suffices to confirm the path.
// ---------------------------------------------------------------------------

TEST_CASE("Waveshaper::process is allocation-free on the audio path (RT-safety)") {
    struct Config {
        Shape shape;
        bool  gainComp;
        float drive;
    };

    const Config configs[] = {
        {Shape::tanh,      false, 2.0f},
        {Shape::tanh,      true,  2.0f},
        {Shape::hardClip,  false, 1.5f},
        {Shape::hardClip,  true,  1.5f},
        {Shape::cubicSoft, false, 1.0f},
        {Shape::cubicSoft, true,  1.0f},
    };

    for (const auto& cfg : configs) {
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(cfg.shape);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(cfg.drive);
        ws.setBias(0.1f);
        ws.setGainCompensation(cfg.gainComp);

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            const float x = 0.2f * std::sin(2.0f * kPi * 1000.0f * t);
            (void)ws.process(x);
        }
        const std::size_t allocs = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocs == 0,
            "shape=", static_cast<int>(cfg.shape),
            " gainComp=", cfg.gainComp,
            " drive=", cfg.drive,
            " allocated=", allocs);
    }
}

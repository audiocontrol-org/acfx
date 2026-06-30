#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>

#include "primitives/nonlinear/adaa-waveshaper.h"
#include "primitives/nonlinear/waveshaper.h"
#include "core/measurement-support.h"

// T018 -- ADAAWaveshaper: aliasing reduction (SC-003), uncovered-shape
// descriptive error (Constitution V), base Waveshaper/acfx::shape::* unchanged
// (FR-013/014).
//
// EXPECTED RED: core/labs/waveshaping/adaa-waveshaper.h does not exist (T020
// pending) and acfx::ADAAWaveshaper / acfx::AdaaOrder are not declared.
// The compile step fails with:
//   fatal error: 'labs/waveshaping/adaa-waveshaper.h' file not found
//   error: unknown type name 'ADAAWaveshaper'
//   error: use of undeclared identifier 'AdaaOrder'
// That compile error IS the correct RED state for T018.
//
// API ASSUMED FOR T020 (so implementation matches):
//   namespace acfx {
//   enum class AdaaOrder : std::uint8_t { first };
//   class ADAAWaveshaper {
//   public:
//     void  init(float sampleRate) noexcept;
//     void  setShape(Shape shape);            // throws std::invalid_argument for
//                                             // uncovered (antiderivative-less) shapes;
//                                             // NOT noexcept — control-thread config call
//     void  setAdaaOrder(AdaaOrder) noexcept;
//     void  setDrive(float) noexcept;
//     void  setBias(float) noexcept;
//     void  setGainCompensation(bool) noexcept;
//     void  reset() noexcept;
//     float process(float x) noexcept;       // RT-safe: noexcept, no alloc, bounded
//   };
//   } // namespace acfx
//
// COORDINATION-FIXED shapes (agreed across T018/T019/T020):
//   COVERED:   Shape::hardClip   — aggressive clipper, has hardClipAntideriv
//   UNCOVERED: Shape::biasedAsym — wrapper-realized, no pure antiderivative

using namespace acfx;

namespace {

constexpr float  kSampleRate  = 48000.0f;
constexpr double kSampleRateD = 48000.0;

// ---------------------------------------------------------------------------
// ALIASING TEST — frequency choice (documented per task brief)
//
// Shape:    Shape::hardClip (odd-symmetric; cleans antiderivative; COVERED).
// f0:       10 000 Hz   sr: 48 000 Hz   Nyquist: 24 000 Hz
// Drive:    4.0  → unit-amplitude input u in [−4, 4], heavily clipped to ±1.
//           The output is near-square at f0, rich in odd harmonics.
//
// Why f0 = 10 000 Hz causes inharmonic aliasing:
//   Naively-processed odd harmonics fold as follows (alias = fold into [0, Ny]):
//     3rd  (30 000 Hz) → 48 000 − 30 000 = 18 000 Hz = 1.8 × f0  ← NOT a harmonic
//     5th  (50 000 Hz) → 50 000 − 48 000 =  2 000 Hz = 0.2 × f0  ← NOT a harmonic
//     7th  (70 000 Hz) → 70 000 − 48 000 = 22 000 Hz = 2.2 × f0  ← NOT a harmonic
//     9th  (90 000 Hz) → 48 000 − (90 000−48 000) =  6 000 Hz = 0.6 × f0  ← NOT a harmonic
//   Every alias lands at a non-integer multiple of f0: aliasingMeasure subtracts
//   the true harmonic series and reports them as inharmonicPower.
//
// Integer-cycle window: 4800 samples × 10 000 Hz / 48 000 Hz = 1 000 complete
// cycles.  Every harmonic and every aliased image sits on a DFT bin (multiple
// of 48 000/4 800 = 10 Hz), so Parseval's identity is exact and the
// inharmonicPower subtraction in aliasingMeasure is leakage-free (T006 contract).
//
// DC-blocker settling: bias = 0.0, hardClip is odd-symmetric → no DC
// component is generated.  The one-pole HP (R = 0.995) removes only a
// single-sample startup transient that decays by R^4800 ≈ e^{−24} ≈ 0 over
// the window.  No explicit warmup is needed for this test.
// ---------------------------------------------------------------------------

constexpr double      kFundamentalHz   = 10000.0;
constexpr float       kAmplitude       = 1.0f;
constexpr float       kDrive           = 4.0f;
constexpr std::size_t kMeasureSamples  = 4800;   // 1000 integer cycles

// ---------------------------------------------------------------------------
// ALIASING MARGIN — documented justification (SC-003)
//
// First-order ADAA replaces naive sample-and-hold approximation with the exact
// area-under-curve between consecutive input samples.  For a 10 kHz tone at
// 48 kHz the inter-sample excursion is up to 2·drive·sin(π·f0/sr) ≈ 2.6,
// which naive shaping badly misrepresents.  First-order ADAA rolls off the
// aliased amplitude of each N-th harmonic by roughly 1/N relative to naive,
// yielding a typical power reduction of 10–30 dB for the dominant aliases.
//
// Chosen margin: ADAA inharmonicPower ≤ 0.50 × naive inharmonicPower
// (≥ 3.0 dB reduction).  This is an extremely conservative lower bound on
// the known first-order ADAA benefit for hardClip; the actual improvement
// is expected to be far larger.  The conservative threshold ensures stability
// across compiler/platform variations while still asserting a meaningful floor.
// ---------------------------------------------------------------------------

constexpr double kAliasingMarginRatio = 0.50;  // ADAA inharmonic ≤ 0.50 × naive

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: ADAA reduces inharmonic (aliased) energy vs naive (SC-003)
// ---------------------------------------------------------------------------

TEST_CASE("ADAA reduces aliasing vs naive for hardClip high-frequency stimulus (SC-003)") {
    // Naive shaper: Waveshaper + Shape::hardClip (no antiderivative anti-aliasing).
    Waveshaper wsNaive;
    wsNaive.init(kSampleRate);
    wsNaive.setShape(Shape::hardClip);
    wsNaive.setEvaluation(Evaluation::closedForm);
    wsNaive.setDrive(kDrive);
    wsNaive.setBias(0.0f);
    wsNaive.setGainCompensation(false);

    // ADAA shaper: ADAAWaveshaper + Shape::hardClip, first-order ADAA.
    ADAAWaveshaper wsAdaa;
    wsAdaa.init(kSampleRate);
    wsAdaa.setShape(Shape::hardClip);   // covered: hardClipAntideriv exists (T019)
    wsAdaa.setAdaaOrder(AdaaOrder::first);
    wsAdaa.setDrive(kDrive);
    wsAdaa.setBias(0.0f);
    wsAdaa.setGainCompensation(false);

    // Capture aliasing measures over an integer-cycle window.
    // captureSineResponse creates a fresh SineGenerator starting at phase 0
    // for each arm, so both arms see identical input.
    const auto naiveMeasure = meastest::aliasingMeasure(
        [&wsNaive](float x) { return wsNaive.process(x); },
        kFundamentalHz, kSampleRateD, kMeasureSamples, kAmplitude);

    const auto adaaMeasure = meastest::aliasingMeasure(
        [&wsAdaa](float x) { return wsAdaa.process(x); },
        kFundamentalHz, kSampleRateD, kMeasureSamples, kAmplitude);

    // Sanity: both arms produced non-trivial output.
    CHECK(naiveMeasure.totalPower > 0.0);
    CHECK(adaaMeasure.totalPower  > 0.0);

    // Naive shaper must exhibit measurable inharmonic (aliased) energy.
    // With drive=4 and f0=10 kHz the naive shaper produces a near-square wave
    // whose odd harmonics alias heavily (see frequency justification above).
    CHECK(naiveMeasure.inharmonicPower > 0.0);

    // SC-003: first-order ADAA inharmonic power is at most kAliasingMarginRatio
    // times naive inharmonic power (≥ 3 dB reduction; conservative lower bound).
    CHECK(adaaMeasure.inharmonicPower
          <= naiveMeasure.inharmonicPower * kAliasingMarginRatio);
}

// ---------------------------------------------------------------------------
// TEST 2: Uncovered (antiderivative-less) shape raises a descriptive error
//          (Constitution V: no silent mis-shaping / no fallback)
// ---------------------------------------------------------------------------

TEST_CASE("ADAAWaveshaper::setShape refuses antiderivative-less shape with std::invalid_argument (Constitution V)") {
    // Shape::biasedAsym is the coordination-fixed UNCOVERED shape:
    //   * It is realized inside Waveshaper::shapeValue() as a wrapper-local
    //     shifted-tanh expression, NOT as a pure acfx::shape::* function.
    //   * Because no standalone acfx::shape::biasedAsym() exists, there is no
    //     analytic antiderivative to give ADAAWaveshaper.
    //   * ADAAWaveshaper MUST refuse it with a descriptive std::invalid_argument
    //     (naming the shape and/or "no antiderivative") rather than silently
    //     mis-shaping or degrading to naive.
    //
    // setShape() is a control-thread configuration call; throwing is acceptable
    // there.  process() stays RT-safe/noexcept regardless.

    ADAAWaveshaper adaa;
    adaa.init(kSampleRate);

    CHECK_THROWS_AS(adaa.setShape(Shape::biasedAsym), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// TEST 3: Base Waveshaper and acfx::shape::* are unchanged by ADAA introduction
//          (FR-013/014: ADAA is layered, not folded into the base)
// ---------------------------------------------------------------------------

TEST_CASE("base Waveshaper and acfx::shape::* produce known analytic values post-ADAA (FR-013/014)") {
    // At n=0 the DC-blocker state is zero (xPrev=yPrev=0), so dcBlock is
    // transparent at the first sample: dcBlock(y) = y − 0 + R·0 = y.
    // With gainComp off: process(x) = hardClip(drive·x + bias) exactly at n=0.
    // These closed-form values assert the base shaper is entirely unmodified.

    SUBCASE("Waveshaper(hardClip): in-range value passes through exactly at n=0") {
        // drive=1, bias=0, x=0.7: u=0.7 → hardClip(0.7)=0.7 (analytic: |u|<1, identity)
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(1.0f);
        ws.setBias(0.0f);
        ws.setGainCompensation(false);

        CHECK(ws.process(0.7f) == doctest::Approx(0.7f).epsilon(1.0e-5));
    }

    SUBCASE("Waveshaper(hardClip): over-range input clamps to +1 at n=0") {
        // drive=2, bias=0, x=0.6: u=1.2 → hardClip(1.2)=1.0 (analytic: |u|>1, clamp)
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(2.0f);
        ws.setBias(0.0f);
        ws.setGainCompensation(false);

        CHECK(ws.process(0.6f) == doctest::Approx(1.0f).epsilon(1.0e-5));
    }

    SUBCASE("Waveshaper(hardClip): drive + bias together before shape at n=0") {
        // drive=2, bias=0.3, x=0.1: u=2·0.1+0.3=0.5 → hardClip(0.5)=0.5
        // (Distinguishes correct signal-chain order from bias-before-drive error.)
        Waveshaper ws;
        ws.init(kSampleRate);
        ws.setShape(Shape::hardClip);
        ws.setEvaluation(Evaluation::closedForm);
        ws.setDrive(2.0f);
        ws.setBias(0.3f);
        ws.setGainCompensation(false);

        CHECK(ws.process(0.1f) == doctest::Approx(0.5f).epsilon(1.0e-5));
    }

    SUBCASE("acfx::shape::hardClip pure function: clamp(u,-1,1) analytic values") {
        // The pure shape function is stateless and unaffected by any ADAA layering.
        // These are closed-form analytic truths, not fabricated numbers.
        CHECK(shape::hardClip(0.0f)  == doctest::Approx( 0.0f).epsilon(1.0e-7));
        CHECK(shape::hardClip(0.5f)  == doctest::Approx( 0.5f).epsilon(1.0e-7));
        CHECK(shape::hardClip(1.5f)  == doctest::Approx( 1.0f).epsilon(1.0e-7));
        CHECK(shape::hardClip(-0.5f) == doctest::Approx(-0.5f).epsilon(1.0e-7));
        CHECK(shape::hardClip(-1.5f) == doctest::Approx(-1.0f).epsilon(1.0e-7));
        // Odd symmetry: hardClip(−u) = −hardClip(u) for all u.
        CHECK(shape::hardClip(-0.7f) == doctest::Approx(-shape::hardClip(0.7f)).epsilon(1.0e-7));
    }
}

// PR#8 review (blocking): ADAAWaveshaper::setShape() must re-pair the cached
// antiderivative FPrev_ with the NEW shape. Before the fix, setShape only
// updated shape_, leaving FPrev_ holding the OLD shape's F(uPrev_); the next
// process() then computed (F_new(u) - F_old(uPrev_))/du, mixing two distinct
// antiderivatives. The bug is visible for a shape whose F(0) != 0 — e.g.
// algebraicAntideriv(0) = sqrt(1) = 1.0 — switched into after a default-tanh
// init (tanhAntideriv(0) = 0).
TEST_CASE("ADAAWaveshaper::setShape re-pairs ADAA history (no stale antiderivative)") {
    // Reference: algebraic selected BEFORE init, so init()'s reset() seeds
    // FPrev_ = algebraicAntideriv(0) = 1.0 correctly.
    ADAAWaveshaper ref;
    ref.setShape(Shape::algebraic);
    ref.setAdaaOrder(AdaaOrder::first);
    ref.init(kSampleRate);
    ref.setDrive(1.0f);
    ref.setBias(0.0f);
    ref.setGainCompensation(false);

    // Subject: default-tanh init (FPrev_ = 0), THEN switch to algebraic. After
    // the fix, setShape re-pairs FPrev_ = algebraicAntideriv(uPrev_=0) = 1.0, so
    // the first sample must match the reference exactly.
    ADAAWaveshaper subj;
    subj.setAdaaOrder(AdaaOrder::first);
    subj.init(kSampleRate);             // shape_ = tanh, FPrev_ = 0
    subj.setDrive(1.0f);
    subj.setBias(0.0f);
    subj.setGainCompensation(false);
    subj.setShape(Shape::algebraic);    // must re-pair FPrev_ to the new shape

    // Same input through both; |du| = |0.5| > kEps, so the difference-quotient
    // branch (not the midpoint fallback) is exercised.
    const float in = 0.5f;
    const float refOut  = ref.process(in);
    const float subjOut = subj.process(in);
    CHECK(subjOut == doctest::Approx(refOut).epsilon(1.0e-6));

    // Guard: the reference itself is the real ADAA average, not the stale-F value
    // 2*algebraicAntideriv(0.5) (= the buggy result). algebraicAntideriv(0.5) =
    // sqrt(1.25) ~= 1.118034; correct ADAA first sample = (1.118034 - 1)/0.5
    // ~= 0.236068; the stale-F bug would give ~= 2.236068.
    CHECK(refOut == doctest::Approx(0.236068f).epsilon(1.0e-4));
}

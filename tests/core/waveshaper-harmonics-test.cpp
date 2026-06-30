// waveshaper-harmonics-test.cpp
// T008 -- US1 harmonic-signature assertions (SC-001/SC-002)
// research.md Decision 1 (harmonic facts table) + Decision 7 (Goertzel reuse)
//
// TWO TEST CASES:
//   1. Symmetric tanh, bias=0  -> ODD-ONLY harmonics (3rd, 5th present; 2nd, 4th negligible)
//   2. Symmetric tanh, bias!=0 -> EVEN+ODD harmonics (2nd, 4th appear; 3rd remains present)
//
// Window design (anti-leakage):
//   f0=1000 Hz, sr=48000 Hz, N=4800.
//   f0 * N / sr = 1000 * 4800 / 48000 = 100 (integer cycles).
//   Every harmonic k*f0 for k=1..6 lands on an exact Goertzel bin.
//   Leakage is negligible; Parseval identity is exact over the window.
//
// Drive choice (per shape class — only tanh is tested here):
//   tanh (odd soft-clipper): drive=2.0 saturates peaks (tanh(2.0) ~= 0.964),
//     generating measurable odd harmonics (~5-10% of fundamental) while
//     keeping even harmonics below noise for bias=0.  This file covers tanh
//     exclusively, so kDrive=2.0 is appropriate for every test case here.
//   Chebyshev: requires |u| <= 1.0 for T_n to stay in its natural [-1,1]
//     domain; harmonic tests for chebyshev would use drive <= 1.0.
//   Asymmetric (diodeCurve): bounded for any drive; forward branch behaves
//     like tanh, so drive=2.0 would be adequate, but these are not tested here.
//   Folding (sineFold/triangleFold): fold depth is controlled by foldGain,
//     not drive; harmonic tests would sweep foldGain rather than drive.
//
// Tolerances (named, analytic-direction -- no fabricated magic numbers):
//   kOddPresentFloor  = 0.01    odd harmonics >= 1% of fundamental
//                               (actual 3rd ~5-10%; 1% is conservative lower bound)
//   kEvenNeglTol      = 1.0e-3  even harmonics < 0.1% in the symmetric case
//                               (leakage residual is ~1e-6; 1e-3 is 3 decades above)
//   kEven2ndFloor     = 0.01    2nd harmonic >= 1% of fundamental after bias
//                               (actual: typically 5-15%; 1% is a safe lower bound)
//   kEven4thFloor     = 1.0e-3  4th harmonic >= 0.1% of fundamental after bias
//                               (conservative; 4th is smaller but still measurable)
//
// Warm-up (biased test):
//   DC-blocker one-pole HP: y[n] = x[n] - x[n-1] + R*y[n-1], R=0.995.
//   tau = 1/(1-R) = 200 samples.  kTransientSamples=2000 ~= 10*tau.
//   Residual transient < R^2000 ~= e^-10 ~= 4.5e-5.  Well below all tolerances.
//   Phase is continued (t = (kTransientSamples + i) / sr) to avoid discontinuity.
//
// The DC-blocker removes the 0 Hz (DC) component produced by the bias but does
// NOT remove the 2nd or 4th harmonics -- those are AC content at 2*f0 and 4*f0,
// far above the blocker's ~38 Hz cutoff.  Assertions target the even HARMONICS,
// not the DC component (the DC assertion lives in waveshaper-test.cpp T007).
//
// Expected RED:
//   acfx::Waveshaper bodies (T010/T011) are not yet implemented; acfx::shape::*
//   bodies (T009) are pending.  This TU compiles cleanly (all symbols declared);
//   the link step fails with "undefined reference" on Waveshaper methods.  That
//   is the correct RED state.

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "primitives/nonlinear/waveshaper.h"
#include "core/measurement-support.h"

using namespace acfx;

namespace {

constexpr double      kF0               = 1000.0;   // fundamental frequency (Hz)
constexpr double      kSampleRate       = 48000.0;  // sample rate (Hz)
constexpr std::size_t kN                = 4800;     // 100 integer cycles (f0*N/sr=100)
constexpr int         kNumHarm          = 6;        // capture harmonics 1..6 (all < Nyquist)
constexpr float       kDrive            = 2.0f;     // pre-gain (saturates tanh peaks)
constexpr float       kAmplitude        = 1.0f;     // unit-amplitude sine input

// Analytic-direction tolerances (rationale in file header above).
constexpr double kOddPresentFloor  = 0.01;      // odd harm ratio must exceed this (symmetric)
constexpr double kEvenNeglTol      = 1.0e-3;    // even harm ratio must stay below this (symmetric)
constexpr double kEven2ndFloor     = 0.01;      // 2nd harm ratio must exceed this (biased)
constexpr double kEven4thFloor     = 1.0e-3;    // 4th harm ratio must exceed this (biased)
constexpr double kOddPresentBiased = 0.01;      // 3rd harm ratio must still exceed this (biased)

// DC-blocker warm-up: tau=200 samples; 2000 samples ~= 10*tau.
constexpr int kTransientSamples = 2000;

// Pi for manual sine generation in the warm-up loop.
constexpr float kPi = 3.14159265358979323846f;

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Symmetric shape (tanh), zero bias -> ODD-ONLY harmonics
//
// Analytic truth: an odd nonlinearity f(-u) = -f(u) applied to a pure
// cosine cos(wt) produces ONLY odd multiples of w (Decision 1: "tanh,
// symmetric").  With bias=0 the wrapper presents a symmetric signal to the
// symmetric shape.  Therefore:
//   * 3rd and 5th harmonics are present (above kOddPresentFloor).
//   * 2nd and 4th harmonics are negligible (below kEvenNeglTol).
// SC-001 / research.md Decision 1.
// ---------------------------------------------------------------------------

TEST_CASE("symmetric tanh, zero bias: odd harmonics present, even harmonics negligible (SC-001)") {
    Waveshaper ws;
    ws.init(static_cast<float>(kSampleRate));
    ws.setShape(Shape::tanh);
    ws.setEvaluation(Evaluation::closedForm);
    ws.setDrive(kDrive);
    ws.setBias(0.0f);
    ws.setGainCompensation(false);

    const meastest::HarmonicSignature sig =
        meastest::harmonicSignature(
            [&ws](float x) { return ws.process(x); },
            kF0, kSampleRate, kN, kNumHarm, kAmplitude);

    const double ratio2 = sig.ratio(2);
    const double ratio3 = sig.ratio(3);
    const double ratio4 = sig.ratio(4);
    const double ratio5 = sig.ratio(5);

    INFO("fund=" << sig.fundamental()
         << " 2nd=" << ratio2 << " 3rd=" << ratio3
         << " 4th=" << ratio4 << " 5th=" << ratio5);

    // Odd harmonics must be present above the analytic lower bound.
    CHECK(ratio3 > kOddPresentFloor);
    CHECK(ratio5 > kOddPresentFloor);

    // Even harmonics must be negligible (symmetry class: odd nonlinearity).
    CHECK(ratio2 < kEvenNeglTol);
    CHECK(ratio4 < kEvenNeglTol);
}

// ---------------------------------------------------------------------------
// TEST 2: Symmetric shape (tanh), nonzero bias -> EVEN + ODD harmonics
//
// Adding a bias (u = drive*x + bias, Decision 2) shifts the operating point
// and breaks the effective symmetry of the signal seen by the shape.  An odd
// nonlinearity f evaluated at a shifted argument f(A*sin + bias) produces
// both even and odd harmonics (Decision 1: "biased/asymmetric: even+odd").
//
// The wrapper's DC-blocker (FR-008) removes the mean (0 Hz) from the output
// but does NOT remove the 2nd or 4th harmonic -- those are AC content at
// 2*f0 and 4*f0, above the ~38 Hz cutoff.  We warm up kTransientSamples to
// let the DC-blocker settle, then assert on steady-state even harmonics.
//
// After settling:
//   * 2nd harmonic is present (above kEven2ndFloor) -- bias introduced it.
//   * 4th harmonic is present (above kEven4thFloor) -- same mechanism.
//   * 3rd harmonic remains present (above kOddPresentBiased) -- odd-order
//     distortion persists alongside the new even content.
//
// SC-001 / research.md Decision 1; SC-002 verifies DC-free (handled in T007).
// ---------------------------------------------------------------------------

TEST_CASE("symmetric tanh, nonzero bias: even harmonics appear above floor (SC-001/SC-002)") {
    constexpr float kBias = 0.5f;

    Waveshaper ws;
    ws.init(static_cast<float>(kSampleRate));
    ws.setShape(Shape::tanh);
    ws.setEvaluation(Evaluation::closedForm);
    ws.setDrive(kDrive);
    ws.setBias(kBias);
    ws.setGainCompensation(false);

    // Warm up: drive DC-blocker to steady state over kTransientSamples.
    // Sine phase starts at t=0 and advances continuously into the capture
    // window (t = (kTransientSamples + i) / sr) to avoid a phase jump.
    for (int i = 0; i < kTransientSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        ws.process(kAmplitude *
                   std::sin(2.0f * kPi * static_cast<float>(kF0) * t));
    }

    // Capture steady-state output, continuing the sine phase from warm-up.
    std::vector<float> outBuf(kN);
    for (std::size_t i = 0; i < kN; ++i) {
        const float t =
            static_cast<float>(kTransientSamples + static_cast<int>(i))
            / static_cast<float>(kSampleRate);
        const float x = kAmplitude *
                        std::sin(2.0f * kPi * static_cast<float>(kF0) * t);
        outBuf[i] = ws.process(x);
    }

    const meastest::HarmonicSignature sig =
        meastest::harmonicSignature(span<const float>(outBuf),
                                    kF0, kSampleRate, kNumHarm);

    const double ratio2 = sig.ratio(2);
    const double ratio3 = sig.ratio(3);
    const double ratio4 = sig.ratio(4);

    INFO("fund=" << sig.fundamental()
         << " 2nd=" << ratio2 << " 3rd=" << ratio3 << " 4th=" << ratio4);

    // Bias breaks symmetry: even harmonics must appear in the steady-state output.
    // DC-blocker removes DC (0 Hz) but passes these AC harmonics through.
    CHECK(ratio2 > kEven2ndFloor);
    CHECK(ratio4 > kEven4thFloor);

    // Odd harmonic content persists alongside the even content.
    CHECK(ratio3 > kOddPresentBiased);
}

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "primitives/dynamics/envelope-follower.h"

// T015 — US2 RMS-mode accuracy and ripple (TDD, written to spec).
//
// Asserts that acfx::EnvelopeFollower, in RMS detection mode, tracks the
// contract described in specs/envelope-followers/spec.md (SC-003, SC-004)
// and research.md Decision 3:
//
//   * RMS mode squares the input, accumulates a moving mean-square via a
//     ONE-POLE LEAKY INTEGRATOR whose time constant is `setRmsWindow(seconds)`
//     — independent of the attack/release ballistics — and returns
//     `sqrt(meanSquare)` as the linear-domain level (FR-009/FR-011).
//   * That linear level is then fed into the ballistics smoother (same
//     branching one-pole as peak mode, FR-013) before being returned from
//     process(). To isolate the RMS mean-square property from ballistics
//     lag, these tests use a FAST attack/release (1 ms) so the smoother
//     tracks its input essentially immediately, and a much longer RMS
//     window (50 ms) so the mean-square stage is the one doing the real
//     averaging.
//   * For a steady sine of amplitude A, RMS = A/sqrt(2) (SC-003): squaring a
//     sine gives A^2*sin^2(theta) = (A^2/2)*(1 - cos(2*theta)), i.e. a DC
//     term A^2/2 plus a ripple at TWICE the input frequency; a leaky
//     integrator with a window long relative to that ripple period passes
//     the DC term and heavily attenuates the ripple, so meanSquare settles
//     near A^2/2 and sqrt(meanSquare) settles near A/sqrt(2).
//   * SC-004: the settled envelope's residual peak-to-peak ripple must stay
//     below a small, named bound (see kRipplePeakToPeakBoundFrac below).
//
// EXPECTED-RED: RMS mode is NOT yet implemented — EnvelopeFollower::detect()
// currently falls through to peak behavior (`std::fabs(x)`, see the
// TODO(T016) in envelope-follower.h) for DetectMode::rms. Under that stub, a
// steady sine's detected level is the RECTIFIED sine |A*sin(...)|, not a
// smoothed A/sqrt(2): its mean is (2/pi)*A (~0.6366*A, not ~0.7071*A) and,
// with a fast 1 ms ballistics smoother, its ripple tracks the rectified
// waveform almost exactly (peak-to-peak on the order of A itself, far above
// the ripple bound below). So both the mean-value and ripple assertions FAIL
// now and are expected to PASS once T016 lands the RMS mean-square+sqrt
// path. These tests are written to the SPEC's intended behavior, not the
// current stub.

using acfx::Ballistics;
using acfx::DetectMode;
using acfx::EnvelopeFollower;

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr double kSampleRate = 48000.0;

// RMS mean-square window: long relative to the sine periods exercised below
// (1 kHz -> ripple at 2 kHz, period 24 samples) so the leaky integrator
// averages well and settled ripple is small (see the analytic estimate in
// the ripple test below).
constexpr float kRmsWindowSeconds = 0.050f; // 50 ms

// Ballistics fast enough that the post-mean-square smoother does not
// meaningfully delay or reshape the settled level; the RMS window is left to
// govern the averaging, per the contract note above.
constexpr float kBallisticsSeconds = 0.001f; // 1 ms

constexpr float kSineFrequencyHz = 1000.0f;

// exp(-1/(tau*fs)) governs settling of the mean-square DC term with time
// constant kRmsWindowSeconds; settling for 10 time constants leaves a
// residual initial-condition error of exp(-10) ~= 4.5e-5, i.e. negligible
// next to the tolerances below.
constexpr int kSettleSamples = static_cast<int>(10.0 * kRmsWindowSeconds * kSampleRate); // 24000

// Measurement window: 100 ms (4800 samples) at 1 kHz covers ~200 periods of
// the 2 kHz mean-square ripple, ample for both a stable mean and a
// representative peak-to-peak ripple reading.
constexpr int kMeasureSamples = 4800;

// A/sqrt(2), computed (not hand-typed) so the analytic RMS-of-sine identity
// is visibly the source of the expected value, not a magic number.
const double kInvSqrt2 = 1.0 / std::sqrt(2.0);

// Mean-value tolerance: settling error is negligible (see kSettleSamples
// above) and the analytic ripple-induced bias on the mean is second-order
// (the ripple estimate below is already < 1% p2p on the envelope, so its
// contribution to the *mean* is smaller still). +/-3% comfortably covers
// float rounding and any residual ripple while staying far tighter than the
// stub's failure mode (a mean near (2/pi)*A ~= 0.6366*A, ~10% off A/sqrt(2)
// ~= 0.7071*A), so the tolerance still makes the red test fail loudly.
constexpr double kMeanTolFrac = 0.03;

// Ripple bound (SC-004): analytically, the mean-square ripple at 2*f=2 kHz
// is attenuated by the RMS one-pole's magnitude response
//   |H(w)| = (1-a) / sqrt(1 - 2*a*cos(w) + a^2),  a = exp(-1/(tau_rms*fs))
// At tau_rms=50ms, fs=48kHz, f=1kHz: a ~= 0.99958, w = 2*pi*2000/48000, giving
// |H(w)| ~= 1.6e-3, so the meanSquare ripple amplitude is
// ~= |H(w)| * (A^2/2) and, propagated through d(sqrt)/dx = 1/(2*sqrt(x)) at
// x = A^2/2, the linear-domain envelope ripple is ~=0.16% peak-to-peak of
// A/sqrt(2) BEFORE the (further-attenuating) ballistics stage. 1% is a
// conservative bound with several-fold headroom over that estimate while
// remaining far below the stub's ripple (on the order of the full amplitude
// A, since the stub tracks the rectified sine directly), so it still
// distinguishes correct behavior from the red stub.
constexpr double kRipplePeakToPeakBoundFrac = 0.01;

struct SettledStats {
    double mean;
    double minVal;
    double maxVal;
};

// Configure a fresh RMS/branching follower at kSampleRate with a fast
// ballistics smoother and the shared kRmsWindowSeconds mean-square window.
EnvelopeFollower makeRmsFollower() {
    EnvelopeFollower ef;
    ef.init(static_cast<float>(kSampleRate));
    ef.setMode(DetectMode::rms);
    ef.setBallistics(Ballistics::branching);
    ef.setAttack(kBallisticsSeconds);
    ef.setRelease(kBallisticsSeconds);
    ef.setRmsWindow(kRmsWindowSeconds);
    ef.reset();
    return ef;
}

// Drive a steady sine of the given amplitude/frequency through a fresh RMS
// follower for kSettleSamples (discarding the initial transient), then
// return the mean/min/max of the envelope over the following
// kMeasureSamples.
SettledStats measureSettledSineRms(float amplitude, float frequencyHz) {
    EnvelopeFollower ef = makeRmsFollower();
    const double omega  = 2.0 * kPi * static_cast<double>(frequencyHz) / kSampleRate;

    for (int n = 0; n < kSettleSamples; ++n) {
        const float x = static_cast<float>(static_cast<double>(amplitude) * std::sin(omega * n));
        ef.process(x);
    }

    double sum    = 0.0;
    double minVal = std::numeric_limits<double>::infinity();
    double maxVal = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < kMeasureSamples; ++i) {
        const int n   = kSettleSamples + i;
        const float x = static_cast<float>(static_cast<double>(amplitude) * std::sin(omega * n));
        const double env = static_cast<double>(ef.process(x));
        sum += env;
        minVal = std::min(minVal, env);
        maxVal = std::max(maxVal, env);
    }
    return SettledStats{sum / static_cast<double>(kMeasureSamples), minVal, maxVal};
}

// Drive a constant (DC) input through a fresh RMS follower for
// kSettleSamples, then return the mean over a short trailing window (DC has
// no ripple to average out, so a short window suffices).
double measureSettledDcRms(float dcValue) {
    EnvelopeFollower ef = makeRmsFollower();

    for (int n = 0; n < kSettleSamples; ++n) {
        ef.process(dcValue);
    }

    constexpr int kDcMeasureSamples = 480; // 10 ms
    double sum = 0.0;
    for (int i = 0; i < kDcMeasureSamples; ++i) {
        sum += static_cast<double>(ef.process(dcValue));
    }
    return sum / static_cast<double>(kDcMeasureSamples);
}

} // namespace

TEST_CASE("rms of a steady sine settles to A/sqrt(2)") {
    SUBCASE("amplitude = 1.0") {
        constexpr float kAmplitude = 1.0f;
        const SettledStats stats   = measureSettledSineRms(kAmplitude, kSineFrequencyHz);

        const double expected  = static_cast<double>(kAmplitude) * kInvSqrt2;
        const double tolerance = kMeanTolFrac * expected;
        CHECK(std::fabs(stats.mean - expected) <= tolerance);
    }

    SUBCASE("amplitude = 0.5") {
        constexpr float kAmplitude = 0.5f;
        const SettledStats stats   = measureSettledSineRms(kAmplitude, kSineFrequencyHz);

        const double expected  = static_cast<double>(kAmplitude) * kInvSqrt2;
        const double tolerance = kMeanTolFrac * expected;
        CHECK(std::fabs(stats.mean - expected) <= tolerance);
    }
}

TEST_CASE("rms settled ripple stays below the bound") {
    SUBCASE("amplitude = 1.0") {
        constexpr float kAmplitude = 1.0f;
        const SettledStats stats   = measureSettledSineRms(kAmplitude, kSineFrequencyHz);

        const double expected      = static_cast<double>(kAmplitude) * kInvSqrt2;
        const double peakToPeak    = stats.maxVal - stats.minVal;
        const double ripplePeakBound = kRipplePeakToPeakBoundFrac * expected;
        CHECK(peakToPeak < ripplePeakBound);
    }

    SUBCASE("amplitude = 0.5") {
        constexpr float kAmplitude = 0.5f;
        const SettledStats stats   = measureSettledSineRms(kAmplitude, kSineFrequencyHz);

        const double expected        = static_cast<double>(kAmplitude) * kInvSqrt2;
        const double peakToPeak      = stats.maxVal - stats.minVal;
        const double ripplePeakBound = kRipplePeakToPeakBoundFrac * expected;
        CHECK(peakToPeak < ripplePeakBound);
    }
}

TEST_CASE("rms of DC equals |DC|") {
    // Mean-square of a constant c is c^2 exactly, so sqrt(meanSquare) = |c|:
    // a useful invariant distinct from (and simpler than) the sine case.
    SUBCASE("positive DC") {
        constexpr float kDc = 0.6f;
        const double measured = measureSettledDcRms(kDc);

        const double expected  = std::fabs(static_cast<double>(kDc));
        const double tolerance = kMeanTolFrac * expected;
        CHECK(std::fabs(measured - expected) <= tolerance);
    }

    SUBCASE("negative DC") {
        constexpr float kDc = -0.4f;
        const double measured = measureSettledDcRms(kDc);

        const double expected  = std::fabs(static_cast<double>(kDc));
        const double tolerance = kMeanTolFrac * expected;
        CHECK(std::fabs(measured - expected) <= tolerance);
    }
}

TEST_CASE("rms detection composes with the decoupled topology") {
    // Topology composition check: the RMS mean-square detection stage must
    // settle correctly no matter which ballistics topology processes its
    // output. Mirrors "rms of a steady sine settles to A/sqrt(2)" above but
    // with Ballistics::decoupled in place of the (default) branching
    // topology, across both the base (smooth=false) and smooth (smooth=true)
    // decoupled variants.
    constexpr float kAmplitude = 1.0f;

    auto measureDecoupled = [](bool smooth) {
        EnvelopeFollower ef;
        ef.init(static_cast<float>(kSampleRate));
        ef.setMode(DetectMode::rms);
        ef.setRmsWindow(kRmsWindowSeconds);
        ef.setBallistics(Ballistics::decoupled);
        ef.setSmooth(smooth);
        ef.setAttack(kBallisticsSeconds);
        ef.setRelease(kBallisticsSeconds);
        ef.reset();

        const double omega = 2.0 * kPi * static_cast<double>(kSineFrequencyHz) / kSampleRate;
        for (int n = 0; n < kSettleSamples; ++n) {
            const float x = static_cast<float>(static_cast<double>(kAmplitude) * std::sin(omega * n));
            ef.process(x);
        }

        double sum = 0.0;
        for (int i = 0; i < kMeasureSamples; ++i) {
            const int n   = kSettleSamples + i;
            const float x = static_cast<float>(static_cast<double>(kAmplitude) * std::sin(omega * n));
            sum += static_cast<double>(ef.process(x));
        }
        return sum / static_cast<double>(kMeasureSamples);
    };

    const double expected  = static_cast<double>(kAmplitude) * kInvSqrt2;
    const double tolerance = kMeanTolFrac * expected;

    SUBCASE("smooth = false") {
        CHECK(std::fabs(measureDecoupled(false) - expected) <= tolerance);
    }
    SUBCASE("smooth = true") {
        CHECK(std::fabs(measureDecoupled(true) - expected) <= tolerance);
    }
}

TEST_CASE("rms mode averages with the default window (no setRmsWindow call)") {
    // Pins that RMS mode performs real averaging OUT OF THE BOX: the
    // EnvelopeFollower's default rmsWindowSeconds_ (see envelope-follower.h)
    // is non-zero, so a caller that calls setMode(rms) WITHOUT ever calling
    // setRmsWindow() still gets sqrt(meanSquare) settling at A/sqrt(2) for a
    // steady sine -- NOT the degenerate zero-window case, which would
    // collapse meanSquare to x^2 every sample (i.e. |x| ~= A, a silent
    // regression to peak behavior Constitution V forbids).
    constexpr float kAmplitude = 1.0f;

    EnvelopeFollower ef;
    ef.init(static_cast<float>(kSampleRate));
    ef.setMode(DetectMode::rms);
    // Deliberately NOT calling setRmsWindow(): exercises the class default.
    ef.reset();

    const double omega = 2.0 * kPi * static_cast<double>(kSineFrequencyHz) / kSampleRate;
    for (int n = 0; n < kSettleSamples; ++n) {
        const float x = static_cast<float>(static_cast<double>(kAmplitude) * std::sin(omega * n));
        ef.process(x);
    }

    double sum = 0.0;
    for (int i = 0; i < kMeasureSamples; ++i) {
        const int n   = kSettleSamples + i;
        const float x = static_cast<float>(static_cast<double>(kAmplitude) * std::sin(omega * n));
        sum += static_cast<double>(ef.process(x));
    }
    const double measured = sum / static_cast<double>(kMeasureSamples);

    const double expected  = static_cast<double>(kAmplitude) * kInvSqrt2;
    const double tolerance = kMeanTolFrac * expected; // ~3%, matches empirical ~0.7075
    CHECK(std::fabs(measured - expected) <= tolerance);

    // Guard against the degenerate zero-window (peak-equivalent) case: the
    // measured mean must NOT be close to the rectified-peak mean (~A), which
    // is what a collapsed default window would yield.
    CHECK(std::fabs(measured - static_cast<double>(kAmplitude)) > tolerance);
}

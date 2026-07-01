// oversampler-transparency-test.cpp
// T007 -- User Story 1 suite: proves acfx::Oversampler<Factor> is transparent
// (i.e. does nothing but delay+filter) when driven with an identity
// nonlinearity (FR-007/011/016, SC-001).
//
// MEASUREMENT DOMAIN (deliberate choice -- read before "fixing" this test):
// assessment is FREQUENCY-DOMAIN (steady-state passband magnitude via a
// single-bin Goertzel analyzer), NOT a wideband sample-exact comparison
// against an integer-delayed reference. Oversampler::latencySamples() is the
// ROUNDED group delay; the true (analytic) group delay for Factor=4/8 is
// fractional (67.5 / 78.75 base-samples -- see oversampler.h's latency
// derivation comment), so an integer-delay sample-by-sample compare would
// FALSELY fail for those factors even though the cascade is a correct
// linear-phase halfband design. Factor=2's group delay (45) IS an integer,
// so that case additionally gets a direct latency-aligned sample compare as
// a bonus check, but the PRIMARY assertion for every factor is magnitude-
// based, per the oversampler author's explicit guidance for this task.

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "primitives/oversampling/oversampler.h"
#include "support/measurement/analyzers.h"
#include "support/measurement/stimulus.h"

using namespace acfx;
using acfx::measure::GoertzelAnalyzer;
using acfx::measure::SineGenerator;

namespace {

// ---------------------------------------------------------------------------
// NAMED TOLERANCES (FR-023 -- no magic numbers)
// ---------------------------------------------------------------------------

// Passband-ripple tolerance: the halfband cascade's design target is <= 0.1
// dB passband ripple (contracts/oversampling-api.md; halfband-coefficients.h
// provenance) and its MEASURED ripple is far tighter still (~0.001 dB, per
// the task brief). 0.1 dB expressed as a linear-amplitude fraction is
//   10^(0.1/20) - 1 = 0.0115794...
// i.e. a ~1.16% deviation from unity gain. Using the DESIGN target (not the
// measured ~0.001 dB figure) as the pass/fail bound keeps the assertion
// generous enough to be stable across compiler/platform floating-point
// variation and the cascade's compounding multi-stage ripple, while staying
// two orders of magnitude tighter than would be needed to hide a real
// transparency defect (a broken passband misses this bound by 10x-100x, see
// self-check notes below).
constexpr double kPassbandRippleTol = 0.0115794; // 10^(0.1/20) - 1

// Silence-in/out tolerance: with an identity eval and an all-zero input, the
// (linear, zero-initial-state) halfband cascade must produce EXACT zero --
// there is no rounding accumulation path that could produce a nonzero
// result from an all-zero input through a linear FIR. The silence check is
// therefore a bit-exact `out == 0.0f` (a `doctest::Approx(0).epsilon(0)`
// would be self-defeating: doctest's Approx compares
// |lhs-rhs| < epsilon*(scale+max|lhs|,|rhs|), which for epsilon 0 and rhs 0
// reduces to `0 < 0` and fails even on an exact zero).

// reset()-equality tolerance: a freshly-reset instance and a freshly-
// constructed instance run the IDENTICAL deterministic floating-point
// instruction sequence on the identical input, so their outputs must be
// bit-for-bit equal. The tiny nonzero epsilon below exists only to absorb
// any incidental non-associative FP reordering the compiler might introduce
// between the two (textually distinct) call sites -- it is not modeling any
// real uncertainty in the algorithm itself.
constexpr float kResetEqualityTol = 1.0e-6f;

// ---------------------------------------------------------------------------
// STIMULUS / WINDOW -- kMeasure chosen so EVERY probe frequency below
// completes an integer number of cycles in the measurement window
// (leakage-free single-bin Goertzel readout, per measurement-support.h's
// window contract):
//   100 Hz  * 4800 / 48000 =   10 cycles
//   1000 Hz * 4800 / 48000 =  100 cycles
//   5000 Hz * 4800 / 48000 =  500 cycles
//   10000Hz * 4800 / 48000 = 1000 cycles
// kSettle (2000 samples) is far larger than the cascade's worst-case group
// delay (Factor=8 -> ~79 base samples, oversampler.h) and its transient
// tail, so the trailing kMeasure window is read in the settled steady state.
// ---------------------------------------------------------------------------
constexpr double      kSampleRate = 48000.0;
constexpr std::size_t kSettle     = 2000;
constexpr std::size_t kMeasure    = 4800;

constexpr double kProbeFreqsHz[] = {100.0, 1000.0, 5000.0, 10000.0};

// Identity nonlinearity: the primitive must be transparent when this is the
// caller's evalAtHighRate (FR-007/SC-001).
constexpr auto kIdentityEval = [](float s) noexcept { return s; };

// Drive `osc` (already init()'d) with a `freqHz` unit sine for kSettle +
// kMeasure samples and return the trailing kMeasure output samples (the
// settled steady-state region).
template <int Factor>
std::vector<float> settledResponse(Oversampler<Factor>& osc, double freqHz) {
    const std::size_t total = kSettle + kMeasure;
    std::vector<float> in(total, 0.0f);
    SineGenerator{freqHz, kSampleRate, 1.0f, 0.0}.fill(acfx::span<float>(in));

    std::vector<float> out(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        out[n] = osc.process(in[n], kIdentityEval);

    return std::vector<float>(out.begin() + static_cast<std::ptrdiff_t>(kSettle), out.end());
}

// Core transparency assertion for one Factor: at every named probe
// frequency, the settled steady-state magnitude must be unity within
// kPassbandRippleTol (FR-007, SC-001).
template <int Factor>
void checkIdentityTransparency() {
    for (const double freqHz : kProbeFreqsHz) {
        Oversampler<Factor> osc;
        osc.init(static_cast<float>(kSampleRate));

        const std::vector<float> settled = settledResponse(osc, freqHz);
        const GoertzelAnalyzer::Bin bin =
            GoertzelAnalyzer{freqHz, kSampleRate}.analyze(acfx::span<const float>(settled));

        INFO("Factor=" << Factor << " freqHz=" << freqHz << " magnitude=" << bin.magnitude);
        CHECK(bin.magnitude == doctest::Approx(1.0).epsilon(kPassbandRippleTol));
    }
}

// Silence-in -> silence-out, no NaN/Inf/denormal (FR-016).
template <int Factor>
void checkSilenceInSilenceOut() {
    Oversampler<Factor> osc;
    osc.init(static_cast<float>(kSampleRate));

    constexpr std::size_t N = kSettle + kMeasure;
    for (std::size_t n = 0; n < N; ++n) {
        const float out = osc.process(0.0f, kIdentityEval);
        REQUIRE(std::isfinite(out));
        CHECK(std::fpclassify(out) != FP_SUBNORMAL);
        CHECK(out == 0.0f);
    }
}

// reset() restores fresh behavior: a warmed-up-then-reset instance must
// reproduce a freshly-constructed instance's output sample-for-sample
// (FR-011).
template <int Factor>
void checkResetRestoresFreshBehavior() {
    constexpr std::size_t kWarmup = 500;
    constexpr std::size_t kTest   = 500;

    std::vector<float> warmup(kWarmup, 0.0f);
    SineGenerator{437.0, kSampleRate, 0.8f, 0.0}.fill(acfx::span<float>(warmup));

    std::vector<float> test(kTest, 0.0f);
    SineGenerator{1000.0, kSampleRate, 1.0f, 0.0}.fill(acfx::span<float>(test));

    // Instance A: dirty its state with the warm-up signal, then reset().
    Oversampler<Factor> a;
    a.init(static_cast<float>(kSampleRate));
    for (const float x : warmup)
        (void)a.process(x, kIdentityEval);
    a.reset();

    std::vector<float> outA(kTest, 0.0f);
    for (std::size_t n = 0; n < kTest; ++n)
        outA[n] = a.process(test[n], kIdentityEval);

    // Instance B: freshly constructed, never warmed up.
    Oversampler<Factor> b;
    b.init(static_cast<float>(kSampleRate));

    std::vector<float> outB(kTest, 0.0f);
    for (std::size_t n = 0; n < kTest; ++n)
        outB[n] = b.process(test[n], kIdentityEval);

    for (std::size_t n = 0; n < kTest; ++n) {
        INFO("Factor=" << Factor << " n=" << n << " outA=" << outA[n] << " outB=" << outB[n]);
        CHECK(outA[n] == doctest::Approx(outB[n]).epsilon(kResetEqualityTol));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// TEST CASES -- one per Factor in {2, 4, 8} (US2 parameterizes further; this
// file's scope per T007 is proving transparency, not sweeping the factor
// dimension exhaustively).
// ---------------------------------------------------------------------------

TEST_CASE("Oversampler<2>: identity eval is transparent within the passband-ripple tolerance "
          "(FR-007, SC-001)") {
    checkIdentityTransparency<2>();
}

TEST_CASE("Oversampler<4>: identity eval is transparent within the passband-ripple tolerance "
          "(FR-007, SC-001)") {
    checkIdentityTransparency<4>();
}

TEST_CASE("Oversampler<8>: identity eval is transparent within the passband-ripple tolerance "
          "(FR-007, SC-001)") {
    checkIdentityTransparency<8>();
}

TEST_CASE("Oversampler<2>: silence-in produces silence-out, no NaN/Inf/denormal (FR-016)") {
    checkSilenceInSilenceOut<2>();
}

TEST_CASE("Oversampler<4>: silence-in produces silence-out, no NaN/Inf/denormal (FR-016)") {
    checkSilenceInSilenceOut<4>();
}

TEST_CASE("Oversampler<8>: silence-in produces silence-out, no NaN/Inf/denormal (FR-016)") {
    checkSilenceInSilenceOut<8>();
}

TEST_CASE("Oversampler<2>: reset() restores fresh behavior (FR-011)") {
    checkResetRestoresFreshBehavior<2>();
}

TEST_CASE("Oversampler<4>: reset() restores fresh behavior (FR-011)") {
    checkResetRestoresFreshBehavior<4>();
}

TEST_CASE("Oversampler<8>: reset() restores fresh behavior (FR-011)") {
    checkResetRestoresFreshBehavior<8>();
}

// ---------------------------------------------------------------------------
// BONUS CHECK -- Factor=2 only: latencySamples() (45) is an INTEGER group
// delay, so (unlike Factor=4/8's fractional delay) a direct time-domain
// latency-aligned sample compare is well-posed here. Because the cascade is
// linear-phase (FR-004: pure gain error, no phase error), the per-sample
// difference between the output and the latency-delayed input is bounded by
// (passband gain error) * |input| <= kPassbandRippleTol * 1.0 (unit
// amplitude stimulus) -- so the SAME named tolerance applies directly as an
// absolute per-sample bound.
// ---------------------------------------------------------------------------

TEST_CASE("Oversampler<2>: latency-aligned identity output matches input sample-for-sample "
          "within the passband-ripple tolerance (FR-007, integer-latency special case)") {
    constexpr int Factor = 2;
    Oversampler<Factor> osc;
    osc.init(static_cast<float>(kSampleRate));
    REQUIRE(osc.latencySamples() == 45);

    constexpr double      freqHz = 1000.0;
    constexpr std::size_t total  = kSettle + kMeasure;
    std::vector<float> in(total, 0.0f);
    SineGenerator{freqHz, kSampleRate, 1.0f, 0.0}.fill(acfx::span<float>(in));

    std::vector<float> out(total, 0.0f);
    for (std::size_t n = 0; n < total; ++n)
        out[n] = osc.process(in[n], kIdentityEval);

    const int delay = osc.latencySamples();
    for (std::size_t n = kSettle; n < total; ++n) {
        const float delayed = in[n - static_cast<std::size_t>(delay)];
        INFO("n=" << n << " out=" << out[n] << " delayed-in=" << delayed);
        CHECK(std::abs(out[n] - delayed) <= kPassbandRippleTol);
    }
}

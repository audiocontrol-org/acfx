// analysis-imd-test.cpp
// T014 -- harmonic-analysis feature, User Story 2: RED test for twin-tone
// intermodulation distortion (IMD) by the SMPTE and CCIF methods
// (contracts/analysis-engine-api.md "imd.h", data-model.md "ImdResult",
// research.md Decision 5, spec.md US2 / FR-003).
//
// Definition under test (FR-003, research Decision 5):
//   Twin-tone IMD. The engine builds the twin-tone stimulus internally, drives
//   it through the effect/callable, and measures the intermodulation PRODUCT
//   bins -- reporting DIFFERENCE and SUM products.
//     - SMPTE: f1 = 60 Hz + f2 = 7000 Hz, amplitude ratio 4:1 (low tone 4x the
//       high tone). Products are the sidebands of the high tone at 7000 +/- k*60:
//       lower sidebands (difference) and upper sidebands (sum).
//     - CCIF: f1 = 19 kHz + f2 = 20 kHz, equal amplitude. Difference product at
//       f2-f1 = 1 kHz; higher-order difference products 2f1-f2 = 18 kHz and
//       2f2-f1 = 21 kHz; sum product f1+f2 = 39 kHz (above Nyquist -> NaN).
//   A product bin coinciding with a harmonic of either tone is attributed
//   unambiguously (documented), never double-counted (spec edge case).
//
// ANALYTIC REFERENCE -- a KNOWN memoryless quadratic nonlinearity:
//   y = x + a*x^2,   x = A1*sin(w1 n) + A2*sin(w2 n)
//   x^2 expands (product-to-sum identities) to exactly:
//     DC     : (A1^2 + A2^2)/2
//     2f1    : A1^2/2 * (-cos 2w1)          amplitude A1^2/2   (HARMONIC, not IMD)
//     2f2    : A2^2/2 * (-cos 2w2)          amplitude A2^2/2   (HARMONIC, not IMD)
//     f2-f1  : A1*A2 *  cos(w1-w2)          amplitude A1*A2    (DIFFERENCE product)
//     f1+f2  : A1*A2 * (-cos(w1+w2))        amplitude A1*A2    (SUM product)
//   so through y = x + a*x^2 every product amplitude is scaled by a:
//     difference product amplitude = a*A1*A2
//     sum        product amplitude = a*A1*A2
//     2f1 harmonic amplitude       = a*A1^2/2   (distinct from a*A1*A2 -> the
//                                                analyzer must NOT confuse the
//                                                harmonic bin for the product)
//   A pure quadratic has NO 3rd-order products, so the higher-order sidebands
//   (7000+/-120 for SMPTE; 2f1-f2 / 2f2-f1 for CCIF) are ~0 -- a strong check.
//   A linear system (a = 0) has NO products at all -> every product ~0.
//
// Asserted contract for the NOT-YET-EXISTING host/analysis/imd.h:
//   - SMPTE quadratic: difference (7000-60) and sum (7000+60) products both
//     equal a*A1*A2 within a named tolerance; the 2nd-order-only sidebands
//     (7000+/-120) are ~0; a linear (a=0) system -> all products ~0.
//   - CCIF quadratic: the 1 kHz difference product equals a*A1*A2; the
//     3rd-order products (18 kHz, 21 kHz) are ~0; the 39 kHz sum product is
//     out-of-band -> NaN (FR-008), never a fabricated 0.0.
//   - Attribution: the reported difference-product amplitude is the intermod
//     value a*A1*A2, NOT the 2f1 harmonic value a*A1^2/2; product frequency
//     sets are disjoint and never a tone harmonic (documented, no double-count).
//
// host/analysis/imd.h does not exist yet at RED time -- this test is expected to
// FAIL TO BUILD until T015 lands it. Do NOT implement imd.h to make this test
// pass; that is a separate task.
//
// Integer-cycle windows throughout: the engine builds the stimulus at
// sampleRate = 48000, N = 4800 (10 Hz/bin), so 60, 7000, 19000, 20000 and every
// sum/difference/harmonic bin (6940, 7060, 6880, 7120, 1000, 18000, 21000, 120,
// 14000, ...) land EXACTLY on their own Goertzel bin -- leakage-free, mirroring
// every other suite in this tree (analysis-fft-test.cpp, analysis-thdn-test.cpp).

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "analysis/imd.h"  // UNDER TEST (does not exist at RED time)
#include "dsp/audio-block.h"
#include "dsp/process-context.h"
#include "dsp/span.h"

using acfx::analysis::ImdMethod;
using acfx::analysis::ImdResult;
using acfx::analysis::imd;
using acfx::analysis::kImdSampleRate;

namespace {

// Minimal identity Effect (prepare/reset/process no-ops, passthrough) --
// enough to satisfy the Effect-contract front door's template constraints
// for the ctx.sampleRate validation test below (code-review finding D2).
// The mismatched-sampleRate throw is expected to fire before any
// process()/capture() work happens.
struct IdentityEffect {
    void prepare(const acfx::ProcessContext&) {}
    void reset() {}
    void process(acfx::AudioBlock&) {}
};

// The stimulus and output are float32 buffers; an integer-cycle Goertzel on an
// exact bin is analytically exact up to that float round-trip (~1e-6 relative).
// 2% mirrors analysis-spectrum-test.cpp's kMagnitudeTolerance / analysis-thdn-
// test.cpp's kHarmonicRelTol -- generous, yet it fails a wrong scale factor, a
// mislocated product bin, or a harmonic-for-product confusion. Named bound, not
// a fitted constant (anti-false-precision).
constexpr double kProductRelTol = 0.02;

// A product bin that should carry NO energy (a 3rd-order product of a pure
// quadratic, or any product of a linear system) still shows the float round-off
// floor of the synthesized buffers (~1e-6 amplitude relative to unit tones).
// This ceiling sits several orders above that floor yet fails any real product.
constexpr double kAbsentProductCeiling = 1.0e-4;

// The quadratic coefficient and tone amplitudes are chosen so every measured
// quantity is DISTINCT: the intermod product a*A1*A2 differs from the 2f1
// harmonic a*A1^2/2 and the 2f2 harmonic a*A2^2/2, so a correct reading can
// only come from the correct bin.
constexpr double kQuadCoeff = 0.5;

// SMPTE amplitudes: low tone (60 Hz) is 4x the high tone (7000 Hz) per FR-003.
constexpr double kSmpteA1 = 0.4;   // 60 Hz  (low)
constexpr double kSmpteA2 = 0.1;   // 7000 Hz (high)   ratio 4:1

// CCIF amplitudes: equal per FR-003.
constexpr double kCcifA = 0.4;     // 19 kHz and 20 kHz

// A memoryless quadratic y = x + a*x^2 as a per-sample callable (the engine
// drives it via captureCallable). a = 0 collapses it to a linear pass-through.
auto quadratic(double a) {
    return [a](float x) {
        const double xd = static_cast<double>(x);
        return static_cast<float>(xd + a * xd * xd);
    };
}

} // namespace

TEST_CASE("imd SMPTE: quadratic twin-tone -> difference & sum products match analytic (FR-003, US2)") {
    const ImdResult r = imd(quadratic(kQuadCoeff), ImdMethod::SMPTE);

    REQUIRE(r.method == ImdMethod::SMPTE);
    REQUIRE(r.differenceProducts.size() >= 1);
    REQUIRE(r.sumProducts.size() >= 1);
    REQUIRE(r.differenceFreqHz.size() == r.differenceProducts.size());
    REQUIRE(r.sumFreqHz.size() == r.sumProducts.size());

    // Analytic 2nd-order product amplitude = a * A1 * A2.
    const double expectedProduct = kQuadCoeff * kSmpteA1 * kSmpteA2;  // 0.5*0.4*0.1 = 0.02

    // The first difference/sum entries are the k=1 sidebands 7000-/+60.
    REQUIRE(r.differenceFreqHz[0] == doctest::Approx(6940.0));
    REQUIRE(r.sumFreqHz[0] == doctest::Approx(7060.0));
    CHECK(r.differenceProducts[0] == doctest::Approx(expectedProduct).epsilon(kProductRelTol));
    CHECK(r.sumProducts[0] == doctest::Approx(expectedProduct).epsilon(kProductRelTol));

    // Attribution: the reported difference product is the INTERMOD value
    // a*A1*A2, NOT the 2f1 harmonic value a*A1^2/2 (= 0.04) -- proving the
    // analyzer read the intermod bin, not a nearby harmonic bin.
    const double harmonic2f1 = kQuadCoeff * kSmpteA1 * kSmpteA1 / 2.0;  // 0.04
    CHECK(std::abs(r.differenceProducts[0] - harmonic2f1) > 10.0 * kAbsentProductCeiling);

    // A pure quadratic has no 3rd-order sidebands (7000 +/- 120), if reported.
    if (r.differenceProducts.size() >= 2) {
        REQUIRE(r.differenceFreqHz[1] == doctest::Approx(6880.0));
        CHECK(r.differenceProducts[1] < kAbsentProductCeiling);
    }
    if (r.sumProducts.size() >= 2) {
        REQUIRE(r.sumFreqHz[1] == doctest::Approx(7120.0));
        CHECK(r.sumProducts[1] < kAbsentProductCeiling);
    }

    // imdRatio is a positive, finite, small ratio for this mild nonlinearity.
    REQUIRE(!std::isnan(r.imdRatio));
    CHECK(r.imdRatio > 0.0);
    CHECK(r.imdRatio < 1.0);
}

TEST_CASE("imd SMPTE: linear system (a=0) -> all products ~0 (FR-003, US2)") {
    const ImdResult r = imd(quadratic(0.0), ImdMethod::SMPTE);

    for (const double p : r.differenceProducts) {
        CHECK(p < kAbsentProductCeiling);
    }
    for (const double p : r.sumProducts) {
        // Out-of-band sum products are NaN (not-measured); in-band ones are ~0.
        if (!std::isnan(p)) {
            CHECK(p < kAbsentProductCeiling);
        }
    }
    // No intermodulation -> the product/carrier ratio is ~0.
    REQUIRE(!std::isnan(r.imdRatio));
    CHECK(r.imdRatio < kAbsentProductCeiling);
}

TEST_CASE("imd CCIF: quadratic twin-tone -> 1 kHz difference product matches analytic (FR-003, US2)") {
    const ImdResult r = imd(quadratic(kQuadCoeff), ImdMethod::CCIF);

    REQUIRE(r.method == ImdMethod::CCIF);
    REQUIRE(r.differenceProducts.size() >= 1);
    REQUIRE(r.differenceFreqHz.size() == r.differenceProducts.size());

    // Difference product at f2 - f1 = 1000 Hz, amplitude a*A1*A2.
    const double expectedProduct = kQuadCoeff * kCcifA * kCcifA;  // 0.5*0.4*0.4 = 0.08
    REQUIRE(r.differenceFreqHz[0] == doctest::Approx(1000.0));
    CHECK(r.differenceProducts[0] == doctest::Approx(expectedProduct).epsilon(kProductRelTol));

    // Higher-order (3rd) difference products 2f1-f2 = 18 kHz, 2f2-f1 = 21 kHz
    // are ~0 for a pure quadratic, if reported.
    for (std::size_t i = 1; i < r.differenceProducts.size(); ++i) {
        const double f = r.differenceFreqHz[i];
        CHECK((f == doctest::Approx(18000.0) || f == doctest::Approx(21000.0)));
        CHECK(r.differenceProducts[i] < kAbsentProductCeiling);
    }
}

TEST_CASE("imd CCIF: sum product f1+f2 = 39 kHz is out-of-band -> NaN, never 0.0 (FR-003, FR-008)") {
    const ImdResult r = imd(quadratic(kQuadCoeff), ImdMethod::CCIF);

    REQUIRE(r.sumProducts.size() >= 1);
    REQUIRE(r.sumFreqHz.size() == r.sumProducts.size());

    // 39 kHz is above Nyquist (24 kHz @ 48 kHz) -> unmeasurable, NaN (FR-008),
    // never a fabricated 0.0 that would masquerade as "no sum product".
    REQUIRE(r.sumFreqHz[0] == doctest::Approx(39000.0));
    CHECK(std::isnan(r.sumProducts[0]));
}

TEST_CASE("imd: product frequency sets are disjoint and never a tone harmonic (attribution, no double-count)") {
    for (const ImdMethod method : {ImdMethod::SMPTE, ImdMethod::CCIF}) {
        const ImdResult r = imd(quadratic(kQuadCoeff), method);

        // Difference and sum product frequency sets share no frequency (a bin is
        // attributed to exactly one product class -> no double-counting).
        for (const double df : r.differenceFreqHz) {
            for (const double sf : r.sumFreqHz) {
                CHECK(df != doctest::Approx(sf));
            }
        }

        // Tone fundamentals/harmonics: for SMPTE 2f1 = 120, 2f2 = 14000; for
        // CCIF the low harmonics. No PRODUCT frequency coincides with a low-order
        // harmonic of either tone (products are attributed to intermodulation,
        // harmonics are owned by the spectrum/THD path).
        const double f1 = (method == ImdMethod::SMPTE) ? 60.0 : 19000.0;
        const double f2 = (method == ImdMethod::SMPTE) ? 7000.0 : 20000.0;
        auto notAHarmonic = [&](double f) {
            for (int k = 2; k <= 3; ++k) {
                CHECK(f != doctest::Approx(k * f1));
                CHECK(f != doctest::Approx(k * f2));
            }
        };
        for (const double df : r.differenceFreqHz) notAHarmonic(df);
        for (const double sf : r.sumFreqHz) notAHarmonic(sf);
    }
}

TEST_CASE("imd Effect overload: ctx.sampleRate != kImdSampleRate throws (fail-loud, code-review D2)") {
    IdentityEffect fx;
    // A caller error -- e.g. a ProcessContext built at the host's live
    // sample rate rather than the fixed internal integer-cycle rate the
    // stimulus is built at -- must be rejected loudly, never silently
    // corrected or silently measured against the wrong rate.
    const acfx::ProcessContext wrongRateCtx{kImdSampleRate * 0.5, 512, 1};
    CHECK_THROWS_AS(imd(fx, wrongRateCtx, ImdMethod::SMPTE), std::invalid_argument);
}

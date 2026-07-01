#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "primitives/oversampling/halfband-coefficients.h"

// T012 -- User Story 2 suite: the half-band FIR coefficient table
// (acfx::kHalfbandCoeffs, acfx::kHalfbandTaps) meets its design passband/
// stopband targets, checked against ANALYTIC FIR truth -- a direct DFT
// (frequency-response) sum over the 91 taps -- not the Oversampler runtime.
//
// Tolerance philosophy (anti-false-precision, mirrors tests/support/
// svf-reference.h and tests/core/measurement-response-test.cpp): we assert
// NAMED, documented bounds derived from the coefficient table's own
// provenance header (core/primitives/oversampling/halfband-coefficients.h),
// which records design targets (stopband >= 80.0 dB, passband ripple
// <= 0.10 dB) and the generator's achieved values (~84.438 dB / ~0.00101 dB,
// measured via a dense 20001-point sweep over [0, 0.5]). The constants below
// reproduce those DESIGN targets verbatim (not the tighter achieved values)
// so this suite tests the contract the table promises, with headroom against
// float round-trip and this suite's own (coarser, 4001-point) grid.
//
// References: specs/oversampling/tasks.md T012; specs/oversampling/spec.md
// FR-009/FR-023, SC-003; specs/oversampling/contracts/oversampling-api.md;
// specs/oversampling/quickstart.md Scenario 3;
// core/primitives/oversampling/halfband-coefficients.h.

namespace {

// ---------------------------------------------------------------------------
// Analytic FIR magnitude response: H(f) = | sum_n h[n] * exp(-j*2*pi*f*n) |
// for normalized frequency f in [0, 0.5] (f = freqHz / sampleRate). Plain
// double-precision DFT sum over the 91 taps -- standard library only, no
// oversampler/runtime involvement, matching the generator's own measurement
// method noted in the coefficient table's provenance comment.
// ---------------------------------------------------------------------------
double halfbandMagnitude(double f) {
    const double w = 2.0 * std::numbers::pi * f;
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < acfx::kHalfbandTaps; ++n) {
        const double h = static_cast<double>(acfx::kHalfbandCoeffs[n]);
        re += h * std::cos(w * static_cast<double>(n));
        im -= h * std::sin(w * static_cast<double>(n));
    }
    return std::sqrt(re * re + im * im);
}

// ---------------------------------------------------------------------------
// Named tolerances (FR-009, FR-023) -- design targets from the coefficient
// table's provenance header, not the tighter achieved measurement, so the
// suite validates the CONTRACT the table promises rather than re-asserting
// today's exact generator output as if it were a spec.
// ---------------------------------------------------------------------------

// Design passband edge / stopband edge (normalized frequency, cycles/sample),
// i.e. the transition band the generator targeted for a 0.25-cutoff halfband
// (see gen-halfband.cpp provenance in halfband-coefficients.h).
constexpr double kPassbandEdge = 0.22;
constexpr double kStopbandEdge = 0.28;

// Passband ripple bound: the provenance header's design target is <= 0.10 dB
// (achieved ~0.00101 dB); 0.10 dB leaves ~100x headroom over the achieved
// value while still being a real, meaningful bound (FR-023 -- named, not
// loosened to whatever this run happens to measure).
constexpr double kPassbandRippleDb = 0.10;

// Stopband attenuation bound: design target is >= 80.0 dB (achieved
// ~84.438 dB); 80 dB is the documented contract floor.
constexpr double kStopbandDb = 80.0;

// Grid density for the passband/stopband sweeps -- fine enough (>2000 points
// across the two half-width sub-bands) to catch a genuine ripple/attenuation
// excursion without asserting the false precision of an exhaustive sweep.
constexpr int kGridPoints = 2000;

} // namespace

TEST_CASE("Half-band FIR passband ripple stays within the named design bound (FR-009/FR-023)") {
    double maxAbsRippleDb = 0.0;
    for (int i = 0; i <= kGridPoints; ++i) {
        const double f = kPassbandEdge * static_cast<double>(i) / static_cast<double>(kGridPoints);
        const double mag = halfbandMagnitude(f);
        REQUIRE(mag > 0.0); // passband magnitude must never collapse to zero
        const double db = 20.0 * std::log10(mag);
        maxAbsRippleDb = std::max(maxAbsRippleDb, std::abs(db));
    }
    INFO("measured passband ripple (dB) = " << maxAbsRippleDb);
    CHECK(maxAbsRippleDb <= kPassbandRippleDb);
}

TEST_CASE("Half-band FIR stopband attenuation meets the named design bound (FR-009/FR-023)") {
    double worstAttenuationDb = 1.0e9; // smallest (least negative-magnitude) attenuation seen
    for (int i = 0; i <= kGridPoints; ++i) {
        const double f = kStopbandEdge
                        + (0.5 - kStopbandEdge) * static_cast<double>(i) / static_cast<double>(kGridPoints);
        const double mag = halfbandMagnitude(f);
        REQUIRE(mag >= 0.0);
        // Guard the degenerate mag == 0 case (attenuation -> +infinity, which
        // only helps the bound) before taking log10.
        const double db = (mag > 0.0) ? 20.0 * std::log10(mag) : -1.0e9;
        worstAttenuationDb = std::min(worstAttenuationDb, -db);
    }
    INFO("measured stopband attenuation (dB) = " << worstAttenuationDb);
    CHECK(worstAttenuationDb >= kStopbandDb);
}

TEST_CASE("Half-band FIR structural properties are exact analytic truths (FR-009)") {
    constexpr int centerIndex = (acfx::kHalfbandTaps - 1) / 2; // 45 for 91 taps

    // Center tap is exactly 0.5 (the half-band unity-passband normalization).
    // Bit-exact: these are exact analytic truths of the table, and
    // doctest::Approx(x).epsilon(0) is self-defeating (it compares
    // |lhs-rhs| < epsilon*(...) which for epsilon 0 is `< 0`, failing on equals).
    CHECK(acfx::kHalfbandCoeffs[centerIndex] == 0.5f);

    // Symmetric (Type-I linear phase): h[n] == h[N-1-n] for every tap (bit-exact).
    for (int n = 0; n < acfx::kHalfbandTaps; ++n) {
        const int mirror = acfx::kHalfbandTaps - 1 - n;
        CHECK(acfx::kHalfbandCoeffs[n] == acfx::kHalfbandCoeffs[mirror]);
    }

    // Half-band structure: every tap at an even offset from the center index
    // (excluding the center itself) is exactly zero.
    for (int n = 0; n < acfx::kHalfbandTaps; ++n) {
        const int offset = n - centerIndex;
        if (offset != 0 && (offset % 2) == 0) {
            CHECK(acfx::kHalfbandCoeffs[n] == 0.0f);
        }
    }
}

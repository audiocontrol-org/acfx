#pragma once

// host/analysis/aliasing.h
//
// Inharmonic / aliased-energy measure (T006; research.md Decision 8; FR-022).
// Namespace: acfx::measure.  Host-side / offline ONLY — may allocate.
//
// RELOCATED (harmonic-analysis T007; research.md Decision 1/8; analyze finding
// F1) from tests/support/measurement/aliasing.h into host/analysis/ so
// product adapters can reuse it without depending on the test tree.
// tests/support/measurement/aliasing.h is now a thin re-export shim; existing
// `acfx::measure::aliasingMeasure(...)` and `meastest::aliasingMeasure(...)`
// call sites are unaffected.

#include <cstddef>  // std::size_t

#include "analysis/analyzers.h"  // GoertzelAnalyzer
#include "dsp/span.h"

namespace acfx::measure {

// ---------------------------------------------------------------------------
// AliasingMeasure
//
// Quantifies output energy that is NOT part of the input's harmonic series —
// i.e. the aliased/folded content a naive (non-bandlimited) nonlinearity
// produces when a high-frequency tone's harmonics exceed Nyquist and fold back
// onto non-harmonic frequencies.
//
// METHOD (power-domain residual; "everything not at an integer multiple of
// the fundamental"):
//   totalPower    = mean-square of the whole output      = (1/N) * sum(x[n]^2)
//                   (Parseval: equals the summed power of all DFT bins)
//   harmonicPower = DC power  (mean^2, the k=0 bin)
//                 + sum over every in-band harmonic k>=1 (k*fundamental <
//                   Nyquist) of (A_k^2 / 2), where A_k is the GoertzelAnalyzer
//                   amplitude at k*fundamental and A^2/2 is a sinusoid's power.
//   inharmonicPower = totalPower - harmonicPower
//
// Whatever remains after removing DC and the true f0-harmonic series is the
// aliased/folded residual (plus any broadband noise).  Callers MUST choose a
// window length so the fundamental completes an INTEGER number of cycles in
// the captured buffer (fundamentalHz * numSamples / sampleRate is a whole
// number) — with an integer-cycle window every harmonic, and every aliased/
// folded image of a high harmonic, lands exactly on a DFT bin, so Parseval is
// exact and the subtraction is clean.  (A folded image can, in rare arithmetic
// coincidences, land exactly on an f0-multiple and be miscounted as harmonic;
// this is negligible for a naive-vs-mitigated comparison and affects both arms
// equally.)
//
// Returns POWER (mean-square, per-sample) so the value is independent of the
// window length N; a naive-vs-mitigated comparison is a ratio, so units
// cancel either way.
// ---------------------------------------------------------------------------
struct AliasingMeasure {
    double totalPower;       // mean-square of the whole output
    double harmonicPower;    // power at DC + the in-band f0-harmonic series
    double inharmonicPower;  // totalPower - harmonicPower (>= 0): aliased residual
};

inline AliasingMeasure aliasingMeasure(acfx::span<const float> out,
                                       double fundamentalHz,
                                       double sampleRate) {
    const std::size_t N = out.size();
    if (N == 0)
        return AliasingMeasure{0.0, 0.0, 0.0};

    double sumSq = 0.0;
    double sum   = 0.0;
    for (std::size_t n = 0; n < N; ++n) {
        const double x = static_cast<double>(out[n]);
        sumSq += x * x;
        sum   += x;
    }
    const double totalPower = sumSq / static_cast<double>(N);
    const double dc         = sum   / static_cast<double>(N);

    double harmonicPower = dc * dc;  // k = 0 (DC) bin power
    const double nyquist = sampleRate / 2.0;
    for (int k = 1; static_cast<double>(k) * fundamentalHz < nyquist; ++k) {
        const double freqHz = static_cast<double>(k) * fundamentalHz;
        const double aK =
            acfx::measure::GoertzelAnalyzer{freqHz, sampleRate}
                .analyze(out)
                .magnitude;
        harmonicPower += 0.5 * aK * aK;  // sinusoid power = A^2 / 2
    }

    // Energy is physically non-negative; clamp a tiny negative from floating-
    // point round-off in the subtraction (occurs when there is essentially no
    // aliasing and totalPower ~= harmonicPower).  Numerical hygiene, not a
    // fallback masking missing functionality.
    double inharmonic = totalPower - harmonicPower;
    if (inharmonic < 0.0)
        inharmonic = 0.0;

    return AliasingMeasure{totalPower, harmonicPower, inharmonic};
}

} // namespace acfx::measure

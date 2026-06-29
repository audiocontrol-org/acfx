#pragma once

// tests/support/measurement/analyzers.h
//
// Effect-agnostic capture seam (FR-004) for the measurement harness.
// Namespace: acfx::measure.  Host-side / offline ONLY — may allocate.
//
// This file also serves as the home for analyzer structs (ImpulseAnalyzer,
// GoertzelAnalyzer, CorrelationAnalyzer). Those are added by later tasks
// (T005, T008) inside the same namespace block below.

#include <algorithm>  // std::min, std::copy_n
#include <cmath>      // std::abs, std::cos, std::sin, std::sqrt, std::atan2
#include <cstddef>    // std::size_t
#include <vector>     // std::vector (offline scratch buffer; NOT audio path)

#include "dsp/audio-block.h"
#include "dsp/process-context.h"
#include "dsp/span.h"

namespace acfx::measure {

// ---------------------------------------------------------------------------
// capture
//
// Run an Effect over `in` into `out` (same length), MONO, in blocks no larger
// than ctx.maxBlockSize.
//
// Steps:
//   1. fx.prepare(ctx)  — called once before any processing
//   2. fx.reset()       — ensure clean state before the run
//   3. Loop: copy each input slice into a mutable scratch buffer, wrap it in
//      an AudioBlock (single channel), call fx.process(blk) in-place, then
//      copy the processed scratch back into out.
//
// Precondition: in.size() == out.size(). If they differ, only
// min(in.size(), out.size()) samples are processed (the remainder of the
// longer span is left untouched).
// ---------------------------------------------------------------------------
template <class FX>
void capture(FX& fx,
             const acfx::ProcessContext& ctx,
             acfx::span<const float> in,
             acfx::span<float> out)
{
    const std::size_t n = std::min(in.size(), out.size());
    const int blockSize = ctx.maxBlockSize > 0 ? ctx.maxBlockSize : 1;

    fx.prepare(ctx);
    fx.reset();

    std::vector<float> scratch(static_cast<std::size_t>(blockSize));

    std::size_t offset = 0;
    while (offset < n) {
        const int blockLen = static_cast<int>(
            std::min(static_cast<std::size_t>(blockSize), n - offset));

        // Copy input slice into scratch buffer.
        std::copy_n(in.data() + offset,
                    static_cast<std::size_t>(blockLen),
                    scratch.data());

        // Wrap scratch in an AudioBlock (single channel, in-place).
        float* chans[1] = { scratch.data() };
        acfx::AudioBlock blk(chans, 1, blockLen);

        fx.process(blk);

        // Copy processed samples back to output.
        std::copy_n(scratch.data(),
                    static_cast<std::size_t>(blockLen),
                    out.data() + offset);

        offset += static_cast<std::size_t>(blockLen);
    }
}

// ---------------------------------------------------------------------------
// captureCallable
//
// Run any per-sample callable float(float) over `in` into `out`.
// out[i] = fn(in[i]) for each i in [0, min(in.size(), out.size())).
// ---------------------------------------------------------------------------
template <class Fn>
void captureCallable(Fn&& fn,
                     acfx::span<const float> in,
                     acfx::span<float> out)
{
    const std::size_t n = std::min(in.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = fn(in[i]);
    }
}

// ---------------------------------------------------------------------------
// ImpulseAnalyzer
//
// Locates the peak (maximum-absolute-value sample) in a captured output
// buffer. Designed for use with an impulse stimulus to find the latency seed.
// ---------------------------------------------------------------------------
struct ImpulseAnalyzer {
    // Returns the index of the sample with the greatest absolute value.
    // Returns 0 for an empty span (documented sentinel; callers should guard
    // against empty input before interpreting the result as a latency seed).
    static int peakIndex(acfx::span<const float> out) noexcept {
        if (out.empty()) return 0;
        int idx = 0;
        float peak = std::abs(out[0]);
        for (std::size_t i = 1; i < out.size(); ++i) {
            const float a = std::abs(out[i]);
            if (a > peak) {
                peak = a;
                idx  = static_cast<int>(i);
            }
        }
        return idx;
    }
};

// ---------------------------------------------------------------------------
// GoertzelAnalyzer
//
// Single-bin DFT (Goertzel algorithm) computing magnitude and phase at a
// single frequency. Suitable for frequency-response and harmonic analysis.
//
// Phase convention: phaseRad = atan2(imag, real) where the DFT kernel uses
//   a negative-exponent convention (e^{-j*2*pi*freqHz/sampleRate * n}).
//   Downstream phase metrics take differences, so a consistent convention
//   cancels.
// ---------------------------------------------------------------------------
struct GoertzelAnalyzer {
    double freqHz;
    double sampleRate;

    struct Bin {
        double magnitude;  // amplitude: ~1.0 for a unit-amplitude sine at freqHz
        double phaseRad;   // atan2(imag, real); see sign convention note above
    };

    // Returns the single-bin DFT result over all N samples in `out`.
    // Magnitude is amplitude-normalised: 2*sqrt(real^2+imag^2)/N so that
    // a pure unit-amplitude sine at exactly freqHz yields magnitude ~= 1.0.
    // Returns {0.0, 0.0} for an empty span.
    Bin analyze(acfx::span<const float> out) const noexcept {
        const std::size_t N = out.size();
        if (N == 0) return {0.0, 0.0};

        constexpr double pi = 3.141592653589793238462643383279502884;
        const double w     = 2.0 * pi * freqHz / sampleRate;
        const double coeff = 2.0 * std::cos(w);

        double sPrev  = 0.0;
        double sPrev2 = 0.0;
        for (std::size_t n = 0; n < N; ++n) {
            const double s = static_cast<double>(out[n]) + coeff * sPrev - sPrev2;
            sPrev2 = sPrev;
            sPrev  = s;
        }

        const double real = sPrev - sPrev2 * std::cos(w);
        const double imag = sPrev2 * std::sin(w);

        const double magnitude = 2.0 * std::sqrt(real * real + imag * imag)
                                 / static_cast<double>(N);
        const double phaseRad  = std::atan2(imag, real);

        return {magnitude, phaseRad};
    }
};

// ---------------------------------------------------------------------------
// CorrelationAnalyzer
//
// Finds the delay lag (in samples, >= 0) at which `out` best correlates with
// `in` via standard non-normalized cross-correlation:
//
//   corr(k) = sum_n  in[n] * out[n + k]
//
//   where n ranges over all indices for which both in[n] and out[n+k] are
//   valid (i.e. n in [0, min(in.size(), out.size() - k))).
//
// The lag k in [0, maxLag] that maximises the correlation MAGNITUDE |corr(k)|
// is returned. Magnitude (not signed value) is used so a polarity-inverted
// delay — out[n] = -in[n - D] — is still detected: its true-delay peak is a
// strong NEGATIVE correlation, which a signed-maximum would miss (lag 0 with
// corr 0 would spuriously win). Callers needing the polarity can inspect the
// sign separately; for the FR-009 latency metric only the delay magnitude
// matters.
//
// maxLag bound: out.size() - 1.  At lag k the inner sum covers
//   min(in.size(), out.size() - k) terms; the final lag k = out.size()-1
//   contributes exactly one term.  Time complexity: O(N * maxLag) — fine for
//   the small offline test buffers used here.
//
// Correctness: if out = ±in delayed by D samples (out[n] = ±in[n - D]), then
//   |corr(k)| reduces to the magnitude of in's autocorrelation at shift k - D,
//   which peaks at k = D.  So lagSamples returns D, the delay of out relative
//   to in, regardless of polarity.
//
// Degenerate / empty spans: returns 0 (documented sentinel).
// ---------------------------------------------------------------------------
struct CorrelationAnalyzer {
    static int lagSamples(acfx::span<const float> in,
                          acfx::span<const float> out) noexcept {
        if (in.empty() || out.empty()) return 0;

        const std::size_t maxLag = out.size() - 1;

        // Compute cross-correlation at lag k.
        const auto corrAt = [&](std::size_t k) -> double {
            const std::size_t terms = std::min(in.size(), out.size() - k);
            double c = 0.0;
            for (std::size_t n = 0; n < terms; ++n)
                c += static_cast<double>(in[n]) * static_cast<double>(out[n + k]);
            return c;
        };

        int    bestLag = 0;
        double bestMag = std::abs(corrAt(0));
        for (std::size_t k = 1; k <= maxLag; ++k) {
            const double mag = std::abs(corrAt(k));
            if (mag > bestMag) {
                bestMag = mag;
                bestLag = static_cast<int>(k);
            }
        }
        return bestLag;
    }
};

} // namespace acfx::measure

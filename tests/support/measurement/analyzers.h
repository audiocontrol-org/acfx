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
// CorrelationAnalyzer — added by later task T008; goes here.
// ---------------------------------------------------------------------------

} // namespace acfx::measure

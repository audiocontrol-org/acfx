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
// Analyzer structs — added by later tasks (T005, T008).
// ImpulseAnalyzer, GoertzelAnalyzer, and CorrelationAnalyzer go here,
// inside this same namespace acfx::measure block.
// ---------------------------------------------------------------------------

} // namespace acfx::measure

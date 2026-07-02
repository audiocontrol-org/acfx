#pragma once

// host/analysis/alias-sweep.h
//
// Alias-vs-frequency sweep (contracts/analysis-engine-api.md "alias-sweep.h",
// data-model.md "AliasSweepCurve", research.md Decision 6, spec.md US2 /
// FR-004; harmonic-analysis T017, GREEN for T016).
//
// Namespace: acfx::analysis. Host-side / offline ONLY -- may allocate. NEVER
// reachable from portable core/ (Constitution IV); no audio-thread use.
//
// Definition (FR-004, research Decision 6):
//   Sweep a tone across frequency; at EACH step measure inharmonic (folded)
//   energy by reusing the SHIPPED integer-cycle inharmonic measure
//   (acfx::measure::aliasingMeasure, aliasing.h) -- no new spectral machinery
//   for the measure itself -- and collect an inharmonic-energy-vs-frequency
//   curve. This directly generalizes the existing single-tone integer-cycle
//   inharmonic measure the naive-vs-ADAA/oversampled suites already use
//   (saturation-aliasing-test.cpp, oversampler-aliasing-test.cpp) across a
//   swept frequency axis.
//
// Method:
//   Frequencies are linearly spaced from FrequencyRange::startHz to ::stopHz,
//   ::numSteps points inclusive of both endpoints (numSteps == 1 measures
//   only startHz). At each step a fresh integer-cycle tone is synthesized at
//   a FIXED, internal (kAliasSweepSampleRate, kAliasSweepSamplesPerStep)
//   configuration -- 48000 Hz over 480 samples, 100 Hz/bin -- so a swept
//   frequency that is itself a whole multiple of 100 Hz lands EXACTLY on a
//   DFT bin, and so does any folded image of its harmonics, keeping
//   aliasingMeasure's subtraction leakage-free (aliasing.h header contract).
//   A caller-supplied frequency that is NOT bin-aligned still produces a
//   defined, finite reading -- just with ordinary DFT leakage, not a
//   fabricated/undefined result.
//
//   Each step drives the effect/callable via the SAME capture/captureCallable
//   seam every other host/analysis/ metric uses (analyzers.h), then reads
//   inharmonicPower straight off acfx::measure::aliasingMeasure -- the exact
//   measure already validated (and NOT re-derived here) by the naive-vs-ADAA
//   comparison.
//
// Deviation from contracts/analysis-engine-api.md's sketch
//   `AliasSweepCurve aliasSweep(EffectOrCallable fx, FrequencyRange sweep);`:
//   - `FrequencyRange` is defined here (not pre-existing elsewhere in the
//     tree) as {startHz, stopHz, numSteps} -- the minimal linear-sweep
//     description the contract's own name implies.
//   - Mirrors imd.h's dual-overload pattern (FR-006, effect-agnostic): a
//     2-arg callable overload `aliasSweep(fn, sweep)` and a 3-arg Effect
//     overload `aliasSweep(fx, ctx, sweep)` (the caller-supplied
//     ProcessContext's sampleRate MUST equal kAliasSweepSampleRate, exactly
//     as imd.h's Effect overload requires -- a mismatched rate is a caller
//     error, not silently corrected).

#include <cstddef>    // std::size_t
#include <vector>     // std::vector (offline scratch; NOT audio path)

#include "analysis/aliasing.h"   // acfx::measure::aliasingMeasure (REUSED, not re-derived)
#include "analysis/analyzers.h"  // capture, captureCallable
#include "analysis/stimulus.h"   // SineGenerator
#include "dsp/process-context.h" // acfx::ProcessContext (Effect overload)
#include "dsp/span.h"

namespace acfx::analysis {

// Swept-frequency description: linearly spaced from startHz to stopHz,
// numSteps points inclusive of both endpoints (data-model.md "AliasSweepCurve"
// companion input). numSteps <= 0 yields an empty curve; numSteps == 1
// measures only startHz.
struct FrequencyRange {
    double startHz;
    double stopHz;
    int    numSteps;
};

// Alias-vs-frequency sweep curve (data-model.md "AliasSweepCurve").
struct AliasSweepCurve {
    std::vector<double> frequencyHz;       // swept tone frequencies, in sweep order
    std::vector<double> inharmonicEnergy;  // folded/inharmonic POWER per step (aliasingMeasure.inharmonicPower)
};

// Fixed internal per-step stimulus configuration: 48000 Hz over 480 samples =
// 100 Hz/bin. A swept frequency that is a whole multiple of 100 Hz lands
// exactly on a bin (see file banner).
inline constexpr double      kAliasSweepSampleRate     = 48000.0;
inline constexpr std::size_t kAliasSweepSamplesPerStep = 480;

namespace detail {

// Linearly-spaced sweep points, startHz..stopHz inclusive, numSteps points.
inline std::vector<double> sweepFrequencies(const FrequencyRange& sweep) {
    std::vector<double> freqs;
    if (sweep.numSteps <= 0)
        return freqs;
    freqs.reserve(static_cast<std::size_t>(sweep.numSteps));
    if (sweep.numSteps == 1) {
        freqs.push_back(sweep.startHz);
        return freqs;
    }
    const double step = (sweep.stopHz - sweep.startHz)
                       / static_cast<double>(sweep.numSteps - 1);
    for (int i = 0; i < sweep.numSteps; ++i)
        freqs.push_back(sweep.startHz + step * static_cast<double>(i));
    return freqs;
}

} // namespace detail

// aliasSweep(fn, sweep): drive a per-sample callable float(float) `fn` (a
// memoryless or stateless-per-block nonlinearity) with an integer-cycle tone
// at each swept frequency, via captureCallable, and report the resulting
// inharmonic-energy-vs-frequency curve (data-model.md "AliasSweepCurve").
template <class Fn>
inline AliasSweepCurve aliasSweep(Fn&& fn, const FrequencyRange& sweep) {
    AliasSweepCurve curve;
    const std::vector<double> freqs = detail::sweepFrequencies(sweep);
    curve.frequencyHz.reserve(freqs.size());
    curve.inharmonicEnergy.reserve(freqs.size());

    std::vector<float> stimulus(kAliasSweepSamplesPerStep, 0.0f);
    std::vector<float> out(kAliasSweepSamplesPerStep, 0.0f);

    for (const double freqHz : freqs) {
        acfx::measure::SineGenerator{freqHz, kAliasSweepSampleRate}
            .fill(acfx::span<float>{stimulus});

        acfx::measure::captureCallable(fn,
                                       acfx::span<const float>(stimulus),
                                       acfx::span<float>{out});

        const acfx::measure::AliasingMeasure m = acfx::measure::aliasingMeasure(
            acfx::span<const float>(out), freqHz, kAliasSweepSampleRate);

        curve.frequencyHz.push_back(freqHz);
        curve.inharmonicEnergy.push_back(m.inharmonicPower);
    }
    return curve;
}

// aliasSweep(fx, ctx, sweep): the Effect-contract front door (FR-006). Drives
// an Effect implementation with an integer-cycle tone at each swept frequency
// via capture(). The caller MUST pass a ProcessContext whose sampleRate ==
// kAliasSweepSampleRate so every step stays integer-cycle (a mismatched rate
// is a caller error, not silently corrected -- mirrors imd.h's Effect
// overload). Distinct arity from aliasSweep(fn, sweep) -> no overload
// ambiguity.
template <class FX>
inline AliasSweepCurve aliasSweep(FX& fx, const acfx::ProcessContext& ctx,
                                  const FrequencyRange& sweep) {
    AliasSweepCurve curve;
    const std::vector<double> freqs = detail::sweepFrequencies(sweep);
    curve.frequencyHz.reserve(freqs.size());
    curve.inharmonicEnergy.reserve(freqs.size());

    std::vector<float> stimulus(kAliasSweepSamplesPerStep, 0.0f);
    std::vector<float> out(kAliasSweepSamplesPerStep, 0.0f);

    for (const double freqHz : freqs) {
        acfx::measure::SineGenerator{freqHz, kAliasSweepSampleRate}
            .fill(acfx::span<float>{stimulus});

        acfx::measure::capture(fx, ctx,
                               acfx::span<const float>(stimulus),
                               acfx::span<float>{out});

        const acfx::measure::AliasingMeasure m = acfx::measure::aliasingMeasure(
            acfx::span<const float>(out), freqHz, kAliasSweepSampleRate);

        curve.frequencyHz.push_back(freqHz);
        curve.inharmonicEnergy.push_back(m.inharmonicPower);
    }
    return curve;
}

} // namespace acfx::analysis

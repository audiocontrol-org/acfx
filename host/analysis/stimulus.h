#pragma once

// host/analysis/stimulus.h
//
// Stimulus generators for the acfx measurement harness (T002).
// Host-side only; allocation is permitted here (not an audio-path file).
// Each generator fills a caller-provided span<float> with a deterministic signal.
// No platform headers; no third-party dependencies.
//
// RELOCATED (harmonic-analysis T007; research.md Decision 1/8; analyze
// finding F1) from tests/support/measurement/stimulus.h into host/analysis/
// so product adapters can reuse it without depending on the test tree.
// tests/support/measurement/stimulus.h is now a thin re-export shim;
// existing `acfx::measure::...` call sites are unaffected.

#include <cmath>
#include <cstdint>
#include <numbers>

#include "dsp/span.h"

namespace acfx::measure {

// Impulse: out[0] = amplitude, all remaining samples = 0.
struct ImpulseGenerator {
    float amplitude = 1.0f;

    void fill(acfx::span<float> out) const noexcept {
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = 0.0f;
        if (!out.empty())
            out[0] = amplitude;
    }
};

// Step: every sample set to level.
struct StepGenerator {
    float level = 1.0f;

    void fill(acfx::span<float> out) const noexcept {
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = level;
    }
};

// Sine: out[n] = amplitude * sin(2*pi*freqHz*n/sampleRate + phase).
struct SineGenerator {
    double freqHz;
    double sampleRate;
    float amplitude = 1.0f;
    double phase = 0.0;

    void fill(acfx::span<float> out) const noexcept {
        const double omega = 2.0 * std::numbers::pi * freqHz / sampleRate;
        for (std::size_t n = 0; n < out.size(); ++n) {
            const double s = static_cast<float>(amplitude)
                             * std::sin(omega * static_cast<double>(n) + phase);
            out[n] = static_cast<float>(s);
        }
    }
};

// Sweep (chirp): linear or logarithmic frequency sweep from f0Hz to f1Hz
// across the entire buffer.
//
// Linear:      instantaneous frequency ramps linearly from f0Hz to f1Hz.
//   phi(n) = 2*pi/sampleRate * (f0*n + (f1-f0)*n^2 / (2*(N-1)))
//
// Logarithmic: instantaneous frequency follows an exponential curve.
//   phi(n) = 2*pi*f0*(N-1)/sampleRate * ((f1/f0)^(n/(N-1)) - 1) / ln(f1/f0)
struct SweepGenerator {
    double f0Hz;
    double f1Hz;
    double sampleRate;
    bool logarithmic = true;

    void fill(acfx::span<float> out) const noexcept {
        const std::size_t N = out.size();
        if (N == 0)
            return;

        const double twoPi = 2.0 * std::numbers::pi;

        // Invalid timing: a non-positive sample rate makes the phase undefined.
        // Emit a well-defined silence rather than NaN/Inf from this noexcept path
        // (AUDIT-20260629-11 — a corrupt stimulus silently poisons measurements).
        if (!(sampleRate > 0.0)) {
            for (std::size_t n = 0; n < N; ++n)
                out[n] = 0.0f;
            return;
        }

        if (N == 1) {
            out[0] = static_cast<float>(std::sin(0.0));
            return;
        }

        // Degenerate sweep: equal endpoints, or non-positive endpoints for a
        // logarithmic sweep (where ratio/log(ratio) would be undefined and emit
        // NaN/Inf). Fall back to a well-defined constant-frequency tone at f0Hz —
        // the limit of a zero-span sweep — keeping the output finite (AUDIT-11).
        const bool degenerate =
            (f0Hz == f1Hz) ||
            (logarithmic && (f0Hz <= 0.0 || f1Hz <= 0.0));
        if (degenerate) {
            const double w = twoPi * f0Hz / sampleRate;
            for (std::size_t n = 0; n < N; ++n)
                out[n] = static_cast<float>(std::sin(w * static_cast<double>(n)));
            return;
        }

        const double Nm1 = static_cast<double>(N - 1);

        if (logarithmic) {
            const double ratio    = f1Hz / f0Hz;
            const double logRatio = std::log(ratio);
            // Even with f0Hz != f1Hz in source, `ratio` can round to exactly 1.0
            // in double when the endpoints are within ~1 ULP (e.g. 1000.0 vs
            // 1000.0+1e-14), giving logRatio == 0 and scale = finite/0 = Inf, then
            // phi = Inf*0 = NaN. Guard on the rounded ratio/logRatio and fall back
            // to the constant-frequency tone (AUDIT-20260629-13).
            if (logRatio == 0.0) {
                const double w = twoPi * f0Hz / sampleRate;
                for (std::size_t n = 0; n < N; ++n)
                    out[n] = static_cast<float>(std::sin(w * static_cast<double>(n)));
                return;
            }
            const double scale    = twoPi * f0Hz * Nm1 / (sampleRate * logRatio);
            for (std::size_t n = 0; n < N; ++n) {
                const double t   = static_cast<double>(n) / Nm1;
                const double phi = scale * (std::pow(ratio, t) - 1.0);
                out[n] = static_cast<float>(std::sin(phi));
            }
        } else {
            const double df    = f1Hz - f0Hz;
            const double scale = twoPi / sampleRate;
            for (std::size_t n = 0; n < N; ++n) {
                const double nd  = static_cast<double>(n);
                const double phi = scale * (f0Hz * nd + df * nd * nd / (2.0 * Nm1));
                out[n] = static_cast<float>(std::sin(phi));
            }
        }
    }
};

// White noise: deterministic xorshift32 PRNG seeded by `seed`.
// Output is in [-amplitude, amplitude].
// seed == 0 is silently promoted to a non-zero internal state so xorshift stays valid.
struct NoiseGenerator {
    float amplitude = 1.0f;
    std::uint32_t seed = 0x1234u;

    void fill(acfx::span<float> out) const noexcept {
        // xorshift32 requires non-zero state.
        std::uint32_t state = (seed != 0u) ? seed : 0xdeadbeefu;

        constexpr float kScale = 2.0f / static_cast<float>(0xffffffffu);

        for (std::size_t i = 0; i < out.size(); ++i) {
            state ^= state << 13u;
            state ^= state >> 17u;
            state ^= state << 5u;
            // Map [0, 0xffffffff] -> [-1, 1], then scale by amplitude.
            const float normalized = static_cast<float>(state) * kScale - 1.0f;
            out[i] = amplitude * normalized;
        }
    }
};

} // namespace acfx::measure

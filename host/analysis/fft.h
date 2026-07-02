// fft.h -- host/analysis, harmonic-analysis feature (T004, GREEN for T003).
//
// Self-contained, in-repo, iterative radix-2 Cooley-Tukey FFT. No external
// dependency (no FFTW/KissFFT/PFFFT) -- matches research.md Decision 2 and the
// repo's no-new-dependency posture. Host/desktop only: MAY allocate at
// construction (twiddle-factor precompute) and inside forward() (windowed
// scratch); this NEVER runs on an audio thread (Constitution IV/VI). The RT
// capture probe (portable core/) has no dependency on this library.
//
// Contract (contracts/analysis-engine-api.md "Fft", FR-009/FR-026):
//   - the transform size MUST be a power of two; a non-power-of-two size is
//     rejected at construction with a descriptive std::invalid_argument -- the
//     transform is NEVER silently zero-padded (Constitution V: no silent
//     behavior that shifts bin frequencies / adds leakage the caller did not
//     ask for).
//   - forward() applies the configured analysis window (default 4-term
//     Blackman-Harris, research.md Decision 3) to the real input frame before
//     transforming, producing a complex spectrum the caller reads magnitude +
//     phase from (data-model.md "Fft").

#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/window.h"
#include "dsp/span.h"

namespace acfx::analysis {

// The complex sample type of the produced spectrum. double precision: this is
// an offline/analysis transform where numerical headroom (a clean noise floor
// well below the Blackman-Harris ~-92 dB sidelobes) matters more than speed.
using Complex = std::complex<double>;

// Windowed, power-of-two-only, iterative radix-2 FFT. Header-only value type;
// twiddle factors and the window are precomputed at construction (init-time
// allocation, off any audio thread).
class Fft {
public:
    // `size` MUST be a power of two (and >= 2). A non-power-of-two (or
    // non-positive) size is rejected here with a descriptive error rather than
    // silently zero-padded (FR-026). `kind` selects the analysis window
    // applied before the transform (default Blackman-Harris, FR-025).
    explicit Fft(int size, WindowKind kind = WindowKind::BlackmanHarris)
        : size_(size),
          window_(requirePowerOfTwo(size), kind),
          twiddles_(computeTwiddles(static_cast<std::size_t>(size))) {}

    [[nodiscard]] int size() const noexcept { return size_; }
    [[nodiscard]] WindowKind windowKind() const noexcept { return window_.kind(); }

    // Apply the configured window to the real input frame, then compute its
    // forward DFT into `out`. `in` and `out` MUST each span exactly `size()`
    // elements; a mismatch is a descriptive error (no truncation / padding).
    void forward(acfx::span<const float> in, acfx::span<Complex> out) const {
        const std::size_t n = static_cast<std::size_t>(size_);
        if (in.size() != n) {
            throw std::invalid_argument(
                "acfx::analysis::Fft::forward: input length (" + std::to_string(in.size()) +
                ") must equal the transform size (" + std::to_string(n) + ")");
        }
        if (out.size() != n) {
            throw std::invalid_argument(
                "acfx::analysis::Fft::forward: output length (" + std::to_string(out.size()) +
                ") must equal the transform size (" + std::to_string(n) + ")");
        }

        const std::vector<double>& w = window_.coeffs();

        // Load windowed input into `out` in bit-reversed order so the
        // in-place iterative butterflies below produce natural-order output.
        for (std::size_t i = 0; i < n; ++i) {
            const double sample = static_cast<double>(in[i]) * w[i];
            out[bitReverse_[i]] = Complex(sample, 0.0);
        }

        // Iterative Cooley-Tukey (decimation-in-time). For a stage whose sub-
        // transform length is `len`, the butterfly twiddle at position j is
        // exp(-2*pi*i * j / len) = twiddles_[j * (n / len)].
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const std::size_t half = len >> 1;
            const std::size_t step = n / len;
            for (std::size_t base = 0; base < n; base += len) {
                for (std::size_t j = 0; j < half; ++j) {
                    const Complex tw = twiddles_[j * step];
                    Complex& even = out[base + j];
                    Complex& odd = out[base + j + half];
                    const Complex t = tw * odd;
                    odd = even - t;
                    even = even + t;
                }
            }
        }
    }

private:
    static constexpr double kTwoPi = 6.28318530717958647692;

    // Throws if `size` is not a power of two (>= 2). Returned so it can gate a
    // member initializer (the Window ctor) in the initializer list.
    static int requirePowerOfTwo(int size) {
        if (size < 2 || (size & (size - 1)) != 0) {
            throw std::invalid_argument("acfx::analysis::Fft size must be a power of two; got " +
                                        std::to_string(size));
        }
        return size;
    }

    // Precompute the n/2 forward-DFT twiddle factors exp(-2*pi*i * k / n).
    static std::vector<Complex> computeTwiddles(std::size_t n) {
        std::vector<Complex> tw(n / 2);
        for (std::size_t k = 0; k < n / 2; ++k) {
            const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(n);
            tw[k] = Complex(std::cos(angle), std::sin(angle));
        }
        return tw;
    }

    // Precompute the bit-reversal permutation for the load step.
    static std::vector<std::size_t> computeBitReverse(std::size_t n) {
        std::vector<std::size_t> rev(n);
        std::size_t bits = 0;
        while ((static_cast<std::size_t>(1) << bits) < n) ++bits;
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t r = 0;
            for (std::size_t b = 0; b < bits; ++b) {
                if ((i >> b) & static_cast<std::size_t>(1)) {
                    r |= static_cast<std::size_t>(1) << (bits - 1 - b);
                }
            }
            rev[i] = r;
        }
        return rev;
    }

    int size_;
    Window window_;
    std::vector<Complex> twiddles_;
    std::vector<std::size_t> bitReverse_ = computeBitReverse(static_cast<std::size_t>(size_));
};

} // namespace acfx::analysis

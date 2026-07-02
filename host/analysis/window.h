// window.h -- host/analysis, harmonic-analysis feature (T006, GREEN for T005).
//
// Selectable analysis window: default 4-term Blackman-Harris (~-92 dB
// sidelobes), plus Hann and flat-top. Host-only (may allocate at
// construction; runs only off the audio thread -- Constitution IV/VI). The
// retained integer-cycle Goertzel path (goertzel.h, relocated separately)
// deliberately does NOT use this class -- it stays rectangular/unwindowed
// (leakage-free by construction), per research.md Decision 3 and
// data-model.md's "Window" entity rule.
//
// Coefficients are precomputed at construction (init-time allocation,
// off any audio thread) and exposed as a read-only value array, mirroring
// data-model.md's Window entity (`kind`, `coeffs`).

#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace acfx::analysis {

// Selectable window shapes (FR-025). Default is BlackmanHarris -- the
// dynamic-range choice for separating low-order harmonics and the noise
// floor from spectral leakage (research.md Decision 3).
enum class WindowKind { BlackmanHarris, Hann, FlatTop };

// Precomputed window coefficients for a given transform size and kind.
// Header-only value type; no product/adapter target needs more than this to
// apply a window before an FFT (fft.h, arriving in T003/T004, multiplies a
// frame by `coeffs()` before transforming).
class Window {
public:
    // `size` is the number of samples the window covers (matches the FFT
    // transform length it will be applied to). `size` must be >= 2 so the
    // symmetric closed forms (which divide by size - 1) are well-defined.
    explicit Window(int size, WindowKind kind = WindowKind::BlackmanHarris)
        : kind_(kind), coeffs_(computeCoeffs(size, kind)) {}

    [[nodiscard]] WindowKind kind() const noexcept { return kind_; }
    [[nodiscard]] int size() const noexcept { return static_cast<int>(coeffs_.size()); }
    [[nodiscard]] const std::vector<double>& coeffs() const noexcept { return coeffs_; }

private:
    static constexpr double kPi = 3.14159265358979323846;

    // 4-term Blackman-Harris: w[n] = a0 - a1*cos(2*pi*n/(N-1))
    //                                  + a2*cos(4*pi*n/(N-1)) - a3*cos(6*pi*n/(N-1))
    static double blackmanHarris(int n, double denom) {
        constexpr double a0 = 0.35875;
        constexpr double a1 = 0.48829;
        constexpr double a2 = 0.14128;
        constexpr double a3 = 0.01168;
        const double phase = 2.0 * kPi * static_cast<double>(n) / denom;
        return a0 - a1 * std::cos(phase) + a2 * std::cos(2.0 * phase) - a3 * std::cos(3.0 * phase);
    }

    // Hann: w[n] = 0.5 * (1 - cos(2*pi*n/(N-1)))
    static double hann(int n, double denom) {
        const double phase = 2.0 * kPi * static_cast<double>(n) / denom;
        return 0.5 * (1.0 - std::cos(phase));
    }

    // 5-term flat-top (standard coefficients; matched-amplitude window used
    // where amplitude accuracy matters more than sidelobe suppression):
    // w[n] = a0 - a1*cos(2pn) + a2*cos(4pn) - a3*cos(6pn) + a4*cos(8pn), p = n/(N-1)
    static double flatTop(int n, double denom) {
        constexpr double a0 = 0.21557895;
        constexpr double a1 = 0.41663158;
        constexpr double a2 = 0.277263158;
        constexpr double a3 = 0.083578947;
        constexpr double a4 = 0.006947368;
        const double phase = 2.0 * kPi * static_cast<double>(n) / denom;
        return a0 - a1 * std::cos(phase) + a2 * std::cos(2.0 * phase) - a3 * std::cos(3.0 * phase) +
               a4 * std::cos(4.0 * phase);
    }

    static std::vector<double> computeCoeffs(int size, WindowKind kind) {
        if (size < 2) {
            throw std::invalid_argument("acfx::analysis::Window: size must be >= 2");
        }

        std::vector<double> coeffs(static_cast<std::size_t>(size));
        const double denom = static_cast<double>(size - 1);

        for (int n = 0; n < size; ++n) {
            double value = 0.0;
            switch (kind) {
                case WindowKind::BlackmanHarris:
                    value = blackmanHarris(n, denom);
                    break;
                case WindowKind::Hann:
                    value = hann(n, denom);
                    break;
                case WindowKind::FlatTop:
                    value = flatTop(n, denom);
                    break;
            }
            coeffs[static_cast<std::size_t>(n)] = value;
        }

        return coeffs;
    }

    WindowKind kind_;
    std::vector<double> coeffs_;
};

} // namespace acfx::analysis

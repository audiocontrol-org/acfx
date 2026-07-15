#pragma once

#include <cmath>
#include <vector>

// Direct DTFT evaluation of a real impulse response at an arbitrary (not
// necessarily bin-aligned) set of frequencies. This is how the frequency
// response is MEASURED from the real acfx::SvfEffect's actual impulse
// response (T009: "measured authoritatively... NOT an analytic
// re-derivation") -- a simple O(N*F) direct sum is fine at this N/F scale for
// a host build-time tool (no real-time constraint here).

namespace lessonassets {

// Log-spaced frequencies in [fMinHz, fMaxHz], `count` points inclusive.
inline std::vector<double> logSpacedFrequencies(double fMinHz, double fMaxHz, int count) {
    std::vector<double> freqs;
    freqs.reserve(static_cast<std::size_t>(count));
    if (count <= 1) {
        freqs.push_back(fMinHz);
        return freqs;
    }
    const double logMin = std::log(fMinHz);
    const double logMax = std::log(fMaxHz);
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1);
        freqs.push_back(std::exp(logMin + t * (logMax - logMin)));
    }
    return freqs;
}

// H(e^jw) = sum_n h[n] * e^(-j*w*n), w = 2*pi*f/fs, evaluated directly at each
// requested frequency from the actual (real, measured) impulse response.
// Returns magnitude in dB (20*log10|H|), floored to avoid -inf on exact zeros.
inline std::vector<double> measureFrequencyResponseDb(const std::vector<float>& impulseResponse,
                                                        double sampleRate,
                                                        const std::vector<double>& freqsHz) {
    std::vector<double> magsDb;
    magsDb.reserve(freqsHz.size());
    constexpr double kFloor = 1e-9; // -180 dB floor
    for (double f : freqsHz) {
        const double w = 2.0 * M_PI * f / sampleRate;
        double real = 0.0;
        double imag = 0.0;
        for (std::size_t n = 0; n < impulseResponse.size(); ++n) {
            const double phase = w * static_cast<double>(n);
            const double h = static_cast<double>(impulseResponse[n]);
            real += h * std::cos(phase);
            imag -= h * std::sin(phase);
        }
        const double mag = std::sqrt(real * real + imag * imag);
        magsDb.push_back(20.0 * std::log10(std::max(mag, kFloor)));
    }
    return magsDb;
}

} // namespace lessonassets

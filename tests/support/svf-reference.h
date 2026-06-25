#pragma once

#include <cmath>

// Known-good SVF frequency-response references (T013). Rather than fabricate exact
// magnitude numbers (false precision), this captures the *analytic* truths a
// correct 2nd-order state-variable filter must satisfy — passband near unity,
// stopband attenuated, bandpass emphasizing its centre — as a measurement helper
// plus named tolerance bounds the SVF test asserts against (T016).

namespace acfx::test {

inline constexpr double kPi = 3.14159265358979323846;

// Steady-state magnitude response |out|/|in| of a per-sample processor at a given
// frequency. `proc` is any callable float(float). A settling prefix is discarded
// so only the steady-state RMS ratio is measured.
template <typename Proc>
double measureMagnitude(Proc proc, double freqHz, double sampleRate, int settle = 8192,
                        int measure = 16384) {
    const double w = 2.0 * kPi * freqHz / sampleRate;
    for (int n = 0; n < settle; ++n)
        (void) proc(static_cast<float>(std::sin(w * static_cast<double>(n))));

    double inSq = 0.0;
    double outSq = 0.0;
    for (int n = 0; n < measure; ++n) {
        const double phase = w * static_cast<double>(settle + n);
        const double in = std::sin(phase);
        const double out = static_cast<double>(proc(static_cast<float>(in)));
        inSq += in * in;
        outSq += out * out;
    }
    return std::sqrt(outSq / inSq);
}

// Reference cutoff used by the SVF response test.
inline constexpr double kRefCutoffHz = 1000.0;
inline constexpr double kRefSampleRate = 48000.0;

// Passband: a decade below cutoff should pass at roughly unity gain.
inline constexpr double kPassbandFreqHz = 100.0;
inline constexpr double kPassbandGainMin = 0.7; // generous: SVF passband ~0 dB

// Stopband: three octaves above cutoff a 2nd-order rolloff is well attenuated.
inline constexpr double kStopbandFreqHz = 8000.0;
inline constexpr double kStopbandGainMax = 0.25; // << passband

} // namespace acfx::test

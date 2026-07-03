// hysteresis-test-support.h
// Shared test helpers for the acfx::Hysteresis (Jiles-Atherton) doctest
// suites (tests/core/hysteresis-test.cpp, tests/core/hysteresis-solver-test.cpp).
// Split out of the original hysteresis-test.cpp's friend accessor + shared
// anonymous-namespace helper blocks so each test TU can stay under the
// Constitution VII per-file line budget without duplicating this logic. Free
// functions/constants live in namespace acfx::hysteresistest as inline
// definitions so multiple TUs can include this without ODR violations,
// mirroring saturation-effect-test-support.h's convention. Doctest macros
// (CHECK/CAPTURE/etc.) are deliberately NOT used here — assertion-bearing
// helpers stay in their owning .cpp file; this header holds only the friend
// accessor and pure (assertion-free) helpers.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "primitives/nonlinear/hysteresis.h"

// Test accessor for the private, RT-internal JA derivative dMdH() (T006 seam).
// Declared as a friend on acfx::Hysteresis; owns no state of its own.
namespace acfx {
struct HysteresisTestAccess {
    [[nodiscard]] static double dMdH(const Hysteresis& h, double H, double M,
                                     double dH) noexcept {
        return h.dMdH(H, M, dH);
    }
};
}  // namespace acfx

namespace acfx::hysteresistest {

// Shoelace/polygon-area formula for a closed, ordered 2D trace: the standard
// analytic measure of the signed area enclosed by a polygon's vertices in
// order. We take the absolute value since trace winding direction is not
// asserted here — only that a loop enclosing nonzero area exists.
[[nodiscard]] inline double shoelaceArea(const std::vector<double>& xs,
                                          const std::vector<double>& ys) noexcept {
    const std::size_t n = xs.size();
    double sum2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        sum2 += xs[i] * ys[j] - xs[j] * ys[i];
    }
    return std::fabs(sum2) * 0.5;
}

// Drives one full sinusoidal cycle of H (amplitude `amp`) through `h`,
// AFTER `settleCycles` warm-up cycles so the trace reflects the settled
// limit loop rather than the zero-state transient, and returns the ordered
// (H, out) trace for that final cycle.
struct Trace {
    std::vector<double> H;
    std::vector<double> out;
};

inline Trace driveSettledLoop(Hysteresis& h, double amp, int settleCycles,
                              int stepsPerCycle) {
    const int warmupTotal = settleCycles * stepsPerCycle;
    for (int n = 0; n < warmupTotal; ++n) {
        const double phase = 2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
        static_cast<void>(h.process(static_cast<float>(amp * std::sin(phase))));
    }
    Trace trace;
    trace.H.reserve(static_cast<std::size_t>(stepsPerCycle));
    trace.out.reserve(static_cast<std::size_t>(stepsPerCycle));
    for (int n = 0; n < stepsPerCycle; ++n) {
        const double phase = 2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
        const double H = amp * std::sin(phase);
        const float out = h.process(static_cast<float>(H));
        trace.H.push_back(H);
        trace.out.push_back(static_cast<double>(out));
    }
    return trace;
}

// Probe of the M value on the rising vs falling branch of a driven sine
// sweep's LAST cycle, at the phase crossings nearest H = 0. Shared by the
// T007 (RK2/RK4) and T008 (Newton-Raphson) solver suites.
struct BranchProbe {
    double risingM = 0.0;   // M near H = 0 while H increasing (rising branch)
    double fallingM = 0.0;  // M near H = 0 while H decreasing (falling branch)
    bool sawRising = false;
    bool sawFalling = false;
};

// Mirrors the private kMBoundMultiplier documented in hysteresis.h (T009);
// duplicated here so the test can assert the *contract's* numeric bound
// (|M| <= kMBoundMultiplier * Ms) without needing friend access.
inline constexpr double kGuardBoundMultiplier = 4.0;

}  // namespace acfx::hysteresistest

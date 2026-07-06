#pragma once

#include <doctest/doctest.h>

#include <array>
#include <cmath>

#include "labs/diode-clippers/solver/transient-clipper.h"
#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// Shared host-side helpers for the diode-clippers Tier-2 suites. The Tier-2
// assertions are split across diode-clipper-transient-test.cpp (solver-proving +
// bounds + non-convergence) and diode-clipper-invariants-test.cpp (assembled
// invariants) to stay under the Constitution VII per-file line budget; this
// header carries the pieces both need — the INDEPENDENT bisection oracles and the
// sine-drive / DFT measurement rig, all convergence-checked (FR-011) — in one
// place so neither file duplicates them.

namespace dcl_test {

using acfx::DiodeSpec;
using acfx::Netlist;
using acfx::siliconSignalDiode;
using acfx::labs::diode_clippers::NewtonStatus;
using acfx::labs::diode_clippers::TransientClipper;

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDt = 1.0e-5;   // 100 kHz (FR-017 lab convention)
inline constexpr int kWindow = 1000;    // 10 periods of 1 kHz — harmonics on bins
inline constexpr int kWarmup = 3000;    // 30 ms → steady state before measuring

inline DiodeSpec diodeSpec() { return siliconSignalDiode(); }

// Well-damped standalone bisection root-finder over a monotonically
// non-decreasing f with a root in [lo, hi]. INDEPENDENT of the solver under test
// (the genuine cross-check, not solver-vs-itself).
template <typename F>
double bisectRoot(F f, double lo, double hi, double tol = 1e-13) {
    while (hi - lo > tol) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) <= 0.0) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// Symmetric-shunt DC oracle (data-model.md): 0 = v + R*2*Is*sinh(v/nVt) - Vin.
inline double oracleSymmetric(double Vin, double R, const DiodeSpec& d) {
    const double nVt = d.n * d.Vt;
    return bisectRoot(
        [&](double v) { return v + R * 2.0 * d.Is * std::sinh(v / nVt) - Vin; },
        -5.0, 5.0);
}

// Asymmetric-shunt DC oracle (data-model.md):
// 0 = v + R*[up*Is*(exp(v/nVt)-1) - down*Is*(exp(-v/nVt)-1)] - Vin.
inline double oracleAsymmetric(double Vin, double R, const DiodeSpec& d, int up, int down) {
    const double nVt = d.n * d.Vt;
    return bisectRoot(
        [&](double v) {
            const double iUp = up * d.Is * (std::exp(v / nVt) - 1.0);
            const double iDown = down * d.Is * (std::exp(-v / nVt) - 1.0);
            return v + R * (iUp - iDown) - Vin;
        },
        -5.0, 5.0);
}

// Settle a built clipper to DC under a constant input by stepping the transient
// solver to steady state, then return the port voltage. The measured answer is
// trustworthy only if the solve CONVERGED (a non-converged final iterate is not a
// physical answer, FR-011) — so assert the settled step converged rather than
// silently reading a stale iterate that could satisfy a loose invariant bound.
template <int N, int M, int D>
double settlePortDC(TransientClipper<N, M, D>& solver, const Netlist<N, M>& nl,
                    double dt, int steps) {
    solver.reset();
    NewtonStatus st;
    for (int i = 0; i < steps; ++i) {
        st = solver.step(nl, dt);
    }
    CHECK(st.converged);  // the measured steady state must be a converged solve
    return solver.clipperVoltage();
}

// Drive a periodic sine into a clipper (rebuilt per sample with the new input —
// the sanctioned control-rate-rebuild drive) and collect a steady-state window of
// input and output samples. `build(vIn)` returns the clipper for input vIn. Every
// sample in the measured window must be a converged solve, else the invariants
// would be measuring stale, non-physical iterates (FR-011).
template <int N, int M, int D, typename BuildFn>
void driveSine(TransientClipper<N, M, D>& solver, BuildFn build, double amp,
               double freqHz, std::array<double, kWindow>& in,
               std::array<double, kWindow>& out) {
    solver.reset();
    const int total = kWarmup + kWindow;
    bool measuredAllConverged = true;
    for (int nStep = 0; nStep < total; ++nStep) {
        const double vIn = amp * std::sin(2.0 * kPi * freqHz * nStep * kDt);
        const auto clip = build(vIn);
        const NewtonStatus st = solver.step(clip.netlist, kDt);
        if (nStep >= kWarmup) {
            if (!st.converged) {
                measuredAllConverged = false;
            }
            in[static_cast<std::size_t>(nStep - kWarmup)] = vIn;
            out[static_cast<std::size_t>(nStep - kWarmup)] = solver.voltage(clip.outNode);
        }
    }
    CHECK(measuredAllConverged);
}

// Sum of squares (energy proxy) over a window.
inline double energy(const std::array<double, kWindow>& x) {
    double e = 0.0;
    for (double v : x) {
        e += v * v;
    }
    return e;
}

// Direct-DFT spectral energy strictly above `cutoff` (Hz) over a window sampled
// at fs. O(N * bins); fine for a host test. The measurement window is an integer
// number of the drive period, so the transform is leakage-clean.
inline double hfEnergyAbove(const std::array<double, kWindow>& x, double fs, double cutoff) {
    double total = 0.0;
    for (int k = 1; k <= kWindow / 2; ++k) {
        if (static_cast<double>(k) * fs / kWindow <= cutoff) {
            continue;
        }
        double re = 0.0, im = 0.0;
        for (int nn = 0; nn < kWindow; ++nn) {
            const double ph = -2.0 * kPi * k * nn / kWindow;
            re += x[static_cast<std::size_t>(nn)] * std::cos(ph);
            im += x[static_cast<std::size_t>(nn)] * std::sin(ph);
        }
        total += re * re + im * im;
    }
    return total;
}

}  // namespace dcl_test

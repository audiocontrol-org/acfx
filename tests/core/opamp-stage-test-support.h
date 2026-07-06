#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "labs/opamp-stages/solver/opamp-clipper-solver.h"
#include "labs/opamp-stages/solver/opamp-stage-solver.h"

// Shared host-side helpers for the opamp-stages US3 assembled-stage
// invariants (tests/core/opamp-stage-invariants-test.cpp) AND the lab harness
// (core/labs/opamp-stages/harness/opamp-stages-harness.cpp) — mirrors the
// tests/core/diode-clipper-test-support.h split (T021-T024;
// specs/opamp-stages/tasks.md), one deliberate difference: this header
// carries NO doctest dependency (no CHECK/REQUIRE) so the harness — a plain
// int main(), no test framework linked — can include it directly. Every
// helper reports convergence via an out-param `bool& converged` instead of
// asserting inline; the caller (a doctest CHECK/REQUIRE, or a harness
// PASS/FAIL print) decides what to do with it. Each helper OWNS its
// out-param with self-contained per-call semantics: it resets
// `converged = true` at entry, then clears it to false if any (measured)
// step fails to converge — a caller never needs to pre-initialize the flag,
// and a caller that shares ONE flag across MULTIPLE calls must AND the
// per-call results together itself (each call resets that shared flag back
// to true). A non-converged iterate is never a physical answer (FR-014), so
// every caller of these helpers must inspect `converged` before trusting the
// measured output.

namespace opst_test {

using acfx::labs::opamp_stages::NewtonStatus;
using acfx::labs::opamp_stages::NullorSolver;
using acfx::labs::opamp_stages::OpAmpClipperSolver;

inline constexpr double kPi = 3.14159265358979323846;

// Sum of squares (energy proxy) over a window. Valid as a comparison between
// two signals only when both sides are the same physical quantity (a node
// voltage) sampled at the same rate over windows of equal length — every use
// below satisfies that.
template <std::size_t N>
double energy(const std::array<double, N>& x) {
    double e = 0.0;
    for (double v : x) {
        e += v * v;
    }
    return e;
}

// Direct-DFT spectral energy strictly above `cutoff` Hz over a window of N
// samples at sample rate `fs` (FR-022's self-contained measurement — no
// external FFT library; O(N * bins), fine for a host test/harness). Bin k
// represents k*fs/N Hz for k in [1, N/2]. The measurement window is expected
// to span an integer number of the drive period so the transform stays
// leakage-clean (mirrors diode-clipper-test-support.h::hfEnergyAbove).
template <std::size_t N>
double hfEnergyAbove(const std::array<double, N>& x, double fs, double cutoff) {
    double total = 0.0;
    for (std::size_t k = 1; k <= N / 2; ++k) {
        if (static_cast<double>(k) * fs / static_cast<double>(N) <= cutoff) {
            continue;
        }
        double re = 0.0, im = 0.0;
        for (std::size_t nn = 0; nn < N; ++nn) {
            const double ph = -2.0 * kPi * static_cast<double>(k * nn) / static_cast<double>(N);
            re += x[nn] * std::cos(ph);
            im += x[nn] * std::sin(ph);
        }
        total += re * re + im * im;
    }
    return total;
}

// Drive an OpAmpClipperSolver with a sine of amplitude `amp` at `freqHz`,
// timestep `dt` (the sanctioned control-rate-rebuild drive, FR-008: `build`
// rebuilds the netlist per sample); discard `warmup` steps, then record the
// next N-sample window of input/output into `in`/`out`. `build(vIn)` must
// return an *Result-shaped value (a `.netlist` Netlist<MaxNodes,
// MaxComponents> and a `.outNode`), per the opamp-stage builder convention.
// Resets the solver at entry (a cold start). Resets `converged` to true at
// entry, then clears it to false if ANY step — warmup or measured — fails to
// converge (a divergent warmup step must not be allowed to silently seed the
// measurement window with corrupt state).
template <int MaxNodes, int MaxComponents, int MaxDiodes, int MaxOpAmps,
          typename BuildFn, std::size_t N>
void driveSine(OpAmpClipperSolver<MaxNodes, MaxComponents, MaxDiodes, MaxOpAmps>& solver,
               BuildFn build, double amp, double freqHz, double dt, int warmup,
               std::array<double, N>& in, std::array<double, N>& out,
               bool& converged) {
    solver.reset();
    converged = true;
    const int total = warmup + static_cast<int>(N);
    for (int step = 0; step < total; ++step) {
        const double vIn = amp * std::sin(2.0 * kPi * freqHz * static_cast<double>(step) * dt);
        const auto clip = build(vIn);
        const NewtonStatus status = solver.step(clip.netlist, dt);
        if (!status.converged) {
            converged = false;
        }
        if (step >= warmup) {
            const std::size_t idx = static_cast<std::size_t>(step - warmup);
            in[idx] = vIn;
            out[idx] = solver.voltage(clip.outNode);
        }
    }
}

// Step an OpAmpClipperSolver at a FIXED DC input `vin` for `steps` timesteps
// (from a cold reset), returning the final output-node voltage. Resets
// `converged` to true at entry, then clears it to false if any step fails to
// converge (same FR-014 contract as driveSine above).
template <int MaxNodes, int MaxComponents, int MaxDiodes, int MaxOpAmps,
          typename BuildFn>
double settleDc(OpAmpClipperSolver<MaxNodes, MaxComponents, MaxDiodes, MaxOpAmps>& solver,
                 BuildFn build, double vin, double dt, int steps, bool& converged) {
    solver.reset();
    converged = true;
    const auto clip = build(vin);
    double vout = 0.0;
    for (int step = 0; step < steps; ++step) {
        const NewtonStatus status = solver.step(clip.netlist, dt);
        if (!status.converged) {
            converged = false;
        }
        vout = solver.voltage(clip.outNode);
    }
    return vout;
}

}  // namespace opst_test

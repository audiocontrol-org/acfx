// diode-clippers-harness.cpp
// Host-only lab harness: never included by portable code.
//
// Assembled-clipper validation for the bounded TRANSIENT nonlinear solver (T019;
// US2/US3; contracts/transient-clipper.h). Mirrors (and stays behavior-identical
// to) the doctest assertions in tests/core/diode-clipper-transient-test.cpp; this
// file exists so the same validation is runnable/readable outside the test
// framework as a small educational CI check (exits nonzero if anything fails).
//
// Validates, in order: a linear-only RC network against the analytic
// backward-Euler recurrence (the solver proven exact before any nonlinearity is
// trusted), each clipper's DC steady state against an INDEPENDENT bisection
// oracle (and the static NewtonClipper curve, where representable), the assembled
// invariants (symmetry / DC-offset, forward saturation, passivity, the reactive
// signature — larger Cf ⇒ less post-clip HF for the shunt clippers), AND an
// explicit non-convergence check (a starved budget surfaces converged == false).
//
// Include root: core/ (see the acfx_lab_diode_clippers_harness CMake target).
// Compile with -std=c++20.

#include "labs/component-abstractions/solver/newton-clipper.h"
#include "labs/diode-clippers/solver/transient-clipper.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/diode-clipper/diode-clipper.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cmath>
#include <cstdio>

using acfx::AsymmetricShuntValues;
using acfx::asymmetricShuntClipper;
using acfx::Capacitor;
using acfx::Diode;
using acfx::DiodeSpec;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::seriesClipper;
using acfx::SeriesValues;
using acfx::siliconSignalDiode;
using acfx::SymmetricShuntValues;
using acfx::symmetricShuntClipper;
using acfx::VoltageSource;
using acfx::labs::component_abstractions::NewtonClipper;
using acfx::labs::diode_clippers::TransientClipper;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDt = 1.0e-5;   // 100 kHz (FR-017 lab convention)
constexpr int kWindow = 1000;    // 10 periods of 1 kHz
constexpr int kWarmup = 3000;    // 30 ms → steady state before measuring
constexpr int kSettle = 4000;    // DC settle steps

void report(bool ok, const char* label, double measured, double expected, bool& allPassed) {
    std::printf("[%s] %-58s measured=% .12g  expected=% .12g\n",
                ok ? "PASS" : "FAIL", label, measured, expected);
    if (!ok) {
        allPassed = false;
    }
}

DiodeSpec diodeSpec() { return siliconSignalDiode(); }

// Independent bisection root-finder (NOT the solver under test).
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

double oracleSymmetric(double Vin, double R, const DiodeSpec& d) {
    const double nVt = d.n * d.Vt;
    return bisectRoot([&](double v) { return v + R * 2.0 * d.Is * std::sinh(v / nVt) - Vin; },
                      -5.0, 5.0);
}

double oracleAsymmetric(double Vin, double R, const DiodeSpec& d, int up, int down) {
    const double nVt = d.n * d.Vt;
    return bisectRoot(
        [&](double v) {
            const double iUp = up * d.Is * (std::exp(v / nVt) - 1.0);
            const double iDown = down * d.Is * (std::exp(-v / nVt) - 1.0);
            return v + R * (iUp - iDown) - Vin;
        },
        -5.0, 5.0);
}

// Convergence tracker: a non-converged solver iterate is not a physical answer
// (FR-011), so a check that reads solver.voltage(...) after a non-converged
// step() would be certifying stale output. Every expected-converged step() below
// clears this flag on non-convergence; each run function resets it at entry and
// reports it, so the harness cannot PASS on a non-converged measurement.
bool gConverged = true;

template <int N, int M, int D>
double settlePortDC(TransientClipper<N, M, D>& solver, const Netlist<N, M>& nl, int steps) {
    solver.reset();
    for (int i = 0; i < steps; ++i) {
        if (!solver.step(nl, kDt).converged) {
            gConverged = false;
        }
    }
    return solver.clipperVoltage();
}

template <int N, int M, int D, typename BuildFn>
void driveSine(TransientClipper<N, M, D>& solver, BuildFn build, double amp, double freqHz,
               std::array<double, kWindow>& in, std::array<double, kWindow>& out) {
    solver.reset();
    const int total = kWarmup + kWindow;
    for (int nStep = 0; nStep < total; ++nStep) {
        const double vIn = amp * std::sin(2.0 * kPi * freqHz * nStep * kDt);
        const auto clip = build(vIn);
        const auto st = solver.step(clip.netlist, kDt);
        if (nStep >= kWarmup) {
            if (!st.converged) {
                gConverged = false;
            }
            in[static_cast<std::size_t>(nStep - kWarmup)] = vIn;
            out[static_cast<std::size_t>(nStep - kWarmup)] = solver.voltage(clip.outNode);
        }
    }
}

double energy(const std::array<double, kWindow>& x) {
    double e = 0.0;
    for (double v : x) {
        e += v * v;
    }
    return e;
}

double hfEnergyAbove(const std::array<double, kWindow>& x, double fs, double cutoff) {
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

// --- 1. Linear RC vs analytic BE recurrence (SC-002 part 1) ----------------
bool runLinearRc() {
    std::puts("-- Linear RC vs backward-Euler recurrence (SC-002) --");
    bool ok = true;

    Netlist<8, 8> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    const double Vin = 5.0, R = 1000.0, C = 1.0e-6;
    nl.add(VoltageSource{n1, kGround, Vin});
    nl.add(Resistor{n1, n2, R});
    nl.add(Capacitor{n2, kGround, C});
    nl.prepare();

    TransientClipper<8, 8> solver;
    solver.reset();  // explicit cold start (match the helper-mediated paths)
    gConverged = true;
    const double alpha = kDt / (kDt + R * C);
    double vAnalytic = 0.0, maxErr = 0.0;
    for (int step = 0; step < 2000; ++step) {
        if (!solver.step(nl, kDt).converged) {
            gConverged = false;
        }
        vAnalytic = alpha * Vin + (1.0 - alpha) * vAnalytic;
        maxErr = std::max(maxErr, std::fabs(solver.voltage(n2) - vAnalytic));
    }
    report(maxErr < 1e-9, "RC: max |solver - BE recurrence| over 2000 steps < 1e-9", maxErr, 1e-9, ok);
    report(std::fabs(solver.voltage(n2) - Vin) < 1e-6, "RC: DC steady state -> Vin",
           solver.voltage(n2), Vin, ok);
    report(gConverged, "RC: every step() converged", gConverged ? 1.0 : 0.0, 1.0, ok);
    return ok;
}

// --- 2. Clipper DC limits vs independent oracle (SC-002 part 2) ------------
bool runDcLimits() {
    std::puts("-- Clipper DC limits vs independent oracle (SC-002) --");
    bool ok = true;
    const double R = 2200.0, Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    gConverged = true;

    TransientClipper<4, 8> sym;
    NewtonClipper<4, 8> staticClipper;
    double maxSymErr = 0.0, maxStaticErr = 0.0;
    for (double Vin = -2.0; Vin <= 2.0 + 1e-9; Vin += 0.25) {
        const auto clip = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, Vin);
        const double vDC = settlePortDC(sym, clip.netlist, kSettle);
        maxSymErr = std::max(maxSymErr, std::fabs(vDC - oracleSymmetric(Vin, R, d)));

        Netlist<4, 8> ref;
        const NodeId rn1 = ref.addNode();
        const NodeId rn2 = ref.addNode();
        ref.add(VoltageSource{rn1, kGround, Vin});
        ref.add(Resistor{rn1, rn2, R});
        ref.add(Diode{rn2, kGround, d.Is, d.n, d.Vt});
        ref.add(Diode{kGround, rn2, d.Is, d.n, d.Vt});
        ref.prepare();
        staticClipper.reset();
        staticClipper.solve(ref, kDt);
        maxStaticErr = std::max(maxStaticErr, std::fabs(vDC - staticClipper.clipperVoltage()));
    }
    report(maxSymErr < 1e-6, "symmetric: settled DC vs bisection oracle, max err", maxSymErr, 1e-6, ok);
    report(maxStaticErr < 1e-6, "symmetric: settled DC vs static NewtonClipper, max err",
           maxStaticErr, 1e-6, ok);

    TransientClipper<4, 8> asym;
    double maxAsymErr = 0.0;
    for (double Vin = -2.0; Vin <= 2.0 + 1e-9; Vin += 0.25) {
        const auto clip = asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, Vin);
        const double vDC = settlePortDC(asym, clip.netlist, kSettle);
        maxAsymErr = std::max(maxAsymErr, std::fabs(vDC - oracleAsymmetric(Vin, R, d, 2, 1)));
    }
    report(maxAsymErr < 1e-6, "asymmetric: settled DC vs bisection oracle, max err", maxAsymErr, 1e-6, ok);

    TransientClipper<5, 8> ser;
    const auto sclip = seriesClipper(SeriesValues{1.0e-6, 4700.0, d, 1}, 1.5);
    ser.reset();
    for (int i = 0; i < 200000; ++i) {
        if (!ser.step(sclip.netlist, kDt).converged) {
            gConverged = false;
        }
    }
    report(std::fabs(ser.voltage(sclip.outNode)) < 1e-3, "series: coupling cap blocks DC (output -> 0)",
           ser.voltage(sclip.outNode), 0.0, ok);
    report(gConverged, "DC limits: every step() converged", gConverged ? 1.0 : 0.0, 1.0, ok);
    return ok;
}

// --- 3. Symmetry / DC-offset (SC-003) --------------------------------------
bool runSymmetry() {
    std::puts("-- Symmetry / DC-offset (SC-003) --");
    bool ok = true;
    const double R = 2200.0, Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    gConverged = true;

    TransientClipper<4, 8> sym, asym;
    double maxSym = 0.0, maxAsym = 0.0;
    for (double x = 0.25; x <= 2.0 + 1e-9; x += 0.25) {
        const auto sP = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, x);
        const auto sN = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, -x);
        maxSym = std::max(maxSym, std::fabs(settlePortDC(sym, sP.netlist, kSettle) +
                                            settlePortDC(sym, sN.netlist, kSettle)));
        const auto aP = asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, x);
        const auto aN = asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, -x);
        maxAsym = std::max(maxAsym, std::fabs(settlePortDC(asym, aP.netlist, kSettle) +
                                              settlePortDC(asym, aN.netlist, kSettle)));
    }
    report(maxSym < 1e-6, "symmetric: |y(+x)+y(-x)| ~ 0 (odd, no DC offset)", maxSym, 1e-6, ok);
    report(maxAsym > 1e-3, "asymmetric: |y(+x)+y(-x)| measurably > 0 (DC offset)", maxAsym, 1e-3, ok);
    report(gConverged, "symmetry: every settled step() converged", gConverged ? 1.0 : 0.0, 1.0, ok);
    return ok;
}

// --- 4. Saturation + passivity (SC-004) ------------------------------------
bool runSaturationPassivity() {
    std::puts("-- Saturation + passivity (SC-004) --");
    bool ok = true;
    const double R = 2200.0, Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    std::array<double, kWindow> in{}, out{};
    gConverged = true;

    TransientClipper<4, 8> sym;
    const auto s = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, 5.0);
    const double vSat = std::fabs(settlePortDC(sym, s.netlist, kSettle));
    report(vSat < 0.8, "symmetric: forward saturation clamps well under 5 V drive", vSat, 0.8, ok);

    driveSine(sym, [&](double v) { return symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, v); },
              2.0, 1000.0, in, out);
    report(energy(out) <= energy(in), "symmetric: output energy <= input energy (passive)",
           energy(out), energy(in), ok);

    TransientClipper<4, 8> asym;
    driveSine(asym, [&](double v) { return asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, v); },
              2.0, 1000.0, in, out);
    report(energy(out) <= energy(in), "asymmetric: output energy <= input energy (passive)",
           energy(out), energy(in), ok);

    TransientClipper<5, 8> ser;
    driveSine(ser, [&](double v) { return seriesClipper(SeriesValues{47.0e-9, 4700.0, d, 1}, v); },
              5.0, 1000.0, in, out);
    double peak = 0.0;
    for (double v : out) {
        peak = std::max(peak, std::fabs(v));
    }
    report(peak < 0.9, "series: AC output peak bounded near diode drop under 5 V drive", peak, 0.9, ok);
    report(energy(out) <= energy(in), "series: output energy <= input energy (passive)",
           energy(out), energy(in), ok);
    report(gConverged, "saturation/passivity: every measured step() converged",
           gConverged ? 1.0 : 0.0, 1.0, ok);
    return ok;
}

// --- 5. Reactive signature (SC-005) ----------------------------------------
bool runReactiveSignature() {
    std::puts("-- Reactive signature: larger Cf strictly cuts post-clip HF (SC-005) --");
    bool ok = true;
    const double R = 2200.0, fs = 1.0 / kDt;
    const DiodeSpec d = diodeSpec();
    const std::array<double, 6> cfSweep{1.0e-9, 2.2e-9, 4.7e-9, 10.0e-9, 22.0e-9, 47.0e-9};
    std::array<double, kWindow> in{}, out{};
    gConverged = true;

    TransientClipper<4, 8> sym;
    double prev = 1e300;
    bool symMono = true;
    for (double Cf : cfSweep) {
        driveSine(sym, [&](double v) { return symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, v); },
                  2.0, 1000.0, in, out);
        const double hf = hfEnergyAbove(out, fs, 5000.0);
        if (!(hf < prev)) {
            symMono = false;
        }
        prev = hf;
    }
    report(symMono, "symmetric: HF(>5kHz) strictly decreasing across ascending Cf sweep",
           symMono ? 1.0 : 0.0, 1.0, ok);

    TransientClipper<4, 8> asym;
    prev = 1e300;
    bool asymMono = true;
    for (double Cf : cfSweep) {
        driveSine(asym, [&](double v) { return asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, v); },
                  2.0, 1000.0, in, out);
        const double hf = hfEnergyAbove(out, fs, 5000.0);
        if (!(hf < prev)) {
            asymMono = false;
        }
        prev = hf;
    }
    report(asymMono, "asymmetric: HF(>5kHz) strictly decreasing across ascending Cf sweep",
           asymMono ? 1.0 : 0.0, 1.0, ok);
    report(gConverged, "reactive signature: every measured step() converged",
           gConverged ? 1.0 : 0.0, 1.0, ok);
    return ok;
}

// --- 6. Non-convergence is surfaced (SC-006) -------------------------------
bool runNonConvergence() {
    std::puts("-- Non-convergence surfaced (SC-006) --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();

    TransientClipper<4, 8> starved(/*maxIterations=*/1);
    starved.reset();  // explicit cold start (match the helper-mediated paths)
    const auto clip = symmetricShuntClipper(SymmetricShuntValues{2200.0, 10.0e-9, d}, 5.0);
    const auto status = starved.step(clip.netlist, kDt);
    report(!status.converged, "starved budget: converged == false is reported",
           status.converged ? 1.0 : 0.0, 0.0, ok);
    report(status.voltageResidual > starved.voltageTolerance(),
           "starved budget: the residual is surfaced (> tolerance)",
           status.voltageResidual, starved.voltageTolerance(), ok);
    return ok;
}

}  // namespace

int main() {
    bool allPassed = true;
    allPassed = runLinearRc() && allPassed;
    allPassed = runDcLimits() && allPassed;
    allPassed = runSymmetry() && allPassed;
    allPassed = runSaturationPassivity() && allPassed;
    allPassed = runReactiveSignature() && allPassed;
    allPassed = runNonConvergence() && allPassed;

    std::puts(allPassed ? "\ndiode-clippers-harness: ALL CHECKS PASSED"
                        : "\ndiode-clippers-harness: FAILURES DETECTED");
    return allPassed ? 0 : 1;
}

// component-abstractions-harness.cpp
// Host-only lab harness: never included by portable code.
//
// Assembled-circuit validation for the deliberately-naive LINEAR reference
// solver (T016; US3/SC-002/SC-003; contracts/reference-solver.md SS Linear).
// Builds three circuits from the component-abstractions vocabulary --
// resistive divider, RC low-pass, series RLC -- runs each through
// LinearSolver, and prints PASS/FAIL lines with measured-vs-analytic
// numbers. Mirrors (and stays behavior-identical to) the doctest assertions
// in tests/core/circuit-solver-test.cpp; this file exists so the same
// validation is runnable/readable outside the test framework as a small,
// educational CI check (exits nonzero if anything fails).
//
// Also validates the diode clipper (T019; US4/SC-004/SC-005; contracts/
// reference-solver.md SS Nonlinear/Scope): a single-diode clipper and an
// antiparallel diode pair, each checked against an INDEPENDENT bisection
// reference over the clipper's own transcendental equation (not the solver
// graded against itself).
//
// Include root: core/ (see the acfx_lab_component_abstractions_harness CMake
// target). Compile with -std=c++20.

#include "labs/component-abstractions/solver/linear-solver.h"
#include "labs/component-abstractions/solver/newton-clipper.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <cmath>
#include <cstdio>

using acfx::Capacitor;
using acfx::Diode;
using acfx::Inductor;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::labs::component_abstractions::LinearSolver;
using acfx::labs::component_abstractions::NewtonClipper;
using acfx::labs::component_abstractions::NewtonStatus;

namespace {

// Print a single PASS/FAIL line and fold the result into `allPassed`.
void report(bool ok, const char* label, double measured, double expected, bool& allPassed) {
    std::printf("[%s] %-58s measured=% .12g  expected=% .12g\n",
                ok ? "PASS" : "FAIL", label, measured, expected);
    if (!ok) {
        allPassed = false;
    }
}

// ---------------------------------------------------------------------------
// 1. Resistive divider (SC-002): Vin --VoltageSource-- node1 --R1-- node2
//    --R2-- ground. Must be EXACT: v[node2] == Vin * R2 / (R1 + R2).
// ---------------------------------------------------------------------------
bool runResistiveDivider() {
    std::puts("-- Resistive divider (SC-002) --");
    bool allPassed = true;

    Netlist<8, 8> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    const double Vin = 10.0;
    const double R1 = 1000.0;
    const double R2 = 2000.0;

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R1});
    nl.add(Resistor{node2, kGround, R2});
    nl.prepare();

    LinearSolver<8, 8> solver;
    solver.solve(nl, 1.0e-3);  // purely resistive: dt does not affect the answer

    const double expected = Vin * R2 / (R1 + R2);
    const double measured = solver.voltage(node2);
    report(std::fabs(measured - expected) < 1e-9, "divider: v[node2] == Vin*R2/(R1+R2), exact to 1e-9",
           measured, expected, allPassed);
    return allPassed;
}

// ---------------------------------------------------------------------------
// 2. RC low-pass (SC-003): source --R-- node2 --C-- ground.
//
// The rigorous check for a backward-Euler solver is agreement with the
// CLOSED-FORM BACKWARD-EULER RECURRENCE for the same R, C, dt:
//   v2[n] = alpha*Vin + (1-alpha)*v2[n-1],  alpha = dt / (dt + R*C)
// Both sides are BE, so they must agree to numerical precision -- this
// proves the solver's assembly is correct. Separately: BE tracks the
// continuous 1/(1+jwRC) response only within its own known damping/warping
// (s -> (z-1)/(dt*z)), which is why sample-for-sample agreement with the
// CONTINUOUS response is not asserted here -- only DC steady state is
// (where BE and the continuous system agree exactly, since steady-state
// current is zero).
// ---------------------------------------------------------------------------
bool runRcLowPass() {
    std::puts("-- RC low-pass (SC-003) --");
    bool allPassed = true;

    Netlist<8, 8> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    const double Vin = 5.0;
    const double R = 1000.0;
    const double C = 1.0e-6;
    const double dt = 1.0e-5;

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R});
    nl.add(Capacitor{node2, kGround, C});
    nl.prepare();

    LinearSolver<8, 8> solver;

    const double alpha = dt / (dt + R * C);
    double v2Analytic = 0.0;
    double maxRecurrenceError = 0.0;

    constexpr int kSteps = 2000;  // 20 RC time constants
    for (int step = 0; step < kSteps; ++step) {
        solver.solve(nl, dt);
        v2Analytic = alpha * Vin + (1.0 - alpha) * v2Analytic;
        const double err = std::fabs(solver.voltage(node2) - v2Analytic);
        if (err > maxRecurrenceError) {
            maxRecurrenceError = err;
        }
    }

    report(maxRecurrenceError < 1e-9,
           "RC: max |solver - BE-recurrence| over 2000 steps < 1e-9",
           maxRecurrenceError, 1e-9, allPassed);
    report(std::fabs(solver.voltage(node2) - Vin) < 1e-6,
           "RC: DC steady state v[node2] -> Vin", solver.voltage(node2), Vin, allPassed);
    return allPassed;
}

// ---------------------------------------------------------------------------
// 3. Series RLC (SC-003): source --R-- node2 --L-- node3 --C-- ground.
//
// No closed-form step comparison (materially more work for a second-order
// recurrence, and SC-003 only asks for magnitude/phase-class agreement).
// Instead: the transient must stay finite (stability), must show the
// OVERSHOOT that marks a damped second-order response (an underdamped RLC
// step response rings past its DC target; a first-order RC never does),
// and must settle to the same zero-steady-state-current DC target as the
// RC case (Vin, since nothing downstream of C draws current at DC).
// ---------------------------------------------------------------------------
bool runSeriesRlc() {
    std::puts("-- Series RLC (SC-003) --");
    bool allPassed = true;

    Netlist<8, 8> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();
    const NodeId node3 = nl.addNode();

    const double Vin = 5.0;
    const double R = 10.0;      // zeta = (R/2)*sqrt(C/L) ~= 0.158: underdamped
    const double L = 1.0e-3;
    const double C = 1.0e-6;
    const double dt = 1.0e-6;   // ~200 samples per natural period

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R});
    nl.add(Inductor{node2, node3, L});
    nl.add(Capacitor{node3, kGround, C});
    nl.prepare();

    LinearSolver<8, 8> solver;

    constexpr int kSteps = 20000;  // 20 ms ~= 100 envelope time constants
    double maxV3 = 0.0;
    bool allFinite = true;

    for (int step = 0; step < kSteps; ++step) {
        solver.solve(nl, dt);
        const double v3 = solver.voltage(node3);
        if (!std::isfinite(v3)) {
            allFinite = false;
            break;
        }
        if (v3 > maxV3) {
            maxV3 = v3;
        }
    }

    report(allFinite, "RLC: transient stays finite (no NaN/Inf) over 20000 steps",
           allFinite ? 1.0 : 0.0, 1.0, allPassed);
    report(maxV3 > 1.05 * Vin, "RLC: damped second-order overshoot (peak > 1.05*Vin)",
           maxV3, 1.05 * Vin, allPassed);
    report(std::fabs(solver.voltage(node3) - Vin) < 1e-6,
           "RLC: DC steady state v[node3] -> Vin", solver.voltage(node3), Vin, allPassed);
    return allPassed;
}

// ---------------------------------------------------------------------------
// Diode-clipper validation (T019; US4/SC-004/SC-005), mirroring the doctest
// cases in tests/core/circuit-solver-test.cpp. The independent references
// below come from a standalone bisection root-finder over the clipper's own
// transcendental equation -- NOT from re-running NewtonClipper -- so this is
// a genuine cross-check, not the solver graded against itself.
// ---------------------------------------------------------------------------

// Well-damped standalone bisection root-finder (see the mirrored doctest
// helper for the derivation). Assumes `f` is monotonically non-decreasing
// over [lo, hi] with a root inside; halves the bracket to width < tol.
template <typename F>
double bisectRoot(F f, double lo, double hi, double tol = 1e-12) {
    double flo = f(lo);
    double fhi = f(hi);
    if (!(flo <= 0.0 && fhi >= 0.0)) {
        std::printf("bisectRoot: root not bracketed over [%.6g, %.6g]: f(lo)=%.6g f(hi)=%.6g\n",
                     lo, hi, flo, fhi);
    }
    while (hi - lo > tol) {
        const double mid = 0.5 * (lo + hi);
        const double fmid = f(mid);
        if (fmid <= 0.0) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// Independent reference for the single-diode clipper: 0 = v + R*Is*(exp(v/
// (n*Vt)) - 1) - Vin.
double independentSingleDiodeClipper(double Vin, double R, double Is, double n, double Vt) {
    const double nVt = n * Vt;
    const auto f = [&](double v) {
        return v + R * Is * (std::exp(v / nVt) - 1.0) - Vin;
    };
    return bisectRoot(f, -3.0, 3.0);
}

// Independent reference for the antiparallel-pair clipper: the two diodes'
// "-1" terms cancel, leaving 0 = v + 2*R*Is*sinh(v/(n*Vt)) - Vin.
double independentAntiparallelClipper(double Vin, double R, double Is, double n, double Vt) {
    const double nVt = n * Vt;
    const auto f = [&](double v) {
        return v + 2.0 * R * Is * std::sinh(v / nVt) - Vin;
    };
    return bisectRoot(f, -3.0, 3.0);
}

// ---------------------------------------------------------------------------
// 4. Single-diode clipper (US4.1/US4.3/SC-004): Vin --VoltageSource--
//    node1 --R-- node2 --Diode(anode=node2, cathode=ground)-- ground. Swept
//    over Vin in [-2, 2] V: must converge within the iteration bound at
//    every bias, match the independent transcendental reference, and
//    forward-saturate near the diode drop (well under Vin) rather than
//    passing the full swing through.
// ---------------------------------------------------------------------------
bool runSingleDiodeClipper() {
    std::puts("-- Single-diode clipper (US4.1/SC-004) --");
    bool allPassed = true;

    const double R = 1000.0;
    const double Is = 1.0e-14;
    const double n = 1.0;
    const double Vt = 0.02585;
    const double dt = 1.0e-5;  // purely resistive port: dt does not affect the answer

    NewtonClipper<8, 8> clipper;

    bool allConverged = true;
    int maxIterationsSeen = 0;
    double maxError = 0.0;

    for (double Vin = -2.0; Vin <= 2.0 + 1e-9; Vin += 0.1) {
        Netlist<8, 8> nl;
        const NodeId node1 = nl.addNode();
        const NodeId node2 = nl.addNode();
        nl.add(VoltageSource{node1, kGround, Vin});
        nl.add(Resistor{node1, node2, R});
        nl.add(Diode{node2, kGround, Is, n, Vt});
        nl.prepare();

        const NewtonStatus status = clipper.solve(nl, dt);
        if (!status.converged) {
            allConverged = false;
        }
        if (status.iterations > maxIterationsSeen) {
            maxIterationsSeen = status.iterations;
        }

        const double expected = independentSingleDiodeClipper(Vin, R, Is, n, Vt);
        const double err = std::fabs(clipper.voltage(node2) - expected);
        if (err > maxError) {
            maxError = err;
        }
    }

    report(allConverged, "clipper: Newton converged at every swept Vin in [-2,2]V",
           allConverged ? 1.0 : 0.0, 1.0, allPassed);
    report(maxIterationsSeen <= clipper.maxIterations(),
           "clipper: iterations stayed within the configured bound",
           maxIterationsSeen, clipper.maxIterations(), allPassed);
    report(maxError < 1e-6,
           "clipper: v matches independent transcendental reference (SC-004), max err",
           maxError, 1e-6, allPassed);

    // Saturation sanity: a deep forward drive clips well under Vin (soft
    // clip near the diode's forward drop), unlike a linear pass-through.
    Netlist<8, 8> nlHigh;
    const NodeId h1 = nlHigh.addNode();
    const NodeId h2 = nlHigh.addNode();
    nlHigh.add(VoltageSource{h1, kGround, 2.0});
    nlHigh.add(Resistor{h1, h2, R});
    nlHigh.add(Diode{h2, kGround, Is, n, Vt});
    nlHigh.prepare();
    clipper.solve(nlHigh, dt);
    const double vHigh = clipper.voltage(h2);
    report(vHigh < 1.0, "clipper: forward saturation stays well under Vin=2V (soft-clipped)",
           vHigh, 1.0, allPassed);

    return allPassed;
}

// ---------------------------------------------------------------------------
// 5. Antiparallel diode pair (US4.2/SC-004): same port, two diodes of
//    opposite orientation. Transfer must be symmetric (output(-Vin) ==
//    -output(Vin)) and match the independent antiparallel reference.
// ---------------------------------------------------------------------------
bool runAntiparallelClipper() {
    std::puts("-- Antiparallel diode pair (US4.2/SC-004) --");
    bool allPassed = true;

    const double R = 1000.0;
    const double Is = 1.0e-14;
    const double n = 1.0;
    const double Vt = 0.02585;
    const double dt = 1.0e-5;

    NewtonClipper<8, 8> clipper;

    bool allConverged = true;
    double maxSymmetryError = 0.0;
    double maxReferenceError = 0.0;

    for (double Vin = 0.1; Vin <= 2.0 + 1e-9; Vin += 0.1) {
        Netlist<8, 8> nlPos;
        const NodeId p1 = nlPos.addNode();
        const NodeId p2 = nlPos.addNode();
        nlPos.add(VoltageSource{p1, kGround, Vin});
        nlPos.add(Resistor{p1, p2, R});
        nlPos.add(Diode{p2, kGround, Is, n, Vt});
        nlPos.add(Diode{kGround, p2, Is, n, Vt});
        nlPos.prepare();
        const NewtonStatus statusPos = clipper.solve(nlPos, dt);
        if (!statusPos.converged) {
            allConverged = false;
        }
        const double vPos = clipper.voltage(p2);

        Netlist<8, 8> nlNeg;
        const NodeId m1 = nlNeg.addNode();
        const NodeId m2 = nlNeg.addNode();
        nlNeg.add(VoltageSource{m1, kGround, -Vin});
        nlNeg.add(Resistor{m1, m2, R});
        nlNeg.add(Diode{m2, kGround, Is, n, Vt});
        nlNeg.add(Diode{kGround, m2, Is, n, Vt});
        nlNeg.prepare();
        const NewtonStatus statusNeg = clipper.solve(nlNeg, dt);
        if (!statusNeg.converged) {
            allConverged = false;
        }
        const double vNeg = clipper.voltage(m2);

        const double symErr = std::fabs(vPos + vNeg);
        if (symErr > maxSymmetryError) {
            maxSymmetryError = symErr;
        }

        const double expected = independentAntiparallelClipper(Vin, R, Is, n, Vt);
        const double refErr = std::fabs(vPos - expected);
        if (refErr > maxReferenceError) {
            maxReferenceError = refErr;
        }
    }

    report(allConverged, "antiparallel: Newton converged at every swept |Vin| in (0,2]V",
           allConverged ? 1.0 : 0.0, 1.0, allPassed);
    report(maxSymmetryError < 1e-6,
           "antiparallel: output(-Vin) == -output(Vin), max err", maxSymmetryError, 1e-6, allPassed);
    report(maxReferenceError < 1e-6,
           "antiparallel: v matches independent reference, max err", maxReferenceError, 1e-6, allPassed);

    return allPassed;
}

}  // namespace

int main() {
    bool allPassed = true;

    allPassed = runResistiveDivider() && allPassed;
    allPassed = runRcLowPass() && allPassed;
    allPassed = runSeriesRlc() && allPassed;
    allPassed = runSingleDiodeClipper() && allPassed;
    allPassed = runAntiparallelClipper() && allPassed;

    std::puts(allPassed ? "\ncomponent-abstractions-harness: ALL CHECKS PASSED"
                         : "\ncomponent-abstractions-harness: FAILURES DETECTED");
    return allPassed ? 0 : 1;
}

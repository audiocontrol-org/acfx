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
// Include root: core/ (see the acfx_lab_component_abstractions_harness CMake
// target). Compile with -std=c++20.

#include "labs/component-abstractions/solver/linear-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <cmath>
#include <cstdio>

using acfx::Capacitor;
using acfx::Inductor;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::labs::component_abstractions::LinearSolver;

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

}  // namespace

int main() {
    bool allPassed = true;

    allPassed = runResistiveDivider() && allPassed;
    allPassed = runRcLowPass() && allPassed;
    allPassed = runSeriesRlc() && allPassed;

    std::puts(allPassed ? "\ncomponent-abstractions-harness: ALL CHECKS PASSED"
                         : "\ncomponent-abstractions-harness: FAILURES DETECTED");
    return allPassed ? 0 : 1;
}

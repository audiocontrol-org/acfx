#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "labs/component-abstractions/solver/linear-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "support/allocation-sentinel.h"

// T016 -- US3/SC-002/SC-003: assembled-circuit validation of the naive linear
// reference solver (labs/component-abstractions/solver/linear-solver.h). Three
// circuit classes, mirrored in the lab harness
// (core/labs/component-abstractions/harness/component-abstractions-harness.cpp):
//   1. Resistive divider  -- must be EXACT (SC-002).
//   2. RC low-pass        -- must match the closed-form backward-Euler
//                             recurrence step-for-step (both are BE, so they
//                             agree to numerical precision), plus DC settling.
//   3. Series RLC         -- stability + damped second-order character +
//                             correct DC steady state (SC-003).
// Plus the no-heap-in-solve() invariant (SC-003 acceptance 4).

using acfx::Capacitor;
using acfx::Inductor;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::labs::component_abstractions::LinearSolver;
using acfx::test::AllocationSentinel;

// ---------------------------------------------------------------------------
// US3.1 / SC-002 -- resistive divider: v[node2] == Vin * R2 / (R1 + R2)
// EXACTLY (to solver floating-point precision, not just "close").
// ---------------------------------------------------------------------------

TEST_CASE("LinearSolver - resistive divider matches the exact ratio (SC-002)") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    const double Vin = 10.0;
    const double R1 = 1000.0;
    const double R2 = 2000.0;

    // Vin --VoltageSource-- node1 --R1-- node2 --R2-- ground
    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R1});
    nl.add(Resistor{node2, kGround, R2});
    nl.prepare();

    LinearSolver<8, 8> solver;
    // Purely resistive: dt is irrelevant to the answer (no reactive
    // companions stamp anything), but the API still requires dt > 0.
    CHECK_NOTHROW(solver.solve(nl, 1.0e-3));

    const double expected = Vin * R2 / (R1 + R2);
    CHECK_MESSAGE(std::fabs(solver.voltage(node2) - expected) < 1e-9,
                  "divider: got ", solver.voltage(node2), " expected ", expected);
    // node1 sits exactly at Vin (fixed-node reduction, not an approximation).
    CHECK_MESSAGE(std::fabs(solver.voltage(node1) - Vin) < 1e-9,
                  "divider: node1 got ", solver.voltage(node1), " expected ", Vin);
}

// ---------------------------------------------------------------------------
// US3.2 / SC-003 -- RC low-pass: source --R-- node2 --C-- ground.
//
// The RIGOROUS check for a backward-Euler solver is not "does it match the
// continuous s-domain transfer function sample-for-sample" (it deliberately
// does not -- BE has known numerical damping/warping relative to the
// continuous 1/(1+jwRC) response). The rigorous check is that the solver's
// assembled system matches the CLOSED-FORM BACKWARD-EULER RECURRENCE for the
// same R, C, dt:
//
//   v2[n] = alpha * Vin + (1 - alpha) * v2[n-1],   alpha = dt / (dt + R*C)
//
// This recurrence is what the solver's stamped system algebraically reduces
// to for this topology (R stamps G=1/R at node2 plus the pinned-node RHS
// contribution G*Vin; the capacitor stamps Geq=C/dt plus history Ieq=Geq*
// vPrev). Both sides are BE, so they must agree to ~1e-9 -- this proves the
// ASSEMBLY is correct, independent of BE's own approximation of the
// continuous response (which is a separate, well-known frequency-domain
// concern: BE maps s -> (z-1)/(dt*z), warping and damping the true 1/(1+jwRC)
// magnitude/phase -- not asserted bit-for-bit here, only via DC agreement).
//
// DC steady state is exact for both: with nothing downstream of the
// capacitor, steady-state current is zero everywhere, so v2 -> Vin exactly.
// ---------------------------------------------------------------------------

TEST_CASE("LinearSolver - RC low-pass matches the backward-Euler recurrence and settles to DC (SC-003)") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();  // Vin node
    const NodeId node2 = nl.addNode();  // R/C junction (the output)

    const double Vin = 5.0;
    const double R = 1000.0;    // ohms
    const double C = 1.0e-6;    // farads (RC = 1 ms)
    const double dt = 1.0e-5;   // 10 us step (100 steps per RC)

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R});
    nl.add(Capacitor{node2, kGround, C});
    nl.prepare();

    LinearSolver<8, 8> solver;

    const double alpha = dt / (dt + R * C);
    double v2Analytic = 0.0;  // cold capacitor: v(0) == 0, matching solver.reset()

    // 2000 steps == 20 RC time constants: the BE recurrence's own settling
    // error decays as (1 - alpha)^n, well under 1e-6 by the end (~2e-9 here),
    // so the final sample also pins down DC steady state (v2 -> Vin).
    constexpr int kSteps = 2000;
    for (int step = 0; step < kSteps; ++step) {
        CHECK_NOTHROW(solver.solve(nl, dt));
        v2Analytic = alpha * Vin + (1.0 - alpha) * v2Analytic;

        const double got = solver.voltage(node2);
        CHECK_MESSAGE(std::fabs(got - v2Analytic) < 1e-9,
                      "RC step ", step, ": solver=", got, " BE-recurrence=", v2Analytic);
    }

    // DC steady state: after 20 time constants the capacitor is (numerically)
    // fully charged and the output equals the source exactly.
    CHECK_MESSAGE(std::fabs(solver.voltage(node2) - Vin) < 1e-6,
                  "RC steady state: got ", solver.voltage(node2), " expected ", Vin);
}

// ---------------------------------------------------------------------------
// US3.3 / SC-003 -- series RLC: source --R-- node2 --L-- node3 --C-- ground.
//
// No closed-form step-by-step comparison here (unlike the RC case, the
// second-order recurrence is materially more work to hand-derive and the
// contract only asks for magnitude/phase-class agreement, SC-003). Instead:
//   - stability: every sample stays finite (no NaN/Inf) across the transient;
//   - damped second-order character: an underdamped RLC step response
//     OVERSHOOTS the DC target before settling (a plain first-order RC never
//     does) -- this is what distinguishes "qualitatively second order" from
//     "qualitatively first order";
//   - correct DC steady state: with nothing downstream of the capacitor,
//     steady-state current through R/L is zero, so v(node3) -> Vin exactly,
//     the same DC argument as the RC case above.
// ---------------------------------------------------------------------------

TEST_CASE("LinearSolver - series RLC is stable, shows damped second-order overshoot, and settles to DC (SC-003)") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();  // Vin node
    const NodeId node2 = nl.addNode();  // R/L junction
    const NodeId node3 = nl.addNode();  // L/C junction (the output)

    const double Vin = 5.0;
    const double R = 10.0;      // ohms
    const double L = 1.0e-3;    // henries
    const double C = 1.0e-6;    // farads
    // zeta = (R/2) * sqrt(C/L) ~= 0.158 -- underdamped, so the step response
    // rings (overshoots) rather than just charging monotonically.
    const double dt = 1.0e-6;   // 1 us step (~200 samples per natural period)

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R});
    nl.add(Inductor{node2, node3, L});
    nl.add(Capacitor{node3, kGround, C});
    nl.prepare();

    LinearSolver<8, 8> solver;

    constexpr int kSteps = 20000;  // 20 ms -- ~100 envelope time constants
    double maxV3 = 0.0;
    bool allFinite = true;

    for (int step = 0; step < kSteps; ++step) {
        CHECK_NOTHROW(solver.solve(nl, dt));
        const double v3 = solver.voltage(node3);
        if (!std::isfinite(v3)) {
            allFinite = false;
            break;
        }
        if (v3 > maxV3) {
            maxV3 = v3;
        }
    }

    CHECK_MESSAGE(allFinite, "RLC transient produced a non-finite sample (instability)");
    // Second-order damped character: the underdamped step response
    // overshoots the DC target (continuous-time peak is ~1.6x Vin for
    // zeta ~= 0.158; BE's numerical damping only partially attenuates this
    // at ~200 samples/period, so a conservative 1.05x threshold safely
    // separates "rings" from "monotonic first-order charging").
    CHECK_MESSAGE(maxV3 > 1.05 * Vin,
                  "RLC: expected overshoot above ", 1.05 * Vin, ", got max ", maxV3);
    // DC steady state, same zero-steady-state-current argument as the RC case.
    CHECK_MESSAGE(std::fabs(solver.voltage(node3) - Vin) < 1e-6,
                  "RLC steady state: got ", solver.voltage(node3), " expected ", Vin);
}

// ---------------------------------------------------------------------------
// SC-003 acceptance 4 -- no heap allocation anywhere in the solve loop.
// Construction, netlist assembly, and prepare() run OUTSIDE the sentinel
// scope (control-thread build step, may allocate); only the repeated
// solve() stepping over an already-prepared netlist is measured, mirroring
// the AllocationSentinel pattern used across the suite (see
// tests/core/circuit-netlist-test.cpp / tests/core/no-allocation-test.cpp).
// ---------------------------------------------------------------------------

TEST_CASE("LinearSolver - stepping a prepared RC circuit allocates nothing (SC-003 acceptance 4)") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{node1, kGround, 5.0});
    nl.add(Resistor{node1, node2, 1000.0});
    nl.add(Capacitor{node2, kGround, 1.0e-6});
    nl.prepare();

    LinearSolver<8, 8> solver;
    const double dt = 1.0e-5;

    AllocationSentinel::reset();
    for (int step = 0; step < 2000; ++step) {
        solver.solve(nl, dt);
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK(std::isfinite(solver.voltage(node2)));
    CHECK_MESSAGE(allocations == 0,
                  "LinearSolver::solve stepping allocated ", allocations);
}

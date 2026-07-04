#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "labs/component-abstractions/solver/linear-solver.h"
#include "labs/component-abstractions/solver/newton-clipper.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
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
//
// T019 -- US4/SC-004/SC-005: assembled-circuit validation of the diode
// clipper (labs/component-abstractions/solver/newton-clipper.h), mirrored in
// the same lab harness:
//   4. Single-diode clipper vs an INDEPENDENT transcendental reference.
//   5. Antiparallel diode pair -- symmetric transfer + independent reference.
//   6. Newton convergence within the configured iteration bound (folded into
//      4 and 5 above -- every swept bias is checked).
//   7. Scope refusal -- two independent (different-node-pair) nonlinearities
//      are out of the reference solver's scope and must be REFUSED (FR-016).

using acfx::Capacitor;
using acfx::Diode;
using acfx::Inductor;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::labs::component_abstractions::LinearSolver;
using acfx::labs::component_abstractions::NewtonClipper;
using acfx::labs::component_abstractions::NewtonStatus;
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

// ---------------------------------------------------------------------------
// T019 -- US4/SC-004/SC-005: diode clipper (newton-clipper.h). The reference
// values below are computed by an INDEPENDENT bisection root-finder over the
// clipper's own transcendental equation -- NOT by re-running NewtonClipper --
// so these are genuine cross-checks, not the solver graded against itself.
// ---------------------------------------------------------------------------

namespace {

// Well-damped standalone bisection root-finder, used ONLY to build the
// independent references below. Assumes `f` is monotonically non-decreasing
// over [lo, hi] and that a root lies in that bracket (both hold for the
// clipper equations here: the port voltage term is monotonic and the diode
// exponential/hyperbolic term only adds to that monotonicity). Halves the
// bracket until its width is under `tol` (1e-12) -- never touches
// NewtonClipper or Diode::evaluate().
template <typename F>
double bisectRoot(F f, double lo, double hi, double tol = 1e-12) {
    double flo = f(lo);
    double fhi = f(hi);
    REQUIRE_MESSAGE((flo <= 0.0 && fhi >= 0.0),
                    "bisectRoot: root not bracketed over [", lo, ", ", hi,
                    "]: f(lo)=", flo, " f(hi)=", fhi);
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

// Independent reference for the single-diode clipper (Vin --R-- node --diode
// (anode=node, cathode=ground)-- ground). KCL at `node` gives
//   (Vin - v) / R = Is*(exp(v/(n*Vt)) - 1)
//   <=>  0 = v + R*Is*(exp(v/(n*Vt)) - 1) - Vin
double independentSingleDiodeClipper(double Vin, double R, double Is,
                                      double n, double Vt) {
    const double nVt = n * Vt;
    const auto f = [&](double v) {
        return v + R * Is * (std::exp(v / nVt) - 1.0) - Vin;
    };
    // [-3, 3] safely brackets the root for |Vin| <= 2V at these R/Is/Vt
    // values without the exp() term overflowing.
    return bisectRoot(f, -3.0, 3.0);
}

// Independent reference for the antiparallel-pair clipper: two diodes of
// opposite orientation across the same port sum their Shockley currents as
// Is*(exp(v/nVt)-1) - Is*(exp(-v/nVt)-1) = 2*Is*sinh(v/nVt) (the "-1" terms
// cancel), giving the port equation
//   0 = v + 2*R*Is*sinh(v/(n*Vt)) - Vin
double independentAntiparallelClipper(double Vin, double R, double Is,
                                       double n, double Vt) {
    const double nVt = n * Vt;
    const auto f = [&](double v) {
        return v + 2.0 * R * Is * std::sinh(v / nVt) - Vin;
    };
    return bisectRoot(f, -3.0, 3.0);
}

}  // namespace

// ---------------------------------------------------------------------------
// US4.1/US4.3 / SC-004 -- single-diode clipper matches the independent
// reference above, and Newton converges within the configured iteration
// bound at every swept bias.
// ---------------------------------------------------------------------------

TEST_CASE("NewtonClipper - single-diode clipper matches an independent transcendental reference (SC-004)") {
    const double R = 1000.0;
    const double Is = 1.0e-14;
    const double n = 1.0;
    const double Vt = 0.02585;
    const double dt = 1.0e-5;  // purely resistive port: dt does not affect the answer

    NewtonClipper<8, 8> clipper;

    for (double Vin = -2.0; Vin <= 2.0 + 1e-9; Vin += 0.1) {
        Netlist<8, 8> nl;
        const NodeId node1 = nl.addNode();
        const NodeId node2 = nl.addNode();
        nl.add(VoltageSource{node1, kGround, Vin});
        nl.add(Resistor{node1, node2, R});
        nl.add(Diode{node2, kGround, Is, n, Vt});
        nl.prepare();

        NewtonStatus status;
        CHECK_NOTHROW(status = clipper.solve(nl, dt));

        CHECK_MESSAGE(status.converged,
                      "Vin=", Vin, ": Newton did not converge within ",
                      clipper.maxIterations(), " iterations (residual v=",
                      status.voltageResidual, " i=", status.currentResidual, ")");
        CHECK_MESSAGE(status.iterations <= clipper.maxIterations(),
                      "Vin=", Vin, ": iterations ", status.iterations,
                      " exceeded the configured bound ", clipper.maxIterations());

        const double expected = independentSingleDiodeClipper(Vin, R, Is, n, Vt);
        const double measured = clipper.voltage(node2);
        CHECK_MESSAGE(std::fabs(measured - expected) < 1e-6,
                      "Vin=", Vin, ": clipper v=", measured,
                      " independent reference=", expected);
    }
}

// ---------------------------------------------------------------------------
// US4.2 / SC-004 -- antiparallel diode pair: transfer is symmetric
// (output(-Vin) == -output(Vin)) and matches the independent reference for
// the antiparallel port equation.
// ---------------------------------------------------------------------------

TEST_CASE("NewtonClipper - antiparallel diode pair is symmetric and matches an independent reference (US4.2/SC-004)") {
    const double R = 1000.0;
    const double Is = 1.0e-14;
    const double n = 1.0;
    const double Vt = 0.02585;
    const double dt = 1.0e-5;

    NewtonClipper<8, 8> clipper;

    for (double Vin = 0.1; Vin <= 2.0 + 1e-9; Vin += 0.1) {
        Netlist<8, 8> nlPos;
        const NodeId p1 = nlPos.addNode();
        const NodeId p2 = nlPos.addNode();
        nlPos.add(VoltageSource{p1, kGround, Vin});
        nlPos.add(Resistor{p1, p2, R});
        nlPos.add(Diode{p2, kGround, Is, n, Vt});
        nlPos.add(Diode{kGround, p2, Is, n, Vt});
        nlPos.prepare();

        NewtonStatus statusPos;
        CHECK_NOTHROW(statusPos = clipper.solve(nlPos, dt));
        CHECK_MESSAGE(statusPos.converged,
                      "Vin=", Vin, ": antiparallel Newton did not converge within ",
                      clipper.maxIterations(), " iterations");
        CHECK_MESSAGE(statusPos.iterations <= clipper.maxIterations(),
                      "Vin=", Vin, ": antiparallel iterations ", statusPos.iterations,
                      " exceeded the configured bound ", clipper.maxIterations());
        const double vPos = clipper.voltage(p2);

        Netlist<8, 8> nlNeg;
        const NodeId m1 = nlNeg.addNode();
        const NodeId m2 = nlNeg.addNode();
        nlNeg.add(VoltageSource{m1, kGround, -Vin});
        nlNeg.add(Resistor{m1, m2, R});
        nlNeg.add(Diode{m2, kGround, Is, n, Vt});
        nlNeg.add(Diode{kGround, m2, Is, n, Vt});
        nlNeg.prepare();

        NewtonStatus statusNeg;
        CHECK_NOTHROW(statusNeg = clipper.solve(nlNeg, dt));
        CHECK_MESSAGE(statusNeg.converged,
                      "Vin=", -Vin, ": antiparallel Newton did not converge within ",
                      clipper.maxIterations(), " iterations");
        const double vNeg = clipper.voltage(m2);

        CHECK_MESSAGE(std::fabs(vPos + vNeg) < 1e-6,
                      "Vin=", Vin, ": antiparallel transfer not symmetric: v(+Vin)=",
                      vPos, " v(-Vin)=", vNeg);

        const double expected = independentAntiparallelClipper(Vin, R, Is, n, Vt);
        CHECK_MESSAGE(std::fabs(vPos - expected) < 1e-6,
                      "Vin=", Vin, ": antiparallel v=", vPos,
                      " independent reference=", expected);
    }
}

// ---------------------------------------------------------------------------
// US4.4 / FR-016 / SC-005 -- two independent nonlinearities (a diode on each
// of two DIFFERENT node pairs) are out of the reference solver's scope and
// must be REFUSED with a descriptive exception, never silently (mis)solved.
// ---------------------------------------------------------------------------

TEST_CASE("NewtonClipper - refuses a netlist with two independent (different-node-pair) diodes (US4.4/FR-016/SC-005)") {
    Netlist<8, 8> nl;
    const NodeId node1 = nl.addNode();  // shared Vin node
    const NodeId nodeA = nl.addNode();  // first clipper's port
    const NodeId nodeB = nl.addNode();  // second, INDEPENDENT clipper's port

    nl.add(VoltageSource{node1, kGround, 1.5});
    nl.add(Resistor{node1, nodeA, 1000.0});
    nl.add(Diode{nodeA, kGround, 1.0e-14, 1.0, 0.02585});
    nl.add(Resistor{node1, nodeB, 1000.0});
    nl.add(Diode{nodeB, kGround, 1.0e-14, 1.0, 0.02585});
    nl.prepare();

    NewtonClipper<8, 8> clipper;

    CHECK_THROWS_AS(clipper.solve(nl, 1.0e-5), std::exception);
    CHECK_THROWS_WITH_AS(clipper.solve(nl, 1.0e-5),
                          doctest::Contains("out of reference-solver scope"),
                          std::runtime_error);

    // Confirm it did NOT silently attempt the solve: the linear solver's
    // state (which solve() only touches AFTER the scope check passes) must
    // remain at its untouched initial value.
    CHECK_MESSAGE(clipper.voltage(nodeA) == 0.0,
                  "scope refusal must not silently perform a partial solve");
}

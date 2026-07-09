#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <cmath>

// ReactiveIntegrator closed-form doctest suite (tasks.md T006/T008).
// (specs/implicit-integration/contracts/reactive-integrator.md;
// specs/implicit-integration/data-model.md.)
//
// ReactiveIntegrator::step() (core/primitives/circuit/integration/
// reactive-integrator.h) composes the linear MNA solve and advances node
// voltages + cross-sample history each step. This suite integrates reactive
// networks under BackwardEuler and compares the observed per-step state against
// the EXACT discrete backward-Euler response computed independently in the test.
// It covers four cases: an RC low-pass, its RL dual, a series-RLC network (an
// independent 2x2 v_C/i_L oracle), and a zero-reactive-element passthrough (S9).
//
// Case 1 -- RC low-pass (capacitor node voltage, contract "Read accessors"
// via MnaSystem::nodeVoltage):
//   VoltageSource{vinNode, gnd, Vin} -- Resistor{vinNode, capNode, R} --
//   Capacitor{capNode, gnd, C}. Zero initial history (integrator freshly
//   plan()ed). After each step(), sys.nodeVoltage(capNode) must match
//     v[n] = v[n-1] + (dt / (R*C + dt)) * (Vin - v[n-1]),   v[0] = 0
//   (pre-step history), monotonically increasing toward the DC steady state
//   Vin (the capacitor is an open circuit at DC).
//
// Case 2 -- RL series (inductor current, the dual; read via
// ReactiveIntegrator::iPrev(reactiveSlot) -- the inductor is Norton-stamped,
// not a branch unknown, so its current is the reconstructed
// i^n = Geq*v^n - Ieq that step() stores into history, and reading iPrev()
// after the step is the correct observation point):
//   VoltageSource{vinNode, gnd, Vin} -- Resistor{vinNode, midNode, R} --
//   Inductor{midNode, gnd, L}. Zero initial history. After each step(),
//   integrator.iPrev(0) (the sole reactive element's slot) must match
//     i[n] = i[n-1] + (dt / (L + R*dt)) * (Vin - R*i[n-1]),   i[0] = 0
//   monotonically increasing toward the DC steady state Vin/R (the inductor
//   is a short circuit at DC).
//
// Values: R = 1e3 ohm, C = 1e-6 F (RC = 1e-3 s), L = 1.0 H (L/R = 1e-3 s) --
// matched time constants so both cases resolve their transient over the same
// dt = 1e-5 s (100 samples/time-constant) and kSteps = 500 (5 time constants,
// ~99.3% of the DC step) visibly approaches steady state. Tolerance starts
// tight (1e-12): a single-reactive-element network is a 1x1 (case 1) or
// 2-unknown-with-branch (case 2) linear solve per step, so a correctly
// implemented step() should track the independently-computed exact recursion
// to within a few ULPs of double precision, not accumulate drift.

using acfx::Capacitor;
using acfx::Inductor;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::integration::BackwardEuler;
using acfx::integration::ReactiveIntegrator;
using acfx::integration::StepResult;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;

namespace {

constexpr double kVin = 5.0;
constexpr double kR = 1.0e3;
constexpr double kC = 1.0e-6;
constexpr double kL = 1.0;
constexpr double kDt = 1.0e-5;
constexpr int kSteps = 500;
constexpr double kTol = 1.0e-12;

}  // namespace

TEST_SUITE("integration-closed-form") {

// ---------------------------------------------------------------------------
// Case 1: RC low-pass step response, capacitor node voltage.
// ---------------------------------------------------------------------------
TEST_CASE("integration-closed-form: RC low-pass capacitor voltage matches the exact backward-Euler recursion") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, capNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, capNode, kR});
    nl.add(Capacitor{capNode, kGround, kC});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);

    double vPrevExact = 0.0;  // v[0], the pre-step history
    double vPrevObserved = 0.0;
    const double alpha = kDt / (kR * kC + kDt);

    for (int n = 0; n < kSteps; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        CHECK(result.converged);

        const double vExact = vPrevExact + alpha * (kVin - vPrevExact);
        const double vObserved = sys.nodeVoltage(capNode);

        CHECK(vObserved == doctest::Approx(vExact).epsilon(kTol));
        CHECK(vObserved > vPrevObserved);  // monotonically increasing
        CHECK(vObserved < kVin);           // approaches, never overshoots, Vin

        vPrevExact = vExact;
        vPrevObserved = vObserved;
    }

    // After 5 time constants (kSteps * kDt == 5 * R*C), the response should
    // have visibly approached the DC steady state Vin (cap -> open at DC).
    CHECK(vPrevObserved == doctest::Approx(kVin).epsilon(1.0e-2));
}

// ---------------------------------------------------------------------------
// Case 2: RL series step response, inductor current (the dual).
// ---------------------------------------------------------------------------
TEST_CASE("integration-closed-form: RL series inductor current matches the exact backward-Euler recursion") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, midNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Inductor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown
    constexpr int kReactiveSlot = 0;  // the sole reactive element (Inductor)

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId midNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, midNode, kR});
    nl.add(Inductor{midNode, kGround, kL});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);

    CHECK(integrator.reactiveCount() == 1);

    const double iSteadyState = kVin / kR;
    double iPrevExact = 0.0;  // i[0], the pre-step history
    double iPrevObserved = 0.0;
    const double alpha = kDt / (kL + kR * kDt);

    for (int n = 0; n < kSteps; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        CHECK(result.converged);

        const double iExact = iPrevExact + alpha * (kVin - kR * iPrevExact);
        const double iObserved = integrator.iPrev(kReactiveSlot);

        CHECK(iObserved == doctest::Approx(iExact).epsilon(kTol));
        CHECK(iObserved > iPrevObserved);       // monotonically increasing
        CHECK(iObserved < iSteadyState);        // approaches, never overshoots

        iPrevExact = iExact;
        iPrevObserved = iObserved;
    }

    // After 5 time constants (kSteps * kDt == 5 * L/R), the response should
    // have visibly approached the DC steady state Vin/R (inductor -> short).
    CHECK(iPrevObserved == doctest::Approx(iSteadyState).epsilon(1.0e-2));
}

// ---------------------------------------------------------------------------
// Case 3 (T008/S1/SC-001): series RLC step response, an independent
// hand-derived backward-Euler oracle for a SECOND-ORDER reactive network (both
// an inductor AND a capacitor, no nonlinear element).
// ---------------------------------------------------------------------------
TEST_CASE("integration-closed-form: series RLC network matches an independent backward-Euler oracle") {
    constexpr int kMaxNodes = 4;      // ground, vinNode, n1 (R-L junction), n2 (L-C junction / cap node)
    constexpr int kMaxComponents = 4; // VoltageSource, Resistor, Inductor, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    constexpr double kVinRlc = 5.0;
    constexpr double kRRlc = 100.0;    // ohm
    constexpr double kLRlc = 1.0e-3;   // H
    constexpr double kCRlc = 1.0e-6;   // F
    constexpr double kDtRlc = 1.0e-7;  // s
    constexpr int kStepsRlc = 6000;    // 600us, several slow-pole time constants
    constexpr double kTolRlc = 1.0e-9;

    // VoltageSource{vinNode, gnd, Vin} -- Resistor{vinNode, n1, R} --
    // Inductor{n1, n2, L} -- Capacitor{n2, gnd, C}.
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVinRlc});
    nl.add(Resistor{vinNode, n1, kRRlc});
    nl.add(Inductor{n1, n2, kLRlc});
    nl.add(Capacitor{n2, kGround, kCRlc});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDtRlc);

    integrator.plan(nl, assembler, sys);

    CHECK(integrator.reactiveCount() == 2);
    // Reactive slots are recorded in netlist add order: Inductor (component
    // index 2) before Capacitor (component index 3).
    constexpr int kInductorSlot = 0;
    CHECK(integrator.reactiveComponentIndex(0) == 2);
    CHECK(integrator.reactiveComponentIndex(1) == 3);

    // Independent oracle: a hand-derived 2x2 backward-Euler nodal solve for
    // THIS topology, advanced in lockstep with the integrator and NEVER
    // calling into ReactiveIntegrator. Unknowns are v1 = V(n1), v2 = V(n2)
    // (== the capacitor voltage, since the cap's low side is ground).
    //
    // Backward-Euler companions (dual to Capacitor::companion /
    // Inductor::companion, re-derived independently here rather than reused):
    //   Geq_L = dt/L, Ieq_L = -iL_prev   (inductor, current-history term)
    //   Geq_C = C/dt, Ieq_C = Geq_C*vC_prev (capacitor, voltage-history term)
    // KCL at n1 (resistor current in from vin, inductor current out to n2):
    //   (Vin - v1)/R = Geq_L*(v1 - v2) - Ieq_L
    //   =>  (GR+GL)*v1 + (-GL)*v2 = GR*Vin + Ieq_L            ... (Eq 1)
    // KCL at n2 (inductor current in from n1, capacitor current out to gnd):
    //   Geq_L*(v1 - v2) - Ieq_L = Geq_C*v2 - Ieq_C
    //   =>  GL*v1 + (-(GL+GC))*v2 = Ieq_L - Ieq_C              ... (Eq 2)
    // Solved each step by Cramer's rule; the reconstructed inductor current is
    // i_L[n] = i_L[n-1] + Geq_L*(v1[n] - v2[n]) (backward-Euler L*di/dt ~=
    // L*(iN-iPrev)/dt = vN), the same reconstruction ReactiveIntegrator::step()
    // performs from its stamped companion.
    const double GR = 1.0 / kRRlc;
    const double GL = kDtRlc / kLRlc;
    const double GC = kCRlc / kDtRlc;

    double vCPrevExact = 0.0;  // v_C[0] == v2[0], the pre-step history
    double iLPrevExact = 0.0;  // i_L[0], the pre-step history
    double vCObservedLast = 0.0;  // last OBSERVED cap voltage (from the solver)

    for (int n = 0; n < kStepsRlc; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        CHECK(result.converged);

        const double IeqL = -iLPrevExact;
        const double IeqC = GC * vCPrevExact;

        const double a11 = GR + GL;
        const double a12 = -GL;
        const double b1 = GR * kVinRlc + IeqL;
        const double a21 = GL;
        const double a22 = -(GL + GC);
        const double b2 = IeqL - IeqC;

        const double det = a11 * a22 - a12 * a21;
        const double v1Exact = (b1 * a22 - a12 * b2) / det;
        const double v2Exact = (a11 * b2 - a21 * b1) / det;
        const double iLExact = iLPrevExact + GL * (v1Exact - v2Exact);

        const double vCObserved = sys.nodeVoltage(n2);
        const double iLObserved = integrator.iPrev(kInductorSlot);

        CHECK(vCObserved == doctest::Approx(v2Exact).epsilon(kTolRlc));
        CHECK(iLObserved == doctest::Approx(iLExact).epsilon(kTolRlc));

        vCPrevExact = v2Exact;
        iLPrevExact = iLExact;
        vCObservedLast = vCObserved;
    }

    // This RLC is heavily overdamped (zeta ~= 1.58); after kStepsRlc * kDtRlc
    // == 600us (several slow-pole time constants), the OBSERVED capacitor voltage
    // (from the solver, not the oracle) should have visibly approached the DC
    // steady state Vin (inductor -> short, capacitor -> open at DC).
    CHECK(vCObservedLast == doctest::Approx(kVinRlc).epsilon(1.0e-2));
}

// ---------------------------------------------------------------------------
// Case 4 (T008/S9): zero reactive elements is a clean, stable no-op
// passthrough -- not an error.
// ---------------------------------------------------------------------------
TEST_CASE("integration-closed-form: zero reactive elements is a stable no-op passthrough") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, midNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Resistor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    constexpr double kVinDiv = 9.0;
    constexpr double kR1 = 2.0e3;
    constexpr double kR2 = 1.0e3;

    // VoltageSource{vinNode, gnd, Vin} -- Resistor{vinNode, midNode, R1} --
    // Resistor{midNode, gnd, R2}: a purely resistive divider, no capacitor,
    // inductor, or diode anywhere in the netlist.
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId midNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVinDiv});
    nl.add(Resistor{vinNode, midNode, kR1});
    nl.add(Resistor{midNode, kGround, kR2});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);  // dt is irrelevant: no reactive element ever reads it

    integrator.plan(nl, assembler, sys);

    CHECK(integrator.reactiveCount() == 0);

    const double vExpected = kVinDiv * kR2 / (kR1 + kR2);

    for (int n = 0; n < 5; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);

        // Clean success, not an error: converged, one linear solve, zero
        // residual (the linear branch always reports {true, 1, 0.0} on
        // convergence).
        CHECK(result.converged);
        CHECK(result.iterations == 1);
        CHECK(result.voltageResidual == 0.0);

        // Stepping repeatedly is a stable no-op passthrough: the divider
        // voltage is identical every step (no reactive element to evolve it).
        CHECK(sys.nodeVoltage(midNode) == doctest::Approx(vExpected).epsilon(kTol));

        // No reactive element exists, so plan()'s reset() zeroed history and
        // step()'s S1/S3/S4 history loop (bounded by reactiveCount_ == 0)
        // never executes -- the history slots stay untouched at zero.
        CHECK(integrator.vPrev(0) == 0.0);
        CHECK(integrator.iPrev(0) == 0.0);
    }
}

}  // TEST_SUITE("integration-closed-form")

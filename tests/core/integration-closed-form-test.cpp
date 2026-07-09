#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <cmath>

// T006 -- ReactiveIntegrator closed-form doctest suite (RED for T007).
// (specs/implicit-integration/contracts/reactive-integrator.md;
// specs/implicit-integration/data-model.md; tasks.md T006).
//
// ReactiveIntegrator::step() (core/primitives/circuit/integration/
// reactive-integrator.h) is currently a PLACEHOLDER: it returns
// StepResult{converged=false, ...} and never advances node voltages or
// cross-sample history. This suite integrates two first-order reactive
// networks under BackwardEuler and compares the observed per-step state
// against the EXACT discrete backward-Euler response computed independently
// in the test. It is expected to FAIL (RED) until T007 lands the real
// composed linear solve; it must still BUILD cleanly against the existing
// ReactiveIntegrator/MnaAssembler/MnaSystem/NewtonSolver/Netlist surface.
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

}  // TEST_SUITE("integration-closed-form")

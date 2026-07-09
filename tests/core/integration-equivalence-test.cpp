#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include "labs/component-abstractions/solver/linear-solver.h"
#include "labs/diode-clippers/solver/transient-clipper.h"

#include <algorithm>
#include <cmath>

// integration-equivalence-test.cpp -- T021/US8, SC-003, FR-024: the
// implicit-integration lab-equivalence oracle.
//
// PROVES that the production primitive acfx::integration::ReactiveIntegrator
// (rule = BackwardEuler) reproduces the TRUSTED lab backward-Euler solvers
// SAMPLE-BY-SAMPLE across a transient, on shared topologies -- not merely at a
// settled DC operating point (that narrower claim already belongs to
// newton-equivalence-test.cpp / mna-equivalence-test.cpp). Both the primitive
// and the labs hardcode the SAME backward-Euler discretization, so on an
// IDENTICAL netlist, driven with the SAME dt, they must trace the SAME
// trajectory to tight numerical tolerance.
//
// Case (a) -- LINEAR: an RC low-pass (VoltageSource -- Resistor -- Capacitor
// to ground), stepped in lockstep by the primitive
// (ReactiveIntegrator<BackwardEuler,...> composed with MnaAssembler/MnaSystem)
// and by the lab acfx::labs::component_abstractions::LinearSolver, both
// against the SAME Netlist instance so node ids and forcing coincide exactly.
// Node voltages are compared at every sample.
//
// Case (b) -- REACTIVE+DIODE: the same first-order reactive diode clipper
// integration-composition-test.cpp Case (b) uses (VoltageSource -- Resistor
// -- node n2, with a Diode{n2, gnd} AND a Capacitor{n2, gnd} both across n2),
// stepped in lockstep by the primitive (ReactiveIntegrator composed with
// NewtonSolver, via newton.plan()) and by the lab
// acfx::labs::diode_clippers::TransientClipper, again against the SAME
// Netlist instance. Node voltages are compared at every sample across the
// full transient (not only the settled tail).
//
// Lab-include confinement (FR-024): this is a place tests/core is allowed a
// core/labs/ include, precisely to hold the primitive to the labs' answer
// (see newton-equivalence-test.cpp / mna-equivalence-test.cpp for the same
// pattern) -- reactive-integrator.h itself includes nothing from core/labs/.

using acfx::Capacitor;
using acfx::Diode;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::integration::BackwardEuler;
using acfx::integration::ReactiveIntegrator;
using acfx::integration::StepResult;
using acfx::labs::component_abstractions::LinearSolver;
using acfx::labs::diode_clippers::TransientClipper;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;

TEST_SUITE("integration-equivalence") {

// ---------------------------------------------------------------------------
// Case (a): LINEAR RC network -- primitive ReactiveIntegrator<BackwardEuler>
// vs lab LinearSolver, sample-by-sample over the transient.
// ---------------------------------------------------------------------------
TEST_CASE("integration-equivalence: linear RC network matches LinearSolver sample-by-sample under BackwardEuler") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, capNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    constexpr double kVin = 5.0;
    constexpr double kR = 1.0e3;
    constexpr double kC = 1.0e-6;
    constexpr double kDt = 1.0e-5;
    constexpr int kSteps = 500;
    constexpr double kTol = 1.0e-9;

    // ONE netlist, shared by both solvers, so node ids and forcing coincide
    // exactly -- the comparison isolates the two solve engines, not topology
    // transcription drift.
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, capNode, kR});
    nl.add(Capacitor{capNode, kGround, kC});
    nl.prepare();

    // --- PRIMITIVE: ReactiveIntegrator<BackwardEuler> + MnaAssembler/MnaSystem ---
    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    // No nonlinear element in this topology, so `newton` is never driven --
    // it is only present because step()'s signature always takes one.
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    CHECK_FALSE(integrator.hasNonlinear());

    // --- LAB: acfx::labs::component_abstractions::LinearSolver ---
    LinearSolver<kMaxNodes, kMaxComponents> labSolver;

    double maxDiff = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const StepResult primResult = integrator.step(nl, assembler, sys, newton);
        REQUIRE(primResult.converged);
        CHECK_NOTHROW(labSolver.solve(nl, kDt));

        const double vPrim = sys.nodeVoltage(capNode);
        const double vLab = labSolver.voltage(capNode);
        const double diff = std::fabs(vPrim - vLab);
        maxDiff = std::max(maxDiff, diff);

        CAPTURE(i);
        CAPTURE(vPrim);
        CAPTURE(vLab);
        CHECK(diff < kTol);
    }

    // Surface the actual observed agreement (the key number for the report).
    MESSAGE("integration-equivalence: linear RC max |primitive - LinearSolver| over ",
            kSteps, " steps = ", maxDiff);
    CHECK(maxDiff < kTol);

    // Sanity: both solvers should have visibly approached the DC steady
    // state Vin after many time constants (the capacitor is an open circuit
    // at DC) -- guards against a degenerate all-zero pass.
    CHECK(sys.nodeVoltage(capNode) == doctest::Approx(kVin).epsilon(1.0e-2));
}

// ---------------------------------------------------------------------------
// Case (b): REACTIVE+DIODE network (first-order reactive diode clipper) --
// primitive ReactiveIntegrator<BackwardEuler>+NewtonSolver vs lab
// TransientClipper, sample-by-sample over the transient.
// ---------------------------------------------------------------------------
TEST_CASE("integration-equivalence: reactive+diode network matches TransientClipper sample-by-sample under BackwardEuler") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, n2
    constexpr int kMaxComponents = 4; // VoltageSource, Resistor, Diode, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    constexpr double kVin = 5.0;
    constexpr double kR = 1.0e3;
    constexpr double kC = 1.0e-6;
    constexpr double kIs = 1.0e-14;  // reverse saturation current (A)
    constexpr double kN = 1.0;       // ideality factor
    constexpr double kVt = 0.02585;  // thermal voltage (V), ~300 K
    constexpr double kDt = 1.0e-5;
    constexpr int kSteps = 5000;     // 50 ms: many RC time constants, DC settle
    constexpr double kTol = 1.0e-7;

    // ONE netlist, shared by both solvers (same shape as
    // integration-composition-test.cpp Case (b)): VoltageSource{vinNode, gnd,
    // Vin} -- Resistor{vinNode, n2, R} -- n2, with BOTH a Diode{n2, gnd, ...}
    // and a Capacitor{n2, gnd, C} across n2.
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, n2, kR});
    nl.add(Diode{n2, kGround, kIs, kN, kVt});
    nl.add(Capacitor{n2, kGround, kC});
    nl.prepare();

    // --- PRIMITIVE: ReactiveIntegrator<BackwardEuler> + NewtonSolver ---
    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    // For a nonlinear netlist the caller plans the NewtonSolver too; step()
    // composes it as the reactive companions' outer loop (same pattern as
    // integration-composition-test.cpp Case (b)).
    newton.plan(nl, assembler, sys);
    CHECK(integrator.hasNonlinear());
    CHECK(integrator.reactiveCount() == 1);

    // --- LAB: acfx::labs::diode_clippers::TransientClipper ---
    TransientClipper<kMaxNodes, kMaxComponents> labSolver;
    labSolver.reset();

    double maxDiff = 0.0;
    for (int i = 0; i < kSteps; ++i) {
        const StepResult primResult = integrator.step(nl, assembler, sys, newton);
        const auto labStatus = labSolver.step(nl, kDt);

        REQUIRE(primResult.converged);
        REQUIRE(labStatus.converged);

        const double vPrim = sys.nodeVoltage(n2);
        const double vLab = labSolver.voltage(n2);
        const double diff = std::fabs(vPrim - vLab);
        maxDiff = std::max(maxDiff, diff);

        CAPTURE(i);
        CAPTURE(vPrim);
        CAPTURE(vLab);
        CHECK(diff < kTol);
    }

    // Surface the actual observed agreement (the key number for the report).
    MESSAGE("integration-equivalence: reactive+diode max |primitive - TransientClipper| over ",
            kSteps, " steps = ", maxDiff);
    CHECK(maxDiff < kTol);
}

}  // TEST_SUITE("integration-equivalence")

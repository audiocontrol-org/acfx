#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

// ReactiveIntegrator plan-drift + accessor guard suite (split from
// integration-integrator-test.cpp to stay under the Constitution VII
// per-file line budget; specs/implicit-integration/contracts/reactive-
// integrator.md; code review: the plan-drift guard was strengthened to a
// COMPLETE per-component topology signature -- kind + all terminal node ids,
// via componentTopoSig<MaxNodes>() in topology-signature.h -- matching
// NewtonSolver's plan-drift guard, so step() now rejects ANY topology drift,
// not just a component-COUNT change).
//
// Coverage:
//   1. Slot-accessor sentinel -- the slot-indexed accessors guard on
//      reactiveCount_, not MaxComponents, so an over-range slot returns the
//      -1 / 0.0 "no such slot" sentinel.
//   2. step()/plan() topology-drift guard, by value, not advancing history:
//      (a) a component-COUNT drift (the original regression),
//      (b) a same-count KIND swap at one index (non-reactive component
//          changes kind, same terminal nodes),
//      (c) a same-count DIODE-MOVED-TO-A-DIFFERENT-SLOT drift (diode and
//          voltage source swap slots),
//      (d) a same-count, same-KIND TERMINAL REWIRE (a resistor's second
//          terminal moves to a different existing node).
// Every drifted netlist below is a VALID, prepare()-able circuit (ground-
// referenced, no floating node) -- the guard must catch a valid-but-DIFFERENT
// topology, not merely reject a malformed netlist.

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
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;

TEST_SUITE("reactive-integrator") {

// ---------------------------------------------------------------------------
// Slot-accessor sentinel: the slot-indexed accessors guard on reactiveCount_,
// not MaxComponents, so an over-range slot returns the -1 / 0.0 "no such
// slot" sentinel and cannot be confused with a real reactive element at
// component index 0 (the reactive element is placed at component index 0
// here -- exactly the case a MaxComponents-bounded guard would misreport as
// component 0).
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: slot accessors return the no-such-slot sentinel past reactiveCount_") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();
    nl.add(Capacitor{capNode, kGround, 1.0e-6});   // index 0 -- reactive, slot 0
    nl.add(Resistor{vinNode, capNode, 1000.0});    // index 1
    nl.add(VoltageSource{vinNode, kGround, 5.0});  // index 2
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);
    integrator.plan(nl, assembler, sys);

    REQUIRE(integrator.reactiveCount() == 1);
    // Valid slot 0 -> the real component index (0). Over-range slots (>=
    // reactiveCount_) -> the -1 sentinel, NOT a spurious component 0.
    CHECK(integrator.reactiveComponentIndex(0) == 0);
    CHECK(integrator.reactiveComponentIndex(1) == -1);
    CHECK(integrator.reactiveComponentIndex(kMaxComponents - 1) == -1);
    CHECK(integrator.reactiveComponentIndex(-1) == -1);
    // Valid-slot history reads (zero seed); over-range slots read 0 without
    // indexing past reactiveCount_.
    CHECK(integrator.vPrev(0) == doctest::Approx(0.0));
    CHECK(integrator.iPrev(0) == doctest::Approx(0.0));
    CHECK(integrator.vPrev(2) == doctest::Approx(0.0));
    CHECK(integrator.iPrev(2) == doctest::Approx(0.0));
}

// ---------------------------------------------------------------------------
// Topology guard (a): component-COUNT drift. step() indexes the netlist by
// the component indices plan() recorded. A netlist whose topology drifted
// from the plan (here a different component count) must be surfaced BY VALUE
// (the all-zero StepResult) rather than indexing the stale plan into
// out-of-bounds access, and must not advance history.
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: step() surfaces a plan/netlist topology drift by value") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;

    Netlist<kMaxNodes, kMaxComponents> planned;
    const NodeId vinNode = planned.addNode();
    const NodeId capNode = planned.addNode();
    planned.add(Resistor{vinNode, capNode, 1000.0});    // index 0
    planned.add(Capacitor{capNode, kGround, 1.0e-6});   // index 1 -- reactive
    planned.add(VoltageSource{vinNode, kGround, 5.0});  // index 2
    planned.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);
    integrator.plan(planned, assembler, sys);

    // A netlist with a DIFFERENT component count (2, not the planned 3): the
    // recorded reactive index (1) no longer describes it.
    Netlist<kMaxNodes, kMaxComponents> drifted;
    const NodeId vin2 = drifted.addNode();
    const NodeId cap2 = drifted.addNode();
    drifted.add(Resistor{vin2, cap2, 1000.0});      // index 0
    drifted.add(VoltageSource{vin2, kGround, 5.0}); // index 1 -- only 2 components
    drifted.prepare();

    const double iPrevBefore = integrator.iPrev(0);
    StepResult result{true, 9, 9.0};
    CHECK_NOTHROW(result = integrator.step(drifted, assembler, sys, newton));
    CHECK_FALSE(result.converged);
    CHECK(result.iterations == 0);
    CHECK(result.voltageResidual == doctest::Approx(0.0));
    CHECK(integrator.iPrev(0) == doctest::Approx(iPrevBefore));
}

// ---------------------------------------------------------------------------
// Topology guard (b): same-count KIND swap. index 0 is a Resistor{n1,n2} in
// the plan; the drifted netlist has the SAME component count (3) and index 0
// is still a two-terminal element on the SAME node pair {n1,n2}, but its KIND
// changed to VoltageSource. A count-only or reactive-slot-only guard would
// miss this; the full per-component topology signature (kind folded in)
// catches it at index 0.
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: step() rejects a same-count kind swap at a non-reactive index (drift regression a)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;

    Netlist<kMaxNodes, kMaxComponents> planned;
    const NodeId n1 = planned.addNode();
    const NodeId n2 = planned.addNode();
    planned.add(Resistor{n1, n2, 1000.0});         // index 0
    planned.add(Capacitor{n2, kGround, 1.0e-6});   // index 1 -- reactive
    planned.add(VoltageSource{n1, kGround, 5.0});  // index 2
    planned.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);
    integrator.plan(planned, assembler, sys);
    REQUIRE_FALSE(integrator.hasNonlinear());

    // Same component COUNT (3) and index 0 still spans the same node pair
    // {1, 2}, but its KIND changed from Resistor to VoltageSource -- the
    // per-component topology signature at index 0 differs (the kind
    // discriminant is folded into the signature).
    Netlist<kMaxNodes, kMaxComponents> drifted;
    const NodeId d1 = drifted.addNode();  // id 1, same as n1
    const NodeId d2 = drifted.addNode();  // id 2, same as n2
    drifted.add(VoltageSource{d1, d2, 5.0});        // index 0 -- was Resistor
    drifted.add(Capacitor{d2, kGround, 1.0e-6});    // index 1
    drifted.add(VoltageSource{d1, kGround, 5.0});   // index 2
    drifted.prepare();

    const double iPrevBefore = integrator.iPrev(0);
    StepResult result{true, 9, 9.0};
    CHECK_NOTHROW(result = integrator.step(drifted, assembler, sys, newton));
    CHECK_FALSE(result.converged);
    CHECK(result.iterations == 0);
    CHECK(result.voltageResidual == doctest::Approx(0.0));
    CHECK(integrator.iPrev(0) == doctest::Approx(iPrevBefore));
}

// ---------------------------------------------------------------------------
// Topology guard (c): same-count DIODE MOVED TO A DIFFERENT SLOT. The plan is
// [Resistor@0, Capacitor@1, Diode@2, VoltageSource@3] (a reactive+diode
// netlist -- hasNonlinear() is true). The drifted netlist has the SAME
// component count (4) and the SAME set of kinds, but the Diode and
// VoltageSource have swapped slots (2 <-> 3): the topology signatures at
// indices 2 and 3 both differ, even though components 0 and 1 are unchanged.
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: step() rejects a same-count diode moved to a different slot (drift regression b)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;
    constexpr double kIs = 1.0e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> planned;
    const NodeId vinNode = planned.addNode();
    const NodeId node = planned.addNode();
    planned.add(Resistor{vinNode, node, 1000.0});      // index 0
    planned.add(Capacitor{node, kGround, 1.0e-6});     // index 1 -- reactive
    planned.add(Diode{node, kGround, kIs, kN, kVt});   // index 2 -- nonlinear
    planned.add(VoltageSource{vinNode, kGround, 5.0}); // index 3
    planned.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);
    integrator.plan(planned, assembler, sys);
    newton.plan(planned, assembler, sys);
    REQUIRE(integrator.hasNonlinear());

    // Same component COUNT (4) and the SAME kinds present overall, but the
    // Diode and VoltageSource have swapped slots (2 <-> 3).
    Netlist<kMaxNodes, kMaxComponents> drifted;
    const NodeId dv = drifted.addNode();  // id 1, same as vinNode
    const NodeId dn = drifted.addNode();  // id 2, same as node
    drifted.add(Resistor{dv, dn, 1000.0});             // index 0 -- unchanged
    drifted.add(Capacitor{dn, kGround, 1.0e-6});       // index 1 -- unchanged
    drifted.add(VoltageSource{dv, kGround, 5.0});      // index 2 -- was Diode
    drifted.add(Diode{dn, kGround, kIs, kN, kVt});     // index 3 -- was VoltageSource
    drifted.prepare();

    const double iPrevBefore = integrator.iPrev(0);
    StepResult result{true, 9, 9.0};
    CHECK_NOTHROW(result = integrator.step(drifted, assembler, sys, newton));
    CHECK_FALSE(result.converged);
    CHECK(result.iterations == 0);
    CHECK(result.voltageResidual == doctest::Approx(0.0));
    CHECK(integrator.iPrev(0) == doctest::Approx(iPrevBefore));
}

// ---------------------------------------------------------------------------
// Topology guard (d): same-kind, same-count TERMINAL REWIRE. index 0 is a
// Resistor{n1,n2} in the plan; the drifted netlist keeps the SAME component
// count (3) and the SAME Resistor kind at index 0, but its second terminal
// moves from n2 to a different existing node n3 -- the terminal-node fold in
// the topology signature changes even though the kind discriminant does not.
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: step() rejects a same-kind terminal rewire (drift regression c)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;

    Netlist<kMaxNodes, kMaxComponents> planned;
    const NodeId n1 = planned.addNode();
    const NodeId n2 = planned.addNode();
    planned.add(Resistor{n1, n2, 1000.0});         // index 0
    planned.add(Capacitor{n2, kGround, 1.0e-6});   // index 1 -- reactive
    planned.add(VoltageSource{n1, kGround, 5.0});  // index 2
    planned.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);
    integrator.plan(planned, assembler, sys);
    REQUIRE_FALSE(integrator.hasNonlinear());

    // Same component COUNT (3) and the SAME Resistor kind at index 0, but its
    // second terminal is rewired from n2 to a different, existing node n3 (a
    // conductive path to ground exists through the resistor -> voltage
    // source union, so this is a VALID, prepare()-able netlist, not a
    // malformed one).
    Netlist<kMaxNodes, kMaxComponents> drifted;
    const NodeId d1 = drifted.addNode();  // id 1, same as n1
    const NodeId d2 = drifted.addNode();  // id 2, same as n2
    const NodeId d3 = drifted.addNode();  // id 3 -- a third, distinct node
    drifted.add(Resistor{d1, d3, 1000.0});         // index 0 -- rewired to d3
    drifted.add(Capacitor{d2, kGround, 1.0e-6});   // index 1
    drifted.add(VoltageSource{d1, kGround, 5.0});  // index 2
    drifted.prepare();

    const double iPrevBefore = integrator.iPrev(0);
    StepResult result{true, 9, 9.0};
    CHECK_NOTHROW(result = integrator.step(drifted, assembler, sys, newton));
    CHECK_FALSE(result.converged);
    CHECK(result.iterations == 0);
    CHECK(result.voltageResidual == doctest::Approx(0.0));
    CHECK(integrator.iPrev(0) == doctest::Approx(iPrevBefore));
}

}  // TEST_SUITE("reactive-integrator")

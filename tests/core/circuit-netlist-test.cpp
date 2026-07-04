#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <type_traits>
#include <variant>

#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "support/allocation-sentinel.h"

// US2: CircuitNetlist assembly + prepare() topology validation + the
// post-prepare no-allocation invariant (spec.md US2 acceptance 1-4;
// SC-005/SC-006; contracts/netlist.md).
//
// prepare() is the ONLY place that validates a Netlist (T013, netlist.h):
// it distinguishes three failure modes as three distinct, descriptive
// exceptions - missing ground reference, floating node, and (at add()/
// addNode() time) capacity overflow. This suite asserts each is raised
// independently and that the happy path (a validated voltage divider)
// reports the expected topology counts.

using acfx::Capacitor;
using acfx::Component;
using acfx::CurrentSource;
using acfx::Diode;
using acfx::Inductor;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::test::AllocationSentinel;

// ---------------------------------------------------------------------------
// US2.1 - a well-formed voltage divider validates and reports the expected
// topology counts.
// ---------------------------------------------------------------------------

TEST_CASE("CircuitNetlist - a grounded voltage divider validates and counts correctly") {
    Netlist<8, 8> nl;

    // Vin --VoltageSource-- node1 --R1-- node2 --R2-- ground
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{node1, acfx::kGround, 9.0});
    nl.add(Resistor{node1, node2, 1000.0});
    nl.add(Resistor{node2, acfx::kGround, 1000.0});

    CHECK_NOTHROW(nl.prepare());

    CHECK(nl.nodeCount() == 3);       // ground + node1 + node2
    CHECK(nl.componentCount() == 3);  // VoltageSource + 2 Resistors

    const auto view = nl.components();
    CHECK(view.size() == static_cast<std::size_t>(nl.componentCount()));
}

// ---------------------------------------------------------------------------
// US2.2 - a floating node (no conductive path to ground) is rejected, and the
// exception message names the offending node.
// ---------------------------------------------------------------------------

TEST_CASE("CircuitNetlist - a node reachable only through a CurrentSource is floating") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    // node1 is properly grounded through an ideal VoltageSource (conductive
    // per contributesConductivePath), so the missing-ground check passes.
    nl.add(VoltageSource{node1, acfx::kGround, 9.0});
    // node2's ONLY link to ground is a CurrentSource, which is deliberately
    // excluded from the conductive-path check (netlist.h
    // contributesConductivePath) - it does not guarantee a DC path.
    nl.add(CurrentSource{node2, acfx::kGround, 0.001});

    CHECK_THROWS_AS(nl.prepare(), std::invalid_argument);
    CHECK_THROWS_WITH_AS(nl.prepare(), doctest::Contains("floating node 2"),
                          std::invalid_argument);
}

TEST_CASE("CircuitNetlist - a resistor island disconnected from ground is floating") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();
    const NodeId node3 = nl.addNode();

    // node1 is grounded, satisfying the missing-ground check.
    nl.add(VoltageSource{node1, acfx::kGround, 9.0});
    // node2 <-> node3 form a conductive island (two resistors in series) that
    // never touches ground.
    nl.add(Resistor{node2, node3, 1000.0});
    nl.add(Resistor{node3, node2, 2200.0});

    CHECK_THROWS_AS(nl.prepare(), std::invalid_argument);
    CHECK_THROWS_WITH_AS(nl.prepare(), doctest::Contains("floating node"),
                          std::invalid_argument);
}

// ---------------------------------------------------------------------------
// US2.3 - a netlist whose components never touch ground is rejected with a
// message DISTINCT from the floating-node message.
// ---------------------------------------------------------------------------

TEST_CASE("CircuitNetlist - no component touching ground is a missing-ground error, distinct from floating-node") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    // Neither component ever references node 0 (ground).
    nl.add(Resistor{node1, node2, 1000.0});
    nl.add(VoltageSource{node1, node2, 9.0});

    CHECK_THROWS_AS(nl.prepare(), std::invalid_argument);
    CHECK_THROWS_WITH_AS(nl.prepare(), doctest::Contains("missing ground"),
                          std::invalid_argument);

    // The two failure modes must never collide on the same wording.
    try {
        nl.prepare();
        FAIL("prepare() was expected to throw");
    } catch (const std::invalid_argument& e) {
        const std::string what = e.what();
        CHECK(what.find("missing ground") != std::string::npos);
        CHECK(what.find("floating node") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// US2.4 - over-capacity add()/addNode() throws std::out_of_range and leaves
// no partial/truncated state behind.
// ---------------------------------------------------------------------------

TEST_CASE("CircuitNetlist - addNode() beyond MaxNodes throws out_of_range without mutating state") {
    Netlist<2, 2> nl;  // ground + exactly one addressable node

    const NodeId node1 = nl.addNode();
    CHECK(node1 == 1);
    CHECK(nl.nodeCount() == 2);

    CHECK_THROWS_AS(nl.addNode(), std::out_of_range);
    // State must be unchanged after the failed call - no partial mutation.
    CHECK(nl.nodeCount() == 2);
}

TEST_CASE("CircuitNetlist - add() beyond MaxComponents throws out_of_range without mutating state") {
    Netlist<2, 2> nl;  // capacity for exactly two components

    const NodeId node1 = nl.addNode();
    nl.add(Resistor{acfx::kGround, node1, 1000.0});
    nl.add(Resistor{acfx::kGround, node1, 2000.0});
    CHECK(nl.componentCount() == 2);

    CHECK_THROWS_AS(nl.add(Resistor{acfx::kGround, node1, 3000.0}), std::out_of_range);
    // State must be unchanged after the failed call - no truncated 3rd entry.
    CHECK(nl.componentCount() == 2);
    CHECK(nl.components().size() == 2);
}

// ---------------------------------------------------------------------------
// Regression (cross-model audit HIGH): prepare() MUST reject a component that
// references a node id outside [0, nodeCount()). Two channels:
//   (a) terminal in [nodeCount(), MaxNodes) — would otherwise read a
//       value-initialized union-find slot (== 0) and SILENTLY unite with
//       ground, letting a malformed/floating netlist pass validation;
//   (b) terminal >= MaxNodes — would otherwise be an out-of-bounds array read.
// Both must throw a descriptive std::invalid_argument, distinct from the
// missing-ground / floating-node / over-capacity errors.
// ---------------------------------------------------------------------------
TEST_CASE("CircuitNetlist - a terminal at-or-above nodeCount but below MaxNodes is rejected (not silently grounded)") {
    Netlist<8, 8> nl;               // nodes 0..7 addressable by the array...
    const NodeId node1 = nl.addNode();  // ...but only node 1 is actually in use (nodeCount()==2)
    nl.add(Resistor{acfx::kGround, node1, 1000.0});  // valid, references ground
    nl.add(Resistor{node1, 5, 2000.0});              // node 5 is in [2, 8) but never addNode()'d

    CHECK_THROWS_AS(nl.prepare(), std::invalid_argument);
    CHECK_THROWS_WITH(nl.prepare(), doctest::Contains("out-of-range node"));
}

TEST_CASE("CircuitNetlist - a terminal at or beyond MaxNodes is rejected before any out-of-bounds access") {
    Netlist<4, 8> nl;
    const NodeId node1 = nl.addNode();
    nl.add(Resistor{acfx::kGround, node1, 1000.0});
    nl.add(Resistor{node1, 9, 2000.0});  // 9 >= MaxNodes (4): must throw, never index parent[9]

    CHECK_THROWS_AS(nl.prepare(), std::invalid_argument);
    CHECK_THROWS_WITH(nl.prepare(), doctest::Contains("out-of-range node"));
}

// ---------------------------------------------------------------------------
// SC-006 - the post-prepare() read path (a solver reading admittance() /
// companion() / evaluate() over components()) allocates nothing on the heap.
// Mirrors the AllocationSentinel pattern used across the suite (see
// tests/core/no-allocation-test.cpp): construction, assembly, and prepare()
// run OUTSIDE the sentinel scope (control-thread build), and only the
// read/accumulate loop over the prepared netlist's components runs INSIDE
// it, matching FR-011 ("the per-sample solve path ... does not allocate").
// ---------------------------------------------------------------------------

TEST_CASE("CircuitNetlist - reading component physics over a prepared netlist allocates nothing") {
    Netlist<8, 8> nl;

    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();
    const NodeId node3 = nl.addNode();

    // A netlist exercising all six component kinds, fully grounded:
    //   ground --VS-- node1 --R-- node2 --L-- node3
    //                          \--C-- ground   \--Diode/CurrentSource-- ground
    nl.add(VoltageSource{node1, acfx::kGround, 9.0});
    nl.add(Resistor{node1, node2, 1000.0});
    nl.add(Capacitor{node2, acfx::kGround, 1e-6});
    nl.add(Inductor{node2, node3, 1e-3});
    nl.add(Diode{node3, acfx::kGround, 1e-14, 1.0, 0.02585});
    nl.add(CurrentSource{node3, acfx::kGround, 0.001});

    nl.prepare();  // control-thread build step: outside the sentinel scope

    const double dt = 1.0 / 48000.0;
    double accumulator = 0.0;

    AllocationSentinel::reset();
    for (int iteration = 0; iteration < 64; ++iteration) {
        for (const Component& c : nl.components()) {
            std::visit(
                [&accumulator, dt](const auto& element) {
                    using T = std::decay_t<decltype(element)>;
                    if constexpr (std::is_same_v<T, Resistor>) {
                        accumulator += element.admittance();
                    } else if constexpr (std::is_same_v<T, Capacitor> ||
                                          std::is_same_v<T, Inductor>) {
                        accumulator += element.companion(dt, 0.0).Geq;
                    } else if constexpr (std::is_same_v<T, Diode>) {
                        accumulator += element.evaluate(0.3).current;
                    } else if constexpr (std::is_same_v<T, VoltageSource>) {
                        accumulator += element.V;
                    } else if constexpr (std::is_same_v<T, CurrentSource>) {
                        accumulator += element.current();
                    }
                },
                c);
        }
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK(std::isfinite(accumulator));
    CHECK_MESSAGE(allocations == 0,
                  "reading prepared-netlist component physics allocated ", allocations);
}

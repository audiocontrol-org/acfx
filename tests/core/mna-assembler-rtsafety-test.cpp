#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "core/mna-test-support.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "support/allocation-sentinel.h"

// T013 -- User Story 4: two-phase assembly with an RT-safe hot path
// (spec.md US4 acceptance scenarios 1-3; FR-012/013/014; SC-003/SC-006;
// research.md D4/D7; contracts/mna-assembler.md "Invariants / guarantees").
//
// These cases prove, AT THE ASSEMBLER LEVEL (not just MnaSystem, which
// mna-system-test.cpp already covers for the abstract engine), that:
//   1. plan() ONCE followed by many refresh()+solve() iterations performs
//      ZERO heap allocation/deallocation (SC-003, US4.1).
//   2. the branch count fixed by plan() is invariant across repeated
//      refresh()+solve() -- refresh() never calls addBranch() (FR-014,
//      US4.2).
//   3. plan() THROWS a descriptive std:: exception, BEFORE any solve, for
//      each of the three fault classes SC-006 names: branch-capacity
//      overflow, an out-of-range node id, and a degenerate element value
//      (US4.3).
//   4. no exception can escape the per-solve phase: refresh()/solve() are
//      noexcept BY TYPE (compile-time static_assert, not just by observed
//      behavior), and a solve-time ill-posed system (as opposed to a
//      plan-time fault) still returns the bool not-solved signal rather than
//      throwing (D7).
// ---------------------------------------------------------------------------

using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::test::AllocationSentinel;
using mna_test::NoCompanions;

// ---------------------------------------------------------------------------
// 1. Zero-heap per-solve (SC-003, US4.1): plan() the floating-voltage-source
// series loop from mna-assembler-test.cpp's case 3 ONCE, OUTSIDE the
// AllocationSentinel scope (the plan phase is the control-thread build step
// and may allocate/throw, D4). Then wrap a 500-iteration refresh()+solve()
// loop -- the RT hot path -- in the sentinel and assert zero allocations AND
// zero deallocations, mirroring the pattern in tests/core/mna-system-test.cpp
// ("a reset/stamp/solve loop over a branch-augmented system allocates
// nothing (SC-003)") and tests/core/circuit-solver-test.cpp.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: plan once then 500 refresh+solve iterations allocate nothing (US4.1/SC-003)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 2;

    const double Vin = 12.0;
    const double R1 = 1000.0;
    const double R2 = 3000.0;
    const double E = 2.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId m = nl.addNode();
    const NodeId a = nl.addNode();
    const NodeId b = nl.addNode();

    nl.add(VoltageSource{m, kGround, Vin});
    nl.add(Resistor{m, a, R1});
    nl.add(VoltageSource{a, b, E});
    nl.add(Resistor{b, kGround, R2});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    // Plan phase: runs ONCE, OUTSIDE the sentinel scope (off the hot path,
    // D4/D7 -- may throw/allocate).
    CHECK_NOTHROW(assembler.plan(nl, sys));

    AllocationSentinel::reset();
    for (int iter = 0; iter < 500; ++iter) {
        assembler.refresh(nl, comps, sys);
        sys.solve();
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    // Sanity: the hot-path loop still produced a finite, correctly-solved
    // result -- the zero-heap assertion below is not vacuously true because
    // the loop body silently failed.
    CHECK(std::isfinite(sys.nodeVoltage(a)));
    CHECK(std::isfinite(sys.nodeVoltage(b)));

    CHECK_MESSAGE(allocations == 0,
                  "MnaAssembler refresh+solve loop allocated ", allocations);
    CHECK_MESSAGE(deallocations == 0,
                  "MnaAssembler refresh+solve loop deallocated ", deallocations);
}

// ---------------------------------------------------------------------------
// 2. Branch count stable across refresh (FR-014, US4.2): plan() fixes the
// branch count topologically (one per ideal voltage source / op-amp); refresh()
// NEVER calls addBranch(), so MnaSystem::branchCount() must read back the SAME
// value after every refresh()+solve() in a repeated loop.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: branch count is invariant across repeated refresh+solve (FR-014/US4.2)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 2;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId m = nl.addNode();
    const NodeId a = nl.addNode();
    const NodeId b = nl.addNode();

    nl.add(VoltageSource{m, kGround, 12.0});
    nl.add(Resistor{m, a, 1000.0});
    nl.add(VoltageSource{a, b, 2.0});
    nl.add(Resistor{b, kGround, 3000.0});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));

    // Two ideal VoltageSources -> exactly two topological branches (FR-014).
    const int plannedBranchCount = sys.branchCount();
    CHECK(plannedBranchCount == 2);

    for (int iter = 0; iter < 10; ++iter) {
        assembler.refresh(nl, comps, sys);
        REQUIRE(sys.solve());
        CHECK(sys.branchCount() == plannedBranchCount);
    }
}

// ---------------------------------------------------------------------------
// 3. Plan-time descriptive throws (SC-006, US4.3): each of the three fault
// classes the spec names -- branch-capacity overflow, an out-of-range node
// id, and a degenerate element value -- must throw a descriptive std::
// exception AT plan(), before any solve() is ever reachable. Each case below
// ends the moment plan() throws (CHECK_THROWS_AS), so solve() is structurally
// never invoked on the faulted assembler/system.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: plan() throws on branch-capacity overflow before any solve (SC-006a/US4.3)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    // Only ONE branch of capacity, but the netlist below needs two (two ideal
    // voltage sources) -- plan() must throw descriptively while allocating
    // the second branch, never silently truncate (FR-012/FR-015).
    constexpr int kMaxBranches = 1;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{node1, kGround, 5.0});
    nl.add(VoltageSource{node2, kGround, 7.0});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;

    CHECK_THROWS_AS(assembler.plan(nl, sys), std::length_error);
}

TEST_CASE("mna-assembler: plan() throws on an out-of-range node id before any solve (SC-006b/US4.3)") {
    // plan()'s node-range guard (validateNode, mna-assembler.h) is the MNA
    // storage-capacity check: "a node id outside [0, MaxNodes) ... the
    // netlist's prepare() validates against its own nodeCount; this is the
    // MNA storage-capacity guard" (contracts/mna-assembler.md). It is a
    // DISTINCT, independent guard from Netlist::prepare()'s own connectivity
    // validation -- exercised here by deliberately NOT calling nl.prepare()
    // (add() itself performs no node-id validation, netlist.h), so the ONLY
    // thing that can catch the malformed node id is the assembler's own
    // plan()-time guard.
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 1;
    constexpr int kMaxBranches = 0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();  // id 1 -- in range.

    // Node id 99 was never allocated via addNode() and is far outside
    // [0, kMaxNodes) -- an unrepresentable reference a stamp would otherwise
    // write out of the fixed matrix bounds for.
    nl.add(Resistor{node1, 99, 1000.0});

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;

    CHECK_THROWS_AS(assembler.plan(nl, sys), std::out_of_range);
}

TEST_CASE("mna-assembler: plan() throws on a degenerate resistor value before any solve (SC-006c/US4.3)") {
    constexpr int kMaxNodes = 2;
    constexpr int kMaxComponents = 1;
    constexpr int kMaxBranches = 0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();

    // R <= 0 is non-physical (no finite conductance); the netlist's own
    // prepare() validates TOPOLOGY only (a Resistor still "contributes a
    // conductive path" regardless of its value), so this is the MNA-specific
    // degenerate-value guard (FR-012, Principle V no fallback).
    nl.add(Resistor{node1, kGround, -100.0});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;

    CHECK_THROWS_AS(assembler.plan(nl, sys), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// 4a. refresh()/solve() are noexcept BY TYPE (compile-time), not merely by
// observed behavior -- a static_assert(noexcept(...)) is the strongest form
// of "no exception can escape the per-solve phase" available: if either
// signature ever lost its noexcept, this would fail to COMPILE rather than
// merely fail a runtime assertion (D4/D7, "refresh ... noexcept").
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: refresh() and solve() are noexcept by type (US4.2/US4.4/D7)") {
    using Assembler = MnaAssembler<4, 4, 2>;
    using System = MnaSystem<4, 2>;

    static_assert(
        noexcept(std::declval<const Assembler&>().refresh(
            std::declval<const Netlist<4, 4>&>(),
            std::declval<const NoCompanions&>(),
            std::declval<System&>())),
        "MnaAssembler::refresh must be noexcept -- it is the RT hot path "
        "(D4/D7) and must never throw regardless of its arguments");
    static_assert(
        noexcept(std::declval<System&>().solve()),
        "MnaSystem::solve must be noexcept -- an ill-posed system reports "
        "the not-solved boolean, never a thrown exception (D7)");

    // The static_asserts above ARE the check; this runtime assertion just
    // gives the TEST_CASE a body doctest can report as passed.
    CHECK(true);
}

// ---------------------------------------------------------------------------
// 4b. A solve-TIME ill-posed system (as opposed to a plan-time fault) must
// still surface as the boolean not-solved signal, never a thrown exception
// (D7). Two grounded ideal voltage sources pinning the SAME node to two
// DIFFERENT values is well-formed topology (Netlist::prepare() passes: both
// sources contribute a conductive path to ground) and plan()s cleanly (two
// branches, within capacity) but is over-determined/singular at solve time --
// mirrors mna-system-test.cpp's "floating node ... reports not-solved without
// NaN" case, but reached through the assembler's plan()/refresh() surface.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: a solve-time singular system reports not-solved without throwing (US4.4/D7)") {
    constexpr int kMaxNodes = 2;
    constexpr int kMaxComponents = 2;
    constexpr int kMaxBranches = 2;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();

    nl.add(VoltageSource{node1, kGround, 5.0});
    nl.add(VoltageSource{node1, kGround, 7.0});  // conflicting pin on node1.
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));

    assembler.refresh(nl, comps, sys);
    const bool solved = sys.solve();

    CHECK_FALSE(solved);
    CHECK(std::isfinite(sys.nodeVoltage(node1)));
}

#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"

#include <array>

// Inconsistent-plan / topology-drift guard (contract S10; govern AUDIT-20260708-
// 02 / 04 / 05 / 06). solve() must operate only on the netlist topology plan()
// validated: MnaAssembler::refresh() stamps through the FIXED plan on its noexcept
// hot path and does NOT re-validate node ids, so any topology drift is unsafe
// (stale branch map, Newton's std::get<Diode> throwing, or MnaSystem indexing its
// matrix with an unvalidated node id — memory corruption). The guard is a
// per-component TOPOLOGY signature (kind + terminal node ids) + component count,
// and surfaces ANY drift deterministically BY VALUE (NewtonStatus{false, 0}) —
// never a throw / OOB on the promised throw-free hot path. Element VALUES may vary
// across solves and are intentionally NOT part of the signature.

using acfx::Diode;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;
using acfx::newton::NewtonStatus;

namespace {

struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

}  // namespace

TEST_SUITE("newton-plan-drift") {

TEST_CASE("newton-plan-drift: solve() surfaces an inconsistent netlist by value (AUDIT-02/04/05)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    Netlist<kMaxNodes, kMaxComponents> planned;
    const NodeId p1 = planned.addNode();
    planned.add(Resistor{p1, kGround, 1000.0});             // index 0
    planned.add(Diode{p1, kGround, 1e-14, 1.0, 0.025852});  // index 1 (diode)
    planned.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    solver.plan(planned, assembler, sys);
    REQUIRE(solver.planned());
    REQUIRE(solver.diodeCount() == 1);

    const std::array<double, kMaxNodes> guess{};

    // Channel (a): a SHORTER netlist → the planned diode index is out of range.
    Netlist<kMaxNodes, kMaxComponents> shorter;
    const NodeId s1 = shorter.addNode();
    shorter.add(Resistor{s1, kGround, 1000.0});             // only index 0
    shorter.prepare();
    NewtonStatus aStatus;
    CHECK_NOTHROW(aStatus = solver.solve(shorter, base, guess, assembler, sys));
    CHECK_FALSE(aStatus.converged);
    CHECK(aStatus.iterations == 0);

    // Channel (b): the planned diode slot (index 1) now holds a NON-diode →
    // std::get<Diode> would throw std::bad_variant_access without the guard.
    Netlist<kMaxNodes, kMaxComponents> swapped;
    const NodeId w1 = swapped.addNode();
    swapped.add(Resistor{w1, kGround, 1000.0});             // index 0
    swapped.add(Resistor{w1, kGround, 2000.0});             // index 1: NOT a diode
    swapped.prepare();
    NewtonStatus bStatus;
    CHECK_NOTHROW(bStatus = solver.solve(swapped, base, guess, assembler, sys));
    CHECK_FALSE(bStatus.converged);
    CHECK(bStatus.iterations == 0);

    // Channel (c) — NON-diode KIND drift (AUDIT-04/05): the diode slot (index 1)
    // is unchanged, but index 0 changed Resistor → VoltageSource (a branch-bearing
    // kind whose planned branch-map entry is stale). Surfaced by value, so the
    // stale-branch refresh never runs.
    Netlist<kMaxNodes, kMaxComponents> kindDrift;
    const NodeId c1 = kindDrift.addNode();
    kindDrift.add(VoltageSource{c1, kGround, 0.5});         // index 0: was a Resistor
    kindDrift.add(Diode{c1, kGround, 1e-14, 1.0, 0.025852});// index 1: still a diode
    kindDrift.prepare();
    NewtonStatus cStatus;
    CHECK_NOTHROW(cStatus = solver.solve(kindDrift, base, guess, assembler, sys));
    CHECK_FALSE(cStatus.converged);
    CHECK(cStatus.iterations == 0);
}

TEST_CASE("newton-plan-drift: solve() surfaces SAME-kind node-id drift by value (AUDIT-06)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    // Plan: [Resistor@0 on node 1, Resistor@1 on node 2, Diode@2 on node 1].
    Netlist<kMaxNodes, kMaxComponents> planned;
    const NodeId n1 = planned.addNode();  // id 1
    const NodeId n2 = planned.addNode();  // id 2
    planned.add(Resistor{n1, kGround, 1000.0});             // index 0: nodes {1,0}
    planned.add(Resistor{n2, kGround, 1000.0});             // index 1: nodes {2,0}
    planned.add(Diode{n1, kGround, 1e-14, 1.0, 0.025852});  // index 2: nodes {1,0}
    planned.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    solver.plan(planned, assembler, sys);
    REQUIRE(solver.planned());

    // Channel (d): SAME kinds [Resistor, Resistor, Diode] and SAME count (3), but
    // the diode's node id drifts 1 → 2. The kind/count fingerprint alone would
    // pass; the full topology signature (which folds terminal node ids) rejects
    // it, so MnaSystem never indexes its matrix with the drifted node.
    Netlist<kMaxNodes, kMaxComponents> nodeDrift;
    const NodeId m1 = nodeDrift.addNode();  // id 1
    const NodeId m2 = nodeDrift.addNode();  // id 2
    nodeDrift.add(Resistor{m1, kGround, 1000.0});           // index 0: nodes {1,0}
    nodeDrift.add(Resistor{m2, kGround, 1000.0});           // index 1: nodes {2,0}
    nodeDrift.add(Diode{m2, kGround, 1e-14, 1.0, 0.025852});// index 2: nodes {2,0} — DRIFTED from {1,0}
    nodeDrift.prepare();

    const std::array<double, kMaxNodes> guess{};
    NewtonStatus dStatus;
    CHECK_NOTHROW(dStatus = solver.solve(nodeDrift, base, guess, assembler, sys));
    CHECK_FALSE(dStatus.converged);
    CHECK(dStatus.iterations == 0);

    // Control: re-solving the ORIGINAL planned netlist still works (the guard does
    // not false-positive on the same topology; values/companions may still vary).
    NewtonStatus ok;
    CHECK_NOTHROW(ok = solver.solve(planned, base, guess, assembler, sys));
    CHECK(ok.iterations >= 1);
}

}  // TEST_SUITE("newton-plan-drift")

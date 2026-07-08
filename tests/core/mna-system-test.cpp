#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>

#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/node.h"
#include "support/allocation-sentinel.h"

// T003 -- RED step for the Layer-1 abstract bordered linear engine
// (specs/modified-nodal-analysis/contracts/mna-system.md). MnaSystem does not
// exist yet; this suite is written FIRST and is expected to fail to build
// until core/primitives/circuit/mna/mna-system.h lands (T004+).
//
// Coverage, per the contract + research.md decisions D1/D2/D3/D5/D7:
//   (a) hand-stamped 2-node resistive divider via stampConductance +
//       stampRhsCurrent -- four-corner ground-aware stamp (D2).
//   (b) one ideal-voltage-source branch between two non-ground nodes via
//       addBranch/stampBranchIncidence/stampBranchValue, r=0 zero diagonal
//       -- only solvable because of partial pivoting (D5).
//   (c) a singular system (floating node) -- solve() returns false, no NaN
//       leaks into nodeVoltage (D1/D7).
//   (d) a poorly-scaled but well-posed system -- relative pivot threshold
//       (D1) accepts it rather than falsely rejecting.
//   (e) statelessness -- reset() + identical re-stamp + solve() is
//       bit-identical to the first solve.
//   (f) zero-heap -- AllocationSentinel across a reset/stamp/solve loop
//       (SC-003).

using acfx::NodeId;
using acfx::kGround;
using acfx::mna::MnaSystem;
using acfx::test::AllocationSentinel;

namespace {

// Small fixed sizing shared by every case below: at most 4 nodes (including
// ground) and at most 2 branches is enough for a 2-3 node resistive network
// plus a single voltage-source branch.
constexpr int kMaxNodes = 4;
constexpr int kMaxBranches = 2;

using System = MnaSystem<kMaxNodes, kMaxBranches>;

}  // namespace

// ---------------------------------------------------------------------------
// (a) Resistive divider stamped by hand: Vin --R1-- node1 --R2-- gnd, with
// Vin injected as a Norton-equivalent current source (I = Vin/R1) alongside
// R1 as a conductance to ground -- i.e. treat node1 as fed by a current
// source Vin/R1 in parallel with R1, feeding R2 to ground. This is the
// textbook two-conductance-plus-injected-current divider:
//   node1: (G1 + G2) * v1 = I_inj      =>  v1 = I_inj / (G1 + G2)
// with G1 = 1/R1, G2 = 1/R2, I_inj = Vin / R1.
// This exercises stampConductance's four-corner ground-aware stamp (D2: a
// ground-referenced conductance contributes a single diagonal +g) and
// stampRhsCurrent, with a known analytic answer.
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: resistive divider via stamped conductances solves to the analytic voltage (D2)") {
    const double R1 = 1000.0;
    const double R2 = 2000.0;
    const double Vin = 10.0;
    const double G1 = 1.0 / R1;
    const double G2 = 1.0 / R2;
    const double Iinj = Vin / R1;

    System sys;
    sys.reset();

    const NodeId node1 = 1;  // node 0 is ground

    // R1 from node1 to ground, R2 from node1 to ground (both single-diagonal
    // ground-referenced stamps).
    sys.stampConductance(node1, kGround, G1);
    sys.stampConductance(node1, kGround, G2);
    sys.stampRhsCurrent(node1, Iinj);

    REQUIRE(sys.solve());

    const double expected = Iinj / (G1 + G2);
    CHECK(sys.nodeVoltage(node1) == doctest::Approx(expected).epsilon(1e-12));
    CHECK(sys.nodeVoltage(kGround) == 0.0);
}

// ---------------------------------------------------------------------------
// (a-2) Two-node network exercising the OFF-diagonal four-corner stamp: a
// current source injects into node1, R1 bridges node1<->node2 (both
// non-ground, so all four corners of the stamp fire), R2 returns node2 to
// ground. KCL:
//   node1: G1*(v1 - v2) = I
//   node2: G1*(v2 - v1) + G2*v2 = 0
// Node2 is a pure series junction (R1 to the driven node, R2 to ground), so all
// of I flows through R2: substituting node1 into node2 gives G2*v2 = I, hence
// v2 = I / G2 and v1 = v2 + I/G1.
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: bridging conductance between two non-ground nodes exercises the four-corner stamp (D2)") {
    const double R1 = 500.0;
    const double R2 = 1500.0;
    const double I = 0.02;  // amps
    const double G1 = 1.0 / R1;
    const double G2 = 1.0 / R2;

    System sys;
    sys.reset();

    const NodeId node1 = 1;
    const NodeId node2 = 2;

    sys.stampConductance(node1, node2, G1);  // bridging: all four corners
    sys.stampConductance(node2, kGround, G2);
    sys.stampRhsCurrent(node1, I);

    REQUIRE(sys.solve());

    const double v2Expected = I / G2;
    const double v1Expected = v2Expected + I / G1;
    CHECK(sys.nodeVoltage(node2) == doctest::Approx(v2Expected).epsilon(1e-12));
    CHECK(sys.nodeVoltage(node1) == doctest::Approx(v1Expected).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// (b) One ideal-voltage-source branch between two non-ground nodes: the
// classic bordered [G B; C 0] form (D3). The branch has r=0 (ideal source,
// zero diagonal -- D5 makes partial pivoting load-bearing here: naive
// elimination would divide by zero on this row). A resistor from node "b" to
// ground closes the loop so the branch current is well defined.
//
//   node "a" --(ideal Vsrc, value E)-- node "b" --R-- ground
//
// The branch constraint is v(a) - v(b) = E. With only R loading node b and
// nothing else touching node a except the source branch, KCL at node b:
//   branch current I flows INTO b from the source, OUT through R to ground:
//   I = v(b) / R,  and v(a) = v(b) + E.
// Node a carries no other connection, so its own KCL row is trivial (all its
// current comes from/goes to the branch); the system is still well-posed
// because the branch row supplies the missing equation (D3).
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: single ideal voltage-source branch between two non-ground nodes requires partial pivoting (D3/D5)") {
    const double E = 4.0;    // imposed voltage difference v(a) - v(b)
    const double R = 100.0;  // ohms, node b to ground

    System sys;
    sys.reset();

    const NodeId a = 1;
    const NodeId b = 2;

    sys.stampConductance(b, kGround, 1.0 / R);

    const int k = sys.addBranch();
    sys.stampBranchIncidence(k, a, b);
    sys.stampBranchValue(k, E);
    sys.stampBranchResistance(k, 0.0);  // ideal source: zero diagonal (D5)

    REQUIRE(sys.solve());

    const double vb = sys.nodeVoltage(b);
    const double va = sys.nodeVoltage(a);
    CHECK((va - vb) == doctest::Approx(E).epsilon(1e-12));

    const double expectedCurrent = vb / R;
    CHECK(sys.branchCurrent(k) == doctest::Approx(expectedCurrent).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// (c) Singular system: the simplest ill-posed topology is a node referenced
// only by an injected current with NO conductance stamped anywhere -- its
// diagonal is exactly zero and no row swap can find a nonzero pivot for its
// column (no other row touches that column either). solve() must return
// false and must not leave a NaN readable via nodeVoltage (D1/D7).
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: a floating node with no path to ground is singular and reports not-solved without NaN (D1/D7)") {
    System sys;
    sys.reset();

    const NodeId floating = 1;
    // Inject current but stamp NO conductance anywhere -- node `floating`'s
    // diagonal is exactly zero and no pivot (even after row swaps) can be
    // found for its column, since no other row touches it either.
    sys.stampRhsCurrent(floating, 1.0);

    const bool solved = sys.solve();
    CHECK_FALSE(solved);

    const double v = sys.nodeVoltage(floating);
    CHECK(std::isfinite(v));
}

// ---------------------------------------------------------------------------
// (d) Poorly-scaled but well-posed system: a micro-siemens conductance
// (G ~ 1e-6) matched with a proportionally tiny current injection. An
// ABSOLUTE singular-pivot floor would risk misclassifying this pivot as
// singular; the RELATIVE threshold (D1, scaled to the largest matrix entry)
// must still accept and solve it correctly.
//
//   node1: G * v1 = I,   G = 1e-6 S,  I = 1e-6 A  =>  v1 = 1.0 V exactly
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: micro-siemens conductance beside a well-posed system solves via the relative pivot threshold (D1)") {
    const double G = 1.0e-6;  // microsiemens-scale conductance
    const double I = 1.0e-6;  // matched current so v1 == 1.0 exactly

    System sys;
    sys.reset();

    const NodeId node1 = 1;
    sys.stampConductance(node1, kGround, G);
    sys.stampRhsCurrent(node1, I);

    REQUIRE(sys.solve());
    CHECK(sys.nodeVoltage(node1) == doctest::Approx(1.0).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// (e) Statelessness: reset() followed by an identical re-stamp of the divider
// from case (a) must reproduce a BIT-IDENTICAL solution -- MnaSystem carries
// no residual state between solves (D4/FR-011).
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: reset() plus identical re-stamping reproduces a bit-identical solution") {
    const double R1 = 1000.0;
    const double R2 = 2000.0;
    const double Vin = 10.0;
    const double G1 = 1.0 / R1;
    const double G2 = 1.0 / R2;
    const double Iinj = Vin / R1;

    System sys;
    const NodeId node1 = 1;

    sys.reset();
    sys.stampConductance(node1, kGround, G1);
    sys.stampConductance(node1, kGround, G2);
    sys.stampRhsCurrent(node1, Iinj);
    REQUIRE(sys.solve());
    const double first = sys.nodeVoltage(node1);

    sys.reset();
    sys.stampConductance(node1, kGround, G1);
    sys.stampConductance(node1, kGround, G2);
    sys.stampRhsCurrent(node1, Iinj);
    REQUIRE(sys.solve());
    const double second = sys.nodeVoltage(node1);

    CHECK(first == second);  // bit-identical, not merely close
}

// ---------------------------------------------------------------------------
// (f) Zero-heap: a reset() -> stamp -> solve() loop performs no heap
// allocation or deallocation (SC-003). addBranch() is plan-time (may throw)
// and is called OUTSIDE the measured region, mirroring the two-phase
// plan/refresh split (D4) and the existing AllocationSentinel usage pattern
// in tests/core/circuit-solver-test.cpp.
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: a reset/stamp/solve loop over a branch-augmented system allocates nothing (SC-003)") {
    const double E = 2.5;
    const double R = 600.0;

    System sys;
    const NodeId a = 1;
    const NodeId b = 2;

    // Plan phase: allocate the branch once, outside the sentinel scope.
    const int k = sys.addBranch();

    AllocationSentinel::reset();
    for (int iter = 0; iter < 500; ++iter) {
        sys.reset();
        sys.stampConductance(b, kGround, 1.0 / R);
        sys.stampBranchIncidence(k, a, b);
        sys.stampBranchValue(k, E);
        sys.stampBranchResistance(k, 0.0);
        sys.solve();
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    CHECK(std::isfinite(sys.nodeVoltage(a)));
    CHECK_MESSAGE(allocations == 0,
                  "MnaSystem reset/stamp/solve loop allocated ", allocations);
    CHECK_MESSAGE(deallocations == 0,
                  "MnaSystem reset/stamp/solve loop deallocated ", deallocations);
}

// ---------------------------------------------------------------------------
// (g) Sparse / interior-gap node ids: a well-posed circuit that references
// nodes 1 and 3 but SKIPS node 2 must solve, and must produce the SAME node
// voltages as the identical circuit renumbered densely to 1 and 2. The active-
// node set omits the never-referenced id 2 (rather than compacting locals
// [0, highestId) densely, which would leave local 1 an all-zero singular row
// and falsely report the well-posed circuit as not-solved). Divider topology
// mirrors case (a): two ground-referenced conductances plus an injected
// current at the driven node.
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: a circuit skipping an interior node id solves and matches the densely-renumbered circuit (sparse-node robustness)") {
    const double G1 = 1.0 / 1000.0;
    const double G2 = 1.0 / 2000.0;
    const double Iinj = 10.0 / 1000.0;
    const double expected = Iinj / (G1 + G2);

    // Sparse: reference nodes 1 and 3, skipping node 2 entirely. kMaxNodes = 4
    // covers ids 0..3. The driven divider lives on node 3; node 1 is present
    // (referenced) as a second ground-referenced conductance leg so the id set
    // is genuinely {1, 3} with an interior gap at 2.
    System sparse;
    sparse.reset();
    const NodeId sparseDriven = 3;
    const NodeId sparseOther = 1;
    sparse.stampConductance(sparseDriven, kGround, G1);
    sparse.stampConductance(sparseDriven, kGround, G2);
    sparse.stampRhsCurrent(sparseDriven, Iinj);
    // A second, independent ground-referenced leg on node 1 so it is referenced
    // and well-posed (its own KCL: G2 * v1 = 0 -> v1 = 0).
    sparse.stampConductance(sparseOther, kGround, G2);

    REQUIRE(sparse.solve());
    CHECK(sparse.nodeVoltage(sparseDriven) ==
          doctest::Approx(expected).epsilon(1e-12));
    CHECK(sparse.nodeVoltage(sparseOther) == doctest::Approx(0.0));
    CHECK(sparse.nodeVoltage(2) == 0.0);  // never-referenced gap reads 0.

    // Dense: the SAME circuit renumbered so the driven node is 2 and the other
    // leg is 1 (no gap). The driven-node voltage must be bit-identical.
    System dense;
    dense.reset();
    const NodeId denseDriven = 2;
    const NodeId denseOther = 1;
    dense.stampConductance(denseDriven, kGround, G1);
    dense.stampConductance(denseDriven, kGround, G2);
    dense.stampRhsCurrent(denseDriven, Iinj);
    dense.stampConductance(denseOther, kGround, G2);

    REQUIRE(dense.solve());
    CHECK(sparse.nodeVoltage(sparseDriven) == dense.nodeVoltage(denseDriven));
}

// ---------------------------------------------------------------------------
// (h) A referenced-but-floating node stays singular: referencing a node (via a
// current injection) with NO conductive path to it must STILL report
// not-solved. The sparse-node fix omits never-referenced ids, but must NOT omit
// a referenced id just because its row is all-zero — that is a genuine
// singularity, not a gap. Mirrors case (c) but with an interior gap around it.
// ---------------------------------------------------------------------------

TEST_CASE("mna-system: a referenced-but-floating node remains singular even beside an interior gap (no spurious solve)") {
    System sys;
    sys.reset();

    // Node 3 is well-grounded; node 1 is referenced (current injected) but has
    // no conductance anywhere -> its row is all-zero. Node 2 is a never-touched
    // gap. The floating referenced node 1 must keep the system singular.
    sys.stampConductance(3, kGround, 1.0 / 1000.0);
    sys.stampRhsCurrent(3, 1.0e-3);
    sys.stampRhsCurrent(1, 1.0);  // referenced, but no path -> singular row.

    CHECK_FALSE(sys.solve());
    CHECK(std::isfinite(sys.nodeVoltage(1)));
    CHECK(std::isfinite(sys.nodeVoltage(3)));
}

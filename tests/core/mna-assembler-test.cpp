#include <doctest/doctest.h>

#include <cmath>

#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// T005 -- RED step for the Layer-2 netlist -> matrix mapper, User Story 1
// (specs/modified-nodal-analysis/contracts/mna-assembler.md; spec.md US1;
// FR-006/007/008; SC-005). MnaAssembler does not exist yet
// (core/primitives/circuit/mna/mna-assembler.h is unwritten); this suite is
// written FIRST and is expected to fail to build until that header lands.
//
// Coverage (US1 acceptance scenarios 1-3, linear elements only -- no
// reactive/nonlinear companions are ever consulted here):
//   1. Resistive divider via a GROUNDED ideal VoltageSource + two Resistors:
//      V(mid) == Vin*R2/(R1+R2).
//   2. CurrentSource into a node with a Resistor to ground: V == I*R exactly.
//   3. FLOATING ideal VoltageSource between two NON-GROUND nodes, closed by
//      resistors into a single series loop -- the capability the labs refuse
//      (SC-005) -- asserting both V(a)-V(b) == E and the source branch
//      current.
//
// Each case follows the contract's two-phase surface (D4): construct the
// Netlist, plan() once (throwing, off the hot path), refresh() with a
// trivial no-op CompanionSupply (US1 has no reactive/nonlinear elements, so
// it is never consulted), then solve() and read back through MnaSystem.

using acfx::Companion;
using acfx::CurrentSource;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;

namespace {

// Trivial CompanionSupply for US1: no capacitor/inductor/diode appears in any
// circuit below, so refresh() must never actually call at() on this -- it
// exists only to satisfy the assembler's per-solve signature (contract
// "CompanionSupply (the sibling seam)"). Returning a zeroed companion is a
// deliberately inert value, not a fallback: if the assembler ever DID call
// it for a linear-only netlist, that would itself be a bug this stub would
// silently mask, but no test below contains an element that could trigger
// the call.
struct NoCompanions {
    Companion at(int /*componentIndex*/) const noexcept {
        return Companion{0.0, 0.0};
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. Resistive divider: Vin --(grounded ideal VoltageSource)-- node1 --R1--
// node2 --R2-- ground.
//
// KCL: node1 is pinned by the branch constraint V(node1) - V(gnd) = Vin =>
// V(node1) = Vin exactly. Node2 has only R1 (to node1) and R2 (to ground)
// touching it, so the current through R1 into node2 must equal the current
// through R2 out of node2 (no other path):
//   (Vin - V2) / R1 = V2 / R2   =>   V2 = Vin * R2 / (R1 + R2).
// This is acceptance scenario 1 (spec.md US1) / FR-007 (resistor
// conductance) + FR-008 (grounded ideal voltage-source branch).
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: resistive divider via a grounded ideal voltage source matches the analytic ratio (US1.1)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;

    const double Vin = 10.0;
    const double R1 = 1000.0;
    const double R2 = 2000.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R1});
    nl.add(Resistor{node2, kGround, R2});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    CHECK(sys.nodeVoltage(node1) == doctest::Approx(Vin).epsilon(1e-12));
    const double expected = Vin * R2 / (R1 + R2);
    CHECK(sys.nodeVoltage(node2) == doctest::Approx(expected).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// 2. Current source into a node with a resistor to ground: node1
// --CurrentSource(I)-- , node1 --R-- ground. No voltage source, hence no
// branch at all (kMaxBranches = 0 below exercises that MnaAssembler does not
// require any branch capacity when the netlist has none).
//
// KCL at node1 (only R touches it): I = V1 / R exactly (FR-007, current
// source stamped as +I at node1's RHS balance; resistor as conductance).
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: current source into a resistor to ground matches I*R exactly (US1.3)") {
    constexpr int kMaxNodes = 2;
    constexpr int kMaxComponents = 2;
    constexpr int kMaxBranches = 0;

    const double I = 0.005;    // amps
    const double R = 4000.0;   // ohms

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();

    nl.add(CurrentSource{node1, kGround, I});
    nl.add(Resistor{node1, kGround, R});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    const double expected = I * R;
    CHECK(sys.nodeVoltage(node1) == doctest::Approx(expected).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// 3. Floating ideal voltage source between two NON-GROUND nodes -- the
// capability the labs refuse (SC-005) -- closed into a single series loop so
// every node has a well-defined operating point:
//
//   Vin --(grounded ideal VoltageSource)-- m --R1-- a
//        --(FLOATING ideal VoltageSource, value E)-- b --R2-- ground
//
// Neither `a` nor `b` is ground; the source's only two terminals are both
// interior nodes. KCL derivation (component -> branch stamp per the contract
// table, FR-008):
//   - branch0 (grounded source) pins V(m) = Vin.
//   - node a is touched ONLY by R1 and the floating-source branch (call its
//     current Ib, defined by the contract's incidence convention as flowing
//     from p=a to n=b): KCL at a gives Ib = (Vin - Va) / R1.
//   - node b is touched ONLY by the floating-source branch and R2 to ground:
//     KCL at b gives Ib = Vb / R2.
//   - the branch's own constraint row is exactly Va - Vb = E (FR-008).
// Substituting Va = Vb + E and Vb = Ib*R2 into Ib = (Vin - Va)/R1 collapses
// to the classic single-loop result:
//   Ib = (Vin - E) / (R1 + R2).
// With Vin=12, R1=1000, R2=3000, E=2: Ib = (12-2)/4000 = 0.0025 A,
// Vb = Ib*R2 = 7.5 V, Va = Vb + E = 9.5 V.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: floating ideal voltage source between two non-ground nodes solves the series loop exactly (US1.2/SC-005)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 2;

    const double Vin = 12.0;
    const double R1 = 1000.0;
    const double R2 = 3000.0;
    const double E = 2.0;  // floating source value: V(a) - V(b)

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId m = nl.addNode();
    const NodeId a = nl.addNode();
    const NodeId b = nl.addNode();

    nl.add(VoltageSource{m, kGround, Vin});  // grounded: pins node m
    nl.add(Resistor{m, a, R1});
    nl.add(VoltageSource{a, b, E});          // FLOATING: neither terminal is ground
    nl.add(Resistor{b, kGround, R2});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    const double expectedIb = (Vin - E) / (R1 + R2);
    const double expectedVb = expectedIb * R2;
    const double expectedVa = expectedVb + E;

    CHECK(sys.nodeVoltage(m) == doctest::Approx(Vin).epsilon(1e-12));
    CHECK(sys.nodeVoltage(a) == doctest::Approx(expectedVa).epsilon(1e-12));
    CHECK(sys.nodeVoltage(b) == doctest::Approx(expectedVb).epsilon(1e-12));

    // The floating source's imposed difference holds exactly (to solve
    // precision), the decisive SC-005 assertion: this topology is refused by
    // the pre-existing labs but is a first-class capability here.
    CHECK((sys.nodeVoltage(a) - sys.nodeVoltage(b)) == doctest::Approx(E).epsilon(1e-12));

    // The floating source is the SECOND VoltageSource added (index 1 among
    // components), hence the SECOND branch planned (index 1 among branches:
    // branch 0 is the grounded source at m, branch 1 is the floating source
    // between a and b) -- plan() walks the netlist in component order.
    constexpr int kFloatingBranch = 1;
    CHECK(sys.branchCurrent(kFloatingBranch) == doctest::Approx(expectedIb).epsilon(1e-12));
}

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "support/allocation-sentinel.h"

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

using acfx::Capacitor;
using acfx::Companion;
using acfx::CurrentSource;
using acfx::Diode;
using acfx::DiodeSample;
using acfx::Netlist;
using acfx::NodeId;
using acfx::OpAmp;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::test::AllocationSentinel;

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

// ---------------------------------------------------------------------------
// T008 -- RED step for User Story 2 (ideal op-amp / nullor circuits;
// specs/modified-nodal-analysis/contracts/mna-assembler.md "OpAmp -> nullor
// border"; spec.md US2 acceptance scenarios 1-3; FR-009). The assembler's
// OpAmp case is a labeled no-op extension point until T009
// (core/primitives/circuit/mna/mna-assembler.h: "OpAmp nullor border stamp:
// T009" in both plan() and refresh()) -- plan() allocates NO branch for the
// op-amp and refresh() stamps NOTHING for it. These cases are written FIRST
// and are EXPECTED TO FAIL now: with no norator branch and no nullator
// constraint, the op-amp's `out` node is only reachable through the feedback
// resistor, so the reduced nodal solve collapses to v(out) == v(inMinus)
// (zero current condition on that lone resistor), NOT the ideal closed-form
// gain. This is the expected RED state; T009 lands the real border stamp and
// turns these green without editing the assertions here.
//
// Each case adds ONE branch capacity for the op-amp's future norator column,
// alongside the ideal voltage source's branch -- an op-amp augments the
// system exactly like a voltage source (contract: "Branch? yes"), so
// kMaxBranches = 2 (source branch + op-amp branch) even though, pre-T009,
// the op-amp branch is never actually allocated by plan().
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 1 & 3. Ideal INVERTING amplifier:
//
//   Vin --(grounded ideal VoltageSource)-- vinNode --Rin-- inMinus --Rf-- out
//   inPlus tied directly to ground; OpAmp{inPlus=gnd, inMinus, out}.
//
// Closed-form derivation (ideal op-amp: infinite gain, zero input current):
//   - The nullator imposes the virtual short V(inMinus) = V(inPlus) = 0 V
//     (inPlus is grounded), and draws zero current at inMinus.
//   - KCL at inMinus (only Rin and Rf touch it, no current into the op-amp
//     input): (Vin - 0)/Rin + (Vout - 0)/Rf = 0
//     => Vout = -Vin * Rf / Rin.
// With Vin=0.5, Rin=1e3, Rf=1e4: Vout = -0.5 * 10000/1000 = -5.0 V exactly
// (spec.md US2 acceptance scenario 1; FR-009).
//
// RED-state expectation: with the op-amp stamped as a no-op (T009 pending),
// `out`'s only stamped connection is Rf to inMinus, so its KCL row alone
// forces v(out) == v(inMinus). And with no nullator constraint tying
// V(inMinus) to V(inPlus)=0, inMinus instead floats to whatever the passive
// Rin/Rf divider gives it fed from vinNode = Vin -- collapsing to
// v(inMinus) == v(vinNode) == Vin (zero net current through the lone
// Rin-Rf series path with no sink at `out`), so v(out) == Vin == 0.5 V,
// NOT the expected -5.0 V. Scenario 3 (the nullator constraint
// V(inPlus) - V(inMinus) == 0) also fails here: V(inPlus) - V(inMinus)
// evaluates to 0 - 0.5 = -0.5, not 0.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: ideal inverting op-amp amplifier matches -Vin*Rf/Rin (US2.1/US2.3, RED pending T009)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 2;  // 1 voltage-source branch + 1 op-amp branch (T009).

    const double Vin = 0.5;
    const double Rin = 1000.0;
    const double Rf = 10000.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId inMinus = nl.addNode();
    const NodeId out = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, Vin});
    nl.add(Resistor{vinNode, inMinus, Rin});
    nl.add(Resistor{out, inMinus, Rf});
    nl.add(OpAmp{kGround, inMinus, out});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    const double expectedVout = -Vin * Rf / Rin;
    // EXPECTED TO FAIL (RED): op-amp not yet stamped (T009 pending).
    CHECK(sys.nodeVoltage(out) == doctest::Approx(expectedVout).epsilon(1e-12));

    // US2 acceptance scenario 3: the nullator constraint V(in+) - V(in-) == 0
    // must hold in the solution. EXPECTED TO FAIL (RED) for the same reason.
    const double nullatorResidual = sys.nodeVoltage(kGround) - sys.nodeVoltage(inMinus);
    CHECK(nullatorResidual == doctest::Approx(0.0).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// 2. Ideal NON-INVERTING amplifier:
//
//   Vin --(grounded ideal VoltageSource)-- inPlus
//   out --Rf-- inMinus --Rg-- ground
//   OpAmp{inPlus, inMinus, out}.
//
// Closed-form derivation (ideal op-amp: infinite gain, zero input current):
//   - The nullator imposes the virtual short V(inMinus) = V(inPlus) = Vin,
//     and draws zero current at inMinus.
//   - KCL at inMinus (only Rf and Rg touch it, no current into the op-amp
//     input): (Vout - Vin)/Rf = (Vin - 0)/Rg
//     => Vout = Vin * (1 + Rf/Rg).
// With Vin=1.0, Rf=9000, Rg=1000: Vout = 1.0 * (1 + 9000/1000) = 10.0 V
// exactly (spec.md US2 acceptance scenario 2; FR-009).
//
// RED-state expectation: with the op-amp a stamped no-op (T009 pending),
// `out`'s only stamped connection is Rf to inMinus, forcing
// v(out) == v(inMinus) from out's own KCL row. With no nullator tying
// inMinus to inPlus, inMinus instead settles from its own KCL against Rf
// (to the now-equal `out`) and Rg (to ground) alone: the Rf term cancels
// (v(out) == v(inMinus)), leaving (1/Rg)*v(inMinus) == 0, so
// v(inMinus) == 0 and therefore v(out) == 0 V, NOT the expected 10.0 V.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: ideal non-inverting op-amp amplifier matches Vin*(1+Rf/Rg) (US2.2, RED pending T009)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 2;  // 1 voltage-source branch + 1 op-amp branch (T009).

    const double Vin = 1.0;
    const double Rf = 9000.0;
    const double Rg = 1000.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId inPlus = nl.addNode();
    const NodeId inMinus = nl.addNode();
    const NodeId out = nl.addNode();

    nl.add(VoltageSource{inPlus, kGround, Vin});
    nl.add(Resistor{out, inMinus, Rf});
    nl.add(Resistor{inMinus, kGround, Rg});
    nl.add(OpAmp{inPlus, inMinus, out});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    const double expectedVout = Vin * (1.0 + Rf / Rg);
    // EXPECTED TO FAIL (RED): op-amp not yet stamped (T009 pending).
    CHECK(sys.nodeVoltage(out) == doctest::Approx(expectedVout).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// T010/T011 -- the CompanionSupply harness + RED cases for User Story 3
// (contracts/mna-assembler.md "CompanionSupply"/"Capacitor / Inductor / Diode
// -> Companion{Geq,Ieq} ... stampConductance(a,b,Geq), stampRhsCurrent with
// Ieq"; data-model.md "CompanionSupply"; spec.md US3 scenarios 1-3;
// FR-010/011; research.md D6).
//
// IndexedCompanions is a hand-written stand-in for the newton-iteration /
// implicit-integration siblings that supply real companions in production: it
// names an EXACT Companion{Geq,Ieq} per component index (`Companion at(int)
// const noexcept`) -- unlike NoCompanions above (one inert value for every
// index, since US1/US2 never call at() at all).
//
// The assembler's Capacitor/Inductor/Diode arm is STILL a labeled no-op
// extension point (mna-assembler.h refresh(): "... T012 ... No-op here"), so
// every case below is EXPECTED TO FAIL: the supplied companion is never
// stamped, so the node solves to whatever the companion-free netlist implies
// instead of the closed form derived independently in each comment. T012
// turns these green without editing the assertions here.
//
// SIGN CONVENTION (auditable against core/labs/diode-clippers/solver/
// transient-clipper.h's buildAugmented(), which performs this exact
// substitution today): a companion element's branch current, directed a -> b
// (same convention as Resistor{a,b,R}), is i(a,b) = Geq*(V(a)-V(b)) - Ieq,
// i.e. Geq stamps as a plain conductance a<->b and Ieq stamps exactly like
// CurrentSource{p=a,n=b,I=Ieq} (+Ieq at a, -Ieq at b). This is precisely
// Capacitor::companion()'s Ieq=Geq*vPrev, and transient-clipper.h stamps it
// unchanged (`CurrentSource{cap->a, cap->b, comp.Ieq}`). For a diode
// linearized at vAK0, that lab computes a locally-named `Ieq = I(vAK0) -
// Geq*vAK0` and stamps `CurrentSource{anode, cathode, -Ieq}`; unwinding the
// sign flip gives Geq*vAK0 - I(vAK0) -- the SAME quantity case 3 uses below.
// ---------------------------------------------------------------------------

namespace {

template <std::size_t N>
struct IndexedCompanions {
    std::array<Companion, N> byIndex{};

    Companion at(int componentIndex) const noexcept {
        return byIndex[static_cast<std::size_t>(componentIndex)];
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// 1. Fed-companion RC step (US3.1): Vin --(grounded VoltageSource)-- vinNode
// --R-- capNode --Capacitor(C)-- ground. The caller (implicit-integration's
// role) discretizes the capacitor with backward Euler given the previous
// across-cap voltage vPrev (Capacitor::companion(): Geq=C/dt, Ieq=Geq*vPrev)
// and supplies exactly that pair for the Capacitor's index. MNA must stamp it
// and hold NO history of its own -- vPrev/dt never appear inside
// MnaAssembler.
//
// Closed form: KCL at capNode (touched only by R and the companion) is
// (1/R)*(V(capNode)-Vin) + Geq*V(capNode) = Ieq, so
//     V(capNode) = (Ieq + Vin/R) / (1/R + Geq)
// -- the backward-Euler RC recurrence (spec.md US3.1; FR-021). Numbers:
// Vin=5V, R=1000ohm, C=1uF, dt=1ms => Geq=1e-3 S; vPrev=2V => Ieq=2e-3 A.
// (Ieq+Vin/R)=7e-3; (1/R+Geq)=2e-3; V(capNode) = 7e-3/2e-3 = 3.5 V exactly.
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: fed-companion RC step matches the backward-Euler recurrence (US3.1, RED pending T012)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr int kCapacitorIndex = 2;  // third component added, index 2

    const double Vin = 5.0;
    const double R = 1000.0;
    const double C = 1.0e-6;
    const double dt = 1.0e-3;
    const double vPrev = 2.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, Vin});
    nl.add(Resistor{vinNode, capNode, R});
    nl.add(Capacitor{capNode, kGround, C});
    nl.prepare();

    const double Geq = C / dt;
    const double Ieq = Geq * vPrev;

    IndexedCompanions<kMaxComponents> comps;
    comps.byIndex[kCapacitorIndex] = Companion{Geq, Ieq};

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    CHECK(sys.nodeVoltage(vinNode) == doctest::Approx(Vin).epsilon(1e-12));

    const double expectedVCap = (Ieq + Vin / R) / (1.0 / R + Geq);
    // EXPECTED TO FAIL (RED): Capacitor is still a no-op extension point
    // (T012 pending) -- the supplied companion is never stamped.
    CHECK(sys.nodeVoltage(capNode) == doctest::Approx(expectedVCap).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// 2. Statelessness (US3.2; FR-011): two successive refresh()+solve() calls
// with an IDENTICAL (netlist, companions) pair must produce bit-identical
// node voltages -- MNA is a pure function of its inputs, carrying no residual
// state between solves. Reuses case 1's RC netlist shape. Unlike case 1 this
// holds (and passes) both before and after T012: purity is invariant to
// whether the companion happens to be stamped, only to whether refresh()
// reads state besides (nl, comps, the fixed plan).
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: repeated refresh+solve with identical (netlist, companions) is bit-identical (US3.2)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr int kCapacitorIndex = 2;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, 5.0});
    nl.add(Resistor{vinNode, capNode, 1000.0});
    nl.add(Capacitor{capNode, kGround, 1.0e-6});
    nl.prepare();

    IndexedCompanions<kMaxComponents> comps;
    comps.byIndex[kCapacitorIndex] = Companion{7.5e-4, 1.25e-3};

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    CHECK_NOTHROW(assembler.plan(nl, sys));

    assembler.refresh(nl, comps, sys);
    REQUIRE(sys.solve());
    const double firstVin = sys.nodeVoltage(vinNode);
    const double firstVCap = sys.nodeVoltage(capNode);

    assembler.refresh(nl, comps, sys);
    REQUIRE(sys.solve());
    const double secondVin = sys.nodeVoltage(vinNode);
    const double secondVCap = sys.nodeVoltage(capNode);

    // Bit-identical, not merely approximately equal (FR-011 "no residual
    // state between solves").
    CHECK(firstVin == secondVin);
    CHECK(firstVCap == secondVCap);
}

// ---------------------------------------------------------------------------
// 3. Diode reduced to a Norton companion by the caller (US3.3): Vin
// --(grounded VoltageSource)-- vinNode --R-- diodeNode --Diode(anode=
// diodeNode, cathode=ground)--. The caller (newton-iteration's role)
// evaluates the diode's Shockley physics at a chosen operating point vAK0 via
// Diode::evaluate() (current I(vAK0), analytic conductance g(vAK0)=dI/dV) and
// linearizes it into a Norton companion per the sign convention above:
//     Geq = g(vAK0),   Ieq = Geq*vAK0 - I(vAK0)
// A LINEAR check at a FIXED companion -- no Newton iteration; that loop is
// newton-iteration's job, not MNA's (D6). Closed form mirrors case 1 with the
// diode's Geq/Ieq: V(diodeNode) = (Ieq + Vin/R) / (1/R + Geq) (spec.md US3.3;
// FR-021).
// ---------------------------------------------------------------------------

TEST_CASE("mna-assembler: diode reduced to a Norton companion matches the linearized-operating-point solution (US3.3, RED pending T012)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr int kDiodeIndex = 2;  // third component added, index 2

    const double Vin = 1.0;
    const double R = 100.0;
    const double Is = 1.0e-12;
    const double n = 1.0;
    const double Vt = 0.025852;
    const double vAK0 = 0.6;  // forward-biased linearization/operating point

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId diodeNode = nl.addNode();
    const Diode diode{diodeNode, kGround, Is, n, Vt};

    nl.add(VoltageSource{vinNode, kGround, Vin});
    nl.add(Resistor{vinNode, diodeNode, R});
    nl.add(diode);
    nl.prepare();

    const DiodeSample sample = diode.evaluate(vAK0);
    const double Geq = sample.conductance;
    const double Ieq = Geq * vAK0 - sample.current;

    IndexedCompanions<kMaxComponents> comps;
    comps.byIndex[kDiodeIndex] = Companion{Geq, Ieq};

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    CHECK(sys.nodeVoltage(vinNode) == doctest::Approx(Vin).epsilon(1e-12));

    const double expectedVDiode = (Ieq + Vin / R) / (1.0 / R + Geq);
    // EXPECTED TO FAIL (RED): Diode is still a no-op extension point (T012
    // pending) -- the supplied companion is never stamped.
    CHECK(sys.nodeVoltage(diodeNode) == doctest::Approx(expectedVDiode).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// 1. Zero-heap per-solve (SC-003, US4.1): plan() the floating-voltage-source
// series loop from case 3 above ONCE, OUTSIDE the AllocationSentinel scope
// (the plan phase is the control-thread build step and may allocate/throw,
// D4). Then wrap a 500-iteration refresh()+solve() loop -- the RT hot path --
// in the sentinel and assert zero allocations AND zero deallocations, mirroring
// the pattern in tests/core/mna-system-test.cpp
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

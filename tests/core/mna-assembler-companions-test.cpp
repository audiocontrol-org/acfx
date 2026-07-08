#include <doctest/doctest.h>

#include "core/mna-test-support.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// T010/T011/T012 -- the CompanionSupply harness + regression cases for User
// Story 3 (contracts/mna-assembler.md "CompanionSupply"/"Capacitor / Inductor
// / Diode -> Companion{Geq,Ieq} ... stampConductance(a,b,Geq), stampRhsCurrent
// with Ieq"; data-model.md "CompanionSupply"; spec.md US3 scenarios 1-3;
// FR-010/011; research.md D6).
//
// IndexedCompanions (mna-test-support.h) is a hand-written stand-in for the
// newton-iteration / implicit-integration siblings that supply real
// companions in production: it names an EXACT Companion{Geq,Ieq} per
// component index (`Companion at(int) const noexcept`) -- unlike
// NoCompanions (one inert value for every index, since the linear-only US1/
// US2 suites in mna-assembler-test.cpp never call at() at all).
//
// The assembler's Capacitor/Inductor/Diode arm stamps the supplied companion
// (Geq as a conductance, Ieq as a current source) at refresh() time, so every
// case below verifies the node solves to the closed form derived
// independently in each comment.
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

using acfx::Capacitor;
using acfx::Companion;
using acfx::Diode;
using acfx::DiodeSample;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using mna_test::IndexedCompanions;

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

TEST_CASE("mna-assembler: fed-companion RC step matches the backward-Euler recurrence (US3.1)") {
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

    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    CHECK(sys.nodeVoltage(vinNode) == doctest::Approx(Vin).epsilon(1e-12));

    const double expectedVCap = (Ieq + Vin / R) / (1.0 / R + Geq);
    CHECK(sys.nodeVoltage(capNode) == doctest::Approx(expectedVCap).epsilon(1e-12));
}

// ---------------------------------------------------------------------------
// 2. Statelessness (US3.2; FR-011): two successive refresh()+solve() calls
// with an IDENTICAL (netlist, companions) pair must produce bit-identical
// node voltages -- MNA is a pure function of its inputs, carrying no residual
// state between solves. Reuses case 1's RC netlist shape. This holds
// regardless of whether the companion is actually stamped: purity is a
// property of whether refresh() reads state besides (nl, comps, the fixed
// plan), not of what the companion arm does with it.
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
    REQUIRE_NOTHROW(assembler.plan(nl, sys));

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

TEST_CASE("mna-assembler: diode reduced to a Norton companion matches the linearized-operating-point solution (US3.3)") {
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

    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    REQUIRE(sys.solve());

    CHECK(sys.nodeVoltage(vinNode) == doctest::Approx(Vin).epsilon(1e-12));

    const double expectedVDiode = (Ieq + Vin / R) / (1.0 / R + Geq);
    CHECK(sys.nodeVoltage(diodeNode) == doctest::Approx(expectedVDiode).epsilon(1e-12));
}

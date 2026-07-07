#include <doctest/doctest.h>

#include "core/mna-test-support.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <cmath>

// T015/T016/T017 -- User Story 5 (spec.md; FR-015/FR-022; SC-004;
// research.md D1/D7; quickstart.md scenarios 4-5). Proves the MNA primitive
// (a) reports ill-posed systems via the `false` not-solved signal -- never a
// throw on the solve path, never a NaN leak, never a silent gmin fallback --
// and (b) honors the physical invariants (passivity, reciprocity,
// monotonicity) an assembled circuit must satisfy.
//
// Ill-posed cases 1-2 below and the well-posedness case 3 map to spec.md
// US5 acceptance scenarios 1-3. Case 1 stamps `MnaSystem` (Layer 1) DIRECTLY,
// bypassing `Netlist`/`MnaAssembler`: a genuinely floating subgraph (no
// conductive path to ground at all) is already rejected by
// `Netlist::prepare()`'s connectivity pre-filter (netlist.h "floating node"
// check), so exercising it through the full pipeline would only prove the
// PLAN-time throw contract, not the engine's own singular-pivot detection
// (FR-004) -- the two are deliberately independent gates
// (netlist.h: "this nodal scan is only a fast, conservative PRE-FILTER ...
// the AUTHORITATIVE well-posedness gate is the non-singularity of the
// augmented system at solve time"). Case 2 (redundant nullor) is the
// canonical example of a topology that PASSES that pre-filter (every node
// has a conductive path to ground) yet is singular at the augmented-matrix
// level, so it is driven through the real `Netlist` -> `MnaAssembler` ->
// `MnaSystem` pipeline, directly exercising T016.

using acfx::CurrentSource;
using acfx::Netlist;
using acfx::NodeId;
using acfx::OpAmp;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using mna_test::NoCompanions;

// ---------------------------------------------------------------------------
// 1. Floating subgraph (US5-AS1, SC-004): two non-ground nodes joined by a
// single resistor, with NO stamp ever touching ground. Layer-1 stamps only
// (MnaSystem directly) -- see file header for why this bypasses Netlist.
//
// Reduced 2x2 conductance block for local nodes {node1, node2} with
// conductance g = 1/R and no branch border:
//   [ g  -g ]   [V1]   [0]
//   [-g   g ] * [V2] = [0]
// This matrix is exactly rank-1 (row2 = -row1): after eliminating column 0,
// the remaining pivot candidate is identically 0, which is < the relative
// threshold (kRelEps * matScale) for ANY g > 0 -- a floating pair is singular
// regardless of scale, so solve() MUST return false.
// ---------------------------------------------------------------------------

TEST_CASE("mna-invariants: floating subgraph reports not-solved with no throw and no NaN (US5.1/SC-004)") {
    constexpr int kMaxNodes = 3;    // ground + node1 + node2
    constexpr int kMaxBranches = 0;  // no source/op-amp branch at all

    const double R = 1000.0;
    const double g = 1.0 / R;

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    const NodeId node1 = 1;
    const NodeId node2 = 2;

    sys.stampConductance(node1, node2, g);

    bool solved = true;
    CHECK_NOTHROW(solved = sys.solve());
    CHECK_FALSE(solved);

    // No silent gmin, no NaN leak (D7/FR-015): every readable output stays
    // finite (in fact exactly 0.0, since solve() zeroes x_ before failing).
    CHECK(std::isfinite(sys.nodeVoltage(node1)));
    CHECK(std::isfinite(sys.nodeVoltage(node2)));
    CHECK(sys.nodeVoltage(node1) == doctest::Approx(0.0));
    CHECK(sys.nodeVoltage(node2) == doctest::Approx(0.0));
}

// ---------------------------------------------------------------------------
// 2. Redundant nullor: two ideal op-amps sharing the SAME input pair
// (inPlus = ground, inMinus = nodeB) but driving DIFFERENT output nodes
// (out1, out2) -- an over-constrained singular border (US5.1, "redundant
// nullor"). Every node has a conductive path to ground (Rin, Rf1, Rf2 all
// count per netlist.h::contributesConductivePath), so Netlist::prepare()
// passes; the redundancy only shows up at the augmented-matrix level,
// exercising the assembler's real plan()/refresh() path (T016).
//
//   Vin --(grounded VoltageSource)-- vinNode --Rin-- nodeB
//   OpAmp1{inPlus=gnd, inMinus=nodeB, out=out1}, out1 --Rf1-- nodeB
//   OpAmp2{inPlus=gnd, inMinus=nodeB, out=out2}, out2 --Rf2-- nodeB
//
// Each op-amp's nullator row is `-1 * V(nodeB) = 0` (inPlus is ground and
// drops out); BOTH branches stamp this IDENTICAL row (same coefficients,
// same RHS) -- the two constraint rows are linearly dependent, so the
// augmented matrix is rank-deficient (singular) regardless of Rin/Rf1/Rf2.
// Physically: with nodeB pinned to 0 V by either nullator alone, nodeB's own
// KCL row collapses to ONE equation in the TWO unknowns V(out1), V(out2)
// (-Vin/Rin - V(out1)/Rf1 - V(out2)/Rf2 = 0) -- underdetermined, the textbook
// signature of a redundant constraint.
// ---------------------------------------------------------------------------

TEST_CASE("mna-invariants: redundant nullor (duplicated op-amp constraint) reports not-solved (US5.1/SC-004)") {
    constexpr int kMaxNodes = 5;       // ground + vinNode + nodeB + out1 + out2
    constexpr int kMaxComponents = 6;  // VS, Rin, Rf1, Rf2, OpAmp1, OpAmp2
    constexpr int kMaxBranches = 3;    // VS branch + 2 op-amp branches

    const double Vin = 1.0;
    const double Rin = 1000.0;
    const double Rf1 = 2000.0;
    const double Rf2 = 3000.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId nodeB = nl.addNode();
    const NodeId out1 = nl.addNode();
    const NodeId out2 = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, Vin});
    nl.add(Resistor{vinNode, nodeB, Rin});
    nl.add(Resistor{out1, nodeB, Rf1});
    nl.add(Resistor{out2, nodeB, Rf2});
    nl.add(OpAmp{kGround, nodeB, out1});
    nl.add(OpAmp{kGround, nodeB, out2});
    CHECK_NOTHROW(nl.prepare());  // passes: every node has a conductive path.

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    CHECK_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, comps, sys);

    bool solved = true;
    CHECK_NOTHROW(solved = sys.solve());
    CHECK_FALSE(solved);

    CHECK(std::isfinite(sys.nodeVoltage(vinNode)));
    CHECK(std::isfinite(sys.nodeVoltage(nodeB)));
    CHECK(std::isfinite(sys.nodeVoltage(out1)));
    CHECK(std::isfinite(sys.nodeVoltage(out2)));
}

// ---------------------------------------------------------------------------
// 3. Poorly-scaled but WELL-POSED system (US5.3/SC-004, research D1): a
// resistive divider built from microsiemens-scale conductances (R1=1e6 ohm,
// R2=2e6 ohm -> G ~ 1e-6 S) beside the voltage source's unit-magnitude branch
// incidence entries. The matrix scale is dominated by the branch's +/-1
// entries, so the smallest pivot encountered is ~1.5e-6 (G1+G2) -- far above
// the RELATIVE threshold kRelEps*matScale ~ 1e-12*1 = 1e-12, but potentially
// at or near an ABSOLUTE floor a naive implementation might use. The
// relative threshold (D1) must not falsely reject this: solve() == true and
// V(node2) == Vin*R2/(R1+R2) exactly, same closed form as the unscaled
// divider (mna-assembler-test.cpp US1.1).
// ---------------------------------------------------------------------------

TEST_CASE("mna-invariants: poorly-scaled but well-posed system solves correctly, not falsely rejected (US5.3)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;

    const double Vin = 1.0;
    const double R1 = 1.0e6;  // 1 microsiemens
    const double R2 = 2.0e6;  // 0.5 microsiemens

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

    bool solved = false;
    CHECK_NOTHROW(solved = sys.solve());
    CHECK(solved);

    CHECK(std::isfinite(sys.nodeVoltage(node1)));
    CHECK(std::isfinite(sys.nodeVoltage(node2)));
    CHECK(sys.nodeVoltage(node1) == doctest::Approx(Vin).epsilon(1e-9));
    const double expected = Vin * R2 / (R1 + R2);
    CHECK(sys.nodeVoltage(node2) == doctest::Approx(expected).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// 4. Passivity (FR-022): a purely passive resistive network driven by one
// ideal voltage source -- power delivered by the source must equal total
// power dissipated in the resistors (energy balance), computed ENTIRELY from
// the solved node voltages / branch current (no transcribed value):
//
//   Vin --(grounded VoltageSource)-- node1 --R1-- node2 --{R2a, R2b}-- gnd
//
// node2 has TWO resistors to ground in parallel (real current splitting).
// The source's branch current is defined p->n (node1 -> ground) through the
// branch's own internal path (mna-system.h stampBranchIncidence); node1's
// KCL row (touched only by R1 and the branch) gives
// I_branch = -(V(node1)-V(node2))/R1, so the current the source actually
// DELIVERS into the external network is -I_branch. Power delivered:
//   P_source = (V(node1) - 0) * (-I_branch)
// Power dissipated: sum of V_R^2/R over every resistor. A passive network
// conserves energy exactly, so these must match to double-precision (1e-9).
// ---------------------------------------------------------------------------

TEST_CASE("mna-invariants: passivity -- source power equals total dissipated power (FR-022)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;

    const double Vin = 5.0;
    const double R1 = 100.0;
    const double R2a = 200.0;
    const double R2b = 300.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R1});
    nl.add(Resistor{node2, kGround, R2a});
    nl.add(Resistor{node2, kGround, R2b});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NoCompanions comps;

    assembler.plan(nl, sys);
    assembler.refresh(nl, comps, sys);
    REQUIRE(sys.solve());

    const double v1 = sys.nodeVoltage(node1);
    const double v2 = sys.nodeVoltage(node2);
    constexpr int kSourceBranch = 0;
    const double iBranch = sys.branchCurrent(kSourceBranch);

    const double powerDelivered = v1 * (-iBranch);
    const double powerR1 = (v1 - v2) * (v1 - v2) / R1;
    const double powerR2a = v2 * v2 / R2a;
    const double powerR2b = v2 * v2 / R2b;
    const double totalDissipated = powerR1 + powerR2a + powerR2b;

    CHECK(powerDelivered == doctest::Approx(totalDissipated).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// 5. Reciprocity (FR-022): a passive resistive "bridge" two-port (terminals
// A and B, both referenced to ground through an internal node Mid):
//
//   A --R1-- Mid --R2-- B ,  Mid --R3-- gnd ,  B --R4-- gnd
//
// Drive a current Ia into A (B undriven) and read V(B); then drive the SAME
// Ia into B (A undriven) and read V(A). The reciprocity theorem for linear
// passive resistive networks demands these transfer voltages be equal.
//
// Closed-form check (conductances Ga=1/R1, Gb=1/R2, Gc=1/R3, Gd=1/R4; with
// R1=1000, R2=1500, R3=3000, R4=2000 -> common factor k=1/6000 gives
// Ga=6k, Gb=4k, Gc=2k, Gd=3k):
//   Case 1 (Ia at A): eliminating V(Mid) between the Mid- and B-rows gives
//     V(Mid) = (7/4)*V(B); substituting into the A/Mid pair with
//     V(A)-V(Mid) = Ia/Ga = 1.0 (since Ia=Ga=0.001) yields V(B) = 12/13.
//   Case 2 (Ia at B): symmetric elimination (V(A)=V(Mid) since A is
//     otherwise undriven) yields V(A) = 12/13 -- the SAME value.
// ---------------------------------------------------------------------------

TEST_CASE("mna-invariants: reciprocity of a passive resistive two-port (FR-022)") {
    constexpr int kMaxNodes = 4;  // ground + A + Mid + B
    constexpr int kMaxComponents = 5;
    constexpr int kMaxBranches = 0;  // CurrentSource needs no branch

    const double R1 = 1000.0;
    const double R2 = 1500.0;
    const double R3 = 3000.0;
    const double R4 = 2000.0;
    const double Ia = 0.001;
    const double kExpectedTransfer = 12.0 / 13.0;

    double vB_case1;
    {
        Netlist<kMaxNodes, kMaxComponents> nl;
        const NodeId a = nl.addNode();
        const NodeId mid = nl.addNode();
        const NodeId b = nl.addNode();
        nl.add(CurrentSource{a, kGround, Ia});
        nl.add(Resistor{a, mid, R1});
        nl.add(Resistor{mid, b, R2});
        nl.add(Resistor{mid, kGround, R3});
        nl.add(Resistor{b, kGround, R4});
        nl.prepare();

        MnaSystem<kMaxNodes, kMaxBranches> sys;
        MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
        NoCompanions comps;
        assembler.plan(nl, sys);
        assembler.refresh(nl, comps, sys);
        REQUIRE(sys.solve());
        vB_case1 = sys.nodeVoltage(b);
    }

    double vA_case2;
    {
        Netlist<kMaxNodes, kMaxComponents> nl;
        const NodeId a = nl.addNode();
        const NodeId mid = nl.addNode();
        const NodeId b = nl.addNode();
        nl.add(CurrentSource{b, kGround, Ia});
        nl.add(Resistor{a, mid, R1});
        nl.add(Resistor{mid, b, R2});
        nl.add(Resistor{mid, kGround, R3});
        nl.add(Resistor{b, kGround, R4});
        nl.prepare();

        MnaSystem<kMaxNodes, kMaxBranches> sys;
        MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
        NoCompanions comps;
        assembler.plan(nl, sys);
        assembler.refresh(nl, comps, sys);
        REQUIRE(sys.solve());
        vA_case2 = sys.nodeVoltage(a);
    }

    // The decisive reciprocity assertion: two INDEPENDENT solves agree.
    CHECK(vB_case1 == doctest::Approx(vA_case2).epsilon(1e-9));
    // And both match the hand-derived closed form (auditability).
    CHECK(vB_case1 == doctest::Approx(kExpectedTransfer).epsilon(1e-9));
    CHECK(vA_case2 == doctest::Approx(kExpectedTransfer).epsilon(1e-9));
}

// ---------------------------------------------------------------------------
// 6. Monotonicity (FR-022): in a resistive divider `Vin-R1-node2-R2-gnd`,
// V(node2) = Vin*R2/(R1+R2) is strictly increasing in R2 for fixed Vin>0,
// R1>0. Sweep R2 across increasing magnitudes and assert the solved V(node2)
// sequence is strictly increasing -- a fresh Netlist/MnaSystem per sample
// (Netlist has no value-mutation API once a component is added).
// ---------------------------------------------------------------------------

TEST_CASE("mna-invariants: increasing divider load resistance monotonically increases output voltage (FR-022)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;

    const double Vin = 1.0;
    const double R1 = 1000.0;
    const double loadSweep[] = {100.0, 1000.0, 10000.0, 100000.0};

    double previousV2 = -1.0;
    bool first = true;
    for (const double R2 : loadSweep) {
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
        assembler.plan(nl, sys);
        assembler.refresh(nl, comps, sys);
        REQUIRE(sys.solve());

        const double v2 = sys.nodeVoltage(node2);
        CHECK(std::isfinite(v2));
        if (!first) {
            CHECK(v2 > previousV2);
        }
        previousV2 = v2;
        first = false;
    }
}

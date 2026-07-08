#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "core/mna-test-support.h"
#include "labs/component-abstractions/solver/linear-solver.h"
#include "labs/opamp-stages/solver/opamp-stage-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// T018/T019 -- US6/SC-002: the equivalence oracle. Proves MnaSystem +
// MnaAssembler is a FAITHFUL SUPERSET of the two pre-existing lab solvers by
// building ONE Netlist per topology and driving BOTH the lab solver and the
// MNA primitive over it, then comparing node voltages (and, for the nullor
// cases, branch currents) to <= 1e-12. This is the safety net a later lab
// migration onto MNA leans on (spec.md User Story 6) -- it is not required
// for MNA's own correctness (US1-US3 already establish that via closed
// forms), only for proving it reproduces what the labs already do.
//
// T018 compares against LinearSolver (component-abstractions lab). To keep
// the comparison EXACT and companion-free, every topology here is purely
// resistive + current-source + voltage-source (no reactive elements, so MNA
// needs no companions -- refresh() is driven with mna_test::NoCompanions,
// which is never actually consulted since no Capacitor/Inductor/Diode is
// present). LinearSolver throws on a FLOATING (non-grounded) ideal voltage
// source (see linear-solver.h imposeVoltageSources) -- that capability is
// MNA-only and already covered by US1's floating-source test in
// mna-assembler-test.cpp, so every topology below keeps every VoltageSource
// grounded, exactly as LinearSolver requires.
//
// T019 compares against NullorSolver (opamp-stages lab). A key BRANCH-INDEX
// difference between the two solvers under test must be bridged by the test,
// not papered over:
//   - NullorSolver imposes VoltageSource by FIXED-NODE REDUCTION (like
//     LinearSolver) and borders the system with ONE row/column PER OP-AMP
//     ONLY. Its branchCurrent(opampIndex) is indexed 0.. by OP-AMP NETLIST
//     ORDER ALONE (opamp-stage-solver.h "op-amp #k (netlist order)").
//   - MnaAssembler instead branch-augments EVERY VoltageSource too (it never
//     does fixed-node reduction -- mna-assembler.h's element->stamp table).
//     Its branchCurrent(k) is indexed 0.. by the addBranch() call order,
//     which walks the netlist and allocates a branch for BOTH VoltageSource
//     AND OpAmp elements, interleaved in netlist order.
// So for an op-amp preceded by N branch-eligible elements (VoltageSources and
// OpAmps together) in netlist order, its NullorSolver opampIndex is (number
// of OpAmps strictly before it), while its MNA branch k is (number of
// VoltageSources-or-OpAmps at-or-before it, minus one). Each test case below
// names both indices explicitly and explains the offset in a comment at the
// point of use -- never inferred implicitly.

using acfx::CurrentSource;
using acfx::Netlist;
using acfx::NodeId;
using acfx::OpAmp;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::labs::component_abstractions::LinearSolver;
using acfx::labs::opamp_stages::NullorSolver;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using mna_test::NoCompanions;

namespace {
// Comparison tolerance shared by every case (spec.md SC-002 / US6 acceptance
// scenarios 1-2): "agree to ~1e-12". MAGNITUDE-SCALED (govern finding): a bare
// absolute 1e-12 floor is near machine epsilon for ~20V-scale quantities and
// is fragile across compilers/FMA contraction, so every comparison bounds the
// absolute difference by kTol times the reference value's own magnitude
// (floored at 1.0 so near-zero references still get an absolute 1e-12 floor).
constexpr double kTol = 1e-12;

double scaledTol(double referenceValue) {
    return kTol * std::max(1.0, std::fabs(referenceValue));
}
}  // namespace

// ===========================================================================
// T018 (SC-002 acceptance 1) -- vs LinearSolver. Three distinct resistive/
// source topologies; dt is passed as a representative positive value since
// it is irrelevant to a purely resistive solve (no companion is ever
// stamped by either solver here).
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Resistive divider (mirrors circuit-solver-test.cpp's canonical case):
// Vin --(grounded VoltageSource)-- node1 --R1-- node2 --R2-- ground.
// ---------------------------------------------------------------------------

TEST_CASE("mna-equivalence: resistive divider matches LinearSolver to 1e-12 (US6/SC-002)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;

    const double Vin = 10.0;
    const double R1 = 1000.0;
    const double R2 = 2000.0;
    const double dt = 1.0e-3;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId node1 = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{node1, kGround, Vin});
    nl.add(Resistor{node1, node2, R1});
    nl.add(Resistor{node2, kGround, R2});
    nl.prepare();

    LinearSolver<kMaxNodes, kMaxComponents> linear;
    CHECK_NOTHROW(linear.solve(nl, dt));

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, NoCompanions{}, sys);
    REQUIRE(sys.solve());

    const double diff1 = std::fabs(linear.voltage(node1) - sys.nodeVoltage(node1));
    const double diff2 = std::fabs(linear.voltage(node2) - sys.nodeVoltage(node2));
    CHECK_MESSAGE(diff1 <= scaledTol(linear.voltage(node1)), "node1: LinearSolver=", linear.voltage(node1),
                  " MNA=", sys.nodeVoltage(node1), " diff=", diff1);
    CHECK_MESSAGE(diff2 <= scaledTol(linear.voltage(node2)), "node2: LinearSolver=", linear.voltage(node2),
                  " MNA=", sys.nodeVoltage(node2), " diff=", diff2);
}

// ---------------------------------------------------------------------------
// 2. Multi-node resistor ladder driven by a CurrentSource -- no VoltageSource
// at all, so MNA allocates ZERO branches here (kMaxBranches = 0) exactly as
// mna-assembler-test.cpp's current-source case does; pure conductance +
// RHS-current stamps on both sides.
//
//   CurrentSource(I) --> n1 --R1(to gnd)   n1 --R2-- n2 --R3(to gnd)
//                        n2 --R4-- n3 --R5(to gnd)
// ---------------------------------------------------------------------------

TEST_CASE("mna-equivalence: current-driven resistor ladder matches LinearSolver to 1e-12 (US6/SC-002)") {
    constexpr int kMaxNodes = 5;
    constexpr int kMaxComponents = 6;
    constexpr int kMaxBranches = 0;

    const double I = 2.0e-3;
    const double R1 = 1000.0;
    const double R2 = 2000.0;
    const double R3 = 1500.0;
    const double R4 = 3000.0;
    const double R5 = 2200.0;
    const double dt = 1.0e-3;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    const NodeId n3 = nl.addNode();

    nl.add(CurrentSource{n1, kGround, I});
    nl.add(Resistor{n1, kGround, R1});
    nl.add(Resistor{n1, n2, R2});
    nl.add(Resistor{n2, kGround, R3});
    nl.add(Resistor{n2, n3, R4});
    nl.add(Resistor{n3, kGround, R5});
    nl.prepare();

    LinearSolver<kMaxNodes, kMaxComponents> linear;
    CHECK_NOTHROW(linear.solve(nl, dt));

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, NoCompanions{}, sys);
    REQUIRE(sys.solve());

    const NodeId nodes[] = {n1, n2, n3};
    // Track the worst diff/tolerance RATIO (not the raw diff) across nodes of
    // differing magnitude, so the aggregate check below stays meaningful under
    // per-node magnitude-scaled tolerances.
    double maxRatio = 0.0;
    for (const NodeId n : nodes) {
        const double diff = std::fabs(linear.voltage(n) - sys.nodeVoltage(n));
        const double tol = scaledTol(linear.voltage(n));
        maxRatio = std::max(maxRatio, diff / tol);
        CHECK_MESSAGE(diff <= tol, "node ", n, ": LinearSolver=", linear.voltage(n),
                      " MNA=", sys.nodeVoltage(n), " diff=", diff, " tol=", tol);
    }
    CHECK(maxRatio <= 1.0);
}

// ---------------------------------------------------------------------------
// 3. Resistive bridge with TWO independent grounded voltage sources (both
// pinned nodes, per LinearSolver's grounded-only fixed-node reduction) and
// two free unknown nodes coupled by a resistor mesh -- exercises MNA
// allocating MULTIPLE branches (kMaxBranches = 2) against LinearSolver
// pinning multiple nodes in one solve.
//
//   n1 --(VoltageSource V1)-- gnd        n2 --(VoltageSource V2)-- gnd
//   n1 --R1-- n3 --R2-- gnd              n3 --R3-- n4
//   n2 --R4-- n4 --R5-- gnd
// ---------------------------------------------------------------------------

TEST_CASE("mna-equivalence: two-source resistive bridge matches LinearSolver to 1e-12 (US6/SC-002)") {
    constexpr int kMaxNodes = 6;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 2;

    const double V1 = 10.0;
    const double V2 = -5.0;
    const double R1 = 1000.0;
    const double R2 = 2000.0;
    const double R3 = 1500.0;
    const double R4 = 3000.0;
    const double R5 = 2200.0;
    const double dt = 1.0e-3;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    const NodeId n3 = nl.addNode();
    const NodeId n4 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, V1});
    nl.add(VoltageSource{n2, kGround, V2});
    nl.add(Resistor{n1, n3, R1});
    nl.add(Resistor{n3, kGround, R2});
    nl.add(Resistor{n3, n4, R3});
    nl.add(Resistor{n2, n4, R4});
    nl.add(Resistor{n4, kGround, R5});
    nl.prepare();

    LinearSolver<kMaxNodes, kMaxComponents> linear;
    CHECK_NOTHROW(linear.solve(nl, dt));

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, NoCompanions{}, sys);
    REQUIRE(sys.solve());

    const NodeId nodes[] = {n1, n2, n3, n4};
    double maxRatio = 0.0;
    for (const NodeId n : nodes) {
        const double diff = std::fabs(linear.voltage(n) - sys.nodeVoltage(n));
        const double tol = scaledTol(linear.voltage(n));
        maxRatio = std::max(maxRatio, diff / tol);
        CHECK_MESSAGE(diff <= tol, "node ", n, ": LinearSolver=", linear.voltage(n),
                      " MNA=", sys.nodeVoltage(n), " diff=", diff, " tol=", tol);
    }
    CHECK(maxRatio <= 1.0);
}

// ===========================================================================
// T019 (SC-002 acceptance 2) -- vs NullorSolver. Resistor + source + ideal
// op-amp (nullor) topologies. See the file header for the branch-index
// mapping between NullorSolver::branchCurrent(opampIndex) (op-amp netlist
// order only) and MnaSystem::branchCurrent(k) (VoltageSource-and-OpAmp
// netlist order, interleaved).
// ===========================================================================

// ---------------------------------------------------------------------------
// 1. Non-inverting amplifier: vinNode --(grounded VoltageSource)--; OpAmp
// {inPlus=vinNode, inMinus=inMinusNode, out=outNode}; Rg from inMinusNode to
// ground; Rf feedback from outNode to inMinusNode. Netlist order: [0]
// VoltageSource, [1] OpAmp, [2] Resistor(Rg), [3] Resistor(Rf). The OpAmp is
// netlist-order OpAmp #0 (NullorSolver opampIndex 0) but is the SECOND
// branch-eligible element after the VoltageSource (MNA branch k = 1).
// Analytic gain: 1 + Rf/Rg.
// ---------------------------------------------------------------------------

TEST_CASE("mna-equivalence: non-inverting amplifier matches NullorSolver voltages+current to 1e-12 (US6/SC-002)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxOpAmps = 1;
    constexpr int kMaxBranches = 2;  // 1 VoltageSource branch + 1 OpAmp branch

    const double Vin = 1.5;
    const double Rg = 2200.0;
    const double Rf = 8200.0;
    const double dt = 1.0e-5;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId inMinusNode = nl.addNode();
    const NodeId outNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, Vin});               // MNA branch 0
    nl.add(OpAmp{vinNode, inMinusNode, outNode});                // MNA branch 1, nullor opampIndex 0
    nl.add(Resistor{inMinusNode, kGround, Rg});
    nl.add(Resistor{outNode, inMinusNode, Rf});
    nl.prepare();

    NullorSolver<kMaxNodes, kMaxComponents, kMaxOpAmps> nullor;
    nullor.reset();
    nullor.solve(nl, dt);

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, NoCompanions{}, sys);
    REQUIRE(sys.solve());

    const NodeId nodes[] = {vinNode, inMinusNode, outNode};
    double maxVRatio = 0.0;
    for (const NodeId n : nodes) {
        const double diff = std::fabs(nullor.voltage(n) - sys.nodeVoltage(n));
        const double tol = scaledTol(nullor.voltage(n));
        maxVRatio = std::max(maxVRatio, diff / tol);
        CHECK_MESSAGE(diff <= tol, "node ", n, ": NullorSolver=", nullor.voltage(n),
                      " MNA=", sys.nodeVoltage(n), " diff=", diff, " tol=", tol);
    }
    CHECK(maxVRatio <= 1.0);

    // Sanity: matches the analytic non-inverting gain (independent of MNA).
    const double analyticGain = 1.0 + Rf / Rg;
    CHECK(std::fabs(nullor.voltage(outNode) - analyticGain * Vin) < 1e-9);

    // Branch current: NullorSolver opampIndex 0 vs MNA branch k = 1 (see
    // mapping comment above).
    const double iNullor = nullor.branchCurrent(0);
    const double iMna = sys.branchCurrent(1);
    const double iDiff = std::fabs(iNullor - iMna);
    CHECK_MESSAGE(iDiff <= scaledTol(iNullor), "opamp branch current: NullorSolver=", iNullor,
                  " MNA=", iMna, " diff=", iDiff);
}

// ---------------------------------------------------------------------------
// 2. Inverting amplifier: vinNode --(grounded VoltageSource)-- --Rin--
// inMinusNode; OpAmp{inPlus=ground, inMinus=inMinusNode, out=outNode}; Rf
// feedback from outNode to inMinusNode. Netlist order: [0] VoltageSource,
// [1] Resistor(Rin), [2] OpAmp, [3] Resistor(Rf). Same offset as case 1: the
// OpAmp is nullor opampIndex 0 but MNA branch k = 1 (branch 0 is the
// VoltageSource). Analytic gain: -Rf/Rin.
// ---------------------------------------------------------------------------

TEST_CASE("mna-equivalence: inverting amplifier matches NullorSolver voltages+current to 1e-12 (US6/SC-002)") {
    constexpr int kMaxNodes = 4;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxOpAmps = 1;
    constexpr int kMaxBranches = 2;

    const double Vin = 2.0;
    const double Rin = 4700.0;
    const double Rf = 47000.0;
    const double dt = 1.0e-5;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId inMinusNode = nl.addNode();
    const NodeId outNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, Vin});               // MNA branch 0
    nl.add(Resistor{vinNode, inMinusNode, Rin});
    nl.add(OpAmp{kGround, inMinusNode, outNode});                // MNA branch 1, nullor opampIndex 0
    nl.add(Resistor{outNode, inMinusNode, Rf});
    nl.prepare();

    NullorSolver<kMaxNodes, kMaxComponents, kMaxOpAmps> nullor;
    nullor.reset();
    nullor.solve(nl, dt);

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, NoCompanions{}, sys);
    REQUIRE(sys.solve());

    const NodeId nodes[] = {vinNode, inMinusNode, outNode};
    double maxVRatio = 0.0;
    for (const NodeId n : nodes) {
        const double diff = std::fabs(nullor.voltage(n) - sys.nodeVoltage(n));
        const double tol = scaledTol(nullor.voltage(n));
        maxVRatio = std::max(maxVRatio, diff / tol);
        CHECK_MESSAGE(diff <= tol, "node ", n, ": NullorSolver=", nullor.voltage(n),
                      " MNA=", sys.nodeVoltage(n), " diff=", diff, " tol=", tol);
    }
    CHECK(maxVRatio <= 1.0);

    const double analyticGain = -Rf / Rin;
    CHECK(std::fabs(nullor.voltage(outNode) - analyticGain * Vin) < 1e-9);

    const double iNullor = nullor.branchCurrent(0);
    const double iMna = sys.branchCurrent(1);
    const double iDiff = std::fabs(iNullor - iMna);
    CHECK_MESSAGE(iDiff <= scaledTol(iNullor), "opamp branch current: NullorSolver=", iNullor,
                  " MNA=", iMna, " diff=", iDiff);
}

// ---------------------------------------------------------------------------
// 3. Two INDEPENDENT op-amp stages (MaxOpAmps = 2), each a current-driven
// inverting integrator-less gain stage with NO VoltageSource anywhere in the
// netlist -- so MNA's branch-eligible-element order and NullorSolver's
// op-amp-only order COINCIDE (both are 0, 1 in netlist order), the simple
// case that contrasts with the offset in cases 1-2.
//
//   Stage A: a --R(1k to gnd)--; OpAmp{gnd, a, b}; R(1k) b<->a;
//            CurrentSource(I1) into a.
//   Stage B: c --R(1k to gnd)--; OpAmp{gnd, c, e}; R(1k) e<->c;
//            CurrentSource(I2) into c.
// The nullator pins V(a)=V(c)=0 (inPlus grounded); the R-to-ground at a/c
// carries zero current (both ends at 0V) but supplies the netlist's
// mandatory conductive-path-to-ground for a/c (OpAmp/CurrentSource do not
// count, netlist.h::contributesConductivePath). KCL at a: I1 (injected) +
// (b-a)/R = 0 => b = -I1*R. Likewise e = -I2*R.
// ---------------------------------------------------------------------------

TEST_CASE("mna-equivalence: two independent op-amp stages match NullorSolver voltages+currents to 1e-12 (US6/SC-002)") {
    constexpr int kMaxNodes = 5;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxOpAmps = 2;
    constexpr int kMaxBranches = 2;  // 2 OpAmp branches, no VoltageSource

    const double R = 1000.0;
    const double I1 = 1.0e-3;
    const double I2 = 2.0e-3;
    const double dt = 1.0e-5;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId a = nl.addNode();
    const NodeId b = nl.addNode();
    const NodeId c = nl.addNode();
    const NodeId e = nl.addNode();

    nl.add(Resistor{a, kGround, R});
    nl.add(OpAmp{kGround, a, b});          // MNA branch 0, nullor opampIndex 0
    nl.add(Resistor{b, a, R});
    nl.add(CurrentSource{a, kGround, I1});
    nl.add(OpAmp{kGround, c, e});          // MNA branch 1, nullor opampIndex 1
    nl.add(Resistor{c, kGround, R});
    nl.add(Resistor{e, c, R});
    nl.add(CurrentSource{c, kGround, I2});
    nl.prepare();

    NullorSolver<kMaxNodes, kMaxComponents, kMaxOpAmps> nullor;
    nullor.reset();
    nullor.solve(nl, dt);

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    REQUIRE_NOTHROW(assembler.plan(nl, sys));
    assembler.refresh(nl, NoCompanions{}, sys);
    REQUIRE(sys.solve());

    const NodeId nodes[] = {a, b, c, e};
    double maxVRatio = 0.0;
    for (const NodeId n : nodes) {
        const double diff = std::fabs(nullor.voltage(n) - sys.nodeVoltage(n));
        const double tol = scaledTol(nullor.voltage(n));
        maxVRatio = std::max(maxVRatio, diff / tol);
        CHECK_MESSAGE(diff <= tol, "node ", n, ": NullorSolver=", nullor.voltage(n),
                      " MNA=", sys.nodeVoltage(n), " diff=", diff, " tol=", tol);
    }
    CHECK(maxVRatio <= 1.0);

    // Sanity: matches the closed-form KCL result derived above.
    CHECK(std::fabs(nullor.voltage(b) - (-I1 * R)) < 1e-9);
    CHECK(std::fabs(nullor.voltage(e) - (-I2 * R)) < 1e-9);

    // Branch currents: no VoltageSource in this netlist, so NullorSolver's
    // op-amp-only order and MNA's branch-allocation order coincide (both
    // 0, 1 in netlist order) -- no offset here, unlike cases 1-2.
    const double i0Nullor = nullor.branchCurrent(0);
    const double i0Mna = sys.branchCurrent(0);
    const double i0Diff = std::fabs(i0Nullor - i0Mna);
    CHECK_MESSAGE(i0Diff <= scaledTol(i0Nullor), "stage A branch current: NullorSolver=", i0Nullor,
                  " MNA=", i0Mna, " diff=", i0Diff);

    const double i1Nullor = nullor.branchCurrent(1);
    const double i1Mna = sys.branchCurrent(1);
    const double i1Diff = std::fabs(i1Nullor - i1Mna);
    CHECK_MESSAGE(i1Diff <= scaledTol(i1Nullor), "stage B branch current: NullorSolver=", i1Nullor,
                  " MNA=", i1Mna, " diff=", i1Diff);
}

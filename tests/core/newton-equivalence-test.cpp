#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/node.h"

#include "labs/diode-clippers/solver/transient-clipper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// newton-equivalence-test.cpp — T018/US7, SC-002: the lab-equivalence oracle.
//
// PROVES that the production primitive acfx::newton::NewtonSolver reproduces
// the TRUSTED lab solver's (acfx::labs::diode_clippers::TransientClipper)
// converged node voltages on the diode-clipper core, to solver tolerance. The
// lab solver is the reference for this topology; matching it de-risks the
// eventual migration of lab-validated behavior onto the primitive.
//
// TOPOLOGY: the resistive diode-clipper CORE -- Vin (ideal source) -> series
// R -> port node n2, with a matched antiparallel Diode pair n2<->ground (the
// same "ref" shape diode-clipper-transient-test.cpp cross-checks the
// TransientClipper against the static NewtonClipper with). Deliberately NO
// reactive element: with no capacitor/inductor, the comparison is a clean
// operating-point match independent of time-stepping -- a single
// TransientClipper::step() (any dt) and a single NewtonSolver::solve() both
// converge directly to the DC operating point, so there is no settling
// transient to launder a disagreement.
//
// This is the ONE place in tests/core a core/labs/ include is allowed: the
// primitive header (newton-solver.h) never includes labs; this equivalence
// test may, precisely to hold the primitive to the lab's answer.
//
// OP-AMP NOTE (task item 5): an op-amp-bearing topology (e.g. TS808) is
// intentionally NOT used here. NewtonSolver never stamps an OpAmp itself --
// it only linearizes Diode components into Norton companions (plan()'s scan
// is `acfx::isNonlinear`, which is diode-only); an OpAmp's ideal nullor
// constraint is stamped by the driven MnaAssembler once per Newton iteration,
// same as every other linear/companion element. The resistive antiparallel-
// diode clipper core below already exercises that full division of labor
// (MnaAssembler stamps R/V/diode-companion, NewtonSolver only drives the
// diode linearization loop) without pulling in the op-amp machinery, so it is
// the simplest solid oracle for this equivalence claim.

using acfx::Diode;
using acfx::DiodeSpec;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;
using acfx::newton::NewtonStatus;
using acfx::labs::diode_clippers::TransientClipper;

namespace {

constexpr int kMaxNodes = 4;
constexpr int kMaxComponents = 8;
constexpr int kMaxBranches = 4;

using ClipperNetlist = Netlist<kMaxNodes, kMaxComponents>;

// Zero/empty base companion supply (v1 DC case, matches newton-solver-test.cpp
// and the task brief): every non-diode index gets the trivial {0,0} companion.
// This netlist has no reactive element, so the base is never even consulted
// (ComposedCompanionSupply intercepts every diode index before reaching it).
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

// The assembled resistive clipper-core netlist plus its driven/port node
// handles -- IDENTICAL netlist type/shape fed to both solvers below.
struct ClipperCore {
    ClipperNetlist netlist;
    NodeId inNode;
    NodeId portNode;
};

// Vin -> series R -> port node n2; matched antiparallel Diode pair
// n2<->ground. No capacitor/inductor (deliberately -- see file header).
ClipperCore buildClipperCore(double vIn, double R, const DiodeSpec& d) {
    ClipperNetlist nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, vIn});
    nl.add(Resistor{n1, n2, R});
    nl.add(Diode{n2, kGround, d.Is, d.n, d.Vt});  // forward leg
    nl.add(Diode{kGround, n2, d.Is, d.n, d.Vt});  // antiparallel reverse leg
    nl.prepare();

    return ClipperCore{nl, n1, n2};
}

}  // namespace

TEST_SUITE("newton-equivalence") {

// T018 (SC-002, US7) -- across several DC drive levels (forward and reverse),
// the primitive NewtonSolver and the lab TransientClipper converge to the
// SAME port voltage on the identical resistive diode-clipper core, to within
// solver tolerance (1e-7 V).
TEST_CASE("newton-equivalence: NewtonSolver matches TransientClipper on the resistive diode-clipper core across DC drive levels") {
    const double R = 2200.0;
    const DiodeSpec d = acfx::siliconSignalDiode();

    // Forward and reverse drive levels, including a deep-clip case in each
    // direction and a couple of small/near-zero cases where neither diode is
    // strongly conducting.
    const std::array<double, 9> driveLevels{
        -3.0, -1.0, -0.5, -0.1, 0.0, 0.1, 0.5, 1.0, 3.0};

    double maxAbsDiff = 0.0;

    for (const double vIn : driveLevels) {
        const ClipperCore clip = buildClipperCore(vIn, R, d);

        // --- LAB solver: acfx::labs::diode_clippers::TransientClipper ---
        // No reactive element in this netlist, so a SINGLE step() (any dt)
        // converges to the DC operating point in one Newton loop -- the
        // "diodeCount_ == 0" fast path in step() does not apply here (there
        // are 2 diodes), but the reactive-history advance the loop-separation
        // comment describes is simply a no-op absent a capacitor/inductor.
        TransientClipper<kMaxNodes, kMaxComponents> labSolver;
        labSolver.reset();
        const auto labStatus = labSolver.step(clip.netlist, /*dt=*/1.0e-5);
        REQUIRE(labStatus.converged);
        const double vLab = labSolver.voltage(clip.portNode);

        // --- PRIMITIVE: acfx::newton::NewtonSolver, zero base companion ---
        MnaSystem<kMaxNodes, kMaxBranches> sys;
        MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
        NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> primitive;
        ZeroCompanionSupply base;

        CHECK_NOTHROW(primitive.plan(clip.netlist, assembler, sys));
        CHECK(primitive.diodeCount() == 2);

        const std::array<double, kMaxNodes> initialNodeVoltages{};
        NewtonStatus primStatus;
        CHECK_NOTHROW(primStatus = primitive.solve(clip.netlist, base,
                                                    initialNodeVoltages,
                                                    assembler, sys));
        REQUIRE(primStatus.converged);
        const double vPrim = sys.nodeVoltage(clip.portNode);

        // --- Agreement, to solver tolerance ---
        const double diff = std::fabs(vLab - vPrim);
        maxAbsDiff = std::max(maxAbsDiff, diff);
        CAPTURE(vIn);
        CAPTURE(vLab);
        CAPTURE(vPrim);
        CHECK(diff < 1e-7);
    }

    // Surface the actual observed agreement (the key number for the report).
    MESSAGE("newton-equivalence: max |vLab - vPrimitive| across drive levels = ", maxAbsDiff);
    CHECK(maxAbsDiff < 1e-7);
}

}  // TEST_SUITE("newton-equivalence")

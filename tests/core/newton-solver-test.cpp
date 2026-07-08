#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"

#include <array>
#include <stdexcept>

// T003 -- Newton-Raphson iteration primitive, RED step
// (specs/newton-iteration/contracts/newton-solver.md; data-model.md).
//
// NewtonSolver does not exist yet (T004 lands
// core/primitives/circuit/newton/newton-solver.h); this suite is the
// intentional RED state the constitution's WIP-commit clause blesses. Once
// T004 lands the header, these cases must compile and pass unmodified.
//
// Coverage:
//   1. Construction validation (C1) -- maxIterations < 1, voltageTol <= 0,
//      currentTol <= 0 each throw std::invalid_argument; a valid
//      construction does not throw.
//   2. plan() builds the diode topology (P2) -- diode component indices and
//      per-component is-diode mask match a hand-built netlist. The mask/
//      table are internal, so this asserts through a PROPOSED minimal read
//      surface -- diodeCount() / isDiodeComponent(int) -- that T004 must
//      implement (see NOTE below).
//   3. solve()-before-plan() guard (S10) -- calling solve() on a freshly
//      constructed (un-planned) solver returns NewtonStatus{converged=false,
//      iterations=0, ...} BY VALUE: deterministic, no throw, not UB.
//
// This file also carries the coupled multi-diode charter (section 4 below).
// Companion composition, statelessness/warm-start, and RT-safety zero-heap
// coverage live in newton-composition-test.cpp and newton-rtsafety-test.cpp
// respectively (split from this file to satisfy the Constitution VII
// per-file line budget).

using acfx::CurrentSource;
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

// Trivial base companion supply for solve() (contract's BaseCompanionSupply
// template parameter). Test case 3 returns before any companion is
// consulted (solve()-before-plan() guard fires first), but solve() still
// requires a supply argument to call.
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

}  // namespace

TEST_SUITE("newton-solver") {

// ---------------------------------------------------------------------------
// 1. Construction validation (contract C1): maxIterations < 1, voltageTol
// <= 0, or currentTol <= 0 each throw std::invalid_argument; a valid
// construction (including the all-defaults constructor) does not throw.
// Construction is off the hot path, so throwing here is the documented,
// correct behavior (C1).
// ---------------------------------------------------------------------------

TEST_CASE("newton-solver: construction rejects maxIterations < 1 (C1)") {
    CHECK_THROWS_AS((NewtonSolver<8, 8, 4>(/*maxIterations*/ 0, 1e-9, 1e-12)),
                     std::invalid_argument);
    CHECK_THROWS_AS((NewtonSolver<8, 8, 4>(/*maxIterations*/ -1, 1e-9, 1e-12)),
                     std::invalid_argument);
}

TEST_CASE("newton-solver: construction rejects voltageTol <= 0 (C1)") {
    CHECK_THROWS_AS((NewtonSolver<8, 8, 4>(50, /*voltageTol*/ 0.0, 1e-12)),
                     std::invalid_argument);
    CHECK_THROWS_AS((NewtonSolver<8, 8, 4>(50, /*voltageTol*/ -1e-9, 1e-12)),
                     std::invalid_argument);
}

TEST_CASE("newton-solver: construction rejects currentTol <= 0 (C1)") {
    CHECK_THROWS_AS((NewtonSolver<8, 8, 4>(50, 1e-9, /*currentTol*/ 0.0)),
                     std::invalid_argument);
    CHECK_THROWS_AS((NewtonSolver<8, 8, 4>(50, 1e-9, /*currentTol*/ -1e-12)),
                     std::invalid_argument);
}

TEST_CASE("newton-solver: a valid construction does not throw (C1)") {
    CHECK_NOTHROW((NewtonSolver<8, 8, 4>()));
    CHECK_NOTHROW((NewtonSolver<8, 8, 4>(50, 1e-9, 1e-12)));
    CHECK_NOTHROW((NewtonSolver<8, 8, 4>(1, 1e-6, 1e-6)));
}

// ---------------------------------------------------------------------------
// 2. plan() builds the diode topology (contract P2). Netlist has a diode at
// KNOWN component indices interleaved with linear components:
//   index 0: Resistor
//   index 1: Diode   <-- diode
//   index 2: Resistor
//   index 3: Diode   <-- diode
// After plan(), planned() must be true, and the solver's diode scan must
// match the netlist exactly: diodeCount() == 2, isDiodeComponent(1) and
// isDiodeComponent(3) true, isDiodeComponent(0) (and, by the same scan,
// index 2) false.
//
// NOTE (proposed read surface): the diode index table / is-diode mask are
// internal to NewtonSolver; the contract's public surface (planned() plus
// NewtonStatus) has no accessor for them. diodeCount() and
// isDiodeComponent(int) are PROPOSED minimal read accessors this test
// requires T004 to implement so the plan() scan is observable without
// exposing the mask/table storage itself.
// ---------------------------------------------------------------------------

TEST_CASE("newton-solver: plan() builds the diode topology from known component indices (P2)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    // Every non-ground node needs a conductive path to ground through an
    // R/L/C/V edge (Netlist::prepare()'s floating-node check does NOT count
    // diode edges), so both n1 and n2 are grounded through resistors while the
    // diodes sit at the known component indices 1 and 3.
    nl.add(Resistor{n1, kGround, 1000.0});                       // index 0
    nl.add(Diode{n1, n2, 1e-14, 1.0, 0.025852});                 // index 1
    nl.add(Resistor{n2, kGround, 1000.0});                       // index 2
    nl.add(Diode{n2, kGround, 1e-14, 1.0, 0.025852});            // index 3
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;

    CHECK_FALSE(solver.planned());

    solver.plan(nl, assembler, sys);

    CHECK(solver.planned());
    CHECK(solver.diodeCount() == 2);
    CHECK(solver.isDiodeComponent(1));
    CHECK(solver.isDiodeComponent(3));
    CHECK_FALSE(solver.isDiodeComponent(0));
    CHECK_FALSE(solver.isDiodeComponent(2));
}

// ---------------------------------------------------------------------------
// 3. solve()-before-plan() guard (contract S10). Calling solve() on a
// freshly constructed (un-planned) solver must return
// NewtonStatus{converged=false, iterations=0, ...} BY VALUE --
// deterministic, no throw, not UB -- distinguishable from a real
// non-converged solve (which always runs iterations >= 1, per S6).
// The netlist below is well-formed (a single diode) purely so plan()
// *would* succeed if called; the point of this case is that plan() is
// deliberately NOT called.
// ---------------------------------------------------------------------------

TEST_CASE("newton-solver: solve() before plan() returns converged=false, iterations=0 by value (S10)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    // n1 grounded through a resistor so prepare()'s floating-node check passes
    // (a lone diode-to-ground edge is not counted as a conductive path).
    nl.add(Resistor{n1, kGround, 1000.0});
    nl.add(Diode{n1, kGround, 1e-14, 1.0, 0.025852});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    std::array<double, kMaxNodes> initialNodeVoltages{};

    CHECK_FALSE(solver.planned());

    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    CHECK_FALSE(status.converged);
    CHECK(status.iterations == 0);
}

// ---------------------------------------------------------------------------
// 4. Coupled multi-diode networks (US2, T008/T009). The solve() loop
// linearizes EVERY diode recorded by plan() into a companion each iteration
// and composes ALL of them into exactly ONE MnaAssembler::refresh +
// MnaSystem::solve() call per iteration (S3) -- a single GLOBAL Newton step
// shared by every diode, never a per-diode sequence of smaller solves. There
// is NO charter refusal anywhere in this primitive for a netlist with >=2
// interacting nonlinearities -- contrast the labs' NewtonClipper
// (circuit-solver-test.cpp, "refuses a netlist with two independent ...
// diodes"), which deliberately throws; NewtonSolver never does.
//
// Each case below builds a VALID netlist (every non-ground node reaches
// ground through an R/L/C/V edge -- diode edges do not count, so grounding
// resistors are added where a diode-only path would otherwise float a node),
// with >=2 diodes at DISTINCT node pairs that are electrically COUPLED
// (they share nodes, so their companions interact within the same linear
// solve). Each is planned and solved from a zero initial guess, and each
// asserts the solve is NEVER REFUSED: plan()/solve() never throw
// (CHECK_NOTHROW), and solve() always returns a NewtonStatus BY VALUE --
// either converged (with residuals) or an honest converged == false, never a
// silent partial solve. diodeCount() is checked against the exact diode
// count added, proving every diode in the netlist is tracked and linearized
// together by that single global step.
// ---------------------------------------------------------------------------

// 4a. Antiparallel pair across a resistor -- two diodes sharing the SAME
// port node pair (n2, ground), one forward, one reverse, under a modest
// forward drive (Vs = 0.5 V, well below either diode's overload region).
TEST_CASE("newton-solver: antiparallel diode pair across a resistor is never refused (US2)") {
    constexpr int kMaxNodes = 12;
    constexpr int kMaxComponents = 12;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.5});
    nl.add(Resistor{n1, n2, 1000.0});
    nl.add(Diode{n2, kGround, kIs, kN, kVt});   // forward: anode n2, cathode gnd
    nl.add(Diode{kGround, n2, kIs, kN, kVt});   // antiparallel: anode gnd, cathode n2
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));

    // Global step (S3): both diodes are tracked and linearized together by
    // the single sys.solve() call each iteration -- no per-diode sequencing.
    CHECK(solver.diodeCount() == 2);

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    // Never refused: solve() ran real iterations and reported a status by
    // value, converged or not -- no throw, no scope refusal either way.
    CHECK(status.iterations >= 1);
    CHECK(status.voltageResidual >= 0.0);
    // Well-conditioned, modest drive -- expect convergence.
    CHECK(status.converged);
}

// 4b. Longer antiparallel string -- two diodes in series in EACH direction
// (4 diodes total) between the port node n2 and ground, the higher-threshold
// clipper topology. Vs = 3.0 V is comfortably above the ~2*Vf needed to turn
// on the forward string. n3/n4 (the mid-string nodes) each need a grounding
// resistor since a diode-only path does not satisfy prepare()'s
// floating-node check; the resistors are large (100 kOhm) so they barely
// perturb the diode string's own operating point.
TEST_CASE("newton-solver: longer antiparallel diode string (2 diodes each direction) is never refused (US2)") {
    constexpr int kMaxNodes = 12;
    constexpr int kMaxComponents = 12;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;
    constexpr double kLeak = 1.0e5;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    const NodeId n3 = nl.addNode();  // mid-node of the forward string
    const NodeId n4 = nl.addNode();  // mid-node of the reverse string

    nl.add(VoltageSource{n1, kGround, 3.0});
    nl.add(Resistor{n1, n2, 1000.0});
    nl.add(Diode{n2, n3, kIs, kN, kVt});        // forward diode 1
    nl.add(Diode{n3, kGround, kIs, kN, kVt});   // forward diode 2
    nl.add(Resistor{n3, kGround, kLeak});       // keeps n3 off the floating list
    nl.add(Diode{kGround, n4, kIs, kN, kVt});   // reverse diode 1
    nl.add(Diode{n4, n2, kIs, kN, kVt});        // reverse diode 2
    nl.add(Resistor{n4, kGround, kLeak});       // keeps n4 off the floating list
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));

    // Global step (S3): all 4 diodes are tracked and linearized together by
    // the single sys.solve() call each iteration -- no per-diode sequencing.
    CHECK(solver.diodeCount() == 4);

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    CHECK(status.iterations >= 1);
    CHECK(status.voltageResidual >= 0.0);
    CHECK(status.converged);
}

// 4c. Bridge (4 diodes) -- a full diode-bridge rectifier topology: the AC
// terminal n1 is driven directly (VoltageSource{n1, gnd, Vac}); the "other AC
// terminal" is ground itself; n2/n3 are the DC+ / DC- rails. All 4 diodes sit
// at DISTINCT node pairs but share nodes with each other (n1, n2, n3, ground),
// so they are electrically coupled through the load resistor across the DC
// rails. n2 and n3 each need their own grounding (bleeder) resistor since the
// diode/load edges alone would otherwise leave them floating.
TEST_CASE("newton-solver: diode bridge rectifier (4 diodes at distinct node pairs) is never refused (US2)") {
    constexpr int kMaxNodes = 12;
    constexpr int kMaxComponents = 12;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;
    constexpr double kBleed = 1.0e5;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();  // AC terminal (the other AC terminal is ground)
    const NodeId n2 = nl.addNode();  // DC+ rail
    const NodeId n3 = nl.addNode();  // DC- rail

    nl.add(VoltageSource{n1, kGround, 5.0});
    nl.add(Diode{n1, n2, kIs, kN, kVt});         // D1: anode n1,   cathode n2
    nl.add(Diode{kGround, n2, kIs, kN, kVt});    // D2: anode gnd,  cathode n2
    nl.add(Diode{n3, n1, kIs, kN, kVt});         // D3: anode n3,   cathode n1
    nl.add(Diode{n3, kGround, kIs, kN, kVt});    // D4: anode n3,   cathode gnd
    nl.add(Resistor{n2, n3, 1000.0});            // load across the DC rails
    nl.add(Resistor{n2, kGround, kBleed});       // keeps n2 off the floating list
    nl.add(Resistor{n3, kGround, kBleed});       // keeps n3 off the floating list
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));

    // Global step (S3): all 4 bridge diodes are tracked and linearized
    // together by the single sys.solve() call each iteration -- no
    // per-diode sequencing, no refusal for the 4-way coupled nonlinearity.
    CHECK(solver.diodeCount() == 4);

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    CHECK(status.iterations >= 1);
    CHECK(status.voltageResidual >= 0.0);
    CHECK(status.converged);
}

// ---------------------------------------------------------------------------
// 8. A throwing re-plan invalidates the plan state (AUDIT-20260708-01). A
// successful plan() followed by a re-plan whose MnaAssembler::plan() throws
// (here a degenerate resistor R < 0, rejected at plan time) must leave the
// solver UNPLANNED — not marked usable with stale topology — so a caller that
// catches the plan error cannot enter solve() on a half-cleared assembler.
// ---------------------------------------------------------------------------

TEST_CASE("newton-solver: a throwing re-plan leaves the solver unplanned (AUDIT-01)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    Netlist<kMaxNodes, kMaxComponents> good;
    const NodeId g1 = good.addNode();
    good.add(Resistor{g1, kGround, 1000.0});
    good.add(Diode{g1, kGround, 1e-14, 1.0, 0.025852});
    good.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    solver.plan(good, assembler, sys);
    REQUIRE(solver.planned());

    // Re-plan with a netlist whose MnaAssembler::plan() throws: a degenerate
    // resistor (R < 0) is rejected at plan time. The netlist itself prepares
    // (prepare() does not validate R), so the throw originates inside
    // assembler.plan(), mid-re-plan.
    Netlist<kMaxNodes, kMaxComponents> bad;
    const NodeId b1 = bad.addNode();
    bad.add(Resistor{b1, kGround, -1.0});  // degenerate → assembler.plan throws
    bad.add(Diode{b1, kGround, 1e-14, 1.0, 0.025852});
    bad.prepare();

    CHECK_THROWS_AS(solver.plan(bad, assembler, sys), std::invalid_argument);

    // AUDIT-01: unplanned after the throwing re-plan (was stale-true before the fix).
    CHECK_FALSE(solver.planned());

    // ...so solve() surfaces the pre-plan guard by value, not stale topology.
    const std::array<double, kMaxNodes> guess{};
    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(bad, base, guess, assembler, sys));
    CHECK_FALSE(status.converged);
    CHECK(status.iterations == 0);
}

// ---------------------------------------------------------------------------
// 9. Inconsistent-plan guard on the hot path (AUDIT-20260708-02). solve() must
// surface a netlist inconsistent with the last plan() BY VALUE (throw-free),
// not index out of range / std::get-throw on the promised throw-free hot path.
// Two channels: (a) a shorter netlist so a planned diode index is out of range;
// (b) a netlist where a planned diode slot no longer holds a Diode.
// ---------------------------------------------------------------------------

TEST_CASE("newton-solver: solve() surfaces an inconsistent netlist by value (AUDIT-02)") {
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

    // Channel (a): a SHORTER netlist → planned diode index 1 is out of range.
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

    // Channel (c) — NON-diode topology drift (AUDIT-20260708-04/05): the diode
    // slot (index 1) is unchanged, but index 0 changed Resistor -> VoltageSource
    // (a branch-bearing kind whose planned branch map entry is stale). The diode
    // slots still match, yet the full kind fingerprint rejects the drift, so the
    // stale-branch refresh never runs — surfaced by value, not wrong stamping.
    Netlist<kMaxNodes, kMaxComponents> drifted;
    const NodeId d1 = drifted.addNode();
    drifted.add(VoltageSource{d1, kGround, 0.5});           // index 0: was a Resistor
    drifted.add(Diode{d1, kGround, 1e-14, 1.0, 0.025852});  // index 1: still a diode
    drifted.prepare();
    NewtonStatus cStatus;
    CHECK_NOTHROW(cStatus = solver.solve(drifted, base, guess, assembler, sys));
    CHECK_FALSE(cStatus.converged);
    CHECK(cStatus.iterations == 0);
}

}  // TEST_SUITE("newton-solver")

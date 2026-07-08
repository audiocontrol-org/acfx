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

using acfx::CurrentSource;
using acfx::Diode;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
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

}  // TEST_SUITE("newton-solver")

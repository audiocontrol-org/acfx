#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"
#include "support/allocation-sentinel.h"

#include <array>
#include <cmath>
#include <cstddef>
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

using acfx::Capacitor;
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
using acfx::test::AllocationSentinel;

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
// 5. ComposedCompanionSupply composition (US3, T010; FR-006/007; contract
// S2/D6). NewtonSolver's nested ComposedCompanionSupply<Base> is the sibling
// seam for implicit-integration: `at(i)` returns the per-iteration diode
// linearization at diode component indices and DELEGATES to the caller's
// base supply — which stays FIXED for the whole solve, since solve() takes
// `base` by `const&` (see newton-solver.h solve()'s signature) — at every
// other index. This is proved two ways: DIRECTLY, by constructing
// ComposedCompanionSupply over a hand-built mask/array/base and asserting
// `at()` picks the right side of the mask per index; and INDIRECTLY, by
// solving an assembled netlist that mixes a reactive (non-diode) companion
// element with diodes and observing the solve never throws and the base
// supply's fixed value is unchanged after the solve.
// ---------------------------------------------------------------------------

// 5a. Direct unit test of ComposedCompanionSupply in isolation: a hand-built
// isDiode mask + diodeCompanion array + a fixed base supply. Proves the
// composition rule itself, with no netlist/assembler/solve involved.
TEST_CASE("newton-solver: ComposedCompanionSupply delegates to base for non-diode indices and overrides diode indices (S2/D6)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    struct FixedBaseSupply {
        acfx::Companion fixed;
        acfx::Companion at(int /*componentIndex*/) const noexcept { return fixed; }
    };

    // Component 2 is a reactive (non-diode) index; component 5 is a diode
    // index -- both arbitrary but distinct and in range.
    constexpr int kReactiveIndex = 2;
    constexpr int kDiodeIndex = 5;

    FixedBaseSupply base{acfx::Companion{0.001, 0.005}};
    std::array<acfx::Companion, kMaxComponents> diodeCompanion{};
    std::array<bool, kMaxComponents> isDiode{};
    isDiode[static_cast<std::size_t>(kDiodeIndex)] = true;
    diodeCompanion[static_cast<std::size_t>(kDiodeIndex)] =
        acfx::Companion{2.5, -0.75};

    using Supply = NewtonSolver<kMaxNodes, kMaxComponents,
                                kMaxBranches>::ComposedCompanionSupply<FixedBaseSupply>;
    const Supply supply{base, diodeCompanion, isDiode};

    // Non-diode index: delegates to the base's fixed companion verbatim.
    const acfx::Companion reactive = supply.at(kReactiveIndex);
    CHECK(reactive.Geq == doctest::Approx(0.001));
    CHECK(reactive.Ieq == doctest::Approx(0.005));

    // Diode index: overridden by the diodeCompanion array, NOT the base.
    const acfx::Companion diode = supply.at(kDiodeIndex);
    CHECK(diode.Geq == doctest::Approx(2.5));
    CHECK(diode.Ieq == doctest::Approx(-0.75));
}

// 5b. Direct unit test of the v1 "empty base" DC case: a diode-only netlist's
// mask (resistor indices non-diode, diode indices diode) composed over a
// ZERO base. Proves only diode indices get a non-trivial companion -- the
// non-diode indices delegate to the base's trivial {0,0}.
TEST_CASE("newton-solver: ComposedCompanionSupply with a zero base -- only diode indices get non-trivial companions (v1 DC case, S2/D6)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    struct ZeroBaseSupply {
        acfx::Companion at(int /*componentIndex*/) const noexcept {
            return acfx::Companion{0.0, 0.0};
        }
    };

    // Mirrors test case 2's diode-only topology: index 0 resistor, index 1
    // diode, index 2 resistor, index 3 diode.
    ZeroBaseSupply base;
    std::array<acfx::Companion, kMaxComponents> diodeCompanion{};
    std::array<bool, kMaxComponents> isDiode{};
    isDiode[1] = true;
    isDiode[3] = true;
    diodeCompanion[1] = acfx::Companion{0.04, -0.6};
    diodeCompanion[3] = acfx::Companion{0.02, -0.3};

    using Supply = NewtonSolver<kMaxNodes, kMaxComponents,
                                kMaxBranches>::ComposedCompanionSupply<ZeroBaseSupply>;
    const Supply supply{base, diodeCompanion, isDiode};

    // Non-diode (resistor) indices: delegate to the zero base -- trivial.
    CHECK(supply.at(0).Geq == doctest::Approx(0.0));
    CHECK(supply.at(0).Ieq == doctest::Approx(0.0));
    CHECK(supply.at(2).Geq == doctest::Approx(0.0));
    CHECK(supply.at(2).Ieq == doctest::Approx(0.0));

    // Diode indices: overridden with the non-trivial per-iteration value.
    CHECK(supply.at(1).Geq == doctest::Approx(0.04));
    CHECK(supply.at(1).Ieq == doctest::Approx(-0.6));
    CHECK(supply.at(3).Geq == doctest::Approx(0.02));
    CHECK(supply.at(3).Ieq == doctest::Approx(-0.3));
}

// 5c. Base pass-through + diode override across a real solve: a netlist
// mixing a NON-diode companion element (a Capacitor, at a KNOWN component
// index) with a diode, plus resistors so no node floats. The hand-written
// base supply returns a FIXED, distinctive companion for the capacitor's
// index and {0,0} elsewhere; solve() takes `base` by `const&`
// (newton-solver.h solve() signature) so it cannot mutate mid-solve
// (FR-007) -- re-querying the SAME index after solve() must still yield the
// identical fixed value.
TEST_CASE("newton-solver: solve() composes a fixed base companion for a reactive element with diode overrides (FR-006/007)") {
    constexpr int kMaxNodes = 12;
    constexpr int kMaxComponents = 12;
    constexpr int kMaxBranches = 4;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 1.0});           // index 0
    nl.add(Resistor{n1, n2, 1000.0});                  // index 1
    nl.add(Capacitor{n2, kGround, 1e-6});              // index 2 -- reactive, KNOWN index
    nl.add(Diode{n2, kGround, 1e-14, 1.0, 0.025852});  // index 3 -- diode
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;

    // Hand-written base supply: FIXED companion at the capacitor's index,
    // {0,0} everywhere else. callCount just documents the base IS consulted
    // every iteration (via ComposedCompanionSupply's delegation), NOT that it
    // changes -- the value returned for a given index never varies.
    struct FixedReactiveBaseSupply {
        int reactiveIndex;
        acfx::Companion fixed;
        mutable int callCount = 0;
        acfx::Companion at(int i) const noexcept {
            ++callCount;
            return i == reactiveIndex ? fixed : acfx::Companion{0.0, 0.0};
        }
    } base{2, acfx::Companion{0.001, 0.005}};

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));
    CHECK(solver.diodeCount() == 1);
    CHECK(solver.isDiodeComponent(3));
    CHECK_FALSE(solver.isDiodeComponent(2));  // the capacitor is NOT in the diode mask

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    CHECK(status.iterations >= 1);
    CHECK(status.converged);
    CHECK(base.callCount > 0);

    // FR-007: base is const& for the whole solve, so it never changed
    // mid-solve -- re-querying the same index post-solve yields the same
    // fixed companion.
    const acfx::Companion after = base.at(2);
    CHECK(after.Geq == doctest::Approx(0.001));
    CHECK(after.Ieq == doctest::Approx(0.005));
}

// 5d. Empty base (v1 DC case) end to end: a netlist with ONLY diodes +
// resistors (no reactive element), solved with a base supply that returns
// {0,0} for everything. Proves the v1 hand-written-base DC scenario solves
// cleanly through the composed supply.
TEST_CASE("newton-solver: diode-only DC netlist solves against a zero base -- v1 hand-written-base case (FR-006/007)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.7});                // index 0
    nl.add(Resistor{n1, n2, 1000.0});                       // index 1
    nl.add(Diode{n2, kGround, 1e-14, 1.0, 0.025852});        // index 2 -- only diode
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;

    struct ZeroBaseCountingSupply {
        mutable int callCount = 0;
        acfx::Companion at(int /*componentIndex*/) const noexcept {
            ++callCount;
            return acfx::Companion{0.0, 0.0};
        }
    } base;

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));
    CHECK(solver.diodeCount() == 1);
    CHECK(solver.isDiodeComponent(2));
    CHECK_FALSE(solver.isDiodeComponent(0));
    CHECK_FALSE(solver.isDiodeComponent(1));

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    CHECK(status.iterations >= 1);
    CHECK(status.converged);
    // MnaAssembler::refresh only calls CompanionSupply::at() for reactive
    // (Capacitor/Inductor) and Diode components (mna-assembler.h refresh()).
    // This netlist has no reactive element, and the lone companion element is
    // the diode -- which ComposedCompanionSupply intercepts via the isDiode
    // mask before ever reaching base.at(). So the base is legitimately never
    // consulted here: the "empty base" v1 DC case, proved end to end.
    CHECK(base.callCount == 0);
}

// ---------------------------------------------------------------------------
// 6. Determinism / statelessness and warm-start (US4, T012; FR-008/009,
// S8/S9, SC-007). NewtonSolver::solve() is STATELESS per solve: its per-solve
// scratch (diodeCompanion_, prevBiasAK_) is reset at the top of every
// solve(), so the only fields that persist solve->solve are immutable config
// (maxIterations_, voltageTol_, currentTol_) and plan-time topology
// (planned_, diodeCount_, diodeComponentIndex_, isDiode_) -- all of which
// depend only on the netlist, never on a prior solve's iterate. The caller
// owns the initial node-voltage guess (warm start); it seeds each diode's
// junction bias but never changes the fixed point the loop converges to.
//
// All three cases below share one single-diode-plus-resistor topology (the
// same shape as case 3 / case 5d): VoltageSource{n1,gnd,0.7} ->
// Resistor{n1,n2,1000} -> Diode{n2,gnd,...} (anode n2, cathode ground).
// ---------------------------------------------------------------------------

// 6a. Determinism / statelessness (S8, SC-007): the SAME solver/assembler/sys
// instance, called twice in a row with IDENTICAL (nl, base,
// initialNodeVoltages), must produce BIT-IDENTICAL NewtonStatus values and
// BIT-IDENTICAL resulting node voltages. This is the strongest form of the
// claim: nothing left over in the solver's own scratch fields from the first
// call may leak into the second. Same inputs -> identical floating-point
// ops -> bit-identical results, so exact == on doubles is the correct
// assertion here (not an approximate one).
TEST_CASE("newton-solver: two identical back-to-back solve() calls are bit-identical -- statelessness (S8, SC-007)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.7});           // index 0
    nl.add(Resistor{n1, n2, 1000.0});                  // index 1
    nl.add(Diode{n2, kGround, kIs, kN, kVt});          // index 2 -- anode n2, cathode gnd
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));

    const std::array<double, kMaxNodes> initialNodeVoltages{};

    const NewtonStatus status1 = solver.solve(nl, base, initialNodeVoltages, assembler, sys);
    const double v1n1 = sys.nodeVoltage(n1);
    const double v1n2 = sys.nodeVoltage(n2);

    // Second call: identical (nl, base, initialNodeVoltages), same solver
    // instance -- proves solve() reset its own scratch rather than carrying
    // anything over from the first call.
    const NewtonStatus status2 = solver.solve(nl, base, initialNodeVoltages, assembler, sys);
    const double v2n1 = sys.nodeVoltage(n1);
    const double v2n2 = sys.nodeVoltage(n2);

    CHECK(status1.converged);  // sanity: not vacuously comparing two failures
    CHECK(status1.converged == status2.converged);
    CHECK(status1.iterations == status2.iterations);
    CHECK(status1.voltageResidual == status2.voltageResidual);
    CHECK(status1.currentResidual == status2.currentResidual);
    CHECK(v1n1 == v2n1);
    CHECK(v1n2 == v2n2);
}

// 6b. Warm start vs cold guess converge to the SAME fixed point (S9): the
// initial node-voltage guess affects iteration count only, never the
// operating point. A cold (zero) guess and a warm guess seeded near the
// diode's known forward-conduction voltage (~0.6 V at n2, the anode) must
// both converge to node voltages that agree to solver tolerance, and the
// warm start must never need MORE iterations than the cold start.
TEST_CASE("newton-solver: warm start and cold start converge to the same fixed point, warm in no more iterations (S9)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.7});
    nl.add(Resistor{n1, n2, 1000.0});
    nl.add(Diode{n2, kGround, kIs, kN, kVt});  // anode n2, cathode gnd
    nl.prepare();

    // Cold: zero guess everywhere.
    MnaSystem<kMaxNodes, kMaxBranches> sysCold;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assemblerCold;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solverCold;
    ZeroCompanionSupply baseCold;
    CHECK_NOTHROW(solverCold.plan(nl, assemblerCold, sysCold));
    const std::array<double, kMaxNodes> coldGuess{};
    const NewtonStatus cold = solverCold.solve(nl, baseCold, coldGuess, assemblerCold, sysCold);

    // Warm: n2 (the diode's anode) seeded near the known ~0.6 V solution.
    MnaSystem<kMaxNodes, kMaxBranches> sysWarm;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assemblerWarm;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solverWarm;
    ZeroCompanionSupply baseWarm;
    CHECK_NOTHROW(solverWarm.plan(nl, assemblerWarm, sysWarm));
    std::array<double, kMaxNodes> warmGuess{};
    warmGuess[static_cast<std::size_t>(n2)] = 0.6;
    const NewtonStatus warm = solverWarm.solve(nl, baseWarm, warmGuess, assemblerWarm, sysWarm);

    CHECK(cold.converged);
    CHECK(warm.converged);

    // Same fixed point regardless of the starting guess (solver tolerance).
    CHECK(std::fabs(sysCold.nodeVoltage(n1) - sysWarm.nodeVoltage(n1)) < 1e-9);
    CHECK(std::fabs(sysCold.nodeVoltage(n2) - sysWarm.nodeVoltage(n2)) < 1e-9);

    // The guess affects iteration count, not the fixed point: starting near
    // the solution never needs MORE iterations than starting cold.
    CHECK(warm.iterations <= cold.iterations);
}

// 6c. Guess shape (FR-009): initialNodeVoltages is std::array<double,
// MaxNodes> -- node voltages only, never branch currents. This is proved two
// ways:
//   (a) compile-time / by construction -- solve()'s third parameter (see
//       newton-solver.h solve()'s signature) is `const std::array<double,
//       MaxNodes>&`; there is no implicit conversion between differently
//       sized std::array template instantiations, so every call below only
//       compiles because the guess is EXACTLY MaxNodes-shaped (a
//       MaxBranches-shaped array, note kMaxBranches != kMaxNodes here, would
//       be a hard compile error at the call site, not a runtime failure).
//   (b) behaviorally -- solve()'s only read of the guess (the "Seed each
//       diode's junction bias" loop) indexes it at each diode's anode/cathode
//       NODE ids. Perturbing an entry at a node the diode is NOT attached to
//       (n1, which only touches the source/resistor) is therefore never read
//       and must not change the seeded bias, the iteration count, or the
//       converged fixed point at all -- exactly what a per-node-voltage-only
//       guess predicts.
TEST_CASE("newton-solver: initialNodeVoltages is a node-voltage-only guess shaped std::array<double, MaxNodes> (FR-009)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    static_assert(kMaxBranches != kMaxNodes,
                  "kMaxBranches and kMaxNodes must differ so a differently "
                  "shaped guess would be a distinguishable compile error");
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.7});
    nl.add(Resistor{n1, n2, 1000.0});
    nl.add(Diode{n2, kGround, kIs, kN, kVt});  // anode n2, cathode gnd -- n1 untouched
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sysZero;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assemblerZero;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solverZero;
    ZeroCompanionSupply baseZero;
    CHECK_NOTHROW(solverZero.plan(nl, assemblerZero, sysZero));
    const std::array<double, kMaxNodes> zeroGuess{};  // exactly MaxNodes-shaped
    const NewtonStatus zero = solverZero.solve(nl, baseZero, zeroGuess, assemblerZero, sysZero);

    MnaSystem<kMaxNodes, kMaxBranches> sysPerturbed;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assemblerPerturbed;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solverPerturbed;
    ZeroCompanionSupply basePerturbed;
    CHECK_NOTHROW(solverPerturbed.plan(nl, assemblerPerturbed, sysPerturbed));
    std::array<double, kMaxNodes> perturbedGuess{};  // exactly MaxNodes-shaped
    perturbedGuess[static_cast<std::size_t>(n1)] = 123.0;  // a node the diode never touches
    const NewtonStatus perturbed =
        solverPerturbed.solve(nl, basePerturbed, perturbedGuess, assemblerPerturbed, sysPerturbed);

    CHECK(zero.converged);
    CHECK(perturbed.converged);
    // n1's guess entry is never read (only the diode's anode/cathode entries
    // are) -- identical seeded bias, identical iteration trajectory, so exact
    // == is the correct assertion (not an approximate one).
    CHECK(zero.iterations == perturbed.iterations);
    CHECK(sysZero.nodeVoltage(n1) == sysPerturbed.nodeVoltage(n1));
    CHECK(sysZero.nodeVoltage(n2) == sysPerturbed.nodeVoltage(n2));
}

// ---------------------------------------------------------------------------
// 7. RT-safety: solve() allocates nothing on the hot path (US5, T014/T015;
// contract S10 header comment "zero heap on solve()"; SC-003). plan() runs
// ONCE, OUTSIDE the measured region (the control-thread build step -- it may
// allocate/throw, D4). The measured region then drives 500 solve() calls --
// the RT hot path -- each warm-started from the PRIOR call's own converged
// operating point (the realistic per-sample usage pattern), and asserts:
//   - AllocationSentinel counts exactly zero allocations AND zero
//     deallocations across the whole loop (mirrors
//     mna-assembler-rtsafety-test.cpp's "plan once then 500 refresh+solve
//     iterations allocate nothing").
//   - no exception escapes any solve() call (CHECK_NOTHROW every iteration).
//   - the loop did REAL solves, not a vacuously-true zero-heap count: every
//     call reports iterations >= 1, and the final node voltage is finite.
//   - the plan is NEVER rebuilt by solve(): solve() calls refresh(), never
//     plan()/addBranch(), so both the solver's own planned() flag and the
//     assembler's planned() flag stay true, and the branch count MnaAssembler
//     ::plan() fixed is unchanged after 500 solves.
// ---------------------------------------------------------------------------

TEST_CASE("newton-solver: solve() hot path allocates nothing across 500 iterations (SC-003, T014/T015/US5)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.7});           // index 0
    nl.add(Resistor{n1, n2, 1000.0});                  // index 1
    nl.add(Diode{n2, kGround, kIs, kN, kVt});          // index 2 -- anode n2, cathode gnd
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    // Plan phase: runs ONCE, OUTSIDE the sentinel scope (off the hot path,
    // D4 -- may throw/allocate). Record the plan-fixed branch count so the
    // post-loop invariant check below has something to compare against.
    CHECK_NOTHROW(solver.plan(nl, assembler, sys));
    REQUIRE(solver.planned());
    const int plannedBranchCount = sys.branchCount();

    std::array<double, kMaxNodes> guess{};

    // allOk is accumulated INSIDE the measured region (a plain bool
    // AND-reduction touches no heap) but asserted AFTER the sentinel scope
    // closes, per the same govern pattern as mna-assembler-rtsafety-test.cpp:
    // a regression that makes solve() silently under-iterate must not still
    // pass this case just because it also allocated nothing.
    bool allOk = true;
    AllocationSentinel::reset();
    for (int i = 0; i < 500; ++i) {
        NewtonStatus status{};
        CHECK_NOTHROW(status = solver.solve(nl, base, guess, assembler, sys));
        allOk &= (status.iterations >= 1);

        // Warm-start the next call from THIS call's own converged operating
        // point -- the realistic per-sample RT usage pattern -- without ever
        // touching the heap.
        guess[static_cast<std::size_t>(n1)] = sys.nodeVoltage(n1);
        guess[static_cast<std::size_t>(n2)] = sys.nodeVoltage(n2);
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    REQUIRE(allOk);

    // Sanity: the hot-path loop actually did real solves -- the zero-heap
    // assertion below is not vacuously true because the loop body silently
    // failed.
    CHECK(std::isfinite(sys.nodeVoltage(n2)));

    CHECK_MESSAGE(allocations == 0,
                  "NewtonSolver::solve hot-path loop allocated ", allocations);
    CHECK_MESSAGE(deallocations == 0,
                  "NewtonSolver::solve hot-path loop deallocated ", deallocations);

    // solve() never rebuilds the plan: it calls MnaAssembler::refresh(),
    // never plan()/addBranch() -- both planned() flags and the plan-fixed
    // branch count are unchanged after 500 solves.
    CHECK(solver.planned());
    CHECK(assembler.planned());
    CHECK(sys.branchCount() == plannedBranchCount);
}

}  // TEST_SUITE("newton-solver")

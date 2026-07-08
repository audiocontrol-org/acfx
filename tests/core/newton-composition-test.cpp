#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"

#include <array>
#include <cmath>
#include <cstddef>

// Split from newton-solver-test.cpp to satisfy the Constitution VII
// per-file line budget (T020). Carries:
//   5. ComposedCompanionSupply composition (US3, T010; FR-006/007; S2/D6).
//   6. Determinism / statelessness and warm-start (US4, T012; FR-008/009,
//      S8/S9, SC-007).
// See newton-solver-test.cpp for construction/plan/multi-diode coverage and
// newton-rtsafety-test.cpp for the RT-safety zero-heap coverage.

using acfx::Capacitor;
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

// Trivial base companion supply shared by the statelessness/warm-start cases
// below (contract's BaseCompanionSupply template parameter).
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

}  // namespace

TEST_SUITE("newton-solver") {

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

    // Mirrors newton-solver-test.cpp's diode-only topology: index 0
    // resistor, index 1 diode, index 2 resistor, index 3 diode.
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
// same shape as newton-solver-test.cpp's plan()/solve() cases):
// VoltageSource{n1,gnd,0.7} -> Resistor{n1,n2,1000} -> Diode{n2,gnd,...}
// (anode n2, cathode ground).
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

}  // TEST_SUITE("newton-solver")

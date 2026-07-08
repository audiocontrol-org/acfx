#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"

#include <array>

// US2 / S3 global-step verification (AUDIT-20260708-03). The multi-diode
// charter's defining invariant is that solve() composes ALL diodes into exactly
// ONE MnaAssembler::refresh + MnaSystem::solve() per Newton iteration — a single
// GLOBAL step, never a per-diode sequence of smaller solves (the anti-pattern
// the labs' single-nonlinearity refusal exists to avoid). diodeCount()==N and
// convergence CANNOT distinguish those two; this suite does, by counting the
// per-iteration refresh calls via a base-companion-supply spy.
//
// Mechanism: MnaAssembler::refresh() consults the CompanionSupply once per
// companion element (diode OR reactive) each refresh. ComposedCompanionSupply
// overrides the diode slots with Newton's own companions and DELEGATES the one
// reactive (capacitor) slot to the base supply. So a counting base supply is
// invoked exactly ONCE per refresh == once per Newton iteration under the
// global-step design; a per-diode-sequencing refactor would invoke it
// diodeCount() times per iteration. The assertion `atCalls == iterations`
// therefore holds ONLY for a single global step and holds regardless of whether
// the solve converges (each iteration performs exactly one refresh either way).

using acfx::Capacitor;
using acfx::Companion;
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

// Base companion supply that COUNTS its at() invocations. In a netlist with one
// reactive (capacitor) companion slot, ComposedCompanionSupply calls this once
// per refresh (the diode slots are overridden and never reach the base), so the
// count equals the number of refresh/solve calls the Newton loop performed.
struct CountingBaseSupply {
    mutable int atCalls = 0;
    Companion at(int /*componentIndex*/) const noexcept {
        ++atCalls;
        return Companion{1.0e-6, 0.0};  // tiny fixed cap companion; keeps solvable
    }
};

}  // namespace

TEST_SUITE("newton-global-step") {

TEST_CASE(
    "newton-global-step: exactly one refresh/solve per iteration over a coupled "
    "multi-diode network (S3, AUDIT-03)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();  // source node
    const NodeId n2 = nl.addNode();  // clipping port (two coupled diodes here)

    nl.add(VoltageSource{n1, kGround, 0.5});      // index 0
    nl.add(Resistor{n1, n2, 1000.0});             // index 1
    nl.add(Capacitor{n2, kGround, 1e-9});         // index 2: reactive (base-supplied) slot
    nl.add(Diode{n2, kGround, kIs, kN, kVt});     // index 3: diode
    nl.add(Diode{kGround, n2, kIs, kN, kVt});     // index 4: antiparallel diode
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    CountingBaseSupply base;

    solver.plan(nl, assembler, sys);
    REQUIRE(solver.planned());
    REQUIRE(solver.diodeCount() == 2);  // two coupled nonlinearities on one port

    const std::array<double, kMaxNodes> guess{};
    const NewtonStatus status = solver.solve(nl, base, guess, assembler, sys);

    REQUIRE(status.iterations >= 1);

    // The defining S3 assertion: the reactive (base) slot was consulted exactly
    // ONCE per Newton iteration -> exactly ONE refresh + solve per iteration ->
    // a single GLOBAL step over BOTH diodes. A per-diode sequence would have
    // consulted the base diodeCount()==2 times per iteration (2 * iterations).
    CHECK(base.atCalls == status.iterations);
    CHECK(base.atCalls != solver.diodeCount() * status.iterations);
}

}  // TEST_SUITE("newton-global-step")

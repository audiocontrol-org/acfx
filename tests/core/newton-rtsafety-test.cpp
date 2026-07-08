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

// Split from newton-solver-test.cpp to satisfy the Constitution VII
// per-file line budget (T020). Carries:
//   7. RT-safety: solve() allocates nothing on the hot path (US5,
//      T014/T015; contract S10 header comment "zero heap on solve()";
//      SC-003).
// See newton-solver-test.cpp for construction/plan/multi-diode coverage and
// newton-composition-test.cpp for the composition/statelessness coverage.

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
// template parameter).
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

}  // namespace

TEST_SUITE("newton-solver") {

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

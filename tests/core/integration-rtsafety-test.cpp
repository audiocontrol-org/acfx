#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"
#include "support/allocation-sentinel.h"

#include <cmath>
#include <cstddef>

// ReactiveIntegrator RT-safety suite (US6/SC-004, tasks.md T017), split from
// integration-integrator-test.cpp to stay under the Constitution VII per-file
// line budget.
//
// Coverage: plan() once then step() many times allocates nothing (contract
// S8 "throw-free, allocation-free" hot path), for both the linear branch (RC
// netlist, no Diode) and the nonlinear branch (reactive+diode netlist,
// Newton-composed).

using acfx::Capacitor;
using acfx::Diode;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::integration::BackwardEuler;
using acfx::integration::ReactiveIntegrator;
using acfx::integration::StepResult;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;
using acfx::test::AllocationSentinel;

TEST_SUITE("reactive-integrator") {

// ---------------------------------------------------------------------------
// RT-safety: plan() once then step() many times allocates nothing (US6,
// T017; contract S8 "throw-free, allocation-free" hot path; SC-004). Mirrors
// the sibling pattern in tests/core/mna-assembler-rtsafety-test.cpp and
// tests/core/newton-rtsafety-test.cpp: plan() runs ONCE, OUTSIDE the
// AllocationSentinel scope (the control-thread build step -- it may
// throw/allocate, D4). The measured region then drives a 1000-iteration
// step() loop -- the RT hot path -- and asserts:
//   - AllocationSentinel counts exactly zero allocations AND zero
//     deallocations across the whole loop.
//   - no exception escapes any step() call (CHECK_NOTHROW every iteration).
//   - the loop did REAL, converged steps, not a vacuously-true zero-heap
//     count.
//   - plan() is NEVER rebuilt by step(): the plan-derived state
//     (planned()/reactiveCount()/hasNonlinear(), and for the nonlinear case
//     newton.planned() too) is unchanged after the loop.
// Companions-once-per-step (SC-004) is structurally guaranteed by step()
// itself: computeReactiveCompanions() is called exactly once per step()
// invocation, before the solve, and held fixed for that solve
// (reactive-integrator.h S1 comment) -- there is no per-Newton-iteration
// recomputation to regress. Covers BOTH the linear branch (RC netlist, no
// Diode) and the nonlinear branch (reactive+diode netlist, Newton-composed).
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: plan once then 1000 step() calls on a linear RC netlist allocate nothing (US6/SC-004/S8)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr double kDt = 1.0e-3;
    constexpr double kC = 1.0e-6;
    constexpr int kSteps = 1000;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(Resistor{vinNode, capNode, 1000.0});    // index 0
    nl.add(Capacitor{capNode, kGround, kC});       // index 1 -- reactive
    nl.add(VoltageSource{vinNode, kGround, 5.0});  // index 2
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    // Plan phase: runs ONCE, OUTSIDE the sentinel scope (off the hot path,
    // D4 -- may throw/allocate). The netlist is purely linear (no Diode), so
    // step() takes the linear branch and `newton` is never planned or used
    // for a real solve -- it is only forwarded because step()'s signature
    // requires it (the nonlinear branch is dead code on this netlist).
    REQUIRE_NOTHROW(integrator.plan(nl, assembler, sys));
    REQUIRE(integrator.planned());
    const int plannedReactiveCount = integrator.reactiveCount();
    const bool plannedHasNonlinear = integrator.hasNonlinear();
    REQUIRE(plannedReactiveCount == 1);
    REQUIRE_FALSE(plannedHasNonlinear);

    // allConverged is accumulated INSIDE the measured region (a plain bool
    // AND-reduction touches no heap) but asserted AFTER the sentinel scope
    // closes, per the same govern pattern as the sibling RT-safety tests: a
    // regression that makes step() silently stop converging must not still
    // pass this case just because it also allocated nothing.
    bool allConverged = true;
    AllocationSentinel::reset();
    for (int i = 0; i < kSteps; ++i) {
        StepResult result{};
        CHECK_NOTHROW(result = integrator.step(nl, assembler, sys, newton));
        allConverged &= result.converged;
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    REQUIRE(allConverged);
    // Sanity: the hot-path loop actually did real steps -- the zero-heap
    // assertion below is not vacuously true because the loop body silently
    // failed.
    CHECK(std::isfinite(sys.nodeVoltage(capNode)));

    CHECK_MESSAGE(allocations == 0,
                  "ReactiveIntegrator linear step() hot-path loop allocated ",
                  allocations);
    CHECK_MESSAGE(
        deallocations == 0,
        "ReactiveIntegrator linear step() hot-path loop deallocated ",
        deallocations);

    // plan-once/step-many: step() never re-plans -- the plan-derived state
    // is unchanged after 1000 steps.
    CHECK(integrator.planned());
    CHECK(integrator.reactiveCount() == plannedReactiveCount);
    CHECK(integrator.hasNonlinear() == plannedHasNonlinear);
}

TEST_CASE("reactive-integrator: plan once then 1000 step() calls on a reactive+diode netlist allocate nothing (US6/SC-004/S8)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;
    constexpr double kDt = 1.0e-3;
    constexpr double kC = 1.0e-6;
    constexpr double kR = 1000.0;
    constexpr double kVin = 5.0;
    constexpr double kIs = 1.0e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;
    constexpr int kSteps = 1000;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId node = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});  // index 0
    nl.add(Resistor{vinNode, node, kR});            // index 1
    nl.add(Diode{node, kGround, kIs, kN, kVt});     // index 2 -- nonlinear
    nl.add(Capacitor{node, kGround, kC});           // index 3 -- reactive
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    // Plan phase: runs ONCE, OUTSIDE the sentinel scope (off the hot path,
    // D4/D7 -- may throw/allocate). Both the integrator's plan() (reactive
    // scan + branch allocation, delegated to MnaAssembler::plan) AND the
    // NewtonSolver's own plan() (diode scan) are control-thread build steps
    // the caller runs before the hot loop -- exactly as
    // "history advances exactly once per step on a reactive+diode transient"
    // above does for a single step().
    REQUIRE_NOTHROW(integrator.plan(nl, assembler, sys));
    REQUIRE_NOTHROW(newton.plan(nl, assembler, sys));
    REQUIRE(integrator.planned());
    REQUIRE(newton.planned());
    const int plannedReactiveCount = integrator.reactiveCount();
    const bool plannedHasNonlinear = integrator.hasNonlinear();
    REQUIRE(plannedReactiveCount == 1);
    REQUIRE(plannedHasNonlinear);

    bool allConverged = true;
    AllocationSentinel::reset();
    for (int i = 0; i < kSteps; ++i) {
        StepResult result{};
        CHECK_NOTHROW(result = integrator.step(nl, assembler, sys, newton));
        allConverged &= result.converged;
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    REQUIRE(allConverged);
    CHECK(std::isfinite(sys.nodeVoltage(node)));

    CHECK_MESSAGE(
        allocations == 0,
        "ReactiveIntegrator reactive+diode step() hot-path loop allocated ",
        allocations);
    CHECK_MESSAGE(
        deallocations == 0,
        "ReactiveIntegrator reactive+diode step() hot-path loop deallocated ",
        deallocations);

    // plan-once/step-many: neither plan is rebuilt by step() -- both
    // planned() flags and the scanned plan state are unchanged after 1000
    // steps, and companions-once-per-step (SC-004) held throughout since
    // step() computes them exactly once per call regardless of how many
    // Newton iterations the composed solve took.
    CHECK(integrator.planned());
    CHECK(newton.planned());
    CHECK(integrator.reactiveCount() == plannedReactiveCount);
    CHECK(integrator.hasNonlinear() == plannedHasNonlinear);
}

}  // TEST_SUITE("reactive-integrator")

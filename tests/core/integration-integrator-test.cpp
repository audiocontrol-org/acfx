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
#include <stdexcept>

// ReactiveIntegrator core suite
// (specs/implicit-integration/contracts/reactive-integrator.md;
// specs/implicit-integration/data-model.md; tasks.md T003/T013/T017).
//
// Covers the foundation (T003: construction, plan scan, step-before-plan guard),
// the history-advance contract under both rules + reactive+diode once-per-step
// (T013), and the RT-safety / zero-heap AllocationSentinel cases (T017).
//
// Coverage (tasks.md T003):
//   1. Construction validation (C1) -- dt <= 0, or an invalid forwarded
//      Newton config (maxIterations < 1 / voltageTol <= 0), each throw
//      std::invalid_argument; a valid construction does not throw.
//   2. plan() scan (P2) -- on a netlist with reactive elements at KNOWN
//      component indices, the is-reactive mask + reactive-index table match
//      the scan, planned() is true, and hasNonlinear() is false without a
//      diode / true when a Diode is present.
//   3. step()-before-plan() guard (S8) -- calling step() before plan()
//      returns StepResult{converged=false, iterations=0, voltageResidual=0}
//      BY VALUE: deterministic, throw-free, not UB.
//
// PROPOSED minimal read surface this test commits ReactiveIntegrator to (the
// contract's public surface -- plan()/step()/reset() -- has no accessors for
// the scanned plan state, so T004/T005 MUST implement exactly these names):
//   - bool planned() const noexcept
//       True once plan() has run (mirrors MnaAssembler::planned() /
//       NewtonSolver::planned()).
//   - bool hasNonlinear() const noexcept
//       Read accessor for hasNonlinear_ (data-model.md "Plan-time state"):
//       does the planned netlist contain a Diode?
//   - int reactiveCount() const noexcept
//       Number of reactive elements found by the last plan() scan.
//   - bool isReactiveComponent(int componentIndex) const noexcept
//       Per-component is-reactive mask read accessor (drives the internal
//       ReactiveCompanionSupply's at()); out-of-range indices are not
//       reactive (mirrors NewtonSolver::isDiodeComponent).
//   - int reactiveComponentIndex(int reactiveSlot) const noexcept
//       reactiveComponentIndex_[reactiveSlot] -- the component index of the
//       reactiveSlot'th reactive element recorded by plan() (reactive slot
//       -> component index, data-model.md "Plan-time state").
//
// Constructor under test (contract "Construction"):
//   explicit ReactiveIntegrator(double dt, int maxIterations = 50,
//                               double voltageTol = 1e-9);

using acfx::Capacitor;
using acfx::Companion;
using acfx::Diode;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;
using acfx::integration::BackwardEuler;
using acfx::integration::ReactiveIntegrator;
using acfx::integration::StepResult;
using acfx::integration::Trapezoidal;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;
using acfx::test::AllocationSentinel;

namespace {

// Shared by "history-advance contract under both rules" (T013, contract
// S3/S4; research R3; FR-008/009). Builds a small linear RC network
// (VoltageSource(5V) -- Resistor(1k) -- Capacitor(1uF) -- ground), steps it
// kSteps times under `Rule`, and after EACH converged step asserts the
// stored history equals the reconstruction from THAT step's stamped
// companion:
//   integrator.iPrev(slot) == Geq*vN - Ieq
//   integrator.vPrev(slot) == vN
// where vN is the cap's terminal voltage this step (sys.nodeVoltage(a) -
// sys.nodeVoltage(b)) and {Geq, Ieq} is the companion the Rule computes from
// the PRE-step history (read via vPrev(slot)/iPrev(slot) BEFORE calling
// step()). CHECK (not REQUIRE) so a failure on one step does not hide a
// failure on a later step. This is the rule-agnostic advance contract the
// linear step() branch (T007) already satisfies -- it must hold identically
// under BackwardEuler and Trapezoidal (RP2), so this helper is instantiated
// for both from the single TEST_CASE below.
template <class Rule>
void checkHistoryAdvanceContractBothRules() {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr double kDt = 1.0e-3;
    constexpr double kC = 1.0e-6;
    constexpr int kSteps = 5;
    constexpr int kSlot = 0;

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
    ReactiveIntegrator<Rule, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    REQUIRE(integrator.reactiveCount() == 1);
    REQUIRE_FALSE(integrator.hasNonlinear());

    for (int step = 0; step < kSteps; ++step) {
        // Read the PRE-step history and compute the expected companion from
        // it via the SAME Rule policy step() itself uses internally.
        const double vPrevBefore = integrator.vPrev(kSlot);
        const double iPrevBefore = integrator.iPrev(kSlot);
        const Companion expectedCompanion =
            Rule::capacitorCompanion(kC, kDt, vPrevBefore, iPrevBefore);

        const StepResult result = integrator.step(nl, assembler, sys, newton);
        REQUIRE(result.converged);

        const double vN =
            sys.nodeVoltage(capNode) - sys.nodeVoltage(kGround);
        const double expectedIPrev =
            expectedCompanion.Geq * vN - expectedCompanion.Ieq;

        CHECK(integrator.vPrev(kSlot) == doctest::Approx(vN).epsilon(1.0e-12));
        CHECK(integrator.iPrev(kSlot) ==
              doctest::Approx(expectedIPrev).epsilon(1.0e-12));
    }
}

}  // namespace

TEST_SUITE("reactive-integrator") {

// ---------------------------------------------------------------------------
// 1. Construction validation (contract C1): dt <= 0, or an invalid
// forwarded Newton config (maxIterations < 1 / voltageTol <= 0), each throw
// std::invalid_argument; a valid construction (including the all-defaults
// constructor) does not throw. Construction is off the hot path, so
// throwing here is the documented, correct behavior (C1).
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: construction rejects dt <= 0 (C1)") {
    CHECK_THROWS_AS((ReactiveIntegrator<BackwardEuler, 4, 4, 2>(/*dt*/ 0.0)),
                     std::invalid_argument);
    CHECK_THROWS_AS((ReactiveIntegrator<BackwardEuler, 4, 4, 2>(/*dt*/ -1.0e-3)),
                     std::invalid_argument);
}

TEST_CASE("reactive-integrator: construction rejects an invalid forwarded Newton config (C1)") {
    CHECK_THROWS_AS(
        (ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3, /*maxIterations*/ 0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3, /*maxIterations*/ -1)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3, 50, /*voltageTol*/ 0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3, 50, /*voltageTol*/ -1.0e-9)),
        std::invalid_argument);
}

TEST_CASE("reactive-integrator: a valid construction does not throw (C1)") {
    CHECK_NOTHROW((ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3)));
    CHECK_NOTHROW((ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3, 50, 1.0e-9)));
    CHECK_NOTHROW((ReactiveIntegrator<BackwardEuler, 4, 4, 2>(1.0e-3, 1, 1.0e-6)));
}

// ---------------------------------------------------------------------------
// 2. plan() scan (contract P2). Netlist has a reactive element (Capacitor)
// at a KNOWN component index interleaved with linear components:
//   index 0: Resistor
//   index 1: Capacitor  <-- reactive
//   index 2: VoltageSource
// After plan(), planned() must be true and the integrator's scan must match
// the netlist exactly: reactiveCount() == 1, reactiveComponentIndex(0) ==
// 1, isReactiveComponent(1) true, isReactiveComponent(0) and (2) false, and
// hasNonlinear() false (no Diode in this netlist).
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: plan() scans the reactive mask + index table from known component indices (P2)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr int kResistorIndex = 0;
    constexpr int kCapacitorIndex = 1;
    constexpr int kVoltageSourceIndex = 2;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(Resistor{vinNode, capNode, 1000.0});    // index 0
    nl.add(Capacitor{capNode, kGround, 1.0e-6});   // index 1
    nl.add(VoltageSource{vinNode, kGround, 5.0});  // index 2
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);

    CHECK_FALSE(integrator.planned());

    integrator.plan(nl, assembler, sys);

    CHECK(integrator.planned());
    CHECK(integrator.reactiveCount() == 1);
    CHECK(integrator.reactiveComponentIndex(0) == kCapacitorIndex);
    CHECK(integrator.isReactiveComponent(kCapacitorIndex));
    CHECK_FALSE(integrator.isReactiveComponent(kResistorIndex));
    CHECK_FALSE(integrator.isReactiveComponent(kVoltageSourceIndex));
    CHECK_FALSE(integrator.hasNonlinear());
}

// ---------------------------------------------------------------------------
// 2b. plan() scan with a Diode present (contract P2): hasNonlinear() must
// become true (choosing the Newton-vs-MNA step branch once, at plan time),
// while the reactive mask/table are unaffected -- the Diode itself is NOT a
// reactive element (isReactiveComponent(diodeIndex) stays false).
//   index 0: Resistor
//   index 1: Capacitor      <-- reactive
//   index 2: VoltageSource
//   index 3: Diode          <-- nonlinear
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: plan() sets hasNonlinear() true when a Diode is present (P2)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 4;
    constexpr int kMaxBranches = 1;
    constexpr int kCapacitorIndex = 1;
    constexpr int kDiodeIndex = 3;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(Resistor{vinNode, capNode, 1000.0});                // index 0
    nl.add(Capacitor{capNode, kGround, 1.0e-6});                // index 1
    nl.add(VoltageSource{vinNode, kGround, 5.0});               // index 2
    nl.add(Diode{vinNode, kGround, 1.0e-14, 1.0, 0.025852});    // index 3
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);

    integrator.plan(nl, assembler, sys);

    CHECK(integrator.planned());
    CHECK(integrator.reactiveCount() == 1);
    CHECK(integrator.reactiveComponentIndex(0) == kCapacitorIndex);
    CHECK(integrator.isReactiveComponent(kCapacitorIndex));
    CHECK_FALSE(integrator.isReactiveComponent(kDiodeIndex));
    CHECK(integrator.hasNonlinear());
}

// ---------------------------------------------------------------------------
// 3. step()-before-plan() guard (contract S8). Calling step() on a freshly
// constructed (un-planned) integrator must return StepResult{converged=
// false, iterations=0, voltageResidual=0} BY VALUE -- deterministic, no
// throw, not UB. The netlist/assembler/sys/newton arguments below are all
// well-formed purely so plan() *would* succeed if called; the point of this
// case is that plan() is deliberately NOT called on the integrator.
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: step() before plan() returns converged=false, iterations=0, voltageResidual=0 by value (S8)") {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(Resistor{vinNode, capNode, 1000.0});
    nl.add(Capacitor{capNode, kGround, 1.0e-6});
    nl.add(VoltageSource{vinNode, kGround, 5.0});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(1.0e-3);

    CHECK_FALSE(integrator.planned());

    StepResult result;
    CHECK_NOTHROW(result = integrator.step(nl, assembler, sys, newton));

    CHECK_FALSE(result.converged);
    CHECK(result.iterations == 0);
    CHECK(result.voltageResidual == 0.0);
}

// ---------------------------------------------------------------------------
// 4. History-advance contract under BOTH rules (contract S3/S4; research R3;
// FR-008/009). PASSES now -- the linear step() branch (T007) already
// satisfies this contract. Runs the shared helper above under BackwardEuler
// and, separately, under Trapezoidal: after every converged step the stored
// history exactly reconstructs from THAT step's pre-step-history companion,
// proving the advance contract is rule-agnostic (T013 US4).
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: history-advance contract holds under both BackwardEuler and Trapezoidal (S3/S4)") {
    checkHistoryAdvanceContractBothRules<BackwardEuler>();
    checkHistoryAdvanceContractBothRules<Trapezoidal>();
}

// ---------------------------------------------------------------------------
// 5. History advances exactly once per step on a reactive+diode transient
// (contract S1/S4; research R3; FR-008/009). RED until T015 (nonlinear step
// branch): step()'s hasNonlinear_ branch is currently a marked placeholder
// that returns StepResult{} (converged=false, iterations=0) without touching
// history at all, so every assertion below fails against today's
// implementation. It documents the invariant T015 must establish: on a
// converged nonlinear (Newton-composed) step, the reactive element's history
// advances EXACTLY ONCE -- not once per Newton iteration -- to the
// reconstruction from the CONVERGED voltages, while StepResult::iterations
// still reports however many Newton iterations the composed solve consumed.
//
// Netlist: VoltageSource -- Resistor -- node -- Diode to ground, Capacitor
// across the diode (node to ground). hasNonlinear() must be true (Diode
// present); reactiveCount() must be 1 (the Capacitor).
//   index 0: VoltageSource
//   index 1: Resistor
//   index 2: Diode        <-- nonlinear
//   index 3: Capacitor    <-- reactive
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: history advances exactly once per step on a reactive+diode transient (RED until T015)") {
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
    constexpr int kSlot = 0;

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

    integrator.plan(nl, assembler, sys);
    // The caller plans the NewtonSolver for a nonlinear netlist; step() composes
    // it as the outer loop over the fixed reactive companion base.
    newton.plan(nl, assembler, sys);
    REQUIRE(integrator.hasNonlinear());
    REQUIRE(integrator.reactiveCount() == 1);

    // Known state: the zero initial condition plan() establishes (RS1) --
    // the cap starts uncharged.
    const double vPrevBefore = integrator.vPrev(kSlot);
    const double iPrevBefore = integrator.iPrev(kSlot);

    const StepResult result = integrator.step(nl, assembler, sys, newton);

    // The composed nonlinear solve must converge, and StepResult::iterations
    // must reflect the Newton iterations it consumed (>= 1, cf. NewtonSolver
    // contract S6).
    CHECK(result.converged);
    CHECK(result.iterations >= 1);

    // History must have moved -- exactly once -- away from the pre-step
    // values to the reconstruction from the CONVERGED terminal voltage.
    const double vN = sys.nodeVoltage(node) - sys.nodeVoltage(kGround);
    const Companion expectedCompanion =
        BackwardEuler::capacitorCompanion(kC, kDt, vPrevBefore, iPrevBefore);
    const double expectedIPrev =
        expectedCompanion.Geq * vN - expectedCompanion.Ieq;

    CHECK(integrator.iPrev(kSlot) != doctest::Approx(iPrevBefore));
    CHECK(integrator.vPrev(kSlot) == doctest::Approx(vN).epsilon(1.0e-12));
    CHECK(integrator.iPrev(kSlot) ==
          doctest::Approx(expectedIPrev).epsilon(1.0e-12));

    // History advanced exactly once (after Newton converged), not per Newton
    // iteration: the post-step iPrev reconstructs from the PRE-step-history
    // companion and the converged terminal voltage, a single increment.
}

// ---------------------------------------------------------------------------
// 6. RT-safety: plan() once then step() many times allocates nothing (US6,
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

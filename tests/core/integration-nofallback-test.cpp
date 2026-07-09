#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cstddef>

// ReactiveIntegrator no-fallback suite (US7, tasks.md T019, contract
// S5/S6/FR-014/FR-015), split from integration-integrator-test.cpp to stay
// under the Constitution VII per-file line budget.
//
// Coverage:
//   1. Non-convergence is surfaced BY VALUE, without advancing reactive
//      history (S5/FR-014) -- no fallback, no fabricated success.
//   2. Rule fidelity -- the selected Rule is used verbatim, no silent switch
//      to backward-Euler (S6/FR-015).

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
using acfx::integration::Trapezoidal;
using acfx::mna::MnaAssembler;
using acfx::mna::MnaSystem;
using acfx::newton::NewtonSolver;

namespace {

// Shared by "the selected Rule is used verbatim" (T019, US7, contract
// S6/FR-015). Builds a linear RC network (VoltageSource(vin) --
// Resistor(R) -- Capacitor(C) -- ground) and steps it kSteps times under
// `Rule` from the zero initial state, returning the cap's terminal voltage
// measured AFTER each step. Both rule instantiations below drive this exact
// same helper over the exact same {R, C, dt, vin} so only the Rule differs
// -- any behavioral difference between the two traces is attributable to
// the Rule policy alone, not to a topology or parameter difference.
template <class Rule>
std::array<double, 5> stiffRcTrace(double R, double C, double dt,
                                    double vin) {
    constexpr int kMaxNodes = 3;
    constexpr int kMaxComponents = 3;
    constexpr int kMaxBranches = 1;
    constexpr int kSteps = 5;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(Resistor{vinNode, capNode, R});         // index 0
    nl.add(Capacitor{capNode, kGround, C});        // index 1 -- reactive
    nl.add(VoltageSource{vinNode, kGround, vin});  // index 2
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<Rule, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(dt);

    integrator.plan(nl, assembler, sys);
    REQUIRE(integrator.reactiveCount() == 1);
    REQUIRE_FALSE(integrator.hasNonlinear());

    std::array<double, kSteps> trace{};
    for (int i = 0; i < kSteps; ++i) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        REQUIRE(result.converged);
        trace[static_cast<std::size_t>(i)] =
            sys.nodeVoltage(capNode) - sys.nodeVoltage(kGround);
    }
    return trace;
}

}  // namespace

TEST_SUITE("reactive-integrator") {

// ---------------------------------------------------------------------------
// Non-convergence is surfaced BY VALUE, without advancing reactive
// history (T019, US7, contract S5; FR-014). Same reactive+diode netlist as
// the T013 "history advances exactly once per step" case, but the
// `NewtonSolver` handed to step() is deliberately configured so it CANNOT
// converge from the cold (zero-history) start: one iteration, an impossibly
// tight voltage tolerance. step() must:
//   - not throw (the hot path stays throw-free even on a non-converged
//     solve),
//   - return converged == false (no fallback, no fabricated success),
//   - leave the reactive element's history UNCHANGED from its pre-step
//     values (S5 -- never advance history from an untrustworthy iterate),
//   - report iterations >= 1, proving a real non-converged Newton solve ran
//     (as opposed to the step()-before-plan() all-zero sentinel, S8, whose
//     iterations == 0).
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: non-convergence is surfaced by value without advancing reactive history (S5/FR-014)") {
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
    // A Newton configuration that CANNOT converge from a cold start: one
    // iteration only, and a voltage tolerance (1e-15) far tighter than a
    // nonlinear diode solve settles to from a zero-history warm start. This
    // is deliberate forcing, not a fragile accident -- it exercises S5
    // without depending on the diode's specific I-V curve being "hard".
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton(
        /*maxIterations*/ 1, /*voltageTol*/ 1.0e-15);
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    // The caller plans the NewtonSolver too (mirrors the T013 composed-solve
    // pattern) -- it is this newton object's tight config, not the
    // integrator's own forwarded defaults, that drives the non-convergence.
    newton.plan(nl, assembler, sys);
    REQUIRE(integrator.hasNonlinear());
    REQUIRE(integrator.reactiveCount() == 1);

    // Pre-step history: zero, straight from plan()'s reset() (RS1).
    const double vPrevBefore = integrator.vPrev(kSlot);
    const double iPrevBefore = integrator.iPrev(kSlot);

    StepResult result{};
    CHECK_NOTHROW(result = integrator.step(nl, assembler, sys, newton));

    // Surfaced by value -- not thrown on the hot path.
    CHECK_FALSE(result.converged);
    // A real non-converged Newton solve ran (distinguishes this from the
    // step()-before-plan() zero sentinel, S8, whose iterations == 0).
    CHECK(result.iterations >= 1);

    // S5: reactive history is NOT advanced from the untrustworthy iterate --
    // it is bit-for-bit unchanged from its pre-step values.
    CHECK(integrator.vPrev(kSlot) == doctest::Approx(vPrevBefore));
    CHECK(integrator.iPrev(kSlot) == doctest::Approx(iPrevBefore));
}

// ---------------------------------------------------------------------------
// Rule fidelity -- no silent switch to backward-Euler (T019, US7, contract
// S6/FR-015). A LINEAR RC network made deliberately STIFF relative to the
// timestep (R*C = dt/50) is integrated from the zero initial state toward a
// constant Vin under BOTH BackwardEuler and Trapezoidal via the shared
// `stiffRcTrace` helper above (identical topology/parameters, only the Rule
// differs).
//
// BackwardEuler's amplification factor 1/(1+dt/RC) = 1/51 is L-stable: it
// stays in (0,1), so each step is a convex combination of Vin and the
// previous voltage -- monotone, NEVER overshoots Vin.
//
// Trapezoidal's amplification factor (1-x)/(1+x) with x = dt/(2RC) = 25 is
// (1-25)/(1+25) = -12/13 -- NEGATIVE. Trapezoidal is A-stable but NOT
// L-stable: a negative amplification factor means the homogeneous response
// oscillates while it decays, so the cap voltage RINGS and overshoots Vin on
// approach -- a signature BackwardEuler never produces on this same
// topology.
//
// This proves the selected Rule is used VERBATIM: step() (see
// reactive-integrator.h) contains no gmin, no rule-switch, and no
// substituted-output branch that would quietly swap in backward-Euler's
// damped behavior to suppress the ringing (FR-015) -- if it did, the
// Trapezoidal trace below would be monotone too, and this assertion would
// fail.
// ---------------------------------------------------------------------------

TEST_CASE("reactive-integrator: the selected Rule is used verbatim -- Trapezoidal rings, BackwardEuler never does, on a stiff RC network (S6/FR-015)") {
    constexpr double kDt = 1.0e-3;
    constexpr double kR = 1000.0;
    constexpr double kC = 2.0e-8;  // R*C = 2e-5 = dt/50 -- deliberately stiff
    constexpr double kVin = 5.0;

    const std::array<double, 5> beTrace =
        stiffRcTrace<BackwardEuler>(kR, kC, kDt, kVin);
    const std::array<double, 5> trapTrace =
        stiffRcTrace<Trapezoidal>(kR, kC, kDt, kVin);

    // BackwardEuler: monotone non-decreasing approach to Vin, and NEVER
    // exceeds it (L-stable damping) -- no gmin/rule-switch branch exists in
    // step() to muddy this, so this is the Rule's own, unmodified behavior.
    double bePrev = 0.0;
    for (double v : beTrace) {
        CHECK(v >= bePrev - 1.0e-9);
        CHECK(v <= kVin + 1.0e-9);
        bePrev = v;
    }

    // Trapezoidal: the A-stable-not-L-stable ringing signature -- at least
    // one step overshoots Vin. BackwardEuler above never does this on the
    // identical topology, so this overshoot is attributable to the
    // Trapezoidal Rule being applied verbatim, not to a shared/substituted
    // step() code path.
    bool overshoot = false;
    for (double v : trapTrace) {
        if (v > kVin) {
            overshoot = true;
            break;
        }
    }
    CHECK(overshoot);
}

}  // TEST_SUITE("reactive-integrator")

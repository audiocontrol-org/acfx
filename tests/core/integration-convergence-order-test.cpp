#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <cmath>
#include <cstddef>
#include <vector>

// T009 -- ReactiveIntegrator convergence-order doctest suite (SC-002/FR-022).
//
// Backward-Euler and Trapezoidal are both already implemented generically as
// `Rule` template policies of ReactiveIntegrator (reactive-integrator.h); this
// suite is the REGRESSION that proves Trapezoidal genuinely achieves 2nd-order
// global accuracy and is not silently aliased to backward-Euler's 1st order.
//
// Part 1 (convergence-order regression): integrate an RC low-pass step
// response to a FIXED final time T with a sequence of shrinking timesteps
// dt_k = T/N for N in {20, 40, 80, 160, 320}. For each dt, compute the global
// error at T against the CONTINUOUS analytic solution
//   v(t) = Vin * (1 - exp(-t / (R*C)))
// (not a discrete recursion -- the continuous oracle is what "order of
// accuracy" means). Regress log(error) vs log(dt) by ordinary least squares;
// the slope is the empirical order of the method. Assert BackwardEuler's
// slope lands in [0.9, 1.2] (~1st order) and Trapezoidal's lands in
// [1.8, 2.2] (~2nd order). T is chosen at T = R*C (v(T) ~= 0.632*Vin) so the
// transient is still curving and truncation error dominates; the dt range
// (N up to a few hundred) keeps truncation error well above double-precision
// roundoff so the regressed slope is clean.
//
// Part 2 (research R1 companion-formula check): directly assert the
// Trapezoidal and BackwardEuler policy functions return the exact {Geq, Ieq}
// pairs research R1 specifies, for a known (C, L, dt, vPrev, iPrev) -- the
// algebraic identity the order test's empirical slope is indirect evidence
// for.
//
// CONSISTENT INITIAL CONDITIONS (resolved via ReactiveIntegrator::seedHistory,
// off the hot path): a constant-source step from rest is INCONSISTENT initial
// data for Trapezoidal. ReactiveIntegrator::plan()/reset() always seed
// vPrev_ = iPrev_ = 0 (contract RS1/P3), which is the correct t=0 state for
// BackwardEuler (each element's companion only reads ONE of {vPrev, iPrev} --
// current continuity for L, the chosen initial voltage for C -- so the
// "unused" zero never matters). Trapezoidal reads BOTH history terms for
// BOTH element types, and the "other" (non-state) term must equal the TRUE
// continuous derivative at t=0 (the capacitor's iPrev(0) must equal
// C*dv/dt(0) = Vin/R for this hard-switched RC step, not 0) for the method's
// designed local O(dt^3) / global O(dt^2) accuracy to hold. Seeding that term
// at 0 when the true value is Vin/R injects an O(dt) error at the very first
// step that otherwise caps the observed order at ~1 regardless of T. The
// fix is NOT a silent rule switch (FR-015 stays intact) and does NOT
// auto-compute the DC operating point (still deferred, a caller concern per
// spec) -- it is a minimal, explicit, caller-invoked opt-in:
// `integrator.seedHistory(slot, vPrev, iPrev)`, called after plan() (which
// zeroes history) and before the first step(). History still DEFAULTS to
// zero when seedHistory is not called, so FR-016 and the zero-state
// closed-form tests are unaffected. With the RC capacitor slot (slot 0)
// seeded to its true t=0 state {vPrev=0, iPrev=Vin/R}, BackwardEuler still
// ignores iPrev for its capacitor row (so its slope stays ~1 -- BE was never
// gated by this), while Trapezoidal now integrates from consistent data and
// demonstrates the real ~2nd-order accuracy gap (SC-002).

using acfx::Capacitor;
using acfx::Companion;
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

constexpr double kVin = 5.0;
constexpr double kR = 1.0e3;
constexpr double kC = 1.0e-6;
// Fixed final time T = R*C: one time constant, v(T) == Vin*(1 - exp(-1))
// ~= 0.632*Vin -- the transient is still curving here, so truncation error
// (not the DC asymptote) dominates the global error at T.
constexpr double kT = kR * kC;

// Shrinking-timestep sequence dt_k = T/N. N is kept in the few-hundred range
// (not millions) so truncation error stays well above double roundoff and the
// regressed log-log slope is clean.
const std::vector<int> kNValues = {20, 40, 80, 160, 320};

// Integrate the RC low-pass (VoltageSource -- Resistor -- Capacitor, capacitor
// node read out) from CONSISTENT initial history to t = T = N*dt under
// `Rule`, returning the observed capacitor node voltage at the final step.
// The sole reactive element is the capacitor (reactive slot 0); its
// consistent t=0 initial condition for a hard-switched step is
// vPrev = v_C(0) = 0 (the cap starts uncharged) and
// iPrev = i_C(0) = Vin/R (the current flowing into the cap the instant the
// step is applied, before any charge has accumulated). Seeding this via
// seedHistory (called after plan(), before the first step(), for BOTH
// rules) is what lets Trapezoidal demonstrate its designed 2nd-order
// accuracy -- see the CONSISTENT INITIAL CONDITIONS note above.
template <class Rule>
double rcFinalVoltage(double dt, int steps) {
    constexpr int kMaxNodes = 3;      // ground, vinNode, capNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown
    constexpr int kCapSlot = 0;       // the capacitor is the sole reactive element

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId capNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, capNode, kR});
    nl.add(Capacitor{capNode, kGround, kC});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<Rule, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(dt);

    integrator.plan(nl, assembler, sys);
    // Consistent t=0 seed: vPrev = v_C(0) = 0, iPrev = i_C(0) = Vin/R.
    integrator.seedHistory(kCapSlot, 0.0, kVin / kR);

    for (int n = 0; n < steps; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        REQUIRE(result.converged);
    }

    return sys.nodeVoltage(capNode);
}

// Ordinary least-squares slope of log(errors) vs log(dts) -- the empirical
// order of accuracy. Requires dts.size() == errors.size() >= 2.
double logLogSlope(const std::vector<double>& dts,
                    const std::vector<double>& errors) {
    const std::size_t n = dts.size();
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXY = 0.0;
    double sumXX = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = std::log(dts[i]);
        const double y = std::log(errors[i]);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }
    const double nd = static_cast<double>(n);
    return (nd * sumXY - sumX * sumY) / (nd * sumXX - sumX * sumX);
}

// Regress the empirical order of `Rule` against the RC continuous analytic
// step response over kNValues, printing the per-dt errors and returning the
// regressed slope.
template <class Rule>
double measureRcOrder(const char* ruleName) {
    const double vExactAtT = kVin * (1.0 - std::exp(-kT / (kR * kC)));

    std::vector<double> dts;
    std::vector<double> errors;
    dts.reserve(kNValues.size());
    errors.reserve(kNValues.size());

    for (const int N : kNValues) {
        const double dt = kT / static_cast<double>(N);
        const double vObserved = rcFinalVoltage<Rule>(dt, N);
        const double error = std::fabs(vObserved - vExactAtT);
        dts.push_back(dt);
        errors.push_back(error);
        MESSAGE(ruleName << ": N=" << N << " dt=" << dt
                         << " error=" << error);
    }

    const double slope = logLogSlope(dts, errors);
    MESSAGE(ruleName << ": regressed log-log slope = " << slope);
    return slope;
}

}  // namespace

TEST_SUITE("integration-convergence-order") {

// T010 (verification, no assertion needed -- confirmed by reading
// ReactiveIntegrator::step() in reactive-integrator.h): `Rule` is a
// compile-time template parameter of ReactiveIntegrator, and step()'s S1
// companion loop dispatches only on ELEMENT TYPE (std::get_if<Capacitor>/
// std::get_if<Inductor>) and on hasNonlinear_ (linear vs. Newton-composed
// branch, chosen once at plan() time) -- there is no per-sample branch on
// which Rule is active anywhere in step(). Rule::capacitorCompanion /
// Rule::inductorCompanion are resolved at compile time (the whole point of
// the template-policy design, research R2), so BackwardEuler and Trapezoidal
// each compile to their own monomorphized step() with zero runtime rule
// dispatch overhead on the hot path.

// ---------------------------------------------------------------------------
// Part 1: convergence-order regression (SC-002) -- BackwardEuler is ~1st
// order, Trapezoidal is ~2nd order, against the CONTINUOUS RC step response,
// both integrated from CONSISTENT t=0 initial history (seeded via
// seedHistory -- see rcFinalVoltage above and the CONSISTENT INITIAL
// CONDITIONS note at the top of this file).
// ---------------------------------------------------------------------------
TEST_CASE("integration-convergence-order: BackwardEuler is ~1st order against the continuous RC step response") {
    const double slope = measureRcOrder<BackwardEuler>("BackwardEuler");
    CHECK(slope >= 0.9);
    CHECK(slope <= 1.2);
}

TEST_CASE("integration-convergence-order: Trapezoidal is ~2nd order against the continuous RC step response") {
    const double slope = measureRcOrder<Trapezoidal>("Trapezoidal");
    CHECK(slope >= 1.8);
    CHECK(slope <= 2.2);
}

// ---------------------------------------------------------------------------
// Part 2: trapezoidal / backward-Euler companion formula checks (research
// R1) -- the algebraic identities the order-test slopes are indirect
// evidence for, asserted directly against a known (C, L, dt, vPrev, iPrev).
// ---------------------------------------------------------------------------
TEST_CASE("integration-convergence-order: Trapezoidal capacitor companion matches Geq=2C/dt, Ieq=Geq*vPrev+iPrev") {
    constexpr double C = 2.0e-6;
    constexpr double dt = 1.0e-5;
    constexpr double vPrev = 1.25;
    constexpr double iPrev = 0.5;

    const Companion companion = Trapezoidal::capacitorCompanion(C, dt, vPrev, iPrev);
    const double geqExpected = 2.0 * C / dt;
    const double ieqExpected = geqExpected * vPrev + iPrev;

    CHECK(companion.Geq == doctest::Approx(geqExpected));
    CHECK(companion.Ieq == doctest::Approx(ieqExpected));
}

TEST_CASE("integration-convergence-order: Trapezoidal inductor companion matches Geq=dt/(2L), Ieq=-(iPrev+Geq*vPrev)") {
    constexpr double L = 1.0e-3;
    constexpr double dt = 1.0e-5;
    constexpr double vPrev = 3.0;
    constexpr double iPrev = 0.75;

    const Companion companion = Trapezoidal::inductorCompanion(L, dt, vPrev, iPrev);
    const double geqExpected = dt / (2.0 * L);
    const double ieqExpected = -(iPrev + geqExpected * vPrev);

    CHECK(companion.Geq == doctest::Approx(geqExpected));
    CHECK(companion.Ieq == doctest::Approx(ieqExpected));
}

TEST_CASE("integration-convergence-order: BackwardEuler capacitor companion matches Geq=C/dt, Ieq=Geq*vPrev (single-sourced contrast)") {
    constexpr double C = 2.0e-6;
    constexpr double dt = 1.0e-5;
    constexpr double vPrev = 1.25;
    constexpr double iPrev = 0.5;  // unused by BackwardEuler's capacitor row

    const Companion companion = BackwardEuler::capacitorCompanion(C, dt, vPrev, iPrev);
    const double geqExpected = C / dt;
    const double ieqExpected = geqExpected * vPrev;

    CHECK(companion.Geq == doctest::Approx(geqExpected));
    CHECK(companion.Ieq == doctest::Approx(ieqExpected));
}

}  // TEST_SUITE("integration-convergence-order")

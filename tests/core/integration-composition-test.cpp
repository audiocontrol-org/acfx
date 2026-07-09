#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <cmath>

// T011 -- ReactiveIntegrator composition doctest suite (US3).
// (specs/implicit-integration/contracts/reactive-integrator.md;
// specs/implicit-integration/data-model.md; tasks.md T011).
//
// This suite proves ReactiveIntegrator::step() composes its per-step reactive
// companions with the sibling solvers -- MnaAssembler::refresh +
// MnaSystem::solve on the LINEAR branch (T007, already landed), and
// NewtonSolver::solve on the NONLINEAR branch (T015, NOT YET landed: step()
// currently returns the placeholder StepResult{converged=false} whenever
// hasNonlinear() is true).
//
// Case (a) -- linear reactive composition (PASSES now): an RC low-pass
// (VoltageSource -- Resistor -- Capacitor to ground), stepped under
// BackwardEuler and compared, sample by sample, against the exact discrete
// recursion v[n] = v[n-1] + (dt/(RC+dt))*(Vin - v[n-1]). This is the same
// closed-form oracle as integration-closed-form-test.cpp Case 1 -- it is
// re-proved HERE to pin that composition through the public step() surface,
// not just the standalone linear path.
//
// Case (b) -- reactive+diode composition (RED until T015): a first-order
// reactive diode clipper (VoltageSource -- Resistor -- node n2, with a
// Diode{n2, gnd} AND a Capacitor{n2, gnd} both across n2). plan() must report
// hasNonlinear() == true (a Diode is present). Stepped to DC steady state
// (the capacitor is an open circuit at DC, so the settled n2 voltage is
// exactly the series R + Diode DC operating point), the settled voltage is
// compared against an INDEPENDENT bisection oracle solving
//   Vin = R * Is * (exp(V / (n*Vt)) - 1) + V
// for V (mirroring the oracleSymmetric bisection pattern in
// core/labs/diode-clippers/harness/diode-clippers-harness.cpp, specialized to
// a single forward-biased diode rather than the symmetric two-diode pair).
// THIS CASE FAILS NOW: hasNonlinear() routes step() into the placeholder
// nonlinear branch, which returns converged == false and never advances the
// node voltages or history, so the settled-voltage assertion cannot pass
// until T015 lands the real Newton-composed nonlinear step.

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

namespace {

// Independent bisection root-finder (NOT the solver under test), mirroring
// core/labs/diode-clippers/harness/diode-clippers-harness.cpp's bisectRoot.
template <typename F>
double bisectRoot(F f, double lo, double hi, double tol = 1.0e-13) {
    while (hi - lo > tol) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) <= 0.0) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// DC operating point of Vin -- R -- Diode{node, gnd, Is, n, Vt} (single
// forward-biased junction, cathode grounded): solve
//   Vin = R * Is * (exp(V / (n*Vt)) - 1) + V
// for V. f is strictly increasing (R > 0, diode conductance > 0), so a single
// bisection bracket suffices; mirrors diode-clippers-harness.cpp's
// oracleSymmetric but specialized to one diode instead of the symmetric pair.
double oracleSeriesDiode(double Vin, double R, double Is, double n, double Vt) {
    const double nVt = n * Vt;
    return bisectRoot(
        [&](double v) { return v + R * Is * (std::exp(v / nVt) - 1.0) - Vin; },
        -1.0, Vin);
}

}  // namespace

TEST_SUITE("integration-composition") {

// ---------------------------------------------------------------------------
// Case (a): linear reactive composition -- RC low-pass through step().
// ---------------------------------------------------------------------------
TEST_CASE("integration-composition: linear RC network composes through step() against the exact backward-Euler recursion") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, capNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    constexpr double kVin = 5.0;
    constexpr double kR = 1.0e3;
    constexpr double kC = 1.0e-6;
    constexpr double kDt = 1.0e-5;
    constexpr int kSteps = 500;
    constexpr double kTol = 1.0e-12;

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
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    CHECK_FALSE(integrator.hasNonlinear());

    double vPrevExact = 0.0;  // v[0], the pre-step history
    const double alpha = kDt / (kR * kC + kDt);

    for (int i = 0; i < kSteps; ++i) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        CHECK(result.converged);

        const double vExact = vPrevExact + alpha * (kVin - vPrevExact);
        const double vObserved = sys.nodeVoltage(capNode);

        CHECK(vObserved == doctest::Approx(vExact).epsilon(kTol));

        vPrevExact = vExact;
    }

    // After 5 time constants the response should have visibly approached the
    // DC steady state Vin (the capacitor is an open circuit at DC).
    CHECK(vPrevExact == doctest::Approx(kVin).epsilon(1.0e-2));
}

// ---------------------------------------------------------------------------
// Case (b): reactive+diode composition -- first-order reactive diode clipper
// through step(). RED until T015 (nonlinear step branch).
// ---------------------------------------------------------------------------
TEST_CASE("integration-composition: reactive+diode network composes through step() against an independent DC bisection oracle") {
    constexpr int kMaxNodes = 3;      // ground, vinNode, n2
    constexpr int kMaxComponents = 4; // VoltageSource, Resistor, Diode, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown

    constexpr double kVin = 5.0;
    constexpr double kR = 1.0e3;
    constexpr double kC = 1.0e-6;
    constexpr double kIs = 1.0e-14;  // reverse saturation current (A)
    constexpr double kN = 1.0;       // ideality factor
    constexpr double kVt = 0.02585;  // thermal voltage (V), ~300 K
    constexpr double kDt = 1.0e-5;
    constexpr int kSteps = 5000;     // 50 ms: many RC time constants, DC settle
    constexpr double kTol = 1.0e-6;

    // VoltageSource{vinNode, gnd, Vin} -- Resistor{vinNode, n2, R} -- n2, with
    // BOTH a Diode{n2, gnd, ...} and a Capacitor{n2, gnd, C} across n2: a
    // first-order reactive diode clipper. At DC the capacitor is an open
    // circuit, leaving the series R + Diode operating point.
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, n2, kR});
    nl.add(Diode{n2, kGround, kIs, kN, kVt});
    nl.add(Capacitor{n2, kGround, kC});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<BackwardEuler, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    CHECK(integrator.hasNonlinear());
    CHECK(integrator.reactiveCount() == 1);

    StepResult result{};
    for (int i = 0; i < kSteps; ++i) {
        result = integrator.step(nl, assembler, sys, newton);
    }

    const double vExpected = oracleSeriesDiode(kVin, kR, kIs, kN, kVt);

    // RED until T015 (nonlinear step branch): step() currently routes
    // hasNonlinear() into a placeholder that returns converged == false
    // without ever composing NewtonSolver into the reactive companion solve,
    // so neither of these can pass yet.
    CHECK(result.converged);
    CHECK(sys.nodeVoltage(n2) == doctest::Approx(vExpected).epsilon(kTol));
}

}  // TEST_SUITE("integration-composition")

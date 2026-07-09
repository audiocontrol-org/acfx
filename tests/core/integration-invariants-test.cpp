#include <doctest/doctest.h>

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-integrator.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <cmath>

// T022 -- ReactiveIntegrator PHYSICAL-INVARIANT doctest suite (US Polish;
// SC-006; FR-023). The closed-form suite (integration-closed-form-test.cpp)
// already proves step() tracks an independently hand-derived discrete
// recursion to a few ULPs; THIS suite steps back and asks whether the
// numbers it produces are physically sane -- the two invariants a reactive
// circuit primitive must never violate, checked under BOTH RULE policies
// (BackwardEuler, Trapezoidal) wherever the invariant is rule-independent:
//
//   1. DC steady state (SC-006): drive a network with a constant source long
//      enough to settle, and confirm a capacitor becomes an OPEN circuit
//      (its reconstructed current i -> 0, read via iPrev(slot)) while its
//      node voltage settles to the resistive DC operating point, and a dual
//      inductor becomes a SHORT circuit (its reconstructed terminal voltage
//      v -> 0, read via vPrev(slot)) while its current settles to Vin/R.
//
//   2. Passivity / no energy gain (FR-023): a source-free, purely passive
//      RLC loop (no independent source at all -- see the passivity test's
//      header comment for why this construction, not a driven step, is the
//      one that supports a STRICT, rigorous assertion) seeded with a nonzero
//      initial capacitor voltage must never gain stored energy step over
//      step: E[n] = 0.5*C*v_C[n]^2 + 0.5*L*i_L[n]^2 is non-increasing for
//      every n, and decays toward zero as the lone resistor dissipates it.
//
// C++17, standard library only; no platform headers. double throughout
// (FR-022).

using acfx::Capacitor;
using acfx::Inductor;
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

// ---------------------------------------------------------------------------
// Part 1 -- DC steady state (SC-006).
// ---------------------------------------------------------------------------

// Result of driving the capacitor-divider network to settle (see
// capacitorReachesOpenAtDc below).
struct CapacitorDcResult {
    double nodeVoltage;  // settled node voltage across R2 || C
    double iPrevCap;     // settled reconstructed capacitor current
};

// Vin --R1-- node2 --{R2 to gnd, Capacitor to gnd}. A genuine resistor
// DIVIDER (R1, R2) with a capacitor loading the divider's output node. At DC
// the capacitor draws no current (open circuit), so node2 settles to the
// PLAIN resistive divider value Vin*R2/(R1+R2) exactly as if the capacitor
// were absent; the capacitor's only footprint on the DC answer is the
// transient time it takes to get there. Run for many multiples of the
// Thevenin time constant tau = (R1||R2)*C so the settle is thorough under
// EITHER rule (both are A-stable / unconditionally stable for this simple
// linear network, so both reach the same DC point).
template <class Rule>
CapacitorDcResult capacitorReachesOpenAtDc() {
    constexpr int kMaxNodes = 3;      // ground, vinNode, node2
    constexpr int kMaxComponents = 4; // VoltageSource, R1, R2, Capacitor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown
    constexpr int kCapSlot = 0;       // sole reactive element -> reactive slot 0

    constexpr double kVin = 5.0;
    constexpr double kR1 = 1.0e3;
    constexpr double kR2 = 2.0e3;
    constexpr double kC = 1.0e-6;
    constexpr double kDt = 1.0e-5;
    constexpr int kSteps = 1000;  // dt*kSteps = 0.01s ~= 15 Thevenin time constants

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId node2 = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, node2, kR1});
    nl.add(Resistor{node2, kGround, kR2});
    nl.add(Capacitor{node2, kGround, kC});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<Rule, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    REQUIRE(integrator.reactiveCount() == 1);

    for (int n = 0; n < kSteps; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        REQUIRE(result.converged);
    }

    return CapacitorDcResult{sys.nodeVoltage(node2), integrator.iPrev(kCapSlot)};
}

// Result of driving the series RL network to settle (see
// inductorReachesShortAtDc below).
struct InductorDcResult {
    double vPrevInductor;  // settled reconstructed terminal voltage
    double iPrevInductor;  // settled reconstructed inductor current
};

// Vin --R-- midNode --Inductor-- gnd (series RL, the dual of the capacitor
// case above). At DC the inductor becomes a SHORT circuit: its terminal
// voltage v_L = V(midNode) - V(gnd) -> 0, and the loop current settles to
// Vin/R (all of Vin drops across R). Same 15-time-constant settle strategy
// (tau = L/R here).
template <class Rule>
InductorDcResult inductorReachesShortAtDc() {
    constexpr int kMaxNodes = 3;      // ground, vinNode, midNode
    constexpr int kMaxComponents = 3; // VoltageSource, Resistor, Inductor
    constexpr int kMaxBranches = 1;   // the VoltageSource's branch unknown
    constexpr int kInductorSlot = 0;  // sole reactive element -> reactive slot 0

    constexpr double kVin = 5.0;
    constexpr double kR = 1.0e3;
    constexpr double kL = 1.0;
    constexpr double kDt = 1.0e-5;
    constexpr int kSteps = 1500;  // dt*kSteps = 0.015s ~= 15 L/R time constants

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId midNode = nl.addNode();

    nl.add(VoltageSource{vinNode, kGround, kVin});
    nl.add(Resistor{vinNode, midNode, kR});
    nl.add(Inductor{midNode, kGround, kL});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<Rule, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    REQUIRE(integrator.reactiveCount() == 1);

    for (int n = 0; n < kSteps; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        REQUIRE(result.converged);
    }

    return InductorDcResult{integrator.vPrev(kInductorSlot),
                             integrator.iPrev(kInductorSlot)};
}

// ---------------------------------------------------------------------------
// Part 2 -- Passivity / no energy gain (FR-023).
// ---------------------------------------------------------------------------

// A DRIVEN step (constant Vin through a series resistance) can only supply
// energy into an RLC network -- its stored energy climbs toward the DC
// asymptote and (for an underdamped choice of R, L, C) can legitimately
// overshoot that asymptote before settling, so "energy never increases" is
// NOT a rigorous assertion for that construction (the task's own guidance:
// prefer a source-free/decaying scenario when strict monotonicity is what
// you want to assert). A SOURCE-FREE loop sidesteps that ambiguity entirely:
// with no independent source anywhere in the netlist, the only thing that
// can happen to the energy stored in L and C is dissipation by R -- there is
// nothing left to inject energy. This is also a direct structural
// consequence of the companion model itself: both BackwardEuler and
// Trapezoidal produce companions with Geq >= 0 for L and C (R1/R2 of
// reactive-integrator.h), so at every timestep the assembled network is
// itself an entirely passive resistive network (R plus two nonnegative
// companion conductances) with no independent source -- exactly the
// structural condition under which the classical passivity/Tellegen
// argument guarantees the reconstructed energy cannot increase from one
// sample to the next, for ANY dt > 0 and ANY prior history.
//
// Topology: gnd --R-- n1 --Inductor-- n2 --Capacitor-- gnd (a closed RLC
// loop touching ground at both ends, so Netlist::prepare()'s connectivity
// pre-filter passes trivially -- n1 has a path to ground via R, n2 via C).
// No VoltageSource/CurrentSource anywhere. Seed a nonzero initial capacitor
// voltage (energy) via seedHistory; R, L, C are chosen underdamped
// (zeta ~= 0.158) so energy visibly trades between L and C (ringing) while
// its TOTAL never grows -- a stronger demonstration than an overdamped,
// monotone-decay-only case would be.
struct EnergyTrace {
    double initial;    // E[0], the seeded pre-step energy
    double finalValue; // E[N-1], after the full transient
    bool nonIncreasing; // true iff E[n] <= E[n-1] (+ tiny slack) for every n
};

template <class Rule>
EnergyTrace sourceFreeRlcEnergyTrace() {
    constexpr int kMaxNodes = 3;      // ground, n1, n2
    constexpr int kMaxComponents = 3; // Resistor, Inductor, Capacitor
    constexpr int kMaxBranches = 0;   // no source, no branch-unknown element
    constexpr int kInductorSlot = 0;  // netlist add order: Inductor before Capacitor
    constexpr int kCapacitorSlot = 1;

    constexpr double kR = 10.0;       // ohm
    constexpr double kL = 1.0e-3;     // H
    constexpr double kC = 1.0e-6;     // F  (zeta = (R/2)*sqrt(C/L) ~= 0.158, underdamped)
    constexpr double kV0 = 10.0;      // V, initial capacitor voltage (the seeded energy)
    constexpr double kDt = 1.0e-6;    // s (~200 samples per natural-frequency period)
    constexpr int kSteps = 3000;      // 3ms ~= 15 decay time constants (tau = 2L/R = 2e-4s)

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(Resistor{kGround, n1, kR});
    nl.add(Inductor{n1, n2, kL});
    nl.add(Capacitor{n2, kGround, kC});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> newton;
    ReactiveIntegrator<Rule, kMaxNodes, kMaxComponents, kMaxBranches>
        integrator(kDt);

    integrator.plan(nl, assembler, sys);
    REQUIRE(integrator.reactiveCount() == 2);
    REQUIRE(integrator.reactiveComponentIndex(kInductorSlot) == 1);
    REQUIRE(integrator.reactiveComponentIndex(kCapacitorSlot) == 2);

    // Seed: capacitor charged to kV0, inductor current zero (no seedHistory
    // call needed for the inductor -- plan()/reset() already zero it, S8/RS1).
    integrator.seedHistory(kCapacitorSlot, kV0, 0.0);

    const auto energyOf = [&](double vC, double iL) {
        return 0.5 * kC * vC * vC + 0.5 * kL * iL * iL;
    };

    const double e0 = energyOf(integrator.vPrev(kCapacitorSlot),
                                integrator.iPrev(kInductorSlot));

    double ePrev = e0;
    double eFinal = e0;
    bool nonIncreasing = true;
    // Tiny relative slack against the seeded energy scale to absorb
    // double-precision roundoff in the linear solve/reconstruction -- NOT a
    // physical tolerance; a real energy-gain finding would dwarf this.
    const double slack = 1.0e-9 * e0;

    for (int n = 0; n < kSteps; ++n) {
        const StepResult result = integrator.step(nl, assembler, sys, newton);
        REQUIRE(result.converged);

        const double e = energyOf(integrator.vPrev(kCapacitorSlot),
                                   integrator.iPrev(kInductorSlot));
        if (e > ePrev + slack) {
            nonIncreasing = false;
        }
        ePrev = e;
        eFinal = e;
    }

    return EnergyTrace{e0, eFinal, nonIncreasing};
}

}  // namespace

TEST_SUITE("integration-invariants") {

// ---------------------------------------------------------------------------
// DC steady state (SC-006) -- capacitor -> open, under both rules.
// ---------------------------------------------------------------------------

TEST_CASE("integration-invariants: BackwardEuler -- capacitor reaches an open circuit at DC steady state") {
    const CapacitorDcResult r = capacitorReachesOpenAtDc<BackwardEuler>();

    // Open circuit: reconstructed capacitor current settles to ~0.
    CHECK(std::abs(r.iPrevCap) < 1.0e-6);
    // Node voltage settles to the plain resistive divider value (the
    // capacitor is transparent to the DC operating point once settled).
    CHECK(r.nodeVoltage == doctest::Approx(5.0 * 2.0e3 / (1.0e3 + 2.0e3)).epsilon(1e-4));
}

TEST_CASE("integration-invariants: Trapezoidal -- capacitor reaches an open circuit at DC steady state") {
    const CapacitorDcResult r = capacitorReachesOpenAtDc<Trapezoidal>();

    CHECK(std::abs(r.iPrevCap) < 1.0e-6);
    CHECK(r.nodeVoltage == doctest::Approx(5.0 * 2.0e3 / (1.0e3 + 2.0e3)).epsilon(1e-4));
}

// ---------------------------------------------------------------------------
// DC steady state (SC-006) -- inductor -> short, under both rules (the dual).
// ---------------------------------------------------------------------------

TEST_CASE("integration-invariants: BackwardEuler -- inductor reaches a short circuit at DC steady state") {
    const InductorDcResult r = inductorReachesShortAtDc<BackwardEuler>();

    // Short circuit: reconstructed terminal voltage settles to ~0.
    CHECK(std::abs(r.vPrevInductor) < 1.0e-3);
    // Current settles to the DC operating point Vin/R.
    CHECK(r.iPrevInductor == doctest::Approx(5.0 / 1.0e3).epsilon(1e-4));
}

TEST_CASE("integration-invariants: Trapezoidal -- inductor reaches a short circuit at DC steady state") {
    const InductorDcResult r = inductorReachesShortAtDc<Trapezoidal>();

    CHECK(std::abs(r.vPrevInductor) < 1.0e-3);
    CHECK(r.iPrevInductor == doctest::Approx(5.0 / 1.0e3).epsilon(1e-4));
}

// ---------------------------------------------------------------------------
// Passivity / no energy gain (FR-023) -- source-free RLC ring-down, under
// both rules.
// ---------------------------------------------------------------------------

TEST_CASE("integration-invariants: BackwardEuler -- source-free passive RLC never gains stored energy") {
    const EnergyTrace trace = sourceFreeRlcEnergyTrace<BackwardEuler>();

    CHECK(trace.nonIncreasing);
    // With no source, the lone resistor must have dissipated essentially all
    // of the seeded energy by 15 decay time constants.
    CHECK(trace.finalValue < 1.0e-6 * trace.initial);
}

TEST_CASE("integration-invariants: Trapezoidal -- source-free passive RLC never gains stored energy") {
    const EnergyTrace trace = sourceFreeRlcEnergyTrace<Trapezoidal>();

    CHECK(trace.nonIncreasing);
    CHECK(trace.finalValue < 1.0e-6 * trace.initial);
}

}  // TEST_SUITE("integration-invariants")

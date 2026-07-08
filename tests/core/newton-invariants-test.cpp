#include <doctest/doctest.h>

#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/companion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// T019 -- Polish: physical invariants of the solved diode networks (SC-006,
// FR-022; specs/newton-iteration/tasks.md).
//
// Where newton-solver-test.cpp proves the SOLVER's own contract (plan/solve
// mechanics, statelessness, RT-safety), this suite proves the PHYSICS of
// what the solver converges to is actually correct -- properties that hold
// for the underlying circuit regardless of any Newton-loop implementation
// detail:
//   1. Odd symmetry of a matched antiparallel clipper's transfer curve.
//   2. I(0) == 0 / zero output at zero drive.
//   3. Monotonicity of the transfer curve.
//   4. Passivity of the diode elements at the converged operating point.
//
// Shared topology (every case below): a SYMMETRIC antiparallel diode
// clipper --
//   VoltageSource{n1, gnd, Vin} -> Resistor{n1, n2, R} -> antiparallel
//   matched diode pair at the port node n2:
//     D1: anode n2,   cathode gnd   (conducts when V(n2) > 0)
//     D2: anode gnd,  cathode n2    (conducts when V(n2) < 0)
//   both diodes share IDENTICAL (Is, n, Vt) -- the match that makes the
//   transfer curve odd by construction: negating Vin swaps the roles of D1
//   and D2 exactly, so the circuit as a whole is symmetric under
//   (Vin, V(n2)) -> (-Vin, -V(n2)).
//
// n1 reaches ground through the VoltageSource; n2 reaches ground through
// the Resistor and then the VoltageSource -- an R/V path, so
// Netlist::prepare()'s floating-node check (which does not count diode
// edges) is satisfied without any extra grounding resistor.

using acfx::Diode;
using acfx::DiodeSample;
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

// Trivial base companion supply (this suite has no reactive elements, so
// the base is never actually consulted -- see newton-solver-test.cpp case
// 5d for why).
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

constexpr int kMaxNodes = 8;
constexpr int kMaxComponents = 8;
constexpr int kMaxBranches = 4;
constexpr double kIs = 1e-14;
constexpr double kN = 1.0;
constexpr double kVt = 0.025852;
constexpr double kR = 1000.0;

// One converged solve of the shared antiparallel-clipper topology at a
// given Vin, from a cold (zero) guess. Returns the converged port voltage
// V(n2) and, via the output parameters, each diode's bias and Shockley
// sample {current, conductance} EVALUATED AT THE CONVERGED OPERATING POINT
// -- exactly the physics the diode itself defines, independent of whatever
// companion linearization the solver used internally to get there.
NewtonStatus solveClipper(double vin, double& portVoltage,
                           double& d1VAK, DiodeSample& d1Sample,
                           double& d2VAK, DiodeSample& d2Sample) {
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, vin});
    nl.add(Resistor{n1, n2, kR});
    const Diode d1{n2, kGround, kIs, kN, kVt};   // conducts when V(n2) > 0
    const Diode d2{kGround, n2, kIs, kN, kVt};   // conducts when V(n2) < 0
    nl.add(d1);
    nl.add(d2);
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    solver.plan(nl, assembler, sys);
    const std::array<double, kMaxNodes> guess{};
    const NewtonStatus status = solver.solve(nl, base, guess, assembler, sys);

    portVoltage = sys.nodeVoltage(n2);
    d1VAK = sys.nodeVoltage(n2) - sys.nodeVoltage(kGround);
    d2VAK = sys.nodeVoltage(kGround) - sys.nodeVoltage(n2);
    d1Sample = d1.evaluate(d1VAK);
    d2Sample = d2.evaluate(d2VAK);

    return status;
}

// Convenience overload for cases that only need the port voltage.
double portVoltageAt(double vin) {
    double portVoltage = 0.0;
    double d1VAK = 0.0, d2VAK = 0.0;
    DiodeSample d1Sample{}, d2Sample{};
    const NewtonStatus status =
        solveClipper(vin, portVoltage, d1VAK, d1Sample, d2VAK, d2Sample);
    REQUIRE(status.converged);
    return portVoltage;
}

}  // namespace

TEST_SUITE("newton-invariants") {

// ---------------------------------------------------------------------------
// 1. Odd symmetry (SC-006): for a matched antiparallel pair, the
// clipping-port transfer curve V_port(Vin) is an ODD function --
// V_port(-Vin) == -V_port(+Vin) -- to tight tolerance. Swept over a set of
// levels spanning well below and well above the diodes' forward-conduction
// threshold, so the check exercises both the linear (near-zero) and
// clipped (saturated) regions of the curve.
// ---------------------------------------------------------------------------

TEST_CASE("newton-invariants: matched antiparallel clipper transfer curve is odd (SC-006)") {
    const double levels[] = {0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 1.0, 1.5, 2.0, 3.0};

    double maxDeviation = 0.0;
    for (const double vin : levels) {
        const double vPos = portVoltageAt(vin);
        const double vNeg = portVoltageAt(-vin);
        const double deviation = std::abs(vPos + vNeg);
        maxDeviation = std::max(maxDeviation, deviation);
        CHECK_MESSAGE(deviation < 1e-7,
                      "odd-symmetry violated at Vin = ", vin,
                      ": V_port(+Vin) = ", vPos, ", V_port(-Vin) = ", vNeg,
                      ", |sum| = ", deviation);
    }
    CHECK(maxDeviation < 1e-7);
}

// ---------------------------------------------------------------------------
// 2. I(0) == 0 / zero output at zero drive: at Vin = 0 the port voltage is
// 0 to solver tolerance, and by extension (Shockley: I(0) = Is*(exp(0)-1)
// = 0 exactly) each diode's own current at that bias is ~0.
// ---------------------------------------------------------------------------

TEST_CASE("newton-invariants: zero drive yields zero port voltage and zero diode current (SC-006)") {
    double portVoltage = 0.0;
    double d1VAK = 0.0, d2VAK = 0.0;
    DiodeSample d1Sample{}, d2Sample{};
    const NewtonStatus status =
        solveClipper(0.0, portVoltage, d1VAK, d1Sample, d2VAK, d2Sample);

    REQUIRE(status.converged);

    CHECK(std::abs(portVoltage) < 1e-9);
    CHECK(std::abs(d1VAK) < 1e-9);
    CHECK(std::abs(d2VAK) < 1e-9);
    // Shockley: I(0) = Is*(exp(0/nVt) - 1) = Is*(1-1) = 0 exactly, so any
    // residual current at this bias is purely a consequence of the
    // converged bias's own (tiny) deviation from exactly 0 -- bounded well
    // under Is itself.
    CHECK(std::abs(d1Sample.current) < 1e-9);
    CHECK(std::abs(d2Sample.current) < 1e-9);
}

// ---------------------------------------------------------------------------
// 3. Monotonic transfer: sweeping Vin strictly increasing over a range that
// spans the linear region through both clipping shoulders, V_port must be
// monotonically non-decreasing at every step -- strictly increasing in the
// unclipped middle, and non-decreasing (flattening, never reversing) once
// clipping saturates each shoulder.
// ---------------------------------------------------------------------------

TEST_CASE("newton-invariants: transfer curve is monotonically non-decreasing over a full sweep (SC-006)") {
    const double sweep[] = {-3.0, -2.0, -1.5, -1.0, -0.7, -0.5, -0.3, -0.2,
                             -0.1, -0.05, 0.0, 0.05, 0.1, 0.2, 0.3, 0.5,
                             0.7, 1.0, 1.5, 2.0, 3.0};

    double prevVout = -1.0e9;  // sentinel below any real port voltage
    for (const double vin : sweep) {
        const double vout = portVoltageAt(vin);
        CHECK_MESSAGE(vout >= prevVout - 1e-12,
                      "non-monotonic step at Vin = ", vin,
                      ": V_port = ", vout, ", previous V_port = ", prevVout);
        prevVout = vout;
    }

    // Sanity: the unclipped middle of the sweep is NOT flat -- proves this
    // is a real monotonic increase, not a vacuously-passing constant curve.
    const double vMidLow = portVoltageAt(-0.1);
    const double vMidHigh = portVoltageAt(0.1);
    CHECK(vMidHigh > vMidLow);
}

// ---------------------------------------------------------------------------
// 4. Passivity: at the converged operating point, each diode's own power
// P = vAK * I(vAK) is non-negative to solver tolerance. This is the
// cleanest concrete statement of "a diode never sources energy": forward
// bias has vAK > 0 and I(vAK) > 0 (P > 0, absorbing power); reverse bias
// has vAK < 0 and I(vAK) ~ -Is < 0 (P = (-)*(-) > 0, still absorbing -- the
// reverse-saturation trickle still dissipates, it never generates). A
// passive element's constitutive law can never put vAK and I(vAK) on
// opposite signs, so P >= 0 at every bias holds by construction of the
// Shockley law itself; this test proves the SOLVER's converged samples obey
// it in practice, across the same drive levels used for the odd-symmetry
// and monotonicity checks above (unclipped, forward-clipped, and
// reverse-clipped operating points alike).
// ---------------------------------------------------------------------------

TEST_CASE("newton-invariants: diodes dissipate non-negative power at the converged operating point (SC-006)") {
    const double levels[] = {-3.0, -1.5, -0.7, -0.3, -0.1, 0.0,
                              0.1, 0.3, 0.7, 1.5, 3.0};
    constexpr double kEpsilon = 1e-12;

    double minPower = 1.0e9;
    for (const double vin : levels) {
        double portVoltage = 0.0;
        double d1VAK = 0.0, d2VAK = 0.0;
        DiodeSample d1Sample{}, d2Sample{};
        const NewtonStatus status =
            solveClipper(vin, portVoltage, d1VAK, d1Sample, d2VAK, d2Sample);
        REQUIRE(status.converged);

        const double d1Power = d1VAK * d1Sample.current;
        const double d2Power = d2VAK * d2Sample.current;
        minPower = std::min(minPower, std::min(d1Power, d2Power));

        CHECK_MESSAGE(d1Power >= -kEpsilon,
                      "D1 sourced energy at Vin = ", vin, ": vAK = ", d1VAK,
                      ", I = ", d1Sample.current, ", P = ", d1Power);
        CHECK_MESSAGE(d2Power >= -kEpsilon,
                      "D2 sourced energy at Vin = ", vin, ": vAK = ", d2VAK,
                      ", I = ", d2Sample.current, ", P = ", d2Power);
    }
    CHECK(minPower >= -kEpsilon);
}

}  // TEST_SUITE("newton-invariants")

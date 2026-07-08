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
#include <stdexcept>

// T005 -- Newton-Raphson iteration primitive, RED closed-form doctest
// (specs/newton-iteration/tasks.md T005; spec.md US1; SC-001).
//
// NewtonSolver EXISTS (T004: construction, two-phase plan(), accessors) but
// solve() is still a marked placeholder that always returns
// NewtonStatus{converged=false, iterations=0} without ever calling
// MnaAssembler::refresh / MnaSystem::solve (T006/US1 lands the real loop).
// So every case in this suite COMPILES against the real API and FAILS AT
// RUNTIME (wrong converged/iterations/voltage) -- that runtime failure is
// the intentional RED this task produces; T006 turns it green.
//
// Coverage:
//   1. Single diode + series resistor + DC source (SC-001, S1-S6): the
//      converged diode-node voltage must match an INDEPENDENTLY-COMPUTED
//      operating point across several forward and reverse Vs levels, and
//      the transfer curve v(n2) vs Vs must be strictly monotonic.
//   2. Zero-diode resistor-divider netlist (S6): exactly ONE linear solve
//      (status.iterations == 1), converged == true, and the node voltage
//      equals the exact linear-divider result.
//
// Independent reference (the oracle): bisectDiodeOperatingPoint() below
// solves the scalar KCL equation at n2,
//     (Vs - vD)/R = Is*(exp(vD/(n*Vt)) - 1),
// i.e. the root of f(vD) = (Vs - vD)/R - Is*(exp(vD/(n*Vt)) - 1), by plain
// bisection. f is strictly monotonically DECREASING in vD (the left term
// decreases linearly; the subtracted exponential term increases), so a
// sign change on a wide bracket brackets exactly one root. This helper
// never touches NewtonSolver -- it is the ground truth the solver's output
// is checked against.

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

// Trivial base companion supply for solve() (contract's BaseCompanionSupply
// template parameter). Neither test circuit has any reactive element, so
// this is never actually consulted for a non-diode component here -- solve()
// still requires a supply argument to call.
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

constexpr int kMaxNodes = 8;
constexpr int kMaxComponents = 8;
constexpr int kMaxBranches = 4;

// The single-diode operating-point circuit's fixed element values (matches
// the contract's netlist: VoltageSource{n1,gnd,Vs} -- Resistor{n1,n2,R} --
// Diode{n2,gnd,Is,n,Vt}).
constexpr double kSeriesR = 1000.0;
constexpr double kDiodeIs = 1e-14;
constexpr double kDiodeN = 1.0;
constexpr double kDiodeVt = 0.025852;

// f(vD) = (Vs - vD)/R - Is*(exp(vD/(n*Vt)) - 1): the scalar KCL residual at
// n2 for the single-diode circuit. Strictly decreasing in vD.
double diodeKclResidual(double vD, double Vs, double R, double Is, double n,
                        double Vt) {
    return (Vs - vD) / R - Is * (std::exp(vD / (n * Vt)) - 1.0);
}

// Independent oracle (does NOT use NewtonSolver): bisect diodeKclResidual
// over a bracket wide enough to always contain the root, to ~1e-13.
double bisectDiodeOperatingPoint(double Vs, double R, double Is, double n,
                                 double Vt) {
    double lo = -std::fabs(Vs) - 1.0;
    double hi = std::fabs(Vs) + 1.0;
    const double fLo = diodeKclResidual(lo, Vs, R, Is, n, Vt);
    const double fHi = diodeKclResidual(hi, Vs, R, Is, n, Vt);
    if (!(fLo > 0.0) || !(fHi < 0.0)) {
        throw std::logic_error(
            "bisectDiodeOperatingPoint: bracket does not contain a sign "
            "change -- oracle precondition violated");
    }

    for (int iter = 0; iter < 200; ++iter) {
        const double mid = 0.5 * (lo + hi);
        const double fMid = diodeKclResidual(mid, Vs, R, Is, n, Vt);
        if (fMid > 0.0) {
            lo = mid;
        } else {
            hi = mid;
        }
        if (hi - lo < 1e-13) {
            break;
        }
    }
    return 0.5 * (lo + hi);
}

// Result of solving the single-diode circuit at a given source level Vs.
struct SingleDiodeResult {
    bool converged;
    int iterations;
    double vD;  // solved v(n2), the diode anode voltage
};

// Build the single-diode circuit fresh (VoltageSource.V is fixed at
// construction, so each Vs level needs its own Netlist/system/solver),
// plan() it, solve() from a zero initial guess, and read back v(n2).
SingleDiodeResult solveSingleDiodeCircuit(double Vs) {
    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, Vs});
    nl.add(Resistor{n1, n2, kSeriesR});
    nl.add(Diode{n2, kGround, kDiodeIs, kDiodeN, kDiodeVt});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    solver.plan(nl, assembler, sys);

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    const NewtonStatus status =
        solver.solve(nl, base, initialNodeVoltages, assembler, sys);

    return SingleDiodeResult{status.converged, status.iterations,
                             sys.nodeVoltage(n2)};
}

}  // namespace

TEST_SUITE("newton-closed-form") {

// ---------------------------------------------------------------------------
// 1a. Single diode operating point matches the bisection reference across
// several FORWARD-bias source levels (SC-001, S1-S6).
// ---------------------------------------------------------------------------

TEST_CASE("newton-closed-form: single-diode operating point matches bisection reference under forward bias (SC-001)") {
    for (const double Vs : {0.2, 0.5, 0.7, 1.0, 5.0}) {
        CAPTURE(Vs);
        const SingleDiodeResult result = solveSingleDiodeCircuit(Vs);
        const double reference =
            bisectDiodeOperatingPoint(Vs, kSeriesR, kDiodeIs, kDiodeN, kDiodeVt);

        CHECK(result.converged);
        CHECK(result.vD == doctest::Approx(reference).epsilon(1e-9));
    }
}

// ---------------------------------------------------------------------------
// 1b. Single diode operating point matches the bisection reference across
// several REVERSE-bias source levels (SC-001, S1-S6).
// ---------------------------------------------------------------------------

TEST_CASE("newton-closed-form: single-diode operating point matches bisection reference under reverse bias (SC-001)") {
    for (const double Vs : {-1.0, -5.0}) {
        CAPTURE(Vs);
        const SingleDiodeResult result = solveSingleDiodeCircuit(Vs);
        const double reference =
            bisectDiodeOperatingPoint(Vs, kSeriesR, kDiodeIs, kDiodeN, kDiodeVt);

        CHECK(result.converged);
        CHECK(result.vD == doctest::Approx(reference).epsilon(1e-9));
    }
}

// ---------------------------------------------------------------------------
// 1c. The transfer curve v(n2) vs Vs is strictly monotonically increasing
// across a combined forward+reverse sweep (SC-001).
// ---------------------------------------------------------------------------

TEST_CASE("newton-closed-form: single-diode transfer curve v(n2) is strictly monotonic across the Vs sweep (SC-001)") {
    const std::array<double, 7> sweep{-5.0, -1.0, 0.2, 0.5, 0.7, 1.0, 5.0};

    double previous = -1.0e300;
    for (const double Vs : sweep) {
        CAPTURE(Vs);
        const SingleDiodeResult result = solveSingleDiodeCircuit(Vs);
        CHECK(result.converged);
        CHECK(result.vD > previous);
        previous = result.vD;
    }
}

// ---------------------------------------------------------------------------
// 2. Zero-diode resistor-divider netlist converges in EXACTLY one linear
// solve (S6): no nonlinearity means the Newton loop must recognize the
// system is already linear and stop after the first (and only) MNA solve.
// ---------------------------------------------------------------------------

TEST_CASE("newton-closed-form: zero-diode resistor divider converges in exactly one linear solve (S6)") {
    constexpr double kVin = 10.0;
    constexpr double kR1 = 1000.0;
    constexpr double kR2 = 2000.0;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, kVin});
    nl.add(Resistor{n1, n2, kR1});
    nl.add(Resistor{n2, kGround, kR2});
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    solver.plan(nl, assembler, sys);

    const std::array<double, kMaxNodes> initialNodeVoltages{};
    const NewtonStatus status =
        solver.solve(nl, base, initialNodeVoltages, assembler, sys);

    CHECK(status.converged);
    CHECK(status.iterations == 1);

    const double expected = kVin * kR2 / (kR1 + kR2);
    CHECK(sys.nodeVoltage(n2) == doctest::Approx(expected).epsilon(1e-12));
}

}  // TEST_SUITE("newton-closed-form")

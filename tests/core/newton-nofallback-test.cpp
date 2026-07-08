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
#include <cstddef>

// T016 -- NewtonSolver no-fallback / surfaced-failure tests (US6, S7/S8, SC-004;
// Constitution Principle V). NewtonSolver::solve() must SURFACE both of its
// failure modes BY VALUE -- never fabricate an output, never gmin/source-step,
// never throw on the hot path -- and must corrupt no state usable by a later
// solve():
//
//   1. Non-convergence within the iteration bound: a real diode circuit driven
//      with a deliberately tight voltageTol and a low maxIterations so it
//      cannot converge in the bound. converged == false, iterations ==
//      maxIterations (the whole bound was consumed), both residuals reported
//      (finite), and the last (non-converged) iterate is left in the
//      MnaSystem -- never a fabricated/zeroed substitute. A second, IDENTICAL
//      solve() on the same instances reproduces the same outcome exactly (S8):
//      a non-converged solve leaves no corrupt state behind.
//
//   2. Singular linearized system: a netlist whose linearized MNA system is
//      structurally singular, so MnaSystem::solve() returns false. solve()
//      must not throw and must return converged == false BY VALUE, with no
//      gmin injection / source stepping / substituted output (verified by
//      inspection in T017 -- newton-solver.h has no such code path at all).
//
// See newton-solver-test.cpp for the ZeroCompanionSupply pattern this suite
// mirrors.

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

// Trivial base companion supply (contract's BaseCompanionSupply template
// parameter) -- mirrors newton-solver-test.cpp's ZeroCompanionSupply. Neither
// case below has a reactive (Capacitor/Inductor) element, so the base is
// never actually consulted; solve() still requires a supply argument.
struct ZeroCompanionSupply {
    acfx::Companion at(int /*componentIndex*/) const noexcept {
        return acfx::Companion{0.0, 0.0};
    }
};

}  // namespace

TEST_SUITE("newton-nofallback") {

// ---------------------------------------------------------------------------
// 1. Forced non-convergence (S7, S8, SC-004). A single diode + series
// resistor + source at a healthy forward drive (Vs = 0.7 V -- the same
// topology every other newton-solver-test.cpp convergence case uses, which
// DOES converge under the default tolerances) is instead solved with an
// astronomically tight voltageTol (1e-15, well beyond double precision's
// useful range for this circuit) and a starved maxIterations (2). A
// Shockley-law diode circuit from a cold (zero) guess needs many more than 2
// damped Newton steps to move max|dV| below 1e-15, so this configuration
// cannot converge within the bound -- the loop runs to exhaustion.
// ---------------------------------------------------------------------------

TEST_CASE("newton-nofallback: non-convergence within the bound is surfaced by value, never fabricated (S7/SC-004)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;
    constexpr int kMaxIterations = 2;       // deliberately starved
    constexpr double kVoltageTol = 1e-15;   // deliberately unreachable

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 0.7});           // index 0
    nl.add(Resistor{n1, n2, 1000.0});                  // index 1
    nl.add(Diode{n2, kGround, kIs, kN, kVt});          // index 2 -- anode n2, cathode gnd
    nl.prepare();

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver(
        kMaxIterations, kVoltageTol, /*currentTol*/ 1e-12);
    ZeroCompanionSupply base;

    CHECK_NOTHROW(solver.plan(nl, assembler, sys));

    const std::array<double, kMaxNodes> initialNodeVoltages{};

    NewtonStatus status;
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    // The bound was genuinely exhausted, not silently under-iterated or
    // fabricated: converged == false and iterations consumed the WHOLE bound.
    CHECK_FALSE(status.converged);
    CHECK(status.iterations == kMaxIterations);

    // Residuals are REPORTED (not suppressed/zeroed on failure) and finite --
    // a real measurement of how far the last iterate is from the gate, never
    // a placeholder.
    CHECK(status.voltageResidual > 0.0);
    CHECK(std::isfinite(status.voltageResidual));
    CHECK(std::isfinite(status.currentResidual));

    // The node voltages left in `sys` are the LAST iterate -- finite, real
    // numbers reflecting genuine (if non-converged) circuit state, never a
    // fabricated/zeroed substitute for the "answer".
    CHECK(std::isfinite(sys.nodeVoltage(n1)));
    CHECK(std::isfinite(sys.nodeVoltage(n2)));
    const double v1n1 = sys.nodeVoltage(n1);
    const double v1n2 = sys.nodeVoltage(n2);

    // S8: no state corruption. An IDENTICAL second solve() on the SAME
    // solver/assembler/sys instances must reproduce the SAME non-convergence
    // outcome exactly -- a non-converged solve leaves no corrupt scratch
    // behind that could poison a later call. Same inputs -> identical
    // floating-point ops -> bit-identical results, so exact == is correct
    // here (not an approximate comparison).
    NewtonStatus status2;
    CHECK_NOTHROW(status2 = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    CHECK_FALSE(status2.converged);
    CHECK(status2.iterations == status.iterations);
    CHECK(status2.voltageResidual == status.voltageResidual);
    CHECK(status2.currentResidual == status.currentResidual);
    CHECK(sys.nodeVoltage(n1) == v1n1);
    CHECK(sys.nodeVoltage(n2) == v1n2);
}

// ---------------------------------------------------------------------------
// 2. Singular linearized system (S7, SC-004). Mechanism: TWO ideal
// VoltageSource components are placed between the SAME node pair (n1, ground)
// with DIFFERENT imposed voltages (5.0 V and 3.0 V). Each is individually
// well-formed -- MnaAssembler::plan() only rejects a source with COINCIDENT
// terminals (p == n) within a single element, and there is no cross-component
// duplicate-source check -- so plan() never throws. But the two branch
// constraint rows both reduce to "1*v(n1) = <value>" with a zero branch
// diagonal (an ideal source has no series resistance): the augmented
// coefficient matrix has two IDENTICAL branch rows (same v(n1) coefficient,
// same all-zero rest), which is structurally rank-deficient regardless of the
// diode's own conductance on that node. Gaussian elimination (mna-system.h
// solve()) finds no nonzero pivot for the last column and returns false --
// mirrors mna-system-test.cpp's floating/singular-row cases (search
// "singular" there), adapted here to route through a prepared Netlist ->
// MnaAssembler -> NewtonSolver rather than stamping MnaSystem directly.
//
// A Diode{n1, kGround, ...} is included so plan() records diodeCount() == 1
// (the solve path actually exercises the diode-linearization step before
// hitting the singular sys.solve()); the singularity itself comes entirely
// from the duplicate voltage-source branch rows, not from the diode.
//
// Both VoltageSource edges independently satisfy Netlist::prepare()'s
// floating-node check (each contributes a conductive path n1<->ground), so
// prepare() also never throws -- the ONLY failure surfaces at
// MnaSystem::solve() time, exactly the S7 contract.
// ---------------------------------------------------------------------------

TEST_CASE("newton-nofallback: a singular linearized system is surfaced by value with no throw and no fabricated output (S7/SC-004)") {
    constexpr int kMaxNodes = 8;
    constexpr int kMaxComponents = 8;
    constexpr int kMaxBranches = 4;
    constexpr double kIs = 1e-14;
    constexpr double kN = 1.0;
    constexpr double kVt = 0.025852;

    Netlist<kMaxNodes, kMaxComponents> nl;
    const NodeId n1 = nl.addNode();

    nl.add(VoltageSource{n1, kGround, 5.0});           // index 0 -- v(n1) = 5
    nl.add(VoltageSource{n1, kGround, 3.0});           // index 1 -- v(n1) = 3 (contradicts index 0)
    nl.add(Diode{n1, kGround, kIs, kN, kVt});          // index 2 -- recorded by plan(), not the cause
    nl.prepare();  // both sources ground n1 -> no floating-node throw

    MnaSystem<kMaxNodes, kMaxBranches> sys;
    MnaAssembler<kMaxNodes, kMaxComponents, kMaxBranches> assembler;
    NewtonSolver<kMaxNodes, kMaxComponents, kMaxBranches> solver;
    ZeroCompanionSupply base;

    // plan() never throws: each VoltageSource has distinct terminals (n1 !=
    // ground) on its own -- the duplicate-source contradiction is a LINEAR
    // structural singularity, not a plan-time-detectable precondition
    // violation.
    CHECK_NOTHROW(solver.plan(nl, assembler, sys));
    CHECK(solver.diodeCount() == 1);

    const std::array<double, kMaxNodes> initialNodeVoltages{};

    NewtonStatus status;
    // The hot path never throws, even when the linearized system is singular
    // (S7): no exception escapes solve().
    CHECK_NOTHROW(status = solver.solve(nl, base, initialNodeVoltages, assembler, sys));

    // Surfaced BY VALUE: converged == false, no fabricated "converged" result.
    CHECK_FALSE(status.converged);

    // The singularity is structural (present from the very first
    // linearization), so solve() fails on iteration 1 -- it does not loop
    // pretending progress is being made against an unsolvable system.
    CHECK(status.iterations == 1);

    // No fabricated output: MnaSystem::solve() leaves every readable output
    // zeroed (never NaN) on a singular pivot (mna-system.h contract, D7), and
    // NewtonSolver applies no gmin injection / source stepping / substituted
    // value on top of that -- there is no such code path in newton-solver.h
    // at all (verified by inspection in T017). The values read back here are
    // finite by construction, not a fabricated "answer".
    CHECK(std::isfinite(sys.nodeVoltage(n1)));
}

}  // TEST_SUITE("newton-nofallback")

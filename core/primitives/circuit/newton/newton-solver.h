#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

// NewtonSolver — the nonlinear outer loop that drives the linear MnaSystem to a
// self-consistent operating point (contracts/newton-solver.md; data-model.md
// "NewtonSolver"). Each iteration linearizes every diode into a Norton
// companion, composes those companions over the caller's base supply, refreshes
// and solves the MNA system once (coupled multi-diode), damps the new junction
// biases through pnjlim, and gates convergence on max|Δv| < voltageTol.
//
// Two-phase surface, mirroring MnaAssembler (D4):
//   - plan(nl, assembler, sys) : the THROWING, once-per-topology pass. Delegates
//     branch allocation + topology validation to MnaAssembler::plan, then scans
//     the netlist once to record the diode component indices and the
//     per-component is-diode mask. Off the hot path.
//   - solve(...) : the throw-free, allocation-free hot path. Runs the Newton
//     iteration. A precondition violation (solve() before plan()) is surfaced
//     deterministically BY VALUE, never as UB (S10).
//
// RT-safety (Principle VI): every buffer is a fixed-capacity std::array sized by
// the template parameters — zero heap on solve(); no locks. No fallbacks
// (Principle V): plan() throws on an unrepresentable netlist; solve() never
// fabricates an output, returning converged == false on singular/non-converged.
//
// This header lands the TYPE SURFACE + construction validation + two-phase
// plan() + read accessors + the pre-plan solve() guard. The Newton iteration
// loop itself is the next increment (T006/US1); solve() is a marked placeholder.
//
// C++17, standard library only; no platform or component-graphics headers.
// double throughout.

namespace acfx::newton {

// The per-solve result value returned by solve() (data-model.md "NewtonStatus";
// contract Types). `converged == false` is a legitimate, surfaced outcome, NOT
// an error: when false, the node voltages left in the MnaSystem are the last
// (non-converged) iterate and must not be trusted as a physical answer.
struct NewtonStatus {
    bool   converged      = false;  // max|Δv| < voltageTol within the bound?
    int    iterations     = 0;      // iterations consumed (≤ maxIterations)
    double voltageResidual = 0.0;   // final max|Δv| across diode biases (V)
    double currentResidual = 0.0;   // final |ΔI_total| of diode current (A)
};

template <int MaxNodes, int MaxComponents, int MaxBranches>
class NewtonSolver {
public:
    static_assert(MaxNodes >= 1, "NewtonSolver requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents >= 1,
                  "NewtonSolver requires MaxComponents >= 1");
    static_assert(MaxBranches >= 0, "NewtonSolver requires MaxBranches >= 0");

    // Solver-internal companion adapter handed to MnaAssembler::refresh
    // (data-model.md "ComposedCompanionSupply"). Satisfies MNA's CompanionSupply
    // contract — `Companion at(int) const noexcept` — by returning Newton's
    // per-iteration diode companion at diode component indices and delegating to
    // the caller's base supply everywhere else. O(1), noexcept, allocation-free.
    // Nested so it names the enclosing MaxComponents for the array sizes.
    template <class Base>
    struct ComposedCompanionSupply {
        const Base& base;
        const std::array<Companion, MaxComponents>& diodeCompanion;
        const std::array<bool, MaxComponents>& isDiode;

        Companion at(int i) const noexcept {
            return isDiode[static_cast<std::size_t>(i)]
                       ? diodeCompanion[static_cast<std::size_t>(i)]
                       : base.at(i);
        }
    };

    // Construction (contract C1/C2). Defaults are the lab-validated values;
    // callers may tighten. Off the hot path, so an out-of-domain configuration
    // throws std::invalid_argument rather than degrading silently (Principle V):
    // the solver MUST NEVER retune these to mask a non-converging case.
    explicit NewtonSolver(int maxIterations = 50, double voltageTol = 1e-9,
                          double currentTol = 1e-12)
        : maxIterations_(maxIterations),
          voltageTol_(voltageTol),
          currentTol_(currentTol) {
        if (maxIterations < 1) {
            throw std::invalid_argument(
                "NewtonSolver: maxIterations must be >= 1 (got " +
                std::to_string(maxIterations) + ")");
        }
        if (!(voltageTol > 0.0)) {
            throw std::invalid_argument(
                "NewtonSolver: voltageTol must be > 0 (got " +
                std::to_string(voltageTol) + ")");
        }
        if (!(currentTol > 0.0)) {
            throw std::invalid_argument(
                "NewtonSolver: currentTol must be > 0 (got " +
                std::to_string(currentTol) + ")");
        }
    }

    // Plan phase (contract P1/P2/P3): delegate branch allocation + topology
    // validation to MnaAssembler::plan (throw-permitted, off the hot path), then
    // scan the netlist ONCE to record the diode component indices and the
    // per-component is-diode mask. Performs no diode physics and no solve.
    // Re-plannable: the mask/count are reset at the top of the scan.
    void plan(const Netlist<MaxNodes, MaxComponents>& nl,
              mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& assembler,
              mna::MnaSystem<MaxNodes, MaxBranches>& sys) {
        // P1: branch allocation + MNA-specific topology validation (may throw).
        assembler.plan(nl, sys);

        // P2: single diode scan. Reset first so re-planning does not accumulate.
        diodeCount_ = 0;
        isDiode_.fill(false);

        const auto components = nl.components();
        for (std::size_t i = 0; i < components.size(); ++i) {
            const bool diode = acfx::isNonlinear(components[i]);
            isDiode_[i] = diode;
            if (diode) {
                diodeComponentIndex_[static_cast<std::size_t>(diodeCount_)] =
                    static_cast<int>(i);
                ++diodeCount_;
            }
        }

        planned_ = true;
    }

    // True once plan() has run (two-phase guard; contract P2/S10).
    bool planned() const noexcept { return planned_; }

    // Number of diodes recorded by the last plan() scan (contract P2).
    int diodeCount() const noexcept { return diodeCount_; }

    // Per-component is-diode mask read accessor (contract P2). Out-of-range
    // indices are not diodes.
    bool isDiodeComponent(int i) const noexcept {
        return i >= 0 && i < MaxComponents &&
               isDiode_[static_cast<std::size_t>(i)];
    }

    // Solve phase (contract S1–S10): the throw-free, allocation-free hot path.
    //
    // The Newton iteration loop is implemented in the NEXT task (T006/US1); this
    // increment lands only the pre-plan guard so the two-phase ordering is
    // observable and the type surface is exercised end to end.
    template <class BaseCompanionSupply>
    NewtonStatus solve(
        const Netlist<MaxNodes, MaxComponents>& nl,
        const BaseCompanionSupply& base,
        const std::array<double, MaxNodes>& initialNodeVoltages,
        mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& assembler,
        mna::MnaSystem<MaxNodes, MaxBranches>& sys) {
        // S10 pre-plan guard: solve() before plan() is a precondition violation.
        // Surface it deterministically BY VALUE — throw-free, allocation-free,
        // and distinguishable from a real non-converged solve (which always runs
        // iterations >= 1, cf. S6) — never as UB.
        if (!planned_) {
            return NewtonStatus{};
        }

        // TODO(T006/US1): Newton iteration loop implemented in the next task.
        (void)nl;
        (void)base;
        (void)initialNodeVoltages;
        (void)assembler;
        (void)sys;
        return NewtonStatus{};
    }

private:
    // ---- configuration (immutable after ctor; data-model.md) -----------------
    int    maxIterations_;
    double voltageTol_;
    double currentTol_;

    // ---- plan-time state (built once by plan(), off the hot path) ------------
    bool                             planned_    = false;
    int                              diodeCount_ = 0;
    std::array<int, MaxComponents>   diodeComponentIndex_{};
    std::array<bool, MaxComponents>  isDiode_{};

    // ---- per-solve scratch (declared now; populated by T006) -----------------
    std::array<Companion, MaxComponents> diodeCompanion_{};
    std::array<double, MaxComponents>    prevBiasAK_{};
};

}  // namespace acfx::newton

#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <variant>
#include <string>
#include <variant>

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
        // Invalidate this solver's plan state BEFORE the throwing delegation
        // (AUDIT-20260708-01): MnaAssembler::plan clears its own branch map first
        // and may then throw (over-capacity / out-of-range / degenerate netlist).
        // If it throws mid-re-plan, leaving planned_ == true here would let a
        // caller that catches the error enter solve() with stale Newton topology
        // and a half-cleared assembler — an inconsistent two-object state. So we
        // mark this solver unplanned first and only re-arm planned_ after the full
        // scan below succeeds; a failed plan() deterministically leaves the solver
        // unplanned (solve() then surfaces the S10 pre-plan guard by value).
        planned_ = false;
        diodeCount_ = 0;
        isDiode_.fill(false);

        // P1: branch allocation + MNA-specific topology validation (may throw).
        assembler.plan(nl, sys);

        // P2: single scan — record the diode topology AND a per-component kind
        // fingerprint (the variant discriminant) so solve() can reject a netlist
        // whose topology drifted from this plan (AUDIT-20260708-04/05).
        const auto components = nl.components();
        plannedComponentCount_ = static_cast<int>(components.size());
        for (std::size_t i = 0; i < components.size(); ++i) {
            componentKind_[i] = components[i].index();
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
    // Runs the Newton–Raphson outer loop. Each iteration linearizes every diode
    // into a Norton companion (S1), composes those over the caller's fixed base
    // supply (S2), refreshes and solves the coupled MNA system once (S3), damps
    // the new junction biases through pnjlim (S4), and gates convergence on
    // max|Δv| < voltageTol only (S5); currentResidual is reported, never gated.
    // A singular solve (S7) or exhausted bound returns converged == false with
    // the last iterate left in `sys`. Zero diodes → exactly one linear solve
    // (S6). All scratch is fixed-capacity member arrays — zero heap (S10).
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

        // Cheap immutable span; cache once (no allocation) — reused every pass.
        const auto components = nl.components();

        // Inconsistent-plan guard (AUDIT-20260708-02/04/05; contract S10): the
        // netlist handed to solve() must have the SAME topology plan() validated,
        // because MnaAssembler::refresh() stamps it through the FIXED plan (its
        // branch map + Newton's diode indices). If the caller passes a netlist
        // whose topology drifted from the plan — a different component count, or
        // ANY component whose kind changed (a formerly-Resistor slot now a
        // branch-bearing VoltageSource/OpAmp carrying a stale `kNoBranch` entry,
        // or a planned Diode slot now a non-diode) — refresh would stamp against a
        // stale plan and Newton's own std::get<Diode> could throw, both on the
        // promised throw-free hot path. Surface that precondition violation
        // deterministically BY VALUE instead. This is a full per-component kind
        // fingerprint (the variant discriminant, which never throws), not just the
        // diode slots — O(componentCount) once per solve, before the loop, no heap.
        // It subsumes the diode-slot and out-of-range checks: a drifted Diode slot
        // or a shorter netlist both fail the count/kind compare below.
        if (static_cast<int>(components.size()) != plannedComponentCount_) {
            return NewtonStatus{};
        }
        for (std::size_t i = 0; i < components.size(); ++i) {
            if (components[i].index() != componentKind_[i]) {
                return NewtonStatus{};
            }
        }

        // Reset per-solve scratch at the top (S8 statelessness): no state from a
        // prior solve leaks into this one.
        diodeCompanion_.fill(Companion{0.0, 0.0});
        prevBiasAK_.fill(0.0);

        // Seed each diode's junction bias from the initial node-voltage guess
        // (S9): vAK = V(anode) - V(cathode) at the guess.
        for (int d = 0; d < diodeCount_; ++d) {
            const std::size_t c =
                static_cast<std::size_t>(diodeComponentIndex_[
                    static_cast<std::size_t>(d)]);
            const auto& diode = std::get<acfx::Diode>(components[c]);
            prevBiasAK_[c] =
                initialNodeVoltages[static_cast<std::size_t>(diode.anode)] -
                initialNodeVoltages[static_cast<std::size_t>(diode.cathode)];
        }

        NewtonStatus status{};
        for (int iter = 0; iter < maxIterations_; ++iter) {
            status.iterations = iter + 1;

            // (a) Linearize every diode at its current bias (S1, S3): one global
            // Norton step. The assembler consumes Companion{Geq,Ieq} as
            //   i(anode,cathode) = Geq·(V(a)-V(c)) - Ieq,
            // so matching the linearization I(v) ≈ I(vAK) + g·(v - vAK) gives
            // Geq = g and Ieq = g·vAK - I(vAK).
            for (int d = 0; d < diodeCount_; ++d) {
                const std::size_t c =
                    static_cast<std::size_t>(diodeComponentIndex_[
                        static_cast<std::size_t>(d)]);
                const auto& diode = std::get<acfx::Diode>(components[c]);
                const double vAK = prevBiasAK_[c];
                const DiodeSample s = diode.evaluate(vAK);
                diodeCompanion_[c] =
                    Companion{s.conductance, s.conductance * vAK - s.current};
            }

            // (b) Compose + refresh + solve once (S2, S3).
            ComposedCompanionSupply<BaseCompanionSupply> supply{
                base, diodeCompanion_, isDiode_};
            assembler.refresh(nl, supply, sys);
            if (!sys.solve()) {
                // S7 singular: surface by value — no throw, no gmin/substitution.
                status.converged = false;
                return status;
            }

            // (c) Read new biases, damp (pnjlim, S4), compute residuals.
            double maxDeltaV = 0.0;
            double currentResidual = 0.0;
            for (int d = 0; d < diodeCount_; ++d) {
                const std::size_t c =
                    static_cast<std::size_t>(diodeComponentIndex_[
                        static_cast<std::size_t>(d)]);
                const auto& diode = std::get<acfx::Diode>(components[c]);
                const double vOld = prevBiasAK_[c];
                const double vNewRaw = sys.nodeVoltage(diode.anode) -
                                       sys.nodeVoltage(diode.cathode);
                const double vNew = diode.limitJunctionVoltage(vNewRaw, vOld);
                maxDeltaV = std::max(maxDeltaV, std::fabs(vNew - vOld));
                // Report-only current residual (S5/FR-011) — never a gate.
                currentResidual = std::max(
                    currentResidual,
                    std::fabs(diode.evaluate(vNew).current -
                              diode.evaluate(vOld).current));
                prevBiasAK_[c] = vNew;
            }
            status.voltageResidual = maxDeltaV;
            status.currentResidual = currentResidual;

            // (d) Convergence gate on the VOLTAGE residual ONLY (S5, FR-011).
            // Zero diodes → maxDeltaV == 0 here, so a single linear solve
            // converges immediately (S6).
            if (maxDeltaV < voltageTol_) {
                status.converged = true;
                break;
            }
        }

        return status;
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
    // Per-component topology fingerprint (variant discriminant + count) so
    // solve() can reject a netlist whose topology drifted from the plan
    // (AUDIT-20260708-04/05) — full-topology, not just the diode slots.
    int                                    plannedComponentCount_ = 0;
    std::array<std::size_t, MaxComponents> componentKind_{};

    // ---- per-solve scratch (declared now; populated by T006) -----------------
    std::array<Companion, MaxComponents> diodeCompanion_{};
    std::array<double, MaxComponents>    prevBiasAK_{};
};

}  // namespace acfx::newton

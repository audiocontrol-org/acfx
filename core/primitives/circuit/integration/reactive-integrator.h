#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/mna/mna-assembler.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/models/inductor.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/newton/newton-solver.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <variant>

// ReactiveIntegrator — the implicit-integration primitive: it discretizes a
// netlist's reactive elements (capacitors, inductors) into per-step Norton
// companions under a chosen implicit integration RULE, composes those fixed
// companions with the linear MnaSystem (or, when a nonlinear element is present,
// with NewtonSolver), and advances the per-element history once per converged
// step (contracts/reactive-integrator.md; research R1-R9; data-model.md).
//
// The integration RULE is a compile-time template policy (research R2): a small
// stateless struct with two pure static functions,
//   capacitorCompanion(C, dt, vPrev, iPrev) -> Companion
//   inductorCompanion (L, dt, vPrev, iPrev) -> Companion
// returning the Norton pair in MNA's consumption convention
//   i(a,b) = Geq*(V(a) - V(b)) - Ieq.
// Fixing the rule per plan keeps the per-sample companion path branch-free and
// mirrors the siblings' template-sizing pattern. Two policies ship here:
// BackwardEuler (first-order, L-stable) and Trapezoidal (second-order,
// A-stable). BackwardEuler is single-sourced against the shipped element
// companion() methods (research R9) so the C/dt and dt/L constants live in
// exactly one place; Trapezoidal computes its own {Geq, Ieq} because there is no
// element method for it (adding one would push history-shaped concerns onto the
// stateless value-types — design record Approach D rejection).
//
// This header lands the two RULE POLICIES. The ReactiveIntegrator class itself
// (StepResult, the two-phase plan()/step() surface, the internal CompanionSupply
// and cross-sample history) is the next increment (T005) and is added to THIS
// same header.
//
// C++17, header-only, standard library only; no platform or component-graphics
// headers. double throughout (FR-022). The policy functions are noexcept,
// allocation-free, and pure (hold no state).

namespace acfx::integration {

// Backward-Euler integration rule (research R1, first-order / L-stable). Reads
// only the history term its rule consumes: the capacitor's voltage history
// (vPrev, ignoring iPrev) and the inductor's current history (iPrev, ignoring
// vPrev). Single-sourced against the shipped element companion() methods (R9):
// the C/dt and dt/L physics live in exactly one place. The unused argument is
// kept so both policies share one uniform 4-arg signature (contract RP2).
struct BackwardEuler {
    // Capacitor backward-Euler companion: Geq = C/dt, Ieq = Geq*vPrev. Delegates
    // to Capacitor::companion(dt, vPrev) so the C/dt constant is single-sourced
    // (RP1/R9); iPrev is unused (backward-Euler's capacitor row does not read
    // the current history) but kept for a uniform 4-arg policy signature (RP2).
    // Node ids do not affect companion(), so a ground-ground carrier of C is used.
    static Companion capacitorCompanion(double C, double dt, double vPrev,
                                        double /*iPrev*/) noexcept {
        return Capacitor{kGround, kGround, C}.companion(dt, vPrev);
    }

    // Inductor backward-Euler companion: Geq = dt/L, Ieq = -iPrev. Delegates to
    // Inductor::companion(dt, iPrev) so the dt/L constant is single-sourced
    // (RP1/R9); vPrev is unused but kept for a uniform 4-arg signature (RP2).
    // Node ids do not affect companion(), so a ground-ground carrier of L is used.
    static Companion inductorCompanion(double L, double dt, double /*vPrev*/,
                                       double iPrev) noexcept {
        return Inductor{kGround, kGround, L}.companion(dt, iPrev);
    }
};

// Trapezoidal integration rule (research R1, second-order / A-stable). Consumes
// BOTH history terms (vPrev and iPrev) for each element. No element method
// exists for the trapezoidal rule (it needs extra history the stateless
// value-types deliberately do not hold), so it computes {Geq, Ieq} directly.
struct Trapezoidal {
    // Capacitor trapezoidal companion: Geq = 2C/dt, Ieq = Geq*vPrev + iPrev.
    static Companion capacitorCompanion(double C, double dt, double vPrev,
                                        double iPrev) noexcept {
        const double Geq = 2.0 * C / dt;
        return Companion{Geq, Geq * vPrev + iPrev};
    }

    // Inductor trapezoidal companion: Geq = dt/(2L), Ieq = -(iPrev + Geq*vPrev).
    static Companion inductorCompanion(double L, double dt, double vPrev,
                                       double iPrev) noexcept {
        const double Geq = dt / (2.0 * L);
        return Companion{Geq, -(iPrev + Geq * vPrev)};
    }
};

// StepResult — the per-step result value returned by ReactiveIntegrator::step()
// (contracts/reactive-integrator.md StepResult; data-model.md "StepResult"). It
// surfaces the composed solve's outcome BY VALUE: `converged` mirrors
// MnaSystem::solve() (linear) or the driven NewtonStatus (nonlinear); `false` is
// a legitimate, surfaced outcome (S5), not an exception. When `converged` is
// false the node voltages are the last (non-converged) iterate and MUST NOT be
// trusted (research R8). All-zero default is the precondition-violation sentinel
// returned by step()-before-plan() (S8).
struct StepResult {
    bool converged = false;       // did the composed solve converge?
    int iterations = 0;           // Newton iterations consumed (0/1 for linear)
    double voltageResidual = 0.0; // final residual from the composed solve
};

// ReactiveIntegrator — the stateful, per-sample implicit-integration driver
// (contracts/reactive-integrator.md; data-model.md). Template-sized and
// header-only; every buffer is a fixed-capacity std::array (zero heap on the hot
// path). `Rule` (BackwardEuler default) is fixed for the life of the object and
// is never silently switched at runtime (C2/research R8).
//
// Two-phase surface, mirroring the sibling engines (MnaAssembler / NewtonSolver):
//   - plan(nl, assembler, sys) : the once-per-topology pass (may throw, off the
//     hot path). Delegates branch allocation + topology validation to
//     MnaAssembler::plan, then scans the netlist ONCE recording the reactive
//     component indices, the per-component is-reactive mask, and whether any
//     nonlinear element is present (choosing the Newton-vs-MNA branch once).
//     Zeroes cross-sample state and marks planned_. Re-plannable.
//   - step(nl, assembler, sys, newton) : the hot path (throw-free,
//     allocation-free). step()-before-plan() is surfaced deterministically BY
//     VALUE as StepResult{} (S8); the real per-sample solve is the next
//     increment (T007 linear / T015 nonlinear).
//
// C++17, standard library only; no platform headers. double throughout.
template <class Rule, int MaxNodes, int MaxComponents, int MaxBranches>
class ReactiveIntegrator {
public:
    static_assert(MaxNodes >= 1,
                  "ReactiveIntegrator requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents >= 1,
                  "ReactiveIntegrator requires MaxComponents >= 1");
    static_assert(MaxBranches >= 0,
                  "ReactiveIntegrator requires MaxBranches >= 0");

    // Construction (contract C1). Off the hot path, so a bad configuration
    // THROWS std::invalid_argument with a descriptive message: dt <= 0 (the
    // companions divide by dt), or an invalid forwarded Newton config
    // (maxIterations < 1 / voltageTol <= 0). `Rule` is fixed for life (C2).
    // Zero-initializes all fixed-capacity state.
    explicit ReactiveIntegrator(double dt, int maxIterations = 50,
                                double voltageTol = 1e-9)
        : dt_(dt), maxIterations_(maxIterations), voltageTol_(voltageTol) {
        if (!(dt > 0.0)) {
            throw std::invalid_argument(
                "ReactiveIntegrator: dt must be > 0 (companions divide by dt), "
                "got dt = " +
                std::to_string(dt));
        }
        if (maxIterations < 1) {
            throw std::invalid_argument(
                "ReactiveIntegrator: forwarded Newton maxIterations must be "
                ">= 1, got " +
                std::to_string(maxIterations));
        }
        if (!(voltageTol > 0.0)) {
            throw std::invalid_argument(
                "ReactiveIntegrator: forwarded Newton voltageTol must be > 0, "
                "got " +
                std::to_string(voltageTol));
        }
        isReactive_.fill(false);
        reactiveComponentIndex_.fill(0);
        reactiveCompanion_.fill(Companion{0.0, 0.0});
        vPrev_.fill(0.0);
        iPrev_.fill(0.0);
        warmStart_.fill(0.0);
    }

    // plan() — off the hot path, throw-permitted (contract P1/P2/P3).
    void plan(const Netlist<MaxNodes, MaxComponents>& nl,
              mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& assembler,
              mna::MnaSystem<MaxNodes, MaxBranches>& sys) {
        // P1: delegate branch allocation + topology validation to the assembler
        // (may throw on over-capacity / out-of-range / degenerate netlists).
        assembler.plan(nl, sys);

        // P2: scan the netlist ONCE, recording the reactive component indices,
        // the per-component is-reactive mask, and whether any nonlinear element
        // is present (hasNonlinear_ chooses the step branch once, at plan time).
        isReactive_.fill(false);
        reactiveComponentIndex_.fill(0);
        reactiveCount_ = 0;
        hasNonlinear_ = false;
        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            if (acfx::isReactive(comps[i])) {
                isReactive_[i] = true;
                reactiveComponentIndex_[static_cast<std::size_t>(reactiveCount_)] =
                    static_cast<int>(i);
                ++reactiveCount_;
            }
            if (acfx::isNonlinear(comps[i])) {
                hasNonlinear_ = true;
            }
        }

        // P3: does not compute companions or advance history; initialize
        // cross-sample state to zero (reset() semantics), then mark planned.
        reset();
        planned_ = true;
    }

    // reset() — off the hot path (contract RS1). Returns cross-sample state
    // (vPrev_, iPrev_, warmStart_) and the per-step companion scratch to zero
    // state; does NOT change the plan (topology). Safe between transients.
    void reset() noexcept {
        vPrev_.fill(0.0);
        iPrev_.fill(0.0);
        warmStart_.fill(0.0);
        reactiveCompanion_.fill(Companion{0.0, 0.0});
    }

    // step() — the hot path (contract S1-S9; throw-free, allocation-free).
    // Computes each reactive element's companion ONCE from its current history
    // (S1), composes the fixed companions with the linear MnaSystem (S2) or the
    // NewtonSolver (nonlinear, T015), reads the converged node voltages and
    // reconstructs per-element {v^n, i^n} (S3), then advances cross-sample
    // history exactly once (S4). A non-converged solve is surfaced BY VALUE
    // without advancing history (S5); a step() before plan() is the all-zero
    // sentinel (S8). Zero reactive elements is a clean passthrough (S9).
    StepResult step(
        const Netlist<MaxNodes, MaxComponents>& nl,
        mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& assembler,
        mna::MnaSystem<MaxNodes, MaxBranches>& sys,
        newton::NewtonSolver<MaxNodes, MaxComponents, MaxBranches>& newton) {
        // S8: a precondition violation (step() before plan()) is surfaced by
        // value, throw-free / allocation-free, as the all-zero sentinel.
        if (!planned_) {
            return StepResult{};
        }

        if (hasNonlinear_) {
            // T015 nonlinear — Newton-composed solve is the next increment.
            (void)newton;
            return StepResult{};
        }

        // ---- linear branch (hasNonlinear_ == false) --------------------------
        const auto comps = nl.components();

        // S1: compute each reactive element's companion ONCE from its current
        // history, keyed by component index (the supply's at() indexing).
        for (int s = 0; s < reactiveCount_; ++s) {
            const int compIdx = reactiveComponentIndex_[static_cast<std::size_t>(s)];
            const Component& comp = comps[static_cast<std::size_t>(compIdx)];
            const std::size_t ci = static_cast<std::size_t>(compIdx);
            const double vPrev = vPrev_[static_cast<std::size_t>(s)];
            const double iPrev = iPrev_[static_cast<std::size_t>(s)];
            if (const auto* cap = std::get_if<Capacitor>(&comp)) {
                reactiveCompanion_[ci] =
                    Rule::capacitorCompanion(cap->C, dt_, vPrev, iPrev);
            } else if (const auto* ind = std::get_if<Inductor>(&comp)) {
                reactiveCompanion_[ci] =
                    Rule::inductorCompanion(ind->L, dt_, vPrev, iPrev);
            }
        }

        // S2: expose the fixed companions and compose with the linear MNA solve.
        const auto supply = companionSupply();
        assembler.refresh(nl, supply, sys);
        const bool converged = sys.solve();

        // S5: a non-converged solve is surfaced BY VALUE; history is NOT advanced
        // from the untrustworthy iterate.
        if (!converged) {
            return StepResult{false, 1, 0.0};
        }

        // S3/S4: reconstruct per-element {v^n, i^n} from the converged voltages
        // and this step's stamped companion, then advance history exactly once.
        for (int s = 0; s < reactiveCount_; ++s) {
            const int compIdx = reactiveComponentIndex_[static_cast<std::size_t>(s)];
            const std::size_t ci = static_cast<std::size_t>(compIdx);
            const auto t = terminalsOf(comps[ci]);
            const double vN = sys.nodeVoltage(t.first) - sys.nodeVoltage(t.second);
            const double iN =
                reactiveCompanion_[ci].Geq * vN - reactiveCompanion_[ci].Ieq;
            vPrev_[static_cast<std::size_t>(s)] = vN;
            iPrev_[static_cast<std::size_t>(s)] = iN;
        }
        for (int n = 0; n < MaxNodes; ++n) {
            warmStart_[static_cast<std::size_t>(n)] = sys.nodeVoltage(n);
        }

        return StepResult{true, 1, 0.0};
    }

    // ---- read accessors (contract "Read accessors"; T003 pins these names) ---

    // True once plan() has run (mirrors MnaAssembler / NewtonSolver planned()).
    bool planned() const noexcept { return planned_; }

    // Does the planned netlist contain any nonlinear element (Diode)?
    bool hasNonlinear() const noexcept { return hasNonlinear_; }

    // Number of reactive elements found by the last plan() scan.
    int reactiveCount() const noexcept { return reactiveCount_; }

    // Per-component is-reactive mask read accessor (drives the internal
    // ReactiveCompanionSupply's at()). Out-of-range indices are not reactive
    // (mirrors NewtonSolver::isDiodeComponent).
    bool isReactiveComponent(int componentIndex) const noexcept {
        if (componentIndex < 0 || componentIndex >= MaxComponents) {
            return false;
        }
        return isReactive_[static_cast<std::size_t>(componentIndex)];
    }

    // Component index of the reactiveSlot'th reactive element recorded by plan()
    // (reactive slot -> component index). Out-of-range slots return -1.
    int reactiveComponentIndex(int reactiveSlot) const noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= MaxComponents) {
            return -1;
        }
        return reactiveComponentIndex_[static_cast<std::size_t>(reactiveSlot)];
    }

    // Stored per-reactive-slot history, exposed for testing (S3/S4). Out-of-range
    // slots return 0.
    double vPrev(int reactiveSlot) const noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= MaxComponents) {
            return 0.0;
        }
        return vPrev_[static_cast<std::size_t>(reactiveSlot)];
    }
    double iPrev(int reactiveSlot) const noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= MaxComponents) {
            return 0.0;
        }
        return iPrev_[static_cast<std::size_t>(reactiveSlot)];
    }

private:
    // ReactiveCompanionSupply (data-model.md) — the view handed to
    // MnaAssembler::refresh (linear) or NewtonSolver::solve as `base`
    // (nonlinear). Satisfies MNA's CompanionSupply contract:
    //   Companion at(int componentIndex) const noexcept
    // returns this step's fixed reactive companion when the index is a reactive
    // component, else a neutral companion {0, 0}. O(1), noexcept, alloc-free.
    struct ReactiveCompanionSupply {
        const std::array<Companion, MaxComponents>& reactiveCompanion;
        const std::array<bool, MaxComponents>& isReactive;

        Companion at(int componentIndex) const noexcept {
            if (componentIndex < 0 || componentIndex >= MaxComponents) {
                return Companion{0.0, 0.0};
            }
            const std::size_t i = static_cast<std::size_t>(componentIndex);
            return isReactive[i] ? reactiveCompanion[i] : Companion{0.0, 0.0};
        }
    };

    // Build the supply view over this step's fixed companions + mask (used by
    // the T007/T015 solve path).
    ReactiveCompanionSupply companionSupply() const noexcept {
        return ReactiveCompanionSupply{reactiveCompanion_, isReactive_};
    }

    // ---- configuration (immutable after construction) ------------------------
    double dt_;            // timestep (s); companions depend on it (> 0)
    int maxIterations_;    // forwarded to NewtonSolver (nonlinear path)
    double voltageTol_;    // forwarded to NewtonSolver (nonlinear path)

    // ---- plan-time state (built once by plan(), off the hot path) ------------
    // Per-component is-reactive mask (drives the supply's at()).
    std::array<bool, MaxComponents> isReactive_{};
    // Reactive slot -> component index of the scanned reactive element.
    std::array<int, MaxComponents> reactiveComponentIndex_{};
    int reactiveCount_ = 0;      // number of reactive elements found
    bool hasNonlinear_ = false;  // any nonlinear element (Diode)?
    bool planned_ = false;       // two-phase guard — step() before plan()

    // ---- cross-sample state (the one stateful sibling) -----------------------
    std::array<double, MaxComponents> vPrev_{};  // per-slot previous voltage
    std::array<double, MaxComponents> iPrev_{};  // per-slot previous current
    std::array<double, MaxNodes> warmStart_{};   // previous node voltages

    // ---- per-step scratch (fixed-capacity; refreshed each step()) ------------
    // This step's fixed reactive companion per reactive component index
    // (computed once before the solve by the T007/T015 path; zero for now).
    std::array<Companion, MaxComponents> reactiveCompanion_{};
};

} // namespace acfx::integration

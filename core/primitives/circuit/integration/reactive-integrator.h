#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/integration/reactive-rules.h"
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
// companions under a chosen implicit integration RULE (the BackwardEuler /
// Trapezoidal policies in reactive-rules.h, a compile-time template parameter),
// composes those fixed companions with the linear MnaSystem (or, when a
// nonlinear element is present, with NewtonSolver), and advances the per-element
// history once per converged step (contracts/reactive-integrator.md; research
// R1-R9; data-model.md).
//
// This header ships the StepResult value type and the full ReactiveIntegrator
// class — its two-phase plan()/step() surface, the internal
// ReactiveCompanionSupply, the cross-sample history, and both the linear
// (MNA-composed) and nonlinear (Newton-composed) step() branches. The rule
// policies live in reactive-rules.h.
//
// C++17, header-only, standard library only; no platform or component-graphics
// headers. double throughout (FR-022).

namespace acfx::integration {

// StepResult — the per-step result value returned by ReactiveIntegrator::step()
// (contracts/reactive-integrator.md StepResult; data-model.md "StepResult"). It
// surfaces the composed solve's outcome BY VALUE: `converged` mirrors
// MnaSystem::solve() (linear) or the driven NewtonStatus (nonlinear); `false` is
// a legitimate, surfaced outcome (S5), not an exception. When `converged` is
// false the node voltages are the last (non-converged) iterate and MUST NOT be
// trusted (research R8). All-zero default is the precondition-violation sentinel
// returned by step()-before-plan() (S8).
//
// `iterations` and `voltageResidual` are MEANINGFUL ONLY on the nonlinear
// (Newton-composed) path, where they carry the driven NewtonStatus's iteration
// count and final residual. On the linear (direct MNA) path there is no Newton
// loop and no residual to measure: `iterations` is a fixed 1 (one direct solve)
// and `voltageResidual` is 0.0 — a placeholder, NOT a measured error. A consumer
// gauging solve health must key on `converged`; do not read `voltageResidual` as
// a linear-path accuracy signal (it reads 0.0 on both success and singular
// failure there).
struct StepResult {
    bool converged = false;       // did the composed solve converge?
    int iterations = 0;           // Newton iterations (nonlinear); 1 on the linear path
    double voltageResidual = 0.0; // final Newton residual (nonlinear); 0.0 placeholder on linear
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
        // Invalidate FIRST (mirrors NewtonSolver::plan): if this is a re-plan and
        // assembler.plan() throws below, the integrator must NOT stay armed
        // against a half-cleared assembler — a later step() would then pass the
        // pre-plan guard and refresh() a stale plan in a release build where the
        // assembler's own assert is compiled out. Re-arm only after BOTH the
        // assembler plan and the integrator scan complete.
        planned_ = false;
        isReactive_.fill(false);
        reactiveComponentIndex_.fill(0);
        reactiveCount_ = 0;
        hasNonlinear_ = false;
        plannedComponentCount_ = 0;

        // P1: delegate branch allocation + topology validation to the assembler
        // (may throw on over-capacity / out-of-range / degenerate netlists).
        assembler.plan(nl, sys);

        // P2: scan the netlist ONCE, recording the reactive component indices,
        // the per-component is-reactive mask, whether any nonlinear element is
        // present (hasNonlinear_ chooses the step branch once), and the planned
        // component count (the topology signature step() checks before trusting
        // the recorded indices).
        const auto comps = nl.components();
        plannedComponentCount_ = static_cast<int>(comps.size());
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

    // Seed a reactive element's initial history {vPrev, iPrev} to a
    // caller-provided consistent initial condition (e.g. the true t=0
    // terminal voltage / current), so a higher-order rule (Trapezoidal)
    // integrates from consistent initial data. Off the hot path; call after
    // plan() (which zeroes history) and before the first step(). No-op for
    // an out-of-range slot. Does NOT change topology or the rule. History
    // still DEFAULTS to zero (this is an explicit opt-in), so FR-016 and the
    // zero-state closed-form tests are unaffected.
    void seedHistory(int reactiveSlot, double vPrev, double iPrev) noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= reactiveCount_) {
            return;
        }
        const std::size_t slot = static_cast<std::size_t>(reactiveSlot);
        vPrev_[slot] = vPrev;
        iPrev_[slot] = iPrev;
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

        // Topology guard: step() trusts the component indices plan() recorded. If
        // the caller passes a netlist whose topology drifted from the plan (a
        // different component count, or a recorded reactive slot that is no longer
        // a reactive element), those indices are stale and would drive
        // out-of-bounds / mis-stamped access below — MnaAssembler::refresh() does
        // NOT revalidate on the hot path. Surface the precondition violation BY
        // VALUE (like the pre-plan guard), before any companion computation or
        // solve, rather than risking UB (mirrors NewtonSolver's topology guard).
        const auto guardComps = nl.components();
        if (static_cast<int>(guardComps.size()) != plannedComponentCount_) {
            return StepResult{};
        }
        for (int s = 0; s < reactiveCount_; ++s) {
            const int ci = reactiveComponentIndex_[static_cast<std::size_t>(s)];
            if (ci < 0 || ci >= static_cast<int>(guardComps.size()) ||
                !acfx::isReactive(guardComps[static_cast<std::size_t>(ci)])) {
                return StepResult{};
            }
        }

        // S1: compute each reactive element's companion ONCE from its current
        // history, before the solve — shared by both branches and held FIXED for
        // the whole solve (Newton varies only diodes — FR-007; dissolves the
        // per-iteration companion recomputation of backlog TASK-13).
        computeReactiveCompanions(nl);
        const auto supply = companionSupply();

        if (hasNonlinear_) {
            // S2 (nonlinear): compose the shipped NewtonSolver, handing the fixed
            // reactive companions as Newton's `base` supply and the
            // integrator-owned warm start as the initial guess. The caller plans
            // the NewtonSolver (newton.plan) for a nonlinear netlist; an unplanned
            // solver surfaces converged == false by value via Newton's own
            // pre-plan guard (never UB).
            const newton::NewtonStatus st =
                newton.solve(nl, supply, warmStart_, assembler, sys);
            // S5: a non-converged solve is surfaced BY VALUE; history is NOT
            // advanced from the untrustworthy iterate (no fallback, no rule
            // switch, no throw on the hot path).
            if (!st.converged) {
                return StepResult{false, st.iterations, st.voltageResidual};
            }
            // S3/S4: reconstruct + advance history exactly ONCE, after Newton
            // converges (never per Newton iteration).
            advanceHistory(nl, sys);
            return StepResult{true, st.iterations, st.voltageResidual};
        }

        // ---- linear branch (hasNonlinear_ == false) --------------------------
        // S2 (linear): stamp the fixed companions and solve the linear MNA system.
        assembler.refresh(nl, supply, sys);
        const bool converged = sys.solve();
        // S5: a non-converged (singular) solve is surfaced BY VALUE; no advance.
        if (!converged) {
            return StepResult{false, 1, 0.0};
        }
        // S3/S4: reconstruct + advance history exactly once.
        advanceHistory(nl, sys);
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
    // (reactive slot -> component index). Valid slots are [0, reactiveCount_);
    // a slot outside that range has no reactive element and returns the -1 "no
    // such slot" sentinel (guarding on reactiveCount_, NOT MaxComponents, so the
    // sentinel actually fires and cannot be confused with a real component 0).
    int reactiveComponentIndex(int reactiveSlot) const noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= reactiveCount_) {
            return -1;
        }
        return reactiveComponentIndex_[static_cast<std::size_t>(reactiveSlot)];
    }

    // Stored per-reactive-slot history, exposed for testing (S3/S4). Valid slots
    // are [0, reactiveCount_); a slot outside that range has no history and
    // returns 0 (guarding on reactiveCount_, so a genuinely-zero history term is
    // not conflated with an over-range slot).
    double vPrev(int reactiveSlot) const noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= reactiveCount_) {
            return 0.0;
        }
        return vPrev_[static_cast<std::size_t>(reactiveSlot)];
    }
    double iPrev(int reactiveSlot) const noexcept {
        if (reactiveSlot < 0 || reactiveSlot >= reactiveCount_) {
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
    // the linear MNA refresh and the Newton-composed solve path).
    ReactiveCompanionSupply companionSupply() const noexcept {
        return ReactiveCompanionSupply{reactiveCompanion_, isReactive_};
    }

    // S1 (shared by both step() branches): compute each reactive element's
    // companion ONCE from its current history via the Rule policy, keyed by
    // component index (the supply's at() indexing). Held fixed for the whole
    // solve — Newton varies only the diodes over this fixed base (FR-007).
    void computeReactiveCompanions(
        const Netlist<MaxNodes, MaxComponents>& nl) noexcept {
        const auto comps = nl.components();
        for (int s = 0; s < reactiveCount_; ++s) {
            const int compIdx = reactiveComponentIndex_[static_cast<std::size_t>(s)];
            const std::size_t ci = static_cast<std::size_t>(compIdx);
            const Component& comp = comps[ci];
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
    }

    // S3/S4 (shared by both step() branches): reconstruct each reactive
    // element's {v^n, i^n} from the converged node voltages and THIS step's
    // stamped companion (i^n = Geq*v^n - Ieq, research R3/R4), then advance
    // cross-sample history exactly ONCE and refresh the warm start. Called only
    // after the composed solve converged.
    void advanceHistory(const Netlist<MaxNodes, MaxComponents>& nl,
                        const mna::MnaSystem<MaxNodes, MaxBranches>& sys) noexcept {
        const auto comps = nl.components();
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
    int plannedComponentCount_ = 0;  // topology signature step() checks
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

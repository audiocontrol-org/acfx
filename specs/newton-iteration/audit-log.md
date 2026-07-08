---
slug: newton-iteration
targetVersion: ""
---

# Audit log — newton-iteration

## 2026-07-08 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260708-01 — Failed re-plan leaves `NewtonSolver` marked usable with stale topology

Finding-ID: AUDIT-20260708-01
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    core/primitives/circuit/newton/newton-solver.h:117-138

`plan()` delegates to `assembler.plan(nl, sys)` before clearing Newton's own plan state. If `assembler.plan()` throws during a re-plan after a previous successful plan, `NewtonSolver::planned_` remains true and `diodeCount_` / `isDiode_` / `diodeComponentIndex_` still describe the old netlist. The paired `MnaAssembler` has already cleared its branch map and marked itself unplanned before it can throw, so the two objects can be left in inconsistent states.

Blast radius is high because a caller that catches a plan-time topology error can still see `solver.planned() == true` and enter the hot `solve()` path with stale Newton topology and an invalid/partial assembler plan. A reasonable fix is to set `planned_ = false` and clear Newton's plan-time arrays before calling `assembler.plan()`, then set `planned_ = true` only after the full scan succeeds.

### AUDIT-20260708-02 — `solve()` does not implement the promised inconsistent-plan guard

Finding-ID: AUDIT-20260708-02
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    core/primitives/circuit/newton/newton-solver.h:171-193

The contract comment says precondition violations are surfaced by value, but the implementation only checks `!planned_`. It then indexes the current `nl.components()` using diode indices captured during the last `plan()` and calls `std::get<acfx::Diode>(components[c])`. If the caller passes a netlist with fewer components, `components[c]` is out of range; if the component at a planned diode index is no longer a diode, `std::get` throws `std::bad_variant_access` on the supposed throw-free hot path.

Blast radius is high because this breaks the two-phase safety contract in the common topology-change case: stale or mismatched netlists become exceptions/undefined access instead of the documented `NewtonStatus{converged=false, iterations=0}`. The fix should add a cheap deterministic guard before any indexed `std::get`: component count and planned diode slots must still be present and hold `Diode`, otherwise return the precondition-failure status by value.

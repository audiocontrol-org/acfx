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

## 2026-07-08 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260708-03 — The S3 "single global Newton step" claim is asserted in prose but never verified by any assertion

Finding-ID: AUDIT-20260708-03
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    tests/core/newton-solver-test.cpp:159-166, 200-278 (section 4; all three coupled-diode cases)

The section-4 header (lines ~204-232) and every case comment inside it make the central US2/S3 charter claim explicit: the solve loop composes *all* diodes into "exactly ONE `MnaAssembler::refresh` + `MnaSystem::solve()` call per iteration (S3) — a single GLOBAL Newton step … never a per-diode sequence of smaller solves." But the only observable assertions in 4a/4b/4c are `solver.diodeCount() == N`, `status.iterations >= 1`, `status.voltageResidual >= 0.0`, and `status.converged`. None of these can distinguish a single global solve from a per-diode sequence of solves that also converges. `diodeCount()` proves only that `plan()` *counted* the diodes; it says nothing about how the hot loop composes them. A refactor that (incorrectly) solved each diode in its own inner sweep would leave every one of these tests green.

The blast radius: commit 2d4f3e6 ("all success criteria demonstrably met") and the govern pass treat this file as the evidence that US2/S3 is verified. A downstream reader — or an unattended agent building on this "verified" foundation — will believe the single-global-step invariant is under test when it is not, and would not catch a regression that reintroduces per-diode sequencing (the exact anti-pattern the labs' `NewtonClipper` refuses). A real fix would instrument the solve path so the per-iteration `refresh`/`solve()` call count is observable (e.g. a counting `BaseCompanionSupply`/assembler spy, or a status field exposing solves-per-iteration) and assert it equals the iteration count, not the diode count.

---

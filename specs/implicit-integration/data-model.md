# Data Model — Implicit-integration primitive (Phase 1)

The primitive introduces **no new component types** (Constitution / FR-017). These are the
integrator-local types and the fixed-capacity state they own. All numeric values are `double`.

## Integration-rule policy (template parameter)

A stateless policy type selecting the per-element companion formula (research R1/R2). Two
implementations; the rule is fixed per plan.

| Policy | `capacitorCompanion(C, dt, vPrev, iPrev)` | `inductorCompanion(L, dt, vPrev, iPrev)` |
|--------|-------------------------------------------|------------------------------------------|
| `BackwardEuler` | `{Geq: C/dt, Ieq: (C/dt)·vPrev}` (reuses `Capacitor::companion`) | `{Geq: dt/L, Ieq: −iPrev}` (reuses `Inductor::companion`) |
| `Trapezoidal` | `{Geq: 2C/dt, Ieq: (2C/dt)·vPrev + iPrev}` | `{Geq: dt/(2L), Ieq: −(iPrev + (dt/(2L))·vPrev)}` |

Both are pure static functions (`noexcept`), no state. Signs are in MNA's consumption
convention `i(a,b) = Geq·(V(a)−V(b)) − Ieq`.

## ReactiveIntegrator&lt;class Rule, int MaxNodes, int MaxComponents, int MaxBranches&gt;

The stateful per-sample time-stepping driver. Template-sized; header-only; namespace
`acfx::integration`. `Rule` is `BackwardEuler` (default) or `Trapezoidal`.

**Configuration (immutable after construction):**

| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `dt_` | `double` | timestep (s); companions depend on it | `> 0` at construction (throw) |
| (composed Newton config) | — | passed through to the driven `NewtonSolver` for nonlinear netlists (defaults `maxIterations = 50`, `voltageTol = 1e-9`) | validated by `NewtonSolver` |

**Plan-time state (built once by `plan()`, off the hot path):**

| Field | Type | Meaning |
|-------|------|---------|
| `planned_` | `bool` | two-phase guard — `step()` before `plan()` is a precondition violation |
| `reactiveCount_` | `int` | number of reactive elements found in the netlist scan |
| `reactiveComponentIndex_` | `std::array<int, MaxComponents>` | component index of each scanned reactive element (reactive slot → component index) |
| `isReactive_` | `std::array<bool, MaxComponents>` | per-component mask: is component `i` a reactive element (drives the supply's `at`) |
| `hasNonlinear_` | `bool` | does the netlist contain any nonlinear element (diode)? Chooses the Newton-vs-MNA step branch once, at plan time (research R6) |

**Cross-sample state (owned by the integrator — the one stateful sibling):**

| Field | Type | Meaning |
|-------|------|---------|
| `vPrev_` | `std::array<double, MaxComponents>` | per-reactive-element previous terminal voltage `v^{n-1}` |
| `iPrev_` | `std::array<double, MaxComponents>` | per-reactive-element previous current `i^{n-1}` |
| `warmStart_` | `std::array<double, MaxNodes>` | previous converged node voltages (the solve's initial guess) |

**Per-step scratch (fixed-capacity; refreshed each `step()`):**

| Field | Type | Meaning |
|-------|------|---------|
| `reactiveCompanion_` | `std::array<Companion, MaxComponents>` | this step's fixed reactive companion per reactive component index (computed once, before the solve) |

**Invariants:**

- **Stateful across samples, by design** (unlike MNA/Newton): `vPrev_`, `iPrev_`, `warmStart_`
  persist step→step and ARE the time-stepping state. `reset()` returns them to zero state.
- **Deterministic**: identical `(cross-sample state, inputs)` → identical `step()` output.
- Zero heap on `step()`: every buffer above is fixed-capacity, sized by template parameters.
- Topology (`reactiveCount_`, indices, masks, `hasNonlinear_`) is fixed at `plan()` and never
  recomputed per step.
- Reactive companions are computed **once per step** (not per Newton iteration) and reused for
  both the stamp and the history advance (dissolves TASK-13).

## ReactiveCompanionSupply (integrator-internal)

The view handed to `MnaAssembler::refresh` (linear) or `NewtonSolver::solve` as `base`
(nonlinear). Satisfies MNA's `CompanionSupply` contract
(`Companion at(int componentIndex) const noexcept`).

| Field | Type | Meaning |
|-------|------|---------|
| `reactiveCompanion_` | `const std::array<Companion, MaxComponents>&` | this step's fixed reactive companions |
| `isReactive_` | `const std::array<bool, MaxComponents>&` | per-component is-reactive mask |

**Behavior:** `at(i)` returns `reactiveCompanion_[i]` when `isReactive_[i]`, else a neutral
companion `{Geq: 0, Ieq: 0}` (a non-reactive, non-diode index is not this supply's concern; for
the Newton path, diode indices never reach here — Newton overrides them before delegating).
O(1), `noexcept`, allocation-free.

## StepResult

The per-step result value (returned by `step()`), surfacing the composed solve's outcome.

| Field | Type | Meaning |
|-------|------|---------|
| `converged` | `bool` | did the composed solve converge? For a linear netlist, mirrors `MnaSystem::solve()`; for a nonlinear netlist, the driven `NewtonStatus.converged`. `false` is a legitimate, surfaced outcome |
| `iterations` | `int` | Newton iterations consumed (0 / 1 for the linear path) |
| `voltageResidual` | `double` | final residual from the composed solve (0 for a clean linear solve) |

**Semantics:** when `converged == false`, node voltages are the last (non-converged) iterate and
MUST NOT be trusted; reactive history is NOT advanced from them as if physical (research R8).

## Relationships / data flow (one `step()`)

```text
cross-sample state: vPrev_, iPrev_ (per reactive element), warmStart_ (node voltages)
                                       │
      ┌────────────────────────────────┘
      ▼
  for each reactive element r (Rule policy, research R1):
    reactiveCompanion_[idx_r] = Rule::{capacitor|inductor}Companion(param_r, dt_, vPrev_[r], iPrev_[r])
                                       │  (computed ONCE per step, fixed for the solve)
                                       ▼
  hasNonlinear_ ?  ── yes ─▶  NewtonSolver::solve(nl, ReactiveCompanionSupply, warmStart_, assembler, sys)
                │                     (reactive companions are Newton's fixed `base`; diodes vary per iteration)
                └── no  ─▶  MnaAssembler::refresh(nl, ReactiveCompanionSupply, sys); MnaSystem::solve()
                                       │
                                       ▼
  read converged node voltages v^n = V(a_r) − V(b_r);   i^n = Geq·v^n − Ieq   (research R3/R4)
                                       ▼
  advance history (once): vPrev_[r] := v^n ; iPrev_[r] := i^n ;  warmStart_ := node voltages
                                       ▼
                              return StepResult
```

## Consumed existing types (unchanged)

- `acfx::Capacitor` (`models/capacitor.h`) — `C()` accessor; `companion(dt, vPrev)` reused for
  the backward-Euler policy (research R9). Stateless physics value type.
- `acfx::Inductor` (`models/inductor.h`) — `L()` accessor; `companion(dt, iPrev)` reused for the
  backward-Euler policy. Stateless.
- `acfx::Companion` (`models/companion.h`) — `{Geq, Ieq}`. The stamp this primitive produces.
- `acfx::Netlist`, `acfx::NodeId`, `components.h` `isReactive`/`isNonlinear` — the circuit,
  node identifiers, and element routing classifiers (consumed as-is).
- `acfx::mna::MnaSystem`, `acfx::mna::MnaAssembler` — the linear engine driven directly for
  linear netlists (`plan`, `refresh`, `solve`, `nodeVoltage`). Driven, not modified.
- `acfx::newton::NewtonSolver`, `acfx::newton::NewtonStatus` — the nonlinear engine composed for
  nonlinear netlists (`plan`, `solve` with this primitive as `base`). Composed, not modified.

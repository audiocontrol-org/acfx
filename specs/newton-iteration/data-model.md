# Data Model — Newton–Raphson iteration primitive (Phase 1)

The primitive introduces **no new component types** (Constitution / FR-020). These are the
solver-local types and the fixed-capacity state they own. All numeric values are `double`.

## NewtonSolver&lt;int MaxNodes, int MaxComponents, int MaxBranches&gt;

The nonlinear outer-loop driver. Template-sized; header-only; namespace `acfx::newton`.

**Configuration (immutable after construction):**

| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `maxIterations_` | `int` | inner Newton iteration bound | `> 0` at construction (throw) |
| `voltageTol_` | `double` | convergence gate `max|Δv| < voltageTol_` (V) | `> 0` at construction (throw) |
| `currentTol_` | `double` | current-residual **reporting** reference (A); NOT a gate | `> 0` at construction (throw) |

**Plan-time state (built once by `plan()`, off the hot path):**

| Field | Type | Meaning |
|-------|------|---------|
| `planned_` | `bool` | two-phase guard — `solve()` before `plan()` is a precondition violation |
| `diodeCount_` | `int` | number of diodes found in the netlist scan |
| `diodeComponentIndex_` | `std::array<int, MaxComponents>` | component index of each scanned diode (diode slot → component index) |
| `isDiode_` | `std::array<bool, MaxComponents>` | per-component mask: is component `i` a diode (drives companion composition) |

**Per-solve scratch (fixed-capacity `std::array`; reset at the top of each `solve()`):**

| Field | Type | Meaning |
|-------|------|---------|
| `diodeCompanion_` | `std::array<Companion, MaxComponents>` | this iteration's Norton companion per diode component index |
| `prevBiasAK_` | `std::array<double, MaxComponents>` | previous-iterate `vAK` per diode (pnjlim `vOld`) — per-`solve()`, not cross-sample |

**Invariants:**

- Stateless across calls: no field persists solve→solve except immutable configuration and
  plan-time topology (which depends only on the netlist, not on past solves).
- Zero heap on `solve()`: every buffer above is fixed-capacity, sized by template parameters.
- Topology (`diodeCount_`, indices, mask) is fixed at `plan()` and never recomputed per
  iteration or per sample.

## NewtonStatus

The per-solve result value (returned by `solve()`).

| Field | Type | Meaning |
|-------|------|---------|
| `converged` | `bool` | did `max|Δv| < voltageTol_` within the bound? `false` is a legitimate, surfaced outcome |
| `iterations` | `int` | Newton iterations actually consumed (≤ `maxIterations_`) |
| `voltageResidual` | `double` | final `max|Δv|` across diode biases (V) |
| `currentResidual` | `double` | final `|ΔI_total|` of diode current (A) — reported, never gated |

**Semantics:** when `converged == false`, the node voltages left in the `MnaSystem` are the
last (non-converged) iterate and MUST NOT be trusted as a physical answer.

## ComposedCompanionSupply&lt;class Base&gt; (solver-internal)

The adapter handed to `MnaAssembler::refresh`. Satisfies MNA's `CompanionSupply` contract
(`Companion at(int componentIndex) const noexcept`).

| Field | Type | Meaning |
|-------|------|---------|
| `base_` | `const Base&` | caller-supplied base supply (reactive companions; fixed for the solve) |
| `diodeCompanion_` | `const std::array<Companion, MaxComponents>&` | Newton's per-iteration diode companions |
| `isDiode_` | `const std::array<bool, MaxComponents>&` | per-component is-diode mask |

**Behavior:** `at(i)` returns `diodeCompanion_[i]` when `isDiode_[i]`, else `base_.at(i)`.
O(1), `noexcept`, allocation-free.

## Relationships / data flow (one `solve()`)

```text
initial node-voltage guess (caller)  ─┐
                                       ▼
        seed diode biases vAK from guess
                                       ▼
  ┌────────────── iterate (≤ maxIterations) ───────────────┐
  │  for each diode d:                                      │
  │    {I,g} = Diode::evaluate(vAK_d)                       │
  │    diodeCompanion_[idx_d] = {Geq:g, Ieq:g·vAK_d − I}    │
  │  MnaAssembler::refresh(nl, ComposedCompanionSupply, sys)│   ← base supply pass-through for non-diodes
  │  ok = MnaSystem::solve()      ── false ⇒ surface, stop  │
  │  read nodeVoltage(); vAK_d' = V(a_d) − V(c_d)           │
  │  vAK_d = Diode::limitJunctionVoltage(vAK_d', prevBias)  │   ← pnjlim damping
  │  residual = max|vAK_d − prevBias|                       │
  │  if residual < voltageTol ⇒ converged, stop             │
  └────────────────────────────────────────────────────────┘
                                       ▼
                           return NewtonStatus
```

## Consumed existing types (unchanged)

- `acfx::Diode` (`models/diode.h`) — `evaluate(vAK) → {current, conductance}`,
  `limitJunctionVoltage(vNew, vOld)`, `vCrit()`. Physics only.
- `acfx::Companion` (`models/companion.h`) — `{Geq, Ieq}`. The stamp Newton produces.
- `acfx::Netlist`, `acfx::NodeId` — the circuit and node identifiers.
- `acfx::mna::MnaSystem`, `acfx::mna::MnaAssembler` — the linear engine Newton drives
  (`plan`, `refresh`, `solve`, `nodeVoltage`). Driven, not modified.

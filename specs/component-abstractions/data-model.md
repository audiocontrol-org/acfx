# Phase 1 Data Model — component-abstractions

Entities are the vocabulary the feature defines. All numeric physics fields are
`double` (FR-022). "No solver state on a component" (design D2) is honored by keeping
per-timestep history in the **solver**, not the component; a component's only mutable
data is its fixed parameters.

---

## NodeId / node set

- **`NodeId`** — an integer node handle. **Ground ≡ `0`** (FR-001). A non-negative index.
- A circuit spans nodes `0 .. nodeCount-1`; `nodeCount` is owned by the `Netlist`.
- **Validation rules:** every component terminal references a `NodeId` in range; node `0`
  (ground) MUST be referenced by at least one component (FR-010 "missing ground"); every
  non-ground node MUST have a conductive path to ground (FR-010 "floating node").

## Component (typed value structs)

Each is a small `struct` of parameters + a terminal→node binding. **No inheritance, no
virtual methods** (FR-002). The v1 set (FR-007):

| Type | Terminals | Parameters (double) | Physics interface |
|---|---|---|---|
| `Resistor` | a, b | `R` (Ω) | `admittance() → G = 1/R` (linear, FR-003) |
| `Capacitor` | a, b | `C` (F) | `companion(dt, vPrev) → {Geq=C/dt, Ieq}` (reactive, FR-005) |
| `Inductor` | a, b | `L` (H) | `companion(dt, iPrev) → {Geq=dt/L, Ieq}` (reactive, FR-005) |
| `VoltageSource` | p, n | `V` (V) | ideal source → **fixed-node reduction** (R1); pins `p−n = V` |
| `CurrentSource` | p, n | `I` (A) | contributes `±I` to the node RHS (linear source) |
| `Diode` | a(anode), k(cathode) | `Is`, `n`, `Vt` | `evaluate(vAK) → {current, conductance}` (Shockley, FR-004/R2) |

- **`admittance()`** (linear elements): returns the conductance contribution with the correct
  nodal sign convention (`+G` on the diagonal for both terminal nodes, `−G` on the off-diagonal).
- **`evaluate(vTerminal)`** (nonlinear): returns `{ current, conductance }` = the Shockley
  current and its small-signal `dI/dV` at the given terminal voltage (R2).
- **`companion(dt, state)`** (reactive): returns `{ Geq, Ieq }` for the backward-Euler
  discretization at timestep `dt`; the `state` (previous voltage for C, previous current for L)
  is supplied by the solver (R3).
- **Invariant (FR-006):** none of these exposes `stamp(...)` or `scatter(...)`. Physics only.

## Component (container form)

- **`Component`** = `std::variant<Resistor, Capacitor, Inductor, VoltageSource,
  CurrentSource, Diode>` (FR-008, R4). Dispatch via `std::visit`; no vtable, no heap.
- Classification helpers (compile-time / `std::visit`): `isLinear`, `isReactive`,
  `isNonlinear` — used by the solver to route each component and to enforce the ≥2-nonlinearity
  refusal (FR-016).

## Netlist<MaxNodes, MaxComponents>

- **Fields:** `nodeCount : int`; `components : std::array<Component, MaxComponents>`;
  `count : int` (number in use). Templated capacities are compile-time (FR-009, OQ3); no heap.
- **Lifecycle:**
  1. *build* (allowed to allocate — but here it does not need to) — add nodes/components;
     adding beyond `MaxComponents`/`MaxNodes` raises a descriptive **over-capacity** error
     (FR-010), no truncation.
  2. *`prepare()`* — runs **topology validation** (missing ground, floating node, over-capacity)
     and raises a descriptive error on failure (FR-010). On success the netlist is
     **solve-ready**.
  3. *solve* (per sample) — consumed by a solver; **no allocation, no throw** (FR-011).
- **Validation rules** (all at `prepare()`): ground referenced; no floating node; counts within
  capacity; exactly the nonlinearity count the target solver accepts is a *solver* concern
  (FR-016), not a netlist concern.

## Companion model (value returned by a reactive component)

- **`{ Geq : double, Ieq : double }`** — the backward-Euler equivalent conductance and history
  current for one reactive element at the current timestep (R3). Produced by
  `Capacitor::companion` / `Inductor::companion`; consumed by the solver when it assembles the
  system. Not stored on the component.

## Reference solver state (lab-only; NOT part of the primitive)

- Lives in `core/labs/component-abstractions/`. Holds the per-node **previous voltages** and
  per-inductor **previous currents** (the history that companions consume), the fixed-size
  working matrix/RHS for Gaussian elimination, and the Newton iteration bound + residual.
- Explicitly **non-normative** and Phase-5-superseded. Documented as such in the lab README.

---

## Entity relationships

```text
Netlist<MaxNodes,MaxComponents>
  ├─ owns nodeCount (NodeId 0..nodeCount-1, ground=0)
  └─ owns std::array<Component> (Component = variant of the 6 v1 types)
         each Component binds terminals → NodeId
         each Component exposes ONLY its own physics (admittance / evaluate / companion)

ReferenceSolver (lab)   ──reads──▶  Netlist (physics)          ──produces──▶  node voltages
  owns history + working matrix        never mutated by solve                  validated vs analytic
  (previous v / previous i)            (component values are const in solve)
```

The arrow that matters: the solver **reads** the netlist's per-component physics and owns all
mutable solve state. That is the seam Phase 5 MNA / Phase 6 WDF reuse — they are alternative
readers of the same immutable vocabulary.

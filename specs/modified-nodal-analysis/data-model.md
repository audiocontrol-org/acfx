# Data Model: Modified Nodal Analysis (MNA) primitive

Phase 1 of `/speckit-plan`. Entities are compile-time-sized value/aggregate types;
there is no persistence. "Validation rules" are enforced at **plan time** (throwing)
per the two-phase contract; the per-solve phase assumes a valid plan.

---

## MnaSystem *(Layer 1 — abstract bordered linear engine)*

Template: `MnaSystem<int MaxNodes, int MaxBranches>`. Knows nothing about components.

| Field | Type | Notes |
|-------|------|-------|
| `a_` | `std::array<std::array<double, Dim>, Dim>` | augmented matrix; `Dim = MaxNodes + MaxBranches` |
| `z_` | `std::array<double, Dim>` | RHS |
| `x_` | `std::array<double, Dim>` | solution (node voltages then branch currents) |
| `branchCount_` | `int` | branches allocated so far (0..MaxBranches) |
| `matScale_` | `double` | largest \|entry\| seen this assembly (for D1 threshold) |

**Relationships**: consumed by `MnaAssembler`; standalone-usable by any caller that
wants to stamp a system directly (this is the reusable core TASK-14 targets).

**Validation rules**:
- `addBranch()` throws if `branchCount_ == MaxBranches` (plan-time).
- stamp/read node indices must be in `[0, MaxNodes)`; ground (0) contributes to no
  retained row/col. Out-of-range is a plan-time precondition violation.
- `solve()` returns `false` when a pivot is singular by the relative threshold (D1);
  never throws.

**State**: `reset()` zeroes `a_/z_/x_/matScale_` (NOT `branchCount_`, which the plan
phase fixes). Between refresh cycles the structure (branch count) is invariant.

## MnaAssembler *(Layer 2 — the element→matrix mapping site)*

Template: `MnaAssembler<int MaxNodes, int MaxComponents, int MaxBranches>` (or the
capacities threaded to match `Netlist`). Holds the fixed plan, no reactive history.

| Field | Type | Notes |
|-------|------|-------|
| `branchOf_` | `std::array<int, MaxComponents>` | component index → branch index, or `-1` if the component is not branch-augmented; filled at plan time |
| `planned_` | `bool` | guards `refresh()` against being called before `plan()` |

**Relationships**: reads a `Netlist<MaxNodes, MaxComponents>`; writes an
`MnaSystem<MaxNodes, MaxBranches>`; reads companions from a `CompanionSupply`.

**Validation rules** (plan-time, throwing): branch overflow (delegated to
`addBranch`), out-of-range node id, degenerate element values (e.g. R ≤ 0), and any
element the assembler cannot represent (surfaced as a descriptive error, never a
silent skip — Principle V).

## Branch *(augmented current unknown)*

Not a stored struct — a **concept**: one extra unknown per ideal voltage source and
per op-amp/nullor. Represented by its integer index `k` (row/col `MaxNodes + k`).
Count is topological, fixed at plan time (FR-014). Diodes/capacitors/inductors add
**no** branches.

## Companion `{Geq, Ieq}` *(caller-supplied Norton reduction)*

Reuses the existing `acfx::Companion` (`core/primitives/circuit/models/companion.h`):

| Field | Type | Notes |
|-------|------|-------|
| `Geq` | `double` | equivalent conductance, stamped via `stampConductance` |
| `Ieq` | `double` | equivalent current, stamped via `stampRhsCurrent` |

MNA **consumes** companions; it never produces them (D6). Supplied per-solve through
the `CompanionSupply` seam for each reactive/nonlinear component.

## CompanionSupply *(the sibling-primitive seam)*

A lightweight, non-owning callable/view: given a component index, returns that
element's `Companion` for the current solve. No allocation, no ownership. The
`newton-iteration` (nonlinear) and `implicit-integration` (reactive) siblings
implement it; v1 tests use a hand-written harness. This is the interface that keeps
MNA stateless and the decomposition intact.

## Solution accessors

`nodeVoltage(NodeId) -> double` (ground returns 0), `branchCurrent(int k) -> double`.
Both total over valid indices and `noexcept` (D7).

---

### Entity relationships (summary)

```text
Netlist ──read──▶ MnaAssembler ──stamps──▶ MnaSystem ──solve()──▶ x_ (voltages, branch currents)
                       ▲
              CompanionSupply (per-solve, from newton-iteration / implicit-integration)
```

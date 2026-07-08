# Contract: `MnaAssembler` (Layer 2 — Netlist → MnaSystem stamps)

Header: `core/primitives/circuit/mna/mna-assembler.h` · Namespace: `acfx::mna`

```cpp
template <int MaxNodes, int MaxComponents, int MaxBranches>
class MnaAssembler;
```

The single authoritative site mapping each component onto `MnaSystem` stamps. Holds
the fixed branch plan; no reactive history, no integration rule.

## Public API

| Method | Signature | Phase | Throws? | Contract |
|--------|-----------|-------|---------|----------|
| `plan` | `void plan(const Netlist<MaxNodes, MaxComponents>& nl, MnaSystem<MaxNodes, MaxBranches>& sys)` | **plan (once)** | **yes** | Walks `nl`; calls `sys.addBranch()` once per ideal voltage source + op-amp; records the component→branch map; validates (overflow, out-of-range node, degenerate value, unrepresentable element → descriptive throw). |
| `refresh` | `void refresh(const Netlist<...>& nl, const CompanionSupply& comps, MnaSystem<...>& sys) noexcept` | per-solve | no | `sys.reset()` then re-stamp values: resistor→conductance; current source→RHS; voltage source→branch incidence+value; op-amp→nullor border; reactive/nonlinear→`comps`-supplied `Companion{Geq,Ieq}` (Geq→conductance, Ieq→RHS). Never calls `addBranch`. |

Callers then invoke `sys.solve()` and read `sys.nodeVoltage(...)` / `branchCurrent(...)`.

### Element → stamp mapping (the authoritative table)

| Component | Stamp | Branch? |
|-----------|-------|---------|
| `Resistor{a,b,R}` | `stampConductance(a, b, 1/R)` | no |
| `CurrentSource{p,n,I}` | `stampRhsCurrent(p, +I)`, `stampRhsCurrent(n, −I)` | no |
| `VoltageSource{p,n,V}` | `addBranch`(plan) + `stampBranchIncidence(k,p,n)` + `stampBranchValue(k,V)` | **yes** — grounded **and floating** |
| `OpAmp{in+,in−,out}` | branch `k`: norator current into `out` KCL; nullator constraint `V(in+)−V(in−)=0` via incidence | **yes** |
| `Capacitor` / `Inductor` / `Diode` | `Companion{Geq,Ieq}` from `CompanionSupply`: `stampConductance(a,b,Geq)`, `stampRhsCurrent` with `Ieq` | no |

## CompanionSupply (the sibling seam)

```cpp
// Non-owning: given a component index, return its Norton companion for this solve.
struct CompanionSupply { Companion at(int componentIndex) const noexcept; };
```

Implemented by `newton-iteration` (nonlinear linearizations) and
`implicit-integration` (reactive companions); a hand-written harness in v1 tests.
MNA never computes a companion (D6, FR-010).

## Invariants / guarantees

- **Two-phase**: `plan` (throwing, once) precedes any `refresh` (`noexcept`,
  alloc-free, hot path). `refresh` before `plan` is a precondition violation
  (guarded by `planned_`).
- **Pure per solve**: identical `(nl, comps)` → identical stamps → identical solution
  (FR-011). No state persists across `refresh` calls except the fixed plan.
- **No silent skips**: an unrepresentable element throws at plan time (Principle V),
  never a silent no-op.
- **Floating sources supported** — the capability gain over the labs (SC-005).

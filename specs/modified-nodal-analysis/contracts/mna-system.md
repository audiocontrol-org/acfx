# Contract: `MnaSystem` (Layer 1 — abstract bordered linear engine)

Header: `core/primitives/circuit/mna/mna-system.h` · Namespace: `acfx::mna`

```cpp
template <int MaxNodes, int MaxBranches>
class MnaSystem;   // Dim = MaxNodes + MaxBranches, fixed at instantiation
```

Knows nothing about components. Pure stamp + solve over an augmented linear system.
This is the reusable linear-algebra core TASK-14 asks the lab solvers to converge on.

## Public API

| Method | Signature | Phase | Throws? | Contract |
|--------|-----------|-------|---------|----------|
| `reset` | `void reset() noexcept` | per-solve | no | Zeroes matrix/RHS/solution/`matScale`; leaves `branchCount` intact. |
| `stampConductance` | `void stampConductance(NodeId i, NodeId j, double g) noexcept` | per-solve | no | Four-corner ground-aware stamp (D2): `+g` on non-ground diagonals, `−g` on non-ground off-diagonals. |
| `stampRhsCurrent` | `void stampRhsCurrent(NodeId n, double i) noexcept` | per-solve | no | Adds `i` to node `n`'s RHS (no-op if `n` is ground). |
| `addBranch` | `int addBranch()` | **plan** | **yes** (overflow) | Allocates a branch-current unknown; returns index `k` in `[0, MaxBranches)`. Throws if capacity exceeded. |
| `stampBranchIncidence` | `void stampBranchIncidence(int k, NodeId p, NodeId n) noexcept` | per-solve | no | Writes `+1/−1` into B (KCL rows × branch col) and C (branch row × node cols) for non-ground terminals. |
| `stampBranchValue` | `void stampBranchValue(int k, double rhs) noexcept` | per-solve | no | Sets branch `k`'s constraint RHS. |
| `stampBranchResistance` | `void stampBranchResistance(int k, double r) noexcept` | per-solve | no | Adds `−r` on branch `k`'s diagonal; `r = 0` → ideal (zero diagonal, D5). |
| `solve` | `bool solve() noexcept` | per-solve | no | Gaussian elimination with partial pivoting (D5). Returns `false` on singular pivot (relative threshold, D1); leaves no NaN in readable outputs. |
| `nodeVoltage` | `double nodeVoltage(NodeId) const noexcept` | read | no | Solution voltage; ground → `0.0`. Total over `[0, MaxNodes)`. |
| `branchCurrent` | `double branchCurrent(int k) const noexcept` | read | no | Solution branch current for `k` in `[0, branchCount)`. |

## Invariants / guarantees

- **RT-safe**: every per-solve/read method is `noexcept` and allocation-free.
- **No fallback**: `solve()` injects no gmin; a singular system yields `false`, not a
  patched result (Principle V).
- **Pivoting mandatory**: constraint rows have a zero diagonal; naive elimination is
  invalid (D5).
- **Stateless across solves**: after `reset()`, output depends only on the stamps
  applied before `solve()`.

## Preconditions

- `addBranch` is a **plan-time** call; it is the only method permitted to throw.
- Node ids passed to stamps/reads are in `[0, MaxNodes)`; branch ids in
  `[0, branchCount)`. Validity is guaranteed by the plan phase (D7); violating it is
  UB-by-precondition, not a runtime throw on the hot path.

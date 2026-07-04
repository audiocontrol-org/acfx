# Contract — AC solver (lab, `core/labs/passive-tone-stacks/solver/ac-solver.h`)

Host-only, non-normative. Computes the exact continuous-time transfer function of a passive netlist by linear complex `.ac` analysis. C++20 permitted (lab); `std::complex<double>` throughout.

## Shape

```
template <int MaxNodes, int MaxComponents>
std::complex<double>
  solveAC(const Netlist<MaxNodes, MaxComponents>& netlist,
          double omega, NodeId inNode, NodeId outNode);
```

## `solveAC(...)` contract

- Assembles the reduced nodal admittance system `Y(jω)·V = I` by **reading each component's admittance at `jω`** — `Resistor → 1/R`, `Capacitor → jωC`, `Inductor → 1/(jωL)` — and imposes the ideal input `VoltageSource` by **fixed-node reduction** (the R1 method reused from `LinearSolver`), not `gmin` and not MNA branch-current augmentation.
- Solves by **complex Gaussian elimination with partial pivoting** over `std::complex<double>`.
- Returns `H = V(outNode) / V(inNode)` at `ω`.
- **The solver reads physics; it never re-derives a component's law** (the FR-006 seam of `component-abstractions`, carried into the frequency domain).
- **Heap-free solve:** every working buffer is a fixed-size `std::array` sized by the `Netlist` template parameters — no `new`/`delete`/`std::vector` in `solveAC`.

## Errors (FR-012) — no fallback

- A zero / near-singular pivot (ill-posed network at that `ω`) → `std::runtime_error` **naming `ω`**. Never a silent wrong `H`, never a fabricated response. Mirrors `LinearSolver`'s zero-pivot policy.

## Boundary (FR-013) — the lab must not grow into MNA

- **No** branch-current unknowns (not MNA), **no** Newton loop, **no** trapezoidal/higher-order integration. This is a **linear complex solve only**. A nonlinearity or a second reactive-history concern is out of scope here (those are Phase 5/6).

## Validation surface (harness + Tier-2 test)

- **Sanity first:** RC low-pass → `−20 dB/decade` slope, phase → `−90°`; resistive divider → flat `R2/(R1+R2)`; matched to closed form to ~`1e-9` (SC-003).
- **Then tone stacks:** FMV and Baxandall `|H(f)|` matched to the independent analytic (Duncan) transfer function within **0.1 dB** on a log grid of ~10 pts/decade over 20 Hz–20 kHz, at ≥3 control settings each (SC-004/SC-005), plus the monotonic mid-scoop / treble invariants.

# Contract — `NewtonSolver` (public header API)

Header: `core/primitives/circuit/newton/newton-solver.h` · Namespace: `acfx::newton` ·
C++17, header-only, template-sized. This is the primitive's public surface. Signatures are
illustrative of the contract (names/shape may refine in implementation); the **behavioral
guarantees** are binding.

## Types

```cpp
namespace acfx::newton {

struct NewtonStatus {
    bool   converged      = false;  // max|Δv| < voltageTol within the bound?
    int    iterations     = 0;      // iterations consumed (≤ maxIterations)
    double voltageResidual = 0.0;   // final max|Δv| across diode biases (V)
    double currentResidual = 0.0;   // final |ΔI_total| of diode current (A) — reported, not gated
};

template <int MaxNodes, int MaxComponents, int MaxBranches>
class NewtonSolver { /* ... */ };

} // namespace acfx::newton
```

## Construction

```cpp
explicit NewtonSolver(int maxIterations = 50,
                      double voltageTol = 1e-9,
                      double currentTol = 1e-12);
```

- **C1.** Throws `std::invalid_argument` if `maxIterations < 1` or `voltageTol <= 0` or
  `currentTol <= 0`. (Construction is off the hot path; throwing here is correct.)
- **C2.** Defaults are the lab-validated values; callers may tighten. The solver MUST NEVER
  silently retune these to mask a non-converging case.

## `plan()` — off the hot path (throw-permitted)

```cpp
void plan(const Netlist<MaxNodes, MaxComponents>& nl,
          mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& asm,
          mna::MnaSystem<MaxNodes, MaxBranches>& sys);
```

- **P1.** Delegates branch allocation + topology validation to `MnaAssembler::plan`
  (which may throw on over-capacity / out-of-range / degenerate netlists).
- **P2.** Scans the netlist once, recording the diode component indices and the per-component
  is-diode mask. After `plan()`, `planned()` is true.
- **P3.** Performs no diode physics and no solve. Idempotent for a given netlist.

## `solve()` — the hot path (throw-free, allocation-free)

```cpp
template <class BaseCompanionSupply>
NewtonStatus solve(const Netlist<MaxNodes, MaxComponents>& nl,
                   const BaseCompanionSupply& base,
                   const std::array<double, MaxNodes>& initialNodeVoltages,
                   mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& asm,
                   mna::MnaSystem<MaxNodes, MaxBranches>& sys);
```

- **S1 (linearization).** Each iteration, for every diode: read `vAK` from the current
  node-voltage iterate, `{I,g} = Diode::evaluate(vAK)`, form `Companion{Geq:g, Ieq:I − g·vAK}`.
- **S2 (composition).** Refreshes the MNA system through a composed supply: diode component
  indices get S1's companion; all other indices delegate to `base` unchanged. `base` is held
  fixed for the whole `solve()`.
- **S3 (global step).** All diodes are linearized at the same iterate and solved together in
  one `MnaSystem::solve()` per iteration (coupled multi-diode; never per-diode sequencing).
- **S4 (damping).** After each solve, each diode's new `vAK` is passed through
  `Diode::limitJunctionVoltage(vNew, vOld)` with `vOld` the previous inner iterate.
- **S5 (convergence).** Gate on `max|Δv| < voltageTol` ONLY. Compute and report
  `currentResidual`; never gate on it.
- **S6 (bound).** At most `maxIterations` iterations. Zero diodes → exactly one linear solve
  (no spurious iteration).
- **S7 (no fallback).** If `MnaSystem::solve()` returns false (singular), stop and return
  `converged == false` — **no throw on the hot path**, no gmin / source-step / substitution.
  On non-convergence within the bound, return `converged == false` with the last iterate left
  in `sys` (documented not physical). Never a fabricated output.
- **S8 (statelessness).** Solving identical `(nl, base, initialNodeVoltages)` twice yields
  identical `NewtonStatus` and identical node voltages. No state persists between calls; a
  non-converged solve corrupts no state usable by a subsequent solve.
- **S9 (initial guess).** `initialNodeVoltages` is node voltages only (index 0 = ground,
  value ignored/zero); branch currents are not part of the guess. The guess affects iteration
  count, never the converged fixed point.
- **S10 (RT-safety).** Zero heap allocation and no locks; all scratch is fixed-capacity. A
  precondition violation (`solve()` before `plan()`, or a netlist inconsistent with the plan)
  is surfaced deterministically, not as undefined behavior.

## Read accessors

- Node voltages / branch currents are read from the driven `MnaSystem` (`nodeVoltage(n)` /
  `branchCurrent(k)`) after `solve()` returns; `NewtonSolver` adds no separate read surface
  beyond `NewtonStatus` and `planned()`.

## Non-goals (out of contract)

- No integration rule, reactive history, or time-stepping (that is `implicit-integration`).
- No linear-algebra of its own (that is `MnaSystem`).
- No new component types; no complex/AC scalar (v1 real `double`).
- No gmin / source-stepping / any silent convergence fallback.

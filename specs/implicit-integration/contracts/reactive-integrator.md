# Contract — `ReactiveIntegrator` (public header API)

Header: `core/primitives/circuit/integration/reactive-integrator.h` · Namespace:
`acfx::integration` · C++17, header-only, template-sized. This is the primitive's public
surface. Signatures are illustrative of the contract (names/shape may refine in
implementation); the **behavioral guarantees** are binding. The assembler parameter is named
`assembler` (not `asm`, a reserved C++ keyword).

## Types

```cpp
namespace acfx::integration {

// Integration-rule policies (research R1/R2). Stateless; pure static functions.
struct BackwardEuler {
    static Companion capacitorCompanion(double C, double dt, double vPrev, double iPrev) noexcept;
    static Companion inductorCompanion (double L, double dt, double vPrev, double iPrev) noexcept;
};
struct Trapezoidal {
    static Companion capacitorCompanion(double C, double dt, double vPrev, double iPrev) noexcept;
    static Companion inductorCompanion (double L, double dt, double vPrev, double iPrev) noexcept;
};

struct StepResult {
    bool   converged      = false;  // did the composed solve converge?
    int    iterations     = 0;      // Newton iterations consumed (0/1 for the linear path)
    double voltageResidual = 0.0;   // final residual from the composed solve
};

template <class Rule, int MaxNodes, int MaxComponents, int MaxBranches>
class ReactiveIntegrator { /* ... */ };

} // namespace acfx::integration
```

## Construction

```cpp
explicit ReactiveIntegrator(double dt,
                            int    maxIterations = 50,     // forwarded to NewtonSolver (nonlinear path)
                            double voltageTol    = 1e-9);
```

- **C1.** Throws `std::invalid_argument` if `dt <= 0` (companions divide by `dt`), or if the
  forwarded Newton config is invalid (`maxIterations < 1` / `voltageTol <= 0`). Construction is
  off the hot path; throwing here is correct.
- **C2.** `Rule` is chosen at instantiation (`BackwardEuler` default). It is fixed for the life
  of the object and is **never** silently switched at runtime (research R8).

## Rule policies — `capacitorCompanion` / `inductorCompanion`

- **RP1.** Return the Norton `Companion{Geq, Ieq}` per research R1, in MNA's convention
  `i(a,b) = Geq·(V(a)−V(b)) − Ieq`. `BackwardEuler` MUST equal the shipped
  `Capacitor::companion(dt, vPrev)` / `Inductor::companion(dt, iPrev)` (single-sourced, R9).
- **RP2.** `noexcept`, allocation-free, pure (no state). `Trapezoidal` consumes both `vPrev` and
  `iPrev`; `BackwardEuler` ignores the term its rule does not read.

## `plan()` — off the hot path (throw-permitted)

```cpp
void plan(const Netlist<MaxNodes, MaxComponents>& nl,
          mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& assembler,
          mna::MnaSystem<MaxNodes, MaxBranches>& sys);
```

- **P1.** Delegates branch allocation + topology validation to `MnaAssembler::plan` (may throw
  on over-capacity / out-of-range / degenerate netlists).
- **P2.** Scans the netlist once, recording the reactive component indices, the per-component
  is-reactive mask, and whether any nonlinear element is present (`hasNonlinear_`, choosing the
  step branch once). After `plan()`, `planned()` is true.
- **P3.** Does not compute companions or advance history. Initializes cross-sample state to zero
  (`reset()` semantics). Idempotent for a given netlist.

## `reset()` — off the hot path

```cpp
void reset() noexcept;
```

- **RS1.** Returns cross-sample state (`vPrev_`, `iPrev_`, `warmStart_`) to zero state
  (FR-016). Does not change the plan (topology). Safe to call between transients.

## `seedHistory()` — off the hot path (caller-provided consistent initial condition)

```cpp
void seedHistory(int reactiveSlot, double vPrev, double iPrev) noexcept;
```

- **IC1.** Sets one reactive element's initial history `{vPrev, iPrev}` to a caller-provided
  consistent initial condition (e.g. the element's true `t=0` terminal voltage / current). This
  is the mechanism by which the deferred DC-operating-point warm start is "a caller concern"
  (spec Open Question 4): history still **defaults to zero** on `plan`/`reset` (FR-016), and this
  is an explicit opt-in. No-op for a `reactiveSlot` outside `[0, reactiveCount_)`. Off the hot
  path; call after `plan()` and before the first `step()`. Does **not** change topology or the
  rule, and is **not** a silent rule switch (FR-015 intact).
- **IC2 (why it matters).** A first-order rule (`BackwardEuler`) reads only the one history term
  that is its own state, so a zero seed is harmless. `Trapezoidal` reads **both** history terms;
  its 2nd-order accuracy (SC-002) requires the non-state term to equal the true initial
  derivative, which a hard step from rest makes nonzero. Seeding the consistent IC lets
  `Trapezoidal` demonstrate its ≈2nd-order convergence; without it, a step-from-zero-state
  transient is inconsistent initial data and the observed order degrades to ≈1.

## `step()` — the hot path (throw-free, allocation-free)

```cpp
StepResult step(const Netlist<MaxNodes, MaxComponents>& nl,
                mna::MnaAssembler<MaxNodes, MaxComponents, MaxBranches>& assembler,
                mna::MnaSystem<MaxNodes, MaxBranches>& sys,
                newton::NewtonSolver<MaxNodes, MaxComponents, MaxBranches>& newton);  // used only if hasNonlinear_
```

- **S1 (companions once).** Compute each reactive element's `Companion` from `Rule`, `dt_`, and
  its current history `{vPrev_, iPrev_}` — **once per step**, before the solve. Held fixed for
  the whole step (never recomputed inside the Newton loop; dissolves TASK-13).
- **S2 (composition).** Expose the fixed reactive companions as a `CompanionSupply` and:
  - `hasNonlinear_ == false` → `MnaAssembler::refresh(nl, supply, sys)` then `MnaSystem::solve()`.
  - `hasNonlinear_ == true`  → `newton.solve(nl, supply, warmStart_, assembler, sys)` — the supply
    is Newton's fixed `base`; Newton overrides diode indices and delegates reactive indices here.
  The reactive companions are held fixed across the whole solve (Newton varies only diodes).
- **S3 (read + reconstruct).** After the solve, read converged node voltages; for each reactive
  element compute `v^n = V(a) − V(b)` and `i^n = Geq·v^n − Ieq` using that step's stamped
  companion (research R3/R4).
- **S4 (advance once).** Advance history exactly once: `vPrev_ := v^n`, `iPrev_ := i^n` per
  reactive element; `warmStart_ := ` converged node voltages. Advance happens **after**
  convergence, **not** per Newton iteration.
- **S5 (no fallback).** On a non-converged composed solve (`StepResult.converged == false`, from
  a propagated `NewtonStatus` or a `false` `MnaSystem::solve()`), surface the failure **by value**
  and do **not** advance reactive history from the untrustworthy iterate as if physical — no throw
  on the hot path, no gmin/substitution, and **no silent rule switch** to rescue stiffness.
- **S6 (rule fidelity).** The selected `Rule` is used verbatim every step. Trapezoidal ringing on
  a stiff node is produced faithfully (A-stable, not L-stable); it is never silently damped by
  switching to backward-Euler.
- **S7 (statefulness / determinism).** `step()` reads and updates cross-sample state; given
  identical `(state, inputs)` it yields identical `StepResult` and identical node voltages. A
  non-converged step leaves history un-advanced (S5), so it does not silently corrupt the next
  step with an unphysical state.
- **S8 (RT-safety).** Zero heap allocation and no locks; all scratch is fixed-capacity. A
  precondition violation (`step()` before `plan()`, or a netlist inconsistent with the plan) is
  surfaced deterministically **by value** as `StepResult{ converged = false, iterations = 0,
  voltageResidual = 0 }` — throw-free and allocation-free on the hot path.
- **S9 (zero reactive elements).** A netlist with no reactive elements makes the supply empty; the
  step reduces to a single composed solve (linear or Newton) with no reactive stamping and a no-op
  history advance — a clean passthrough, not an error (spec Edge Cases).

## Read accessors

- Node voltages / branch currents are read from the driven `MnaSystem` (`nodeVoltage(n)` /
  `branchCurrent(k)`) after `step()` returns. Reactive-element current for a given step is the
  reconstructed `i^n` (S3); the integrator exposes its stored history via read accessors for
  testing (`vPrev(reactiveSlot)`, `iPrev(reactiveSlot)`), not a separate solve surface.

## Non-goals (out of contract)

- No linear-algebra of its own (that is `MnaSystem`) and no nonlinear loop of its own (that is
  `NewtonSolver`) — both are composed, not reimplemented.
- No new component types; no complex/AC scalar (v1 real `double`); no variable-`dt` path (fixed
  per plan, spec Open Question 2).
- No gmin / source-stepping / adaptive-order / any silent convergence or rule fallback.
- No DC-operating-point auto-initialization (history defaults to zero state; a settled warm start
  is a caller concern — spec Open Question 4).

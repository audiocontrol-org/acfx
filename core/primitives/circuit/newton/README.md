# Newton-Iteration Solver — Production Primitive

The `core/primitives/circuit/newton/` primitive is the nonlinear outer loop for
circuit modeling. It implements a single `NewtonSolver` type that drives the
shipped MNA core (`core/primitives/circuit/mna/`) by linearizing nonlinearities,
planning the iteration structure once per netlist, and running throw-free
convergence checks on the hot path. It does **not modify MNA** — instead, it
supplies nonlinear companions to the assembler and manages iteration state.

## Architecture — Stateless Two-Phase, RT-Safe Contract

The primitive is split into **two phases with a strict error contract**:

- **Plan phase** (`NewtonSolver::plan()`) — runs once per netlist, off the hot
  path: allocates iteration state, probes nonlinearities to discover their
  linearized companions, validates network topology for solvability, and fixes
  the iteration structure. **May throw** descriptive errors for non-convergent
  networks, incompatible component combinations, or singular topologies.

- **Per-solve phase** (`NewtonSolver::solve()`) — runs hot, per sample: executes
  the Newton loop (typically 2–4 iterations), refreshing nonlinear companions at
  each step and solving the linearized MNA system. **Throws-free and
  heap-allocation-free**. Convergence status is returned by value
  (`NewtonStatus.converged`), never masked.

The caller owns the initial node-voltage warm-start guess (seeded by prior
timestep or DC sweep); Newton iterates from there.

## Charter — Multi-Nonlinearity Capability

This primitive **lifts the existing labs' single-nonlinearity refusal** by
supporting ≥2 interacting nonlinearities at distinct node pairs. A diode bridge,
Schottky doubler, soft clipper with biased diode pair, or dual soft-saturation
stage — any circuit with multiple independently-modeled nonlinear elements — can
now be solved.

Each nonlinear component supplies a `Companion{Geq, Ieq}` at each iteration.
Newton converges by iteratively linearizing and re-solving the MNA system until
the node-voltage residuals fall below tolerance or iteration count is exhausted.

## Contract — No Fallback, No Silent Failure

Non-convergence and singular systems are **surfaced by value** in `NewtonStatus`:

- `converged` — whether the iteration reached tolerance
- `iterations` — iterations consumed
- `voltageResidual` — final max|Δv| across diode biases (V)
- `currentResidual` — final |ΔI_total| of diode current (A)

There is **no silent gmin injection, no source stepping, no substitution**. If the
network does not converge, the caller sees it and decides the response (retry with
finer timestep, clamp outputs, log and continue, etc.).

## Scope — What's Intentionally Not Here

- **Linear system solver** — owned by `MnaSystem` in the shipped
  `core/primitives/circuit/mna/`. Newton is a wrapper, not a solver.
- **Integration rule and reactive history** — owned by `implicit-integration`.
  Newton handles nonlinearity; integration rules handle inductors and capacitors.
- **Lab migration (TASK-14)** — retiring the duplicated outer-loop code in the
  existing labs (`OpAmpClipperSolver`, `TransientClipper`, etc.) onto `NewtonSolver` is
  captured follow-on scope, sequenced after this primitive lands. Not part of
  this deliverable.
- **Bounded-iteration heuristics** — v1 uses fixed iteration count or residual
  tolerance. Adaptive step-size, affine-invariant norms, or globalized Armijo
  line-search are documented future directions.

## Design Record & Spec

- **Spec** — [`specs/newton-iteration/spec.md`](../../../../specs/newton-iteration/spec.md)
  — functional requirements and test acceptance criteria.
- **API Contract** — [`specs/newton-iteration/contracts/newton-solver.md`](../../../../specs/newton-iteration/contracts/newton-solver.md)
  — detailed type and method signatures.
- **Design Record** — [`docs/superpowers/specs/2026-07-07-newton-iteration-design.md`](../../../../docs/superpowers/specs/2026-07-07-newton-iteration-design.md)
  — design-phase decisions and open questions.
- **Project Folder** — [`specs/newton-iteration/`](../../../../specs/newton-iteration/)
  — the plan, research decisions, and validation guide.

Host-side validation lives in `tests/core/newton-*-test.cpp` (solver,
convergence, multi-diode circuits, RT-safety, invariants, and a lab-equivalence
oracle proving parity with existing lab solvers).

# Research: Modified Nodal Analysis (MNA) primitive

Phase 0 of `/speckit-plan`. Resolves the **implementation-shaping** decisions the
spec leaves to the plan. It does **not** resolve the spec's captured *open
questions* (complex/AC scalar, controlled sources, canonical `DiodeSpec` home,
sibling sequencing) — those are deliberately deferred to a later scoping pass.

Prior art is the four existing lab solvers; decisions below reuse their proven
mechanics so the primitive is a faithful superset (SC-002).

---

## D1 — Singular-pivot threshold: relative, matrix-scaled

**Decision**: `solve()` rejects a pivot as singular when
`|pivot| < kRelEps · matScale`, where `matScale` is the largest absolute entry
seen during assembly (or the max abs diagonal) and `kRelEps ≈ 1e-12`. Returns
`false`; injects nothing.

**Rationale**: Adopts `AugmentedSolver`'s relative gate (augmented-solve.h ~:240)
over `LinearSolver`'s absolute `1e-300` floor. An absolute floor mis-classifies a
well-posed but poorly scaled system (e.g. µS conductances beside unit sources).
Satisfies SC-004 and the US5-AS3 "small-but-nonzero pivots still solve" scenario.

**Alternatives considered**: absolute floor (rejected — scale-fragile);
condition-number estimate (rejected — extra work on the hot path, unjustified at
these matrix sizes).

## D2 — Ground-aware four-corner conductance stamp

**Decision**: local index `local(id) = id − 1` (node 0 = ground = dropped row/col).
`stampConductance(i, j, g)` adds `+g` to the two diagonal entries and `−g` to the
two off-diagonal entries **only for terminals that are not ground**; a
ground-referenced conductance stamps a single diagonal `+g`.

**Rationale**: The canonical reduced-MNA stamp; matches `LinearSolver`
(linear-solver.h) exactly, which is prerequisite for the equivalence oracle (SC-002).

**Alternatives considered**: keeping the ground row and pinning it (rejected —
wastes a dimension and diverges from the labs' representation).

## D3 — Branch augmentation: incidence + optional resistance, zero-diagonal constraint

**Decision**: a branch `k` occupies row/col `MaxNodes + k` in the augmented matrix.
`stampBranchIncidence(k, p, n)` writes `+1/−1` into the B block (node KCL rows,
branch cols) and the C block (branch rows, node cols) for non-ground terminals;
`stampBranchValue(k, rhs)` sets the branch row's RHS; `stampBranchResistance(k, r)`
adds `−r` on the branch diagonal (0 for an ideal source/nullor → the zero diagonal
that makes partial pivoting load-bearing, D5).

**Rationale**: the classic `[G B; C 0]` bordered form; generalizes
`AugmentedSolver::borderOpAmps` (which special-cased op-amps) to any branch-augmented
element (voltage sources incl. **floating**, op-amps, future controlled sources).

**Alternatives considered**: separate matrices per augmentation type (rejected —
one uniform border is simpler and is exactly what "Modified" nodal analysis is).

## D4 — Two-phase surface: `plan()` (once) vs `refresh()` + `solve()` (hot path)

**Decision**: `MnaAssembler` exposes
- `void plan(const Netlist&, MnaSystem&)` — walks the netlist once, calls
  `addBranch()` per voltage source + op-amp, records a **fixed** component→branch
  index map, validates (throws on overflow / out-of-range / degenerate). May throw.
- `bool refresh(const Netlist&, const CompanionSupply&, MnaSystem&)` then
  `system.solve()` — re-stamps conductance/RHS/companion **values** into the fixed
  structure. `noexcept`, alloc-free. Never calls `addBranch()`.

`MnaSystem::reset()` zeroes the matrix/RHS between refreshes (no reallocation).

**Rationale**: Branch count is topological and invariant across iterations/timesteps
(FR-014), so allocation + validation happen once, off the hot path (FR-012/013).
This is strictly stronger than "assembly may fail before solve" (design Decision 6)
and structurally avoids TASK-13's per-iteration topology recompute.

**Alternatives considered**: single `assemble()` per solve (rejected — puts the
throwing allocator on the hot path, violating Principle VI); caching companions
inside MNA (rejected — would give MNA history/state, breaking the sibling seam).

## D5 — Partial pivoting is mandatory, not optional

**Decision**: Gaussian elimination **with partial pivoting** (row swap to the
largest-magnitude sub-column pivot) is the only solve path.

**Rationale**: the C block / branch-constraint rows have a **zero diagonal**;
naive elimination divides by zero on the first constraint row. Matches every lab
that borders (opamp-stage-solver.h documents this). FR-003.

**Alternatives considered**: none viable — pivoting is a correctness requirement
here, not a numerical nicety.

## D6 — Companion supply: caller-provided, index-keyed, no self-computation

**Decision**: the per-solve phase receives companions through a lightweight
`CompanionSupply` seam — a callable / view that, given a reactive-or-nonlinear
component's index, returns its `Companion{Geq, Ieq}` for this solve. The assembler
stamps `Geq` as a conductance (D2) across the element's terminals and `Ieq` as a
node current (`stampRhsCurrent`), with the sign convention matching the existing
`Capacitor::companion` / `Inductor::companion` / diode Norton reductions. MNA never
computes a companion and holds no history (FR-010/011).

**Rationale**: this is the seam to `implicit-integration` (reactive companions) and
`newton-iteration` (nonlinear linearizations). Keeping it a caller-supplied view (no
ownership, no allocation) preserves RT-safety and statelessness. For v1 tests, a
hand-written harness plays the companion role.

**Alternatives considered**: MNA computing backward-Euler companions itself
(rejected — collapses the sibling decomposition, gives MNA state/integration rule);
a heap container of companions (rejected — allocation on the hot path).

## D7 — Error signalling: throw at plan time, boolean at solve time, total accessors

**Decision**: plan/prepare-time faults throw descriptive `std::` exceptions
(overflow, out-of-range node, degenerate value). `solve()` returns `bool`
(`false` = not solved). `nodeVoltage`/`branchCurrent` are total over valid indices
and `noexcept`; an out-of-range index is a precondition violation guarded at plan
time, never a hot-path throw. This explicitly avoids the `NullorSolver`
`advanceHistory()` `noexcept`-calls-throwing-`voltage()` latent bug (TASK-14).

**Rationale**: FR-004/005/015 + Principle V (no fallbacks) + Principle VI (no
hot-path throw).

**Alternatives considered**: throwing on singular (rejected — the caller/Newton loop
must branch on it without exception cost on the hot path); silent gmin (rejected —
Principle V bug factory).

## D8 — v1 numeric scope (from spec decisions, restated for the plan)

Real-valued `double`; inductor via caller-supplied Norton companion (native
branch-current inductor MNA is out of v1 scope); ground = node 0. No new component
types. Sub-namespace `acfx::mna`. These are settled by the spec/design, not reopened
here.

---

## Consolidated technology choices

- **Language**: C++17, header-only, templates for compile-time sizing. No C++20
  (the labs' host-only C++20 use does not graduate into the portable core).
- **Storage**: `std::array<std::array<double, Dim>, Dim>` + `std::array<double,Dim>`;
  `Dim = MaxNodes + MaxBranches`. Fixed at instantiation.
- **Testing**: doctest; `AllocationSentinel` for the no-heap assertion; equivalence
  oracle against `LinearSolver` and `NullorSolver`.
- **Portability**: passes `scripts/check-portability.sh` (no platform headers).

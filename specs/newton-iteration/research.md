# Research — Newton–Raphson iteration primitive (Phase 0)

Implementation-shaping decisions. Each: Decision / Rationale / Alternatives considered.
No `NEEDS CLARIFICATION` remained after the approved design record; these resolve the
implementation-level unknowns the plan surfaced.

## R1 — Diode linearization: the Norton companion and its stamp signs

**Decision.** Each iteration, for a diode biased at `vAK` (from the current iterate),
compute `{I, g} = Diode::evaluate(vAK)` and stamp the Norton companion
`Companion{Geq: g, Ieq: g·vAK − I}`. MNA already fixes the stamp signs for a diode
directed anode→cathode: `stampConductance(anode, cathode, Geq)`,
`stampRhsCurrent(anode, +Ieq)`, `stampRhsCurrent(cathode, −Ieq)` (verified in
`mna-assembler.h`), so it consumes the companion as
`i(anode,cathode) = Geq·(V(a)−V(c)) − Ieq`. Newton only supplies the `Companion`
values through the `CompanionSupply`; it does not re-derive the stamp.

**Rationale.** The linearized diode current is `i(v) ≈ I(vAK) + g·(v − vAK) = g·v +
(I − g·vAK)`. Matching this to MNA's consumption `i = Geq·v − Ieq` term-by-term gives
`Geq = g` and `Ieq = −(I − g·vAK) = g·vAK − I(vAK)` — i.e. the Norton history current
carries the **negative** of the linearized-current constant term, because MNA subtracts
`Ieq` from the conductance term. This matches `mna-assembler.h`'s own note
(`Ieq = Geq·vAK₀ − I(vAK₀)`). Using `Diode::evaluate` keeps all physics in the diode
(the seam the diode header defines); the sign convention is MNA's, and the implemented
sign is confirmed by the independent single-diode operating-point oracle (the value that
makes the closed-form bisection reference match). (An earlier draft of this decision wrote
`Ieq = I − g·vAK`, dropping MNA's `−Ieq` sign — corrected here during implementation.)

**Alternatives considered.** (a) Newton computes and stamps directly into `MnaSystem` —
rejected: bypasses the authoritative `MnaAssembler` element→matrix mapping and
re-duplicates stamp logic. (b) Gate on a current-form residual — rejected under R3.

## R2 — Damping: pnjlim and the previous-iterate bookkeeping

**Decision.** Between iterations, damp each junction with
`Diode::limitJunctionVoltage(vNewAK, vOldAK)` where `vOldAK` is that diode's bias from
the *previous* inner iterate. Store a fixed-capacity `std::array` of per-diode previous
biases, indexed by the diode's slot in Newton's diode scan, reset at the top of each
`solve()`. The damped `vAK` is what feeds the next iteration's `Diode::evaluate`.

**Rationale.** `pnjlim` (SPICE) prevents the exponential from exploding between
iterations without moving the fixed point (it is inactive once iterates settle). It is a
property of the junction, so it lives on `Diode`; Newton only supplies `vNew`/`vOld`.
The previous-bias array is per-`solve()` scratch, not cross-sample state (preserves
statelessness, FR-008/FR-010).

**Alternatives considered.** Global step-size damping / line search — rejected for v1:
`pnjlim` is the established, diode-local, allocation-free method the labs already use;
a generic line search adds cost and a non-diode worldview without evidence it is needed.

## R3 — Convergence gate: voltage residual only; current residual reported

**Decision.** Gate convergence on `max over diodes |Δv_AK| < voltageTol` (equivalently
the max node-voltage change). Compute and **report** a current residual
(`|ΔI_total|`) in `NewtonStatus.currentResidual` but never gate on it. Default
configuration mirrors the labs: `maxIterations = 50`, `voltageTol = 1e-9` V,
`currentTol` (reporting reference) `= 1e-12` A — validated `> 0` at construction.

**Rationale.** A diode's reverse-saturation current sits at `−Is` and can exceed any
fixed `currentTol` even at a fully settled voltage, so a current gate would spuriously
reject a converged solve — the `OpAmpClipperSolver` comment documents exactly this. The
lab defaults are the proven starting point; tasks may tighten them per test but never
loosen them to hide non-convergence (FR-012).

**Alternatives considered.** Dual voltage+current gate — rejected (spurious rejects).
KCL-residual gate — deferred; voltage residual is the lab-validated criterion and is
sufficient for the closed-form and oracle acceptance.

## R4 — Companion composition: wrapping the base supply

**Decision.** Newton defines a small internal `CompanionSupply` adapter that holds (a) a
reference to the caller's **base** supply and (b) Newton's per-iteration diode companions
(a fixed-capacity `std::array<Companion, MaxComponents>` plus a per-index "is-diode"
mask, both built at `plan()` time from the netlist scan). Its `at(i) const noexcept`
returns Newton's diode companion when component `i` is a diode, else delegates to
`base.at(i)`. This is the single object handed to `MnaAssembler::refresh`.

**Rationale.** MNA's `refresh` consumes exactly one `CompanionSupply` with
`Companion at(int) const noexcept`. Two siblings feed it (reactive from
`implicit-integration`, diode from Newton); composition preserves that single-supply
contract and the sibling boundary. The is-diode mask is computed once (topology is fixed
across iterations), so `at(i)` stays O(1) and allocation-free.

**Alternatives considered.** (a) Newton owns *all* companions (including reactive) —
rejected: absorbs `implicit-integration`'s charter. (b) MNA grows a second supply
parameter — rejected: changes the shipped MNA contract; composition needs no MNA change.

## R5 — Reading the iterate and seeding the initial guess

**Decision.** After each `MnaSystem::solve()`, read node voltages via
`MnaSystem::nodeVoltage(n)` and form each diode's `vAK = nodeVoltage(anode) −
nodeVoltage(cathode)`. The initial guess is a caller-supplied **node-voltage array**
(length `MaxNodes`, node 0 = ground fixed at 0); on the first iteration Newton derives
each diode's starting `vAK` from that array. Branch currents are never part of the guess
(they are pure outputs of each solve).

**Rationale.** Aligns with MNA's read surface and the third-party-reviewed decision
(Decision 6a). Node voltages fully determine every diode bias; branch-current warm-start
cannot affect nonlinear convergence (R1 shows the diode companion depends only on `vAK`).

**Alternatives considered.** Per-diode `vAK` guess — rejected (bakes a diode-port
worldview into a global solver). Full augmented `x` guess — rejected (branch currents are
inert for convergence; needless surface).

## R6 — No-fallback failure semantics

**Decision.** `solve()` returns `NewtonStatus{converged, iterations, voltageResidual,
currentResidual}`. Non-convergence within `maxIterations` → `converged = false`,
`iterations = maxIterations`, residuals populated, node voltages left at the last iterate
(documented as not physical). A singular linearized system → `MnaSystem::solve()` returns
false → Newton stops and returns `converged = false` (same channel), **no throw on the
hot path**, no gmin / source-step / substituted output.

**Rationale.** Constitution Principle V and the lab precedent (`FR-014`, "no hidden
fallback"). A surfaced failure is actionable; a masked one is a silent bug factory. The
hot path stays throw-free (RT-safety); construction-time validation is where throws live.

**Alternatives considered.** gmin homotopy / source stepping — rejected for v1 (silent
substitution). A future *explicitly-reported* convergence aid remains possible (spec Open
Question 3), never a silent fallback.

## R7 — Two-phase structure and zero-heap hot path

**Decision.** `plan()` (off hot path, throw-permitted) delegates to
`MnaAssembler::plan` (branch allocation, validation) and builds Newton's is-diode mask +
diode-slot table from a single netlist scan. `solve()` (hot path) does only: seed from
the guess, loop ≤ `maxIterations` × { build diode companions, `refresh`, `MnaSystem::solve`,
read voltages, `pnjlim`, residual test }. All working storage is fixed-capacity
`std::array` sized by the template parameters; no `addBranch`/allocation on `solve()`.

**Rationale.** Mirrors MNA's proven split and sidesteps the lab's per-iteration
recompute waste (TASK-13) by construction: topology and the companion index tables are
fixed once. `AllocationSentinel` asserts zero heap across many solves (FR-024/SC-003).

**Alternatives considered.** Single-phase solve that re-plans each call — rejected
(re-derives topology every sample; heap/throw risk on the hot path).

## R8 — Global multi-diode step

**Decision.** All diodes are linearized at the *same* current iterate and stamped
together before a single `MnaSystem::solve()`; the whole node-voltage vector updates at
once (one global Newton step per iteration). No per-diode Gauss–Seidel sweep.

**Rationale.** A global Newton step over the coupled system is the standard, quadratically
convergent method and is what lifts the lab's single-nonlinearity restriction (FR-003/004,
US2). Per-diode sequencing would converge slowly and introduce ordering artifacts.

**Alternatives considered.** Per-diode relaxation — rejected (slow, order-dependent).
gmin-stepped continuation for stiffness — deferred (R6; spec Open Question 3).

## R9 — Validation strategy

**Decision.** Per the recorded `circuit-model-validation-approach`: (a) **exact closed
forms** — single diode + series resistor operating point via an in-test Lambert-W /
independently iterated reference at several DC levels; symmetric antiparallel pair → 0 V
at 0 drive; (b) **physical invariants** — monotonic transfer, antiparallel odd symmetry,
`I(0)=0`, passivity; (c) **equivalence oracle** vs `OpAmpClipperSolver` /
`TransientClipper` on the TS808 clipper core; (d) **`AllocationSentinel`** zero-heap on
`solve()`; (e) **no-fallback** tests — forced non-convergence and singular systems return
surfaced failure with no state corruption.

**Rationale.** Proves the solver exact where math allows, matches the trusted lab
reference on real topologies, and enforces the RT + no-fallback contracts — without
transcribing published values.

**Alternatives considered.** Transcribed SPICE/published operating points — rejected
(brittle, not first-principles; the memory explicitly avoids this).

## R10 — Default numeric tolerances (deferred detail, now pinned)

**Decision.** `maxIterations = 50`, `voltageTol = 1e-9` V, current-residual reference
`1e-12` A — the lab defaults — as the constructor defaults; all overridable and validated
`> 0`. Test tolerances for closed-form / oracle comparison are pinned per test in
`tasks.md` (start at `1e-9`–`1e-12` relative, tightened where the math is exact).

**Rationale.** Reuses the values already validated by the labs; keeps a single obvious
default while leaving room to tighten in tests. This is the item `/speckit-clarify`
deliberately deferred from spec to plan.

**Alternatives considered.** Leaving tolerances unspecified — rejected (tasks/tests need a
concrete default to assert against).

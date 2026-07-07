# Phase 0 — Research: opamp-stages

Resolves the technical unknowns behind the plan. Consolidated decisions; each carries rationale + alternatives. No `NEEDS CLARIFICATION` remains (the two spec markers were resolved in the 2026-07-06 clarify session).

---

## R1 — The `OpAmp` element shape (ideal nullor)

**Decision.** Add `struct OpAmp { NodeId inPlus; NodeId inMinus; NodeId out; };` to `models/opamp.h`, joined into the `Component` `std::variant` in `components.h`. It is a **constraint element** like `VoltageSource`: no `admittance()`, no `companion()`, no physical parameter fields at all in v1 (no `Vsat`/`GBW`/`slewRate`/finite-gain — design D1/FR-001). The three node handles are the non-inverting input, inverting input, and output.

**Rationale.** An ideal op-amp is a nullor (nullator at the inputs + norator at the output). The nullator imposes `V(inPlus) = V(inMinus)` and draws zero input current; the norator's output current is a free unknown the solve determines. Neither is expressible as an admittance, so the struct carries no conductance hook — exactly the `VoltageSource` pattern (`sources.h`: "a constraint, not a conductance").

**Classifiers (`components.h`).** `isNonlinear(OpAmp) = false`, `isReactive(OpAmp) = false`, `isLinear(OpAmp) = true` — the same bucket sources occupy for solver routing (the ideal op-amp is linear in its operating region; it carries no nonlinearity and no reactive state).

**`netlist.h` extensions.**
- `terminalsOf(OpAmp)` returns the **input pair** `{inPlus, inMinus}` — the two terminals the virtual-short constraint spans (the output is the driven terminal, reported separately by the augmentation, not a passive two-terminal edge).
- `contributesConductivePath(OpAmp) = false` — the op-amp **output is excluded** from the conductive-path set (mirrors `CurrentSource`/`Diode`). An op-amp output does not by itself guarantee a DC path to ground; the feedback network provides reachability (R5).

**Alternatives rejected.** (a) A finite-gain VCVS with fields `{gain, ...}` — the gmin/large-gain fallback `sources.h` forbids (R4). (b) Carrying ideal-defaulted `Vsat`/`GBW`/`slewRate` fields now — invites a solver to read them and quietly become nonideal; the non-ideality axis is a captured, separate deliverable (FR-025), so the struct stays bare.

---

## R2 — The nullor MNA augmentation (the solver increment)

**Decision.** Realize each `OpAmp` by **bordering** the reduced nodal system with one row and one column — the classic MNA stamp for a nullor:

```
[ G   B ] [ v ]   [ i ]
[ C   0 ] [ j ] = [ 0 ]
```

- `G` is the existing reduced nodal conductance matrix (resistors, capacitor/inductor companions, fixed-node-reduced voltage sources) the lab already assembles.
- `v` is the vector of unknown node voltages; `i` the existing nodal RHS (current injections).
- **Per op-amp** add one unknown `j` = the norator **output branch current**. Its column `B` places `+j` into the KCL of the op-amp's `out` node (the norator injects current only at `out`; the ideal op-amp's input terminals draw none).
- **Per op-amp** add one constraint row `C`: `V(inPlus) − V(inMinus) = 0` — a `+1` at the `inPlus` column, `−1` at the `inMinus` column, RHS `0`. This is the nullator (virtual short).
- The `0` block is the empty coupling between the constraint row and the branch-current column.

Solve the whole bordered system with the lab's **existing fixed-size Gaussian elimination with partial pivoting** — the augmentation is just extra rows/cols, no new linear-algebra machinery.

**Rationale.** This is the textbook nullor stamp and the minimal honest realization: it imposes the virtual short exactly (not a large-gain approximation) and lets the feedback network determine the output current. It is literally "one row / one column per op-amp," which is what makes the bounded charter checkable (R6).

**Ground/reference handling.** Node 0 is ground (`kGround`), already excluded from the unknown set by the lab's reduction. An op-amp terminal tied to ground (e.g. the inverting-config `inPlus`) contributes its `±1` to the ground column, which the reduction drops — consistent with how grounded voltage-source terminals are already handled.

**Alternatives rejected.** (a) Substitute a large finite gain `A` and stamp a VCVS — ill-conditioned as `A → ∞`, and the forbidden fallback (R4). (b) Gaussian-eliminate the constraint by hand per topology — brittle, non-general, and re-derives the op-amp law in the solver (violates the seam). The bordered stamp keeps the component's law in the element and the assembly in the solver.

---

## R3 — Per-topology node/BOM layout (the four builders)

**Decision.** Canonical minimal topologies, each returning `{ Netlist, inNode, outNode }`:

- **`nonInvertingGain`** — input `VoltageSource` drives `inPlus`; `out → Rf → inMinus`, `inMinus → Rg → ground`. Closed-loop gain `1 + Rf/Rg`. (The TS input-stage shape.)
- **`invertingGain`** — input `VoltageSource` → `Rin → inMinus`; `out → Rf → inMinus`; `inPlus → ground`. Gain `−Rf/Rin`.
- **`activeFirstOrder`** — inverting first-order **low-pass** (OQ4 resolution): input → `Rin → inMinus`; feedback is `Cf ∥ Rf` from `out` to `inMinus`; `inPlus → ground`. Finite DC gain `−Rf/Rin`, corner at `1/(2π·Rf·Cf)`.
- **`opAmpDiodeClipper`** — the TS808 core: input → `Rin → inMinus`; feedback network from `out` to `inMinus` is an **antiparallel diode pair (population configurable) in parallel with `Cf`** (and, per standard TS, a series `Rf` setting the clean-gain floor — a plan-time BOM detail); `inPlus → ground`. Exactly one nonlinearity location (the feedback diode pair).

**Rationale.** Each isolates one axis (feedback sign, reactive feedback, nonlinear feedback) while sharing the same op-amp element and builder shape — the validation ladder of design D4. The clipper's feedback cap across the diodes is the real TS "soft clipping band" element that the reactive-signature invariant (R7) exercises.

**Alternatives rejected.** A unity-gain buffer (voltage follower) as a fifth exemplar — degenerate (`Rf = 0`, `Rg = ∞`), adds no axis coverage; not in the design's four.

---

## R4 — Why the op-amp is never a large-but-finite-gain source

**Decision.** Forbidden by construction. The op-amp is realized only as the nullor constraint of R2.

**Rationale.** `sources.h` states an ideal source "cannot be expressed as a finite admittance without an ad hoc (and forbidden) gmin-style approximation." A large-but-finite op-amp gain `A` (a VCVS with huge `A`, or a large-conductance feedback divider) is the same fallback: it makes the system ill-conditioned, hides the virtual-short constraint behind a fragile approximation, and violates Constitution V. The bordered nullor stamp imposes the constraint exactly with no free `A`.

---

## R5 — Well-posedness gate (authoritative vs pre-filter)

**Decision.** The **authoritative** well-posedness gate is the **non-singularity of the augmented (bordered) system at solve time** — a singular system raises a descriptive error. `contributesConductivePath` is retained only as a **fast, conservative, nodal-only pre-filter** in `prepare()`, to which the op-amp contributes nothing.

**Rationale.** Once the system is bordered with nullor rows, a pure nodal connectivity scan is no longer authoritative in either direction: a node held only by the virtual short can be perfectly determined yet look "floating" (false-positive), and conversely a nodal-passing circuit can have a singular bordered matrix. So singularity at solve is the real gate. **Verified sound for the four v1 exemplars**: every interior node (both amps' inverting inputs, the active-stage summing node, the clipper's summing node) has a real resistor or capacitor-companion path to a determined node, so the conservative pre-filter produces no false-positive rejection here. This soundness is a property of these four topologies, **not** a general law (design OQ5).

**Alternatives rejected.** Making `contributesConductivePath` "op-amp aware" (e.g. treating the virtual short as tying `inPlus`/`inMinus` for reachability) — over-clever, still unsound in general, and unnecessary for the four exemplars; the singularity gate is the honest backstop.

---

## R6 — Bounded charter: the three tripwires (nullor augmentation only)

**Decision.** Encode the bound as three checkable properties (design D5 / FR-015), not an intention:
1. **`OpAmp`-specific augmentation.** Only `OpAmp` branches are row/column-augmented. `VoltageSource` stays on fixed-node reduction; `Resistor`/`Capacitor`/`Inductor`/`CurrentSource` stay nodal/companion. If anything else needs branch-augmentation, that is the "becoming general MNA" signal and the solver stops (descriptive error).
2. **Single-nonlinearity-location refusal carried forward.** ≥2 interacting nonlinearities (distinct node pairs) → descriptive out-of-scope error, exactly as `diode-clippers`' `TransientClipper` refuses.
3. **One row/one column per op-amp, sized at instantiation.** No dynamic growth; the augmented dimension is a compile-time function of the template capacities + `numOpAmps` (+ `2·MaxDiodes` for the clipper's per-iteration diode companions).

**Rationale.** These make "did not become MNA" a testable assertion (SC-007) and keep the lab solver from silently generalizing. The existing `LinearSolver` is not modified (its charter forbids MNA growth); the augmentation lives in the new `opamp-stage-solver.h`.

---

## R7 — Validation: the ladder, the DC oracle, the reactive signature

**Decision.**
- **Rung (a) — analytic gains.** Non-inverting and inverting resistive stages: the solved `out/in` equals `1 + Rf/Rg` and `−Rf/Rin` to ~1e-9. Proves the nullor stamp exact.
- **Rung (b) — analytic first-order response.** The active first-order low-pass matches the closed-form backward-Euler recurrence of a first-order system with DC gain `−Rf/Rin` and corner `1/(Rf·Cf)` to ~1e-9. Proves nullor+reactive exact **before** any nonlinearity.
- **Rung (c) — DC-limit bisection oracle.** For the clipper at DC steady state, an **independent bisection root-find** of the KCL equation at the virtual-short node (the feedback current through the antiparallel Shockley pair balancing the input current, with the op-amp holding `inMinus = inPlus = 0`) to ~1e-6 — a genuine cross-check, not solver-vs-itself.
- **Assembled invariants.** Forward saturation near the feedback-diode drop; odd-symmetry (symmetric population) / DC-offset (asymmetric); passivity of the passive sub-network (its output energy ≤ input energy; the op-amp's active gain accounted separately).
- **Reactive signature (OQ3 resolution).** A **1 kHz sine driven into clipping at fixed drive**, solved at **`dt = 1e-5 s`** (100 kHz); increasing `Cf` **strictly monotonically reduces output energy above 5 kHz** across an ascending sweep. Mirrors `diode-clippers` SC-005.

**Rationale.** The ladder proves each layer exact on a closed form before the next is trusted, isolating any clipper fault to the Newton coupling (design D7). The DC oracle is independent of the solver's own method. The reactive-signature parameters match the shipped sibling for consistency.

**Alternatives rejected.** Validating the clipper against transcribed TS808 measurements — the transcribed-published-numbers trap the project's validation approach avoids; exact limits + invariants are used instead.

---

## R8 — Reactive discretization & Newton defaults

**Decision.** Backward Euler for the reactive companions (reusing the frozen `capacitor.h` `companion()` hook), advanced once per timestep — the lab's deliberately-naive, non-normative choice (trapezoidal/general is Phase-5 implicit integration). The clipper's Newton holds `diode-clippers`' defaults initially (`maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12`); non-convergence is surfaced via `NewtonStatus`, never hidden. Retuning is opened only if a real reactive+nullor case is measured to fail (OQ2).

**Rationale.** Reuse the proven `diode-clippers` machinery unchanged; the only new element in the inner loop is that the linear solve it wraps is now the **bordered** (nullor-augmented) system rather than a plain nodal one.

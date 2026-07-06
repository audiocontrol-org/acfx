> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: opamp-stages

**Feature Branch**: `opamp-stages`

**Created**: 2026-07-06

**Input**: Design record: `docs/superpowers/specs/2026-07-05-opamp-stages-design.md` (approved 2026-07-06, design-to-spec exit gate 7/7). Roadmap item: `design:primitive/opamp-stages`, part-of `multi:feature/phase-circuit-modeling`, depends-on `multi:feature/phase-dynamic-systems` (shipped). The **active-element** deliverable of Phase 4 (Circuit Modeling), following `component-abstractions`, `passive-tone-stacks`, and `diode-clippers`. It is the **first feature to extend the frozen circuit vocabulary** — adding the ideal op-amp (nullor) — and it removes the block the shipped `diode-clippers` recorded against op-amp-feedback clipper topologies (the Tube Screamer core).

**Status**: Draft

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is threefold, matching the platform's audience framing (as in `component-abstractions` / `passive-tone-stacks` / `diode-clippers`):

- the **circuit-model author** — the developer who assembles a *named* op-amp gain stage (a non-inverting amplifier, an inverting amplifier, an active first-order stage, an op-amp feedback-diode clipper) from a bill of materials, and expects a `Netlist` describing the circuit's physics **without committing to a solver**;
- the **solver author** — who, in Phase 5 (MNA) and Phase 6 (WDF), will consume **the same builders** as an adapter target, reading each component's own physics — now including the op-amp constraint — to stamp a matrix or scatter waves;
- the **lab reader** — learning how an active gain stage is modelled: the op-amp as an ideal nullor imposing a virtual short, solved by a **bounded nullor MNA augmentation**, and seeing an op-amp+feedback-diode clipper (the Tube Screamer core) actually solved and validated.

This feature is the **active-element deliverable of phase-circuit-modeling**, and the first that **extends** the `component-abstractions` vocabulary rather than only composing it. It teaches **one concept** (Constitution Principle XI — one concept at a time): **the op-amp gain stage** — an active element that drives its output until its two inputs sit at the same voltage (the virtual short), so the surrounding feedback network — resistive, reactive, or diode-loaded — sets the stage's gain and character. Each story below is an independently testable slice.

### User Story 1 - Assemble a named op-amp stage, solver-neutrally (Priority: P1)

A circuit-model author calls a named builder — `nonInvertingGain`, `invertingGain`, `activeFirstOrder`, or `opAmpDiodeClipper` — passing a bill of materials (resistor / capacitor values, and for the clipper the diode parameters and population), and receives a `Netlist` of ordinary frozen-vocabulary components **plus the new `OpAmp` element** describing the circuit. No solver-specific concept appears; the netlist can be handed to any solver (the lab nullor-augmented solver now, Phase-5 MNA later).

**Why this priority**: This is the MVP — the defining artifact of the feature and the one new composition the deliverable owns. Without a solver-neutral op-amp-stage builder there is nothing for a solver author to adapt and nothing for the lab reader to solve. It is the generic active-stage vocabulary the later named features (`tube-screamer`, `rat-distortion`, `neve-preamp`) compose.

**Independent Test**: Call each builder at representative component values and inspect the returned `Netlist` directly — it `prepare()`s cleanly (ground referenced, no floating node under the conservative pre-filter), its component and node counts match the topology's bill of materials, and it holds only frozen-vocabulary elements plus the `OpAmp`. No solver required.

**Acceptance Scenarios**:

1. **Given** a non-inverting-gain bill of materials (an `OpAmp`, a feedback resistor `Rf` and a ground-leg resistor `Rg`, input driven to `inPlus`), **When** `nonInvertingGain(values)` is called, **Then** it returns a `Netlist` that `prepare()`s without error and whose component/node counts match the non-inverting topology.
2. **Given** an inverting-gain bill of materials (an `OpAmp`, input resistor `Rin` to `inMinus`, feedback resistor `Rf`, `inPlus` grounded), **When** `invertingGain(values)` is called, **Then** it returns a `prepare()`-valid `Netlist` for the inverting topology.
3. **Given** an active first-order bill of materials (an `OpAmp` with a reactive feedback element — a capacitor in the feedback path), **When** `activeFirstOrder(values)` is called, **Then** it returns a `prepare()`-valid `Netlist` whose reactive element is present in the feedback network.
4. **Given** an op-amp feedback-diode clipper bill of materials (an `OpAmp`, a feedback resistor, an antiparallel feedback-diode pair, and a feedback capacitor across the feedback network — the TS808 core), **When** `opAmpDiodeClipper(values)` is called, **Then** it returns a `prepare()`-valid `Netlist` whose feedback network carries the diode population and the cap.
5. **Given** any returned stage `Netlist`, **When** it is inspected, **Then** every component is a member of the circuit vocabulary — the `component-abstractions` inhabitants **plus the single new `OpAmp` element this feature adds** — introducing no element type beyond `OpAmp` and modifying no existing one.
6. **Given** the builder's translation unit, **When** its includes are inspected, **Then** it includes nothing under `core/labs/` — the primitive does not depend on any lab.

---

### User Story 2 - Solve the assembled stage with a bounded nullor augmentation (Priority: P2)

The lab reader (and the future solver author) wants an assembled op-amp stage actually solved: given a stage `Netlist`, realize each ideal op-amp **honestly** — one extra unknown (the norator output branch current) and one extra constraint row (`V(inPlus) − V(inMinus) = 0`, the virtual short) appended to the reduced nodal system — and solve. For the active first-order stage the reactive element is discretized as a backward-Euler companion; for the op-amp+diode clipper the diode nonlinearity is resolved by the same bounded Newton iteration `diode-clippers` established, wrapped around the augmented linear solve. This is the genuinely new content the deliverable owns — the **first taste of Modified Nodal Analysis**, added in bounded, nullor-only form.

**Why this priority**: The nullor-augmented solver *is* what turns an assembled op-amp-stage netlist into observed behavior. It is separated from US1 so the solver is provable in isolation on the linear closed-form stages, and from US3 so the augmentation (and the nullor+reactive layer) is proven exact before the diode nonlinearity is coupled in.

**Independent Test**: Run the host-only nullor-augmented solver on the linear stages first — the non-inverting and inverting resistive stages must reproduce their analytic closed-form gains; the active first-order stage must reproduce its analytic backward-Euler response. Then couple the diode in for the clipper and cross-check the DC limit against an independent bisection root-find. Drive a deliberately starved iteration budget against a stiff input and confirm the returned status reports non-convergence rather than a fabricated value.

**Acceptance Scenarios**:

1. **Given** a non-inverting resistive stage, **When** the nullor-augmented solver solves it, **Then** the output/input ratio equals the analytic `1 + Rf/Rg` to numerical precision (~1e-9) — the nullor stamp is exact.
2. **Given** an inverting resistive stage, **When** the solver solves it, **Then** the output/input ratio equals the analytic `−Rf/Rin` to ~1e-9 — both feedback signs are exact.
3. **Given** the active first-order stage (reactive feedback), **When** the solver is stepped over many samples, **Then** the node voltages match the closed-form backward-Euler recurrence of that stage's first-order response to ~1e-9 — confirming the nullor+reactive combination before any nonlinearity is trusted.
4. **Given** the op-amp feedback-diode clipper at DC steady state, **When** the coupled Newton-over-augmented-solve converges, **Then** the port voltage matches an independent bisection root-find of the stage's KCL equation at the virtual-short node to ~1e-6 — a genuine cross-check, not solver-vs-itself.
5. **Given** a starved iteration budget against a stiff excitation, **When** a timestep fails to converge, **Then** the solver returns a status reporting `converged == false` with the residual — the non-convergence is surfaced to the caller/test, never swallowed, and no fabricated output is produced.
6. **Given** a netlist presenting a second nonlinearity at a distinct node pair (an interacting nonlinearity), **When** the solver is asked to solve it, **Then** it raises a descriptive error declaring the case out of the bounded solver's scope (deferred to Phase 5) — it does not attempt a general nonlinear solve.
7. **Given** an ill-posed feedback network whose augmented system is singular, **When** the solver attempts the solve, **Then** it raises a descriptive error — never a silent wrong answer — the augmented-system singularity being the authoritative well-posedness gate.

---

### User Story 3 - Validate the assembled stages' behavior (Priority: P3)

With the solver proven exact on the closed-form stages, the lab reader confirms each assembled stage behaves as its topology dictates: the linear gains hold; the active stage's reactive response is correct; and the op-amp+diode clipper forward-saturates near the feedback-diode drop, carries the symmetry/asymmetry of its diode population, dissipates in its passive sub-network, and — the reactive signature — softens its high-frequency content as the feedback capacitor increases.

**Why this priority**: The assembled-stage invariants are what make each stage *recognizable* and prove the whole chain (builder → assembled netlist → nullor-augmented solve). They depend on US1 (a netlist to solve) and US2 (a trustworthy augmented solver), so they are last.

**Independent Test**: Drive each stage builder's netlist through the solver with fixed excitation and check the invariants directly — analytic gain equality for the linear stages, first-order response for the active stage, and for the clipper: output bounded near the feedback-diode drop, symmetry/asymmetry per population, output-energy ≤ input-energy in the passive sub-network, and monotonic high-frequency attenuation as the feedback cap increases.

**Acceptance Scenarios**:

1. **Given** the non-inverting and inverting stages under a swept input level, **When** their transfer is measured, **Then** each is linear at its analytic gain (`1 + Rf/Rg`, `−Rf/Rin`) across the tested range.
2. **Given** the op-amp feedback-diode clipper driven far past the feedback-diode threshold, **When** the output is measured, **Then** it clamps near the diode forward drop (bounded), not tracking the input linearly.
3. **Given** the clipper with a symmetric feedback-diode population, **When** its transfer is measured, **Then** it is odd-symmetric within tolerance; with an asymmetric population, a measurable DC-offset / even-harmonic component is present.
4. **Given** the clipper's passive sub-network over a bounded excitation, **When** input and output energy of that sub-network are compared, **Then** output energy ≤ input energy (it dissipates; the op-amp's active gain is accounted separately).
5. **Given** the clipper solved at several feedback-capacitor values with excitation and drive held fixed, **When** the post-clip output high-frequency energy is measured, **Then** it decreases monotonically as the feedback capacitor increases — the reactive tone-shaping the TS "soft" clipping band exhibits.

---

### Edge Cases

- **Floating op-amp input / missing feedback path**: the builder raises a descriptive `std::invalid_argument` at build, or `prepare()` reports it — never a silently ill-posed netlist. (An op-amp output does not itself guarantee a DC path; the feedback network must provide reachability.)
- **Singular augmented system (ill-posed feedback)**: the solver raises a descriptive error at solve time. The augmented-system non-singularity is the **authoritative** well-posedness gate; `contributesConductivePath` is only a conservative nodal-only pre-filter (to which the op-amp contributes nothing) and is not relied on to prove well-posedness in general.
- **A second nonlinearity at a distinct node pair**: the solver refuses with a descriptive out-of-scope error (bounded single-nonlinearity charter; deferred to Phase 5), carrying forward `diode-clippers`' single-nonlinearity boundary unchanged.
- **Newton non-convergence at a timestep** (clipper): the solver reports `converged == false` with the residual and does not advance a fabricated value; the harness/tests surface it.
- **Invalid BOM input** (non-positive R / C, or a diode parameter ≤ 0): descriptive `std::invalid_argument` on the build thread, never on an audio path (there is none here).
- **`dt ≤ 0`** (reactive/nonlinear stages): descriptive `std::invalid_argument` from the solver — no silent substitution.
- **Op-amp approximated as a large-but-finite-gain source**: forbidden by construction — the op-amp is realized as a nullor constraint, never as a large-conductance / high-gain VCVS (the gmin fallback `sources.h` prohibits).
- **Lab deleted**: the primitive (including the `OpAmp` element) and its Tier-1 tests must still build and pass; only the solved validations disappear.

## Requirements *(mandatory)*

### Functional Requirements

**Vocabulary extension (portable primitive — `core/primitives/circuit/`)**

- **FR-001**: The system MUST add a single new circuit-vocabulary element `OpAmp { NodeId inPlus, inMinus, out }` to the `Component` variant in `components.h` — the ideal op-amp (nullor): virtual short `V(inPlus) = V(inMinus)`, infinite input impedance (zero input current), zero output impedance, infinite **linear** gain. Like `VoltageSource` it is a **constraint, not a conductance**: it exposes **no** `admittance()` and **no** `companion()`. The struct MUST be bare — **no** ideal-defaulted non-ideality fields (no `Vsat` / `GBW` / `slewRate` / finite-gain / offset) in v1, so nothing exists for a solver to accidentally honor.
- **FR-002**: The classifiers MUST treat `OpAmp` as `isLinear = true`, `isNonlinear = false`, `isReactive = false` (the same bucket sources occupy for solver-routing). `terminalsOf` in `netlist.h` MUST recognize the `OpAmp` (its input pair; the output is its driven terminal), and `contributesConductivePath` MUST **exclude** the op-amp output from the conductive-path set (mirroring `CurrentSource` / `Diode`) — the feedback network, not the op-amp, provides reachability.

**Builders (portable primitive — `core/primitives/circuit/opamp-stage/`)**

- **FR-003**: The system MUST provide `nonInvertingGain(...)` returning a fixed-capacity `Netlist` modelling an `OpAmp` with a resistive feedback divider (`Rf`, `Rg`) giving closed-loop gain `1 + Rf/Rg`, input driven to `inPlus` (the Tube Screamer input-stage shape). Topology only — **no** solved response, **no** audio-path / `process()` realization.
- **FR-004**: The system MUST provide `invertingGain(...)` returning a `Netlist` modelling an `OpAmp` with input resistor `Rin` into `inMinus`, feedback resistor `Rf`, `inPlus` grounded — closed-loop gain `−Rf/Rin`.
- **FR-005**: The system MUST provide `activeFirstOrder(...)` returning a `Netlist` for a **linear reactive** op-amp stage whose feedback path contains a capacitor [NEEDS CLARIFICATION: OQ4 — exact form: a pure inverting integrator (cap-only feedback) vs an inverting first-order low-pass (cap in parallel with a feedback resistor, finite DC gain)? Pick the form whose closed-form oracle is cleanest and that avoids a trivially-unstable pure integrator].
- **FR-006**: The system MUST provide `opAmpDiodeClipper(...)` returning a `Netlist` for an op-amp with a feedback network carrying an antiparallel diode pair (or configurable population) **and** a feedback capacitor across it — the TS808 clipping core: the op-amp provides gain, the feedback diodes soft-clip, the feedback cap shapes the clipping band. Exactly **one** nonlinearity (the feedback diodes); the op-amp stays linear.
- **FR-007**: The builders MUST use **only** the circuit vocabulary — the frozen `component-abstractions` inhabitants (`Resistor`, `Capacitor`, `Inductor`, `VoltageSource`, `CurrentSource`, `Diode`, `Netlist`) **plus the single new `OpAmp`** — introducing no element beyond `OpAmp` and modifying no existing one, and MUST include nothing under `core/labs/`.
- **FR-008**: The builders MUST be **pure functions with no retained state**; a component-value change is expressed by rebuilding the netlist (control-rate). The returned `Netlist` MUST be heap-free (fixed compile-time capacities per topology; no `new` / `delete` / `std::vector`).
- **FR-009**: The builders MUST return a `Netlist` that `prepare()`s cleanly (ground referenced, no floating node under the conservative pre-filter) across representative bills of materials for each topology.
- **FR-010**: Ill-posed input on the build thread — non-positive resistor / capacitor values, non-positive diode parameters, a **floating op-amp input**, or a **missing feedback path** — MUST raise a descriptive `std::invalid_argument`. No silent clamp, no fallback, no fabricated topology.

**Nullor-augmented solver (host-only lab — `core/labs/opamp-stages/`)**

- **FR-011**: The lab MUST provide a templated nullor-augmented solver that solves an assembled op-amp-stage netlist by appending, **per op-amp**, exactly **one unknown** (the norator output branch current) and **one constraint row** (`V(inPlus) − V(inMinus) = 0`, the virtual short) to the reduced nodal system, and solving the augmented system by the lab's existing fixed-size Gaussian elimination with partial pivoting. The augmentation MUST be sized at template instantiation (`nodes + non-ground reductions + numOpAmps`, and for the nonlinear stage `+ 2·MaxDiodes` for per-iteration diode companion stamps) — **no dynamic growth**, heap-free on the solve path.
- **FR-012**: For the **active first-order stage** the solver MUST discretize the reactive element as a **backward-Euler companion** via the existing `component-abstractions` `companion()` hooks, advancing reactive history **once per timestep**.
- **FR-013**: For the **op-amp feedback-diode clipper** the solver MUST reuse `diode-clippers`' **separated timestep / Newton structure**: reactive companions computed once per timestep from held history; the inner Newton holds companions fixed, companion-linearizes the diode string into a Norton pair, solves the augmented linear system (**now carrying the nullor rows as well**), damps the new junction voltage through the diode's `limitJunctionVoltage` (pnjlim), and tests `|Δv| < tol`; after convergence the reactive history is advanced **exactly once**. Because the op-amp is linear, the clipper carries **one** nonlinearity and reuses this proven bounded-Newton pattern rather than a general engine.
- **FR-014**: The solver MUST report, for the nonlinear stage, a per-sample status carrying at least `{ converged, iterations, voltageResidual, currentResidual }`. Non-convergence MUST be **reported to the caller**, never masked by a fallback or a fabricated output. Newton defaults are `maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12` initially; they MUST NOT be silently retuned to hide a non-converging case.
- **FR-015**: The solver MUST stay **bounded — "nullor augmentation only"** — enforced by three checkable tripwires: (i) the branch-current augmentation code path is **`OpAmp`-specific** (`VoltageSource` stays on fixed-node reduction; `Resistor` / `Capacitor` / `Inductor` / `CurrentSource` stay nodal / companion) — if anything other than an `OpAmp` needs branch-augmentation, that is the becoming-general-MNA signal and it stops; (ii) `diode-clippers`' **single-nonlinearity-location refusal carries forward unchanged** — ≥2 interacting nonlinearities (distinct node pairs) MUST raise a descriptive out-of-scope error; (iii) **one row / one column per op-amp, sized at instantiation**. The solver MUST NOT modify the existing `component-abstractions` `LinearSolver` (whose charter forbids growing into MNA), and MUST NOT introduce general Modified Nodal Analysis, gmin stepping, or a general nonlinear engine.
- **FR-016**: The **authoritative well-posedness gate** MUST be the non-singularity of the augmented system at solve time — a descriptive error on a singular augmented system, never a silent wrong answer. `contributesConductivePath` MUST be treated only as a **fast, conservative, nodal-only pre-filter** (op-amp contributes nothing to it); the spec does not claim it proves well-posedness for op-amp circuits in general (it is verified sound only for the four v1 exemplars). The solver MUST raise a descriptive `std::invalid_argument` on `dt ≤ 0`.
- **FR-017**: The op-amp MUST **never** be approximated as a large-but-finite-gain source or a large-but-finite conductance (the gmin fallback `sources.h` explicitly forbids). It is realized exactly as the nullor constraint of FR-011.
- **FR-018**: The lab MUST compute in `double` throughout; there is **no** `float` audio boundary in this deliverable (no audio path).

**Validation (host-only lab)**

- **FR-019**: The lab MUST prove the solver **exact first**, as a ladder, before trusting it on the nonlinear stage: (a) the non-inverting and inverting resistive stages match their analytic gains `1 + Rf/Rg` and `−Rf/Rin` to ~1e-9 (the nullor stamp is exact); (b) the active first-order stage matches its analytic backward-Euler step / first-order frequency response to ~1e-9 (nullor + reactive exact **before** any nonlinearity).
- **FR-020**: The lab MUST cross-check the op-amp feedback-diode clipper's **DC steady-state limit** against an **independent bisection root-find** oracle of the stage's KCL equation at the virtual-short node to ~1e-6 — a genuine cross-check, not solver-vs-itself.
- **FR-021**: The lab MUST validate the clipper's assembled behavior by invariants: forward saturation clamping near the feedback-diode drop under large drive; symmetry (`y(−x) = −y(x)`) for a symmetric feedback-diode population and an explicit DC-offset / even-harmonic presence for an asymmetric population; and passivity of the passive sub-network (its output energy ≤ input energy over a bounded excitation, the op-amp's active gain accounted separately).
- **FR-022**: The lab MUST validate the **reactive signature** as a pinned invariant of the clipper: with excitation and drive held fixed, increasing the feedback capacitor MUST **monotonically reduce** the post-clip output high-frequency energy across an ascending sweep [NEEDS CLARIFICATION: OQ3 — measurement parameters: excitation (a fixed sine at what frequency, or a band-limited edge), sample rate / `dt`, the HF cutoff band, and the monotonic-decrease margin — analogous to `diode-clippers`' 1 kHz / `dt = 1e-5 s` / 5 kHz resolution].
- **FR-023**: The lab MUST provide a host-only harness (`int main()`, `opamp-stages-harness.cpp`) that mirrors the Tier-2 assertions as PASS/FAIL lines with measured-vs-expected numbers, **including an explicit non-convergence check** (drive the clipper solve to deliberate non-convergence and assert the status is surfaced), and exits nonzero on any failure.

**Isolation & scope**

- **FR-024**: Deleting `core/labs/opamp-stages/` MUST leave the op-amp-stage primitive (including the `OpAmp` vocabulary element) and its Tier-1 tests compiling and passing; only the solved validations disappear (mirrors the siblings). The primitive MUST have no include of anything under the lab.
- **FR-025**: Out of scope by construction (each **captured** for a later phase / feature, not a gap to fill here): **no** realtime `process()` / audio-path realization; **no** op-amp non-ideality — finite open-loop gain, output rails / saturation, finite GBW single-pole roll-off, slew rate, input offset / bias current — which is the charter of a later nonideal follow-on / Phase-5 solver (the `OpAmp` element is deliberately shaped so it can gain `Vsat` / `GBW` / `slewRate` fields later with ideal defaults, non-breaking); **no** general multi-nonlinearity / Modified Nodal Analysis (Phase 5); **no** named-product BOMs / voicings (TS808 / RAT / Neve — their own `design:feature/*` nodes); **no** DAW / hardware acceptance.

### Key Entities

- **`OpAmp` element** *(primitive)*: the single new circuit-vocabulary inhabitant — `{ NodeId inPlus, inMinus, out }`, the ideal nullor. A constraint (virtual short), not a conductance; no `admittance()` / `companion()`; classified `isLinear`.
- **Op-amp-stage BOM (per topology)**: the bill of materials for a builder — the feedback / input / ground resistor value(s), the feedback / reactive capacitor value(s) where present, and (for the clipper) the diode parameters (`Is`, `n`, `Vt`) plus the feedback-diode population (count / orientation).
- **Op-amp-stage Netlist**: a fixed-capacity `Netlist<MaxNodes, MaxComponents>` of vocabulary components including the `OpAmp`, capacities sized per topology, plus the input / output node handles.
- **Nullor augmentation** *(lab)*: the per-op-amp extra unknown (norator output current) + constraint row (`V(inPlus) − V(inMinus) = 0`) appended to the reduced nodal system — the bounded, first taste of MNA.
- **`NewtonStatus`** *(lab, clipper)*: the per-sample convergence report `{ converged, iterations, voltageResidual, currentResidual }`.
- **DC-limit oracle** *(lab)*: the independent bisection root-find of the clipper's steady-state KCL equation at the virtual-short node, used to prove the coupled solve exact at the DC limit.
- **Reactive signature** *(lab)*: the post-clip high-frequency output energy as a function of feedback capacitance, for fixed excitation and drive — the pinned monotonic invariant of FR-022.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For all four stages (non-inverting gain, inverting gain, active first-order, op-amp+diode clipper), the builder returns a `prepare()`-valid `Netlist` at representative bills of materials — 100% of tested topologies, zero `prepare()` failures — holding only vocabulary elements (including exactly one new element type, `OpAmp`) with component / node counts matching each topology.
- **SC-002**: The nullor-augmented solver reproduces the analytic linear gains `1 + Rf/Rg` and `−Rf/Rin` to ≈1e-9 and the active first-order stage's analytic backward-Euler response to ≈1e-9 — the augmentation and the nullor+reactive layer are validated before any nonlinearity is trusted.
- **SC-003**: The op-amp feedback-diode clipper's DC steady-state limit matches the independent bisection oracle to ≈1e-6 — a genuine cross-check.
- **SC-004**: The clipper forward-saturates (output bounded near the feedback-diode drop under large drive); a symmetric feedback-diode population yields an odd-symmetric transfer and an asymmetric population a measurable DC-offset / even-harmonic component; the passive sub-network is passive (output energy ≤ input energy over a bounded excitation).
- **SC-005**: With excitation and drive held fixed, increasing the clipper's feedback capacitor across an ascending sweep monotonically reduces the post-clip output high-frequency energy at every step — the reactive signature holds for the whole sweep (measurement parameters per the OQ3 resolution).
- **SC-006**: A deliberately non-converging timestep is surfaced as a reported `converged == false` status in both the Tier-2 test and the harness — the non-convergence contract is verified, not assumed.
- **SC-007**: The bounded charter is verified: a netlist with ≥2 interacting nonlinearities raises a descriptive out-of-scope error, and the augmentation code path augments only `OpAmp` branches — the solver never becomes general MNA and never modifies `LinearSolver`.
- **SC-008**: With `core/labs/opamp-stages/` deleted, the op-amp-stage primitive (including the `OpAmp` element) and its Tier-1 tests build and pass — isolation is itself a verified outcome.
- **SC-009**: No heap in the builder path — `core/primitives/circuit/opamp-stage/` contains no `new` / `delete` / `std::vector`; capacities are compile-time `Netlist` template parameters, and the solver is heap-free on the solve path.

## Assumptions

- **Op-amp ideality (D1 — binding).** The v1 `OpAmp` is the pure ideal nullor: linear, no rails / GBW / slew / finite gain / offset. Non-ideality is captured and deferred (FR-025), not dropped; the element is shaped to gain those fields later with ideal defaults.
- **Well-posedness authority (OQ5 — resolved in design review).** The augmented-system non-singularity at solve time is the authoritative well-posedness gate; `contributesConductivePath` is a conservative nodal-only pre-filter, verified sound for the four v1 exemplars (every interior node has a real resistor or capacitor-companion path) but not asserted as a general law of op-amp circuits.
- **Newton defaults (OQ2 — decided, measurement-gated).** The clipper solve holds `diode-clippers`' Newton defaults (`maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12`) initially; non-convergence is a test-surfaced contract. Retuning is opened only as a tuning question if a real reactive+nullor case is measured to fail to converge — the budget is raised, not the failure hidden.
- **Reactive discretization.** Backward Euler (reusing the primitive's existing `companion()` hooks) is the lab's deliberately-naive, non-normative choice; trapezoidal / the general treatment is Phase-5 implicit integration. The primitive never hard-codes an integration method.
- **Canonical component values.** Representative bills of materials for each topology are drawn from standard op-amp-stage and TS808 practice; the exact BOM constants are finalized in the plan / implementation, and correctness is defined by the analytic gains, the DC-limit oracle, and the invariants — not by any single vendor's part list.
- **Dependency.** Builds on the shipped `component-abstractions` primitive (`core/primitives/circuit/`, `Netlist`, the component vocabulary and classifiers, `Diode` with Shockley `evaluate` / `vCrit` / `limitJunctionVoltage`) and reuses the lab reference-solver structure (`core/labs/component-abstractions/solver/LinearSolver` reduction + companion pattern; `diode-clippers`' transient Newton coupling) as the basis for the new nullor-augmented solve. It does not modify `LinearSolver`.
- **Drive/gain framing.** For the clipper, "drive" is the op-amp's own gain plus any upstream stage feeding it; the reactive-signature and passivity invariants hold excitation and drive fixed while sweeping the feedback capacitor.

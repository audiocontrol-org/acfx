> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: diode-clippers

**Feature Branch**: `diode-clippers`

**Created**: 2026-07-05

**Input**: Design record: `docs/superpowers/specs/2026-07-04-diode-clippers-design.md` (approved 2026-07-04, design-to-spec exit gate 7/7). Roadmap item: `design:feature/diode-clippers`, part-of `multi:feature/phase-circuit-modeling`, depends-on `multi:feature/phase-dynamic-systems` (shipped). Third feature deliverable of Phase 4 (Circuit Modeling), following `component-abstractions` and `passive-tone-stacks`.

**Status**: Draft

## Clarifications

### Session 2026-07-05 (clarify)

- Q: OQ3 — how is the reactive-signature invariant (FR-017/SC-005: larger `Cf` ⇒ less post-clip HF) measured for fixed excitation and drive? → A: **A 1 kHz sine driven into clipping at a fixed drive, solved at the existing lab convention `dt = 1e-5 s` (100 kHz), measuring output spectral energy above a 5 kHz cutoff, asserted to decrease strictly monotonically at each step of an ascending `Cf` sweep.** A musical fundamental with clear clipping-generated harmonics to attenuate; reuses `component-abstractions`' `dt` convention. (Rejected: a band-limited edge/step measured by rise-time or spectral centroid — a fuzzier scalar to pin monotonically; and a swept-sine chirp — broader but heavier and less pointed for a monotonic-in-`Cf` assertion. The exact numeric monotonic margin is a plan/implementation detail.)
- Q: OQ5 — where does the series-clipper exemplar's capacitor sit? → A: **An input coupling capacitor in series, ahead of the inline diodes.** This places the reactance distinctly from the shunt exemplars (which put the cap across the diodes), so the series builder genuinely isolates the topology / reactance-placement axis — its reason for being the third exemplar. (Rejected: a cap across the series diodes — closer to the shunt exemplars' placement, partly duplicating that axis rather than isolating the topology axis.)

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is threefold, matching the platform's audience framing (as in `component-abstractions` / `passive-tone-stacks`):

- the **circuit-model author** — the developer who assembles a *named* diode clipping stage (a symmetric shunt clipper, an asymmetric shunt clipper, a series clipper) from a bill of materials, and expects a `Netlist` describing the circuit's physics **without committing to a solver**;
- the **solver author** — who, in Phase 5 (MNA) and Phase 6 (WDF), will consume **the same builders** as an adapter target, reading each component's own physics to stamp a matrix or scatter waves;
- the **lab reader** — learning how a real diode clipping stage is modelled as a nonlinear reactive network, and seeing its **transient nonlinear response** actually solved and validated — the reactive tone-shaping a memoryless waveshaping curve cannot represent.

This feature is the **third deliverable of phase-circuit-modeling**, and the first that composes the `component-abstractions` vocabulary into an assembled **nonlinear** circuit with reactive memory. It teaches **one concept** (Constitution Principle XI — one concept at a time): **the diode clipping stage** — a passive network whose nonlinearity is one or more junction diodes at a single port, clamping the signal against the diode forward drop, with the surrounding RC network coloring that clamping in a frequency- and program-dependent way. Each story below is an independently testable slice.

### User Story 1 - Assemble a named diode-clipper stage, solver-neutrally (Priority: P1)

A circuit-model author calls a named builder — `symmetricShuntClipper`, `asymmetricShuntClipper`, or `seriesClipper` — passing a bill of materials (resistor / capacitor values and diode parameters), and receives a `Netlist` of ordinary frozen-vocabulary components (`Resistor`, `Capacitor`, `Diode`, `VoltageSource`) describing the circuit. No solver-specific concept appears; the netlist can be handed to any solver (the lab transient solver now, Phase-5 MNA later).

**Why this priority**: This is the MVP — the defining artifact of the feature and the one new composition the deliverable owns. Without a solver-neutral diode-clipper builder there is nothing for a solver author to adapt and nothing for the lab reader to solve. It is the generic clipping-stage vocabulary the later named-pedal features (`tube-screamer`, `rat-distortion`, `big-muff`) compose.

**Independent Test**: Call each builder at representative component values and inspect the returned `Netlist` directly — it `prepare()`s cleanly (ground referenced, no floating node), its component and node counts match the topology's bill of materials, and it holds only frozen-vocabulary elements. No solver required.

**Acceptance Scenarios**:

1. **Given** a symmetric-shunt bill of materials (series R, a matched antiparallel diode pair to ground, a filter cap across the diodes), **When** `symmetricShuntClipper(values)` is called, **Then** it returns a `Netlist` that `prepare()`s without error and whose component/node counts match the symmetric-shunt topology.
2. **Given** an asymmetric-shunt bill of materials (unequal diode population, e.g. 2-up / 1-down), **When** `asymmetricShuntClipper(values)` is called, **Then** it returns a `prepare()`-valid `Netlist` whose diode count and orientation match the asymmetric population.
3. **Given** a series-clipper bill of materials (diodes inline in the signal path with a coupling cap), **When** `seriesClipper(values)` is called, **Then** it returns a `prepare()`-valid `Netlist` for the inline topology.
4. **Given** any returned clipper `Netlist`, **When** it is inspected, **Then** every component is a member of the frozen `component-abstractions` vocabulary — the builders introduce **no** new element type and modify **no** existing one.
5. **Given** the builder's translation unit, **When** its includes are inspected, **Then** it includes nothing under `core/labs/` — the primitive does not depend on any lab.

---

### User Story 2 - Solve the assembled clipper's reactive nonlinear transient (Priority: P2)

The lab reader (and the future solver author) wants an assembled clipper actually solved in the time domain, with its reactive elements active: given a clipper `Netlist` and a timestep `dt`, advance the circuit one sample by discretizing each reactive element as a backward-Euler companion and driving a bounded Newton iteration over the diode nonlinearity, reporting a per-sample convergence status. This is the genuinely new content the deliverable owns — the transient nonlinear solve that `component-abstractions`' static solver deliberately refused (it rejects reactive elements alongside a diode).

**Why this priority**: The transient solver *is* what turns an assembled clipper netlist into observed behavior — and the reactive dimension is the feature's defining increment over the already-shipped static single-diode transfer curve. It is separated from US1 so the solver is provable in isolation on sanity networks, and from US3 so the engine is proven before its assembled-clipper behavior is judged.

**Independent Test**: Run the host-only lab transient solver on known sanity networks first — a linear-only RC network must reproduce the analytic backward-Euler step response; a resistive-plus-diode network at DC steady state must match an independent bisection root-find. Drive a deliberately starved iteration budget against a stiff input and confirm the returned status reports non-convergence rather than a fabricated value.

**Acceptance Scenarios**:

1. **Given** a linear-only RC network (no diode), **When** the transient solver is stepped over many samples, **Then** the node voltage matches the closed-form backward-Euler recurrence to numerical precision (~1e-9), confirming the reactive discretization and the timestep/history handling before any nonlinearity is trusted.
2. **Given** a single-diode-plus-resistor network at DC steady state, **When** the transient solver converges, **Then** the port voltage matches an independent bisection root-find of the diode equation to ~1e-6 **and** agrees with the existing static `NewtonClipper` curve — a genuine cross-check, not solver-vs-itself.
3. **Given** an asymmetric clipper whose diode count exceeds the existing static solver's two-diode cap, **When** the transient solver is run, **Then** it solves the port with all diodes present (the templated `MaxDiodes` admits the population), never silently dropping a diode.
4. **Given** a starved iteration budget against a stiff excitation, **When** a timestep fails to converge, **Then** the solver returns a status reporting `converged == false` with the residual — the non-convergence is surfaced to the caller/test, never swallowed, and no fabricated output is produced.
5. **Given** a netlist with a second nonlinearity at a distinct node pair (an interacting nonlinearity), **When** the transient solver is asked to solve it, **Then** it raises a descriptive error declaring the case out of the lab solver's bounded scope (deferred to Phase 5) — it does not attempt a general nonlinear solve.

---

### User Story 3 - Validate the assembled clippers' behavior (Priority: P3)

With the solver proven, the lab reader confirms each assembled clipper behaves as its topology dictates: the symmetric clipper is odd-symmetric; the asymmetric clipper carries a DC offset / even-harmonic content; every clipper forward-saturates near the diode drop and dissipates rather than amplifies; and — the behavior a static curve cannot show — increasing the filter capacitor progressively softens the clipped signal's high-frequency content.

**Why this priority**: The assembled-clipper invariants are what make a clipping stage *recognizable* and prove the whole chain (builder → assembled netlist → transient solve). They depend on US1 (a netlist to solve) and US2 (a trustworthy solver), so they are last.

**Independent Test**: Drive each clipper builder's netlist through the transient solver with fixed excitation and check the invariants directly — symmetry of the transfer, presence/absence of DC offset, output-bounded-near-diode-drop, output-energy ≤ input-energy, and the monotonic high-frequency attenuation as the filter cap increases.

**Acceptance Scenarios**:

1. **Given** the symmetric shunt clipper under a bipolar excitation, **When** its transfer is measured, **Then** it is odd-symmetric — `y(−x) = −y(x)` within tolerance — with no DC offset.
2. **Given** the asymmetric shunt clipper under the same excitation, **When** its transfer is measured, **Then** it is **not** odd-symmetric — a measurable DC offset / even-harmonic component is present, distinguishing it from the symmetric case.
3. **Given** any clipper driven far past the diode threshold, **When** the output is measured, **Then** it clamps near the diode forward drop (bounded), not tracking the input linearly.
4. **Given** any passive clipper over a bounded excitation, **When** input and output energy are compared, **Then** output energy ≤ input energy (the network dissipates; it never adds gain).
5. **Given** a clipper solved at several filter-capacitor values with excitation and drive held fixed, **When** the post-clip output high-frequency energy is measured, **Then** it decreases monotonically as the filter capacitor increases — the reactive tone-shaping the static transfer curve cannot represent.

---

### Edge Cases

- **Diode population exceeds `MaxDiodes`**: the builder / solver raises a descriptive error at build/`prepare()` naming the overflow — never a silent drop of a diode.
- **A second nonlinearity at a distinct node pair**: the transient solver refuses with a descriptive out-of-scope error (bounded single-port charter; deferred to Phase 5), mirroring `component-abstractions`' single-nonlinearity boundary.
- **Newton non-convergence at a timestep**: the solver reports `converged == false` with the residual and does not advance a fabricated value; the harness/tests surface it.
- **Invalid BOM input** (non-positive R / C, or a diode parameter ≤ 0): descriptive `std::invalid_argument` on the build thread, never on an audio path (there is none here).
- **`dt ≤ 0`**: descriptive `std::invalid_argument` from the solver — no silent substitution.
- **Lab deleted**: the primitive and its Tier-1 tests must still build and pass; only the transient-response validations disappear.

## Requirements *(mandatory)*

### Functional Requirements

**Builders (portable primitive — `core/primitives/circuit/diode-clipper/`)**

- **FR-001**: The system MUST provide `symmetricShuntClipper(...)` returning a fixed-capacity `Netlist` of frozen-vocabulary components modelling a series-resistor → matched antiparallel diode pair to ground, with a filter capacitor across the diodes. It MUST produce topology only — **no** solved response and **no** audio-path / `process()` realization.
- **FR-002**: The system MUST provide `asymmetricShuntClipper(...)` returning a `Netlist` for a shunt clipper with an **unequal** diode population (e.g. 2-up / 1-down), whose transfer is intentionally not odd-symmetric.
- **FR-003**: The system MUST provide `seriesClipper(...)` returning a `Netlist` for an inline (series-path) diode clipper whose capacitor is an **input coupling capacitor in series, ahead of the inline diodes** — a topology distinct from the shunt family (different node structure, and reactance placed in series rather than across the diodes, so it isolates the topology/reactance-placement axis).
- **FR-004**: The builders MUST use **only** the frozen `component-abstractions` vocabulary (`Resistor`, `Capacitor`, `Diode`, `VoltageSource`, and `Netlist`) — introducing no new element type and modifying no existing one — and MUST include nothing under `core/labs/`.
- **FR-005**: The builders MUST be **pure functions with no retained state**; a component-value change is expressed by rebuilding the netlist (control-rate). The returned `Netlist` MUST be heap-free (fixed compile-time capacities per topology; no `new`/`delete`/`std::vector`).
- **FR-006**: The builders MUST return a `Netlist` that `prepare()`s cleanly (ground referenced, no floating node) across representative bills of materials for each topology.
- **FR-007**: Invalid BOM input on the build thread — non-positive resistor/capacitor values or non-positive diode parameters (`Is`, `n`, `Vt`) — MUST raise a descriptive `std::invalid_argument`. No silent clamp, no fallback, no fabricated value.

**Transient nonlinear solver (host-only lab — `core/labs/diode-clippers/`)**

- **FR-008**: The lab MUST provide a templated `TransientClipper<MaxNodes, MaxComponents, MaxDiodes>` (default `MaxDiodes = 4`) that advances an assembled clipper netlist one timestep given `dt`, discretizing each reactive element (capacitor/inductor) as a **backward-Euler companion** via the existing `component-abstractions` `companion()` hooks, and resolving the diode nonlinearity by a bounded Newton iteration.
- **FR-009**: The solver MUST **separate the timestep loop from the Newton loop**: reactive companion values are computed once per timestep from held history and the reactive history is advanced **exactly once**, after Newton converges for that timestep; the inner Newton iterations MUST hold the reactive companions fixed (no per-iteration history advance). This is the mechanism that makes the reactive+nonlinear case correct — the case `component-abstractions`' static solver deliberately refused.
- **FR-010**: Within a timestep the solver MUST companion-linearize the diode string into a Norton pair, append it as ordinary linear stamps, solve the resulting purely-linear system, and damp the new junction voltage through the diode's `limitJunctionVoltage` (pnjlim). The internal augmented capacity MUST accommodate `MaxComponents + 2·MaxDiodes` (each diode linearizes to a `Resistor` + `CurrentSource`).
- **FR-011**: The solver MUST report a per-sample status carrying at least `{ converged, iterations, voltageResidual, currentResidual }`. Non-convergence MUST be **reported to the caller**, never masked by a fallback or a fabricated output (no silent substitution). Newton defaults are `maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12` initially; they MUST NOT be silently retuned to hide a non-converging case.
- **FR-012**: The solver MUST stay **bounded and non-MNA**: a single clipper port and a single nonlinearity *location* (one node pair carrying the diode string). It MUST raise a descriptive error for ≥2 *interacting* nonlinearities (distinct node pairs) — deferred to Phase 5 — mirroring `component-abstractions`' single-nonlinearity boundary. It MUST NOT introduce general Modified Nodal Analysis, gmin stepping, or a general nonlinear engine.
- **FR-013**: The solver MUST raise a descriptive `std::invalid_argument` on `dt ≤ 0` and a descriptive error on a singular system — never a silent wrong answer, mirroring `LinearSolver`'s zero-pivot policy.
- **FR-014**: The lab MUST compute in `double` throughout; there is **no** `float` audio boundary in this deliverable (no audio path).

**Validation (host-only lab)**

- **FR-015**: The lab MUST prove the transient solver **exact first** on closed-form sanity networks before trusting it on a clipper: a linear-only RC network matches the analytic backward-Euler step response to ~1e-9, and a resistor-plus-diode network at DC steady state matches an **independent bisection root-find** oracle to ~1e-6 (and agrees with the existing static `NewtonClipper` curve).
- **FR-016**: The lab MUST validate each assembled clipper by behavioral invariants: symmetry (`y(−x) = −y(x)` for the symmetric clipper; an explicit DC-offset / even-harmonic presence for the asymmetric clipper), forward saturation clamping near the diode drop, and a passivity/energy bound (output energy ≤ input energy).
- **FR-017**: The lab MUST validate the **reactive signature** as a pinned invariant **for the shunt clippers (symmetric + asymmetric), whose capacitor `Cf` sits across the diodes**: with a **1 kHz sine driven into clipping at a fixed drive**, solved at `dt = 1e-5 s` (100 kHz), increasing `Cf` MUST **strictly monotonically reduce the output spectral energy above a 5 kHz cutoff** at each step of an ascending `Cf` sweep (attack softening / frequency-dependent clipping) — the behavior a static transfer curve cannot represent. The **series** clipper's capacitor is an input **coupling** cap `Cc` (a high-pass element ahead of the diodes), not a filter cap across them; its distinct reactive character is **not** asserted as this Cf-HF invariant in v1 (it is captured for a later refinement).
- **FR-018**: The lab MUST provide a host-only harness (`int main()`) that mirrors the Tier-2 assertions as PASS/FAIL lines with measured-vs-expected numbers, **including an explicit non-convergence check** (drive the solver to deliberate non-convergence and assert the status is surfaced), and exits nonzero on any failure.

**Isolation & scope**

- **FR-019**: Deleting `core/labs/diode-clippers/` MUST leave the diode-clipper primitive and its Tier-1 tests compiling and passing; only the transient-response validations disappear (mirrors `component-abstractions` / `passive-tone-stacks`). The primitive MUST have no include of anything under the lab.
- **FR-020**: Out of scope by construction (each captured for a later phase/feature, not a gap to fill here): no realtime `process()` / audio-path realization and no oversampling / ADAA anti-aliasing (a later effect feature; ADAA is inapplicable to a Newton solve — no analytic antiderivative); op-amp-feedback (Tube Screamer) clippers (blocked on the deferred op-amp / nullor element — the separate `opamp-stages` deliverable); named-product BOMs / voicings (Rat / DS-1 / Big Muff / Tube Screamer — their own `design:feature/*` nodes); general multi-nonlinearity / MNA (Phase 5); no DAW/hardware acceptance.

### Key Entities

- **Clipper BOM (per topology)**: the bill of materials for a builder — series/load resistor value(s), filter/coupling capacitor value(s), and the diode parameters (`Is`, `n`, `Vt`) plus the diode population (count/orientation) for the topology.
- **Clipper Netlist**: a fixed-capacity `Netlist<MaxNodes, MaxComponents>` of frozen-vocabulary components (`Resistor`, `Capacitor`, `Diode`, `VoltageSource`), capacities sized per topology, plus the input/output/port node handles.
- **TransientClipper `NewtonStatus`** *(lab)*: the per-sample convergence report `{ converged, iterations, voltageResidual, currentResidual }`.
- **DC-limit oracle** *(lab)*: the independent bisection root-find of each clipper's steady-state diode equation, used to prove the solver exact at the DC limit.
- **Reactive signature** *(lab)*: the post-clip high-frequency output energy as a function of filter capacitance, for fixed excitation and drive — the pinned monotonic invariant of FR-017.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For all three clippers (symmetric shunt, asymmetric shunt, series), the builder returns a `prepare()`-valid `Netlist` at representative bills of materials — 100% of tested topologies, zero `prepare()` failures — holding only frozen-vocabulary elements with component/node counts matching each topology.
- **SC-002**: The transient solver reproduces the analytic backward-Euler RC step response to ≈1e-9 and matches the independent bisection DC-limit oracle (and the existing static `NewtonClipper` curve) to ≈1e-6 — the solver is validated before being trusted on a clipper.
- **SC-003**: The symmetric clipper's measured transfer is odd-symmetric within tolerance (no DC offset); the asymmetric clipper's transfer carries a measurable DC-offset / even-harmonic component — the two are distinguishable by this invariant.
- **SC-004**: Every clipper forward-saturates (output bounded near the diode drop under large drive) and is passive (output energy ≤ input energy over a bounded excitation) — 100% of tested clippers.
- **SC-005**: For **each shunt clipper** (symmetric + asymmetric), driven by a fixed 1 kHz sine at fixed drive (solved at `dt = 1e-5 s`), increasing the filter capacitor `Cf` across an ascending sweep strictly reduces the output energy above 5 kHz at every step — the reactive signature holds for the whole sweep. (The series clipper's coupling cap is not covered by this invariant; see FR-017.)
- **SC-006**: A deliberately non-converging timestep is surfaced as a reported `converged == false` status in both the Tier-2 test and the harness — the non-convergence contract is verified, not assumed.
- **SC-007**: With `core/labs/diode-clippers/` deleted, the diode-clipper primitive and its Tier-1 tests build and pass (isolation is itself a verified outcome).
- **SC-008**: No heap in the builder path — `core/primitives/circuit/diode-clipper/` contains no `new`/`delete`/`std::vector`; capacities are compile-time `Netlist` template parameters, and the solver is heap-free on the solve path.

## Assumptions

- **Reactive-signature measurement parameters (OQ3 — resolved 2026-07-05 clarify).** The FR-017 / SC-005 check uses a 1 kHz sine driven into clipping at a fixed drive, solved at `dt = 1e-5 s` (100 kHz), measuring output energy above a 5 kHz cutoff, asserted strictly decreasing across an ascending `Cf` sweep. The exact numeric monotonic margin is a plan/implementation detail.
- **Series-clipper reactance placement (OQ5 — resolved 2026-07-05 clarify).** The series exemplar places its capacitor as an input coupling cap in series ahead of the inline diodes (not across the diodes), so it isolates the topology / reactance-placement axis without duplicating the shunt exemplars' cap-across-diodes reactance.
- **Diode-population cap.** `MaxDiodes = 4` by default comfortably admits the 2-up / 1-down asymmetric string with headroom; a topology needing more instantiates the template larger. No global ceiling; heap-free by construction.
- **Reactive discretization.** Backward Euler (reusing the primitive's existing `companion()` hooks) is the lab's deliberately-naive, non-normative choice; trapezoidal / the general treatment is Phase-5 implicit integration. The primitive never hard-codes an integration method.
- **Canonical component values.** Representative bills of materials for each topology are drawn from standard passive clipper practice; the exact BOM constants are finalized in the plan/implementation, and correctness is defined by the invariants and the solver cross-check, not by any single vendor's part list.
- **Dependency.** Builds on the shipped `component-abstractions` primitive (`core/primitives/circuit/`, `Netlist`, `Diode` with Shockley `evaluate` / `vCrit` / `limitJunctionVoltage`) and reuses the lab reference-solver structure (`core/labs/component-abstractions/solver/` — `LinearSolver`, the static `NewtonClipper` companion-linearization pattern) as the basis for the new transient solve.
- **Drive/gain is upstream.** A pedal's "drive" knob is an upstream gain stage feeding the clipper; it is not part of the passive clipping-stage vocabulary and is out of scope here.

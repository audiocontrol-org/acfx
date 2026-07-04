> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: component-abstractions

**Feature Branch**: `component-abstractions`

**Created**: 2026-07-03

**Status**: Draft

**Input**: Design record: `docs/superpowers/specs/2026-07-03-component-abstractions-design.md` (approved 2026-07-03). Roadmap item: `design:primitive/component-abstractions`, part-of `multi:feature/phase-circuit-modeling`, depends-on `multi:feature/phase-dynamic-systems` (shipped). First deliverable of Phase 4 (Circuit Modeling).

## Clarifications

### Session 2026-07-03 (from design review, recorded binding)

- Q: OQ1 — Reactive discretization method for the reference solver? → A: **Backward Euler**, explicitly **non-normative** (the lab's naive choice). Trapezoidal / the general treatment are Phase 5 (implicit integration); the primitive itself never hard-codes a method.
- Q: OQ2 — How far does the lab's nonlinear solve go before it is "really Phase 5 MNA"? → A: **Held at the clipper.** The reference solver handles only linear circuits (Gaussian elimination) and the single-diode / antiparallel-diode clipper (a bounded fixed-iteration Newton). Any circuit with multiple interacting nonlinearities waits for Phase 5. The lab must not become MNA.
- Q: OQ3 — Capacity model? → A: **Templated fixed capacity** `Netlist<MaxNodes, MaxComponents>` (and a matching fixed-size solver system), not hard-coded global limits — no hard ceiling, heap-free by construction.
- Q: OQ4 — Controlled sources / op-amp / nullor as v1 inhabitants? → A: **Defer.** Captured in the taxonomy, not v1 inhabitants; they land when the op-amp-stages deliverable needs them. The v1 inhabitant set is R, C, L, independent V/I sources, diode.
- Note: the inductor **L is retained** in v1 (design review proposed deferring it) so the reactive-companion seam is exercised by two distinct elements (guarding against over-fitting the abstraction to the capacitor) and so the RLC analytic-validation circuit exists — at near-zero marginal cost, L being the dual of C's companion.

### Session 2026-07-03 (clarify)

- Q: What numeric precision does the component physics and the solve use, given the audio path is `float`? → A: **Compute in `double` internally, convert at the `float` audio boundary.** Circuit solving is numerically sensitive (matrix conditioning, the diode's `exp`); `double` buys robustness where it matters, and the reference solver is a lab/validation artifact rather than the embedded hot path, so its `double` cost is acceptable. Audio I/O stays `float`, consistent with the rest of `core/`. (Rejected: `float` throughout — risks conditioning/`exp` precision loss and forces looser tolerances; and templating the numeric type — deferred, it compounds the open OQ5 code-size question without a present need.)

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is threefold, matching the platform's audience framing (as in `svf-vertical-slice` / `tape-dynamics`):

- the **circuit-model author** — the developer who assembles a netlist of typed components (a tone stack, a clipper) and expects it to describe the circuit's physics without committing to a solver;
- the **solver author** — who, in Phase 5 (MNA) and Phase 6 (WDF), will consume this vocabulary as an *adapter*, reading each component's own physics to stamp a matrix or scatter waves;
- the **lab reader** — learning how electronic components are represented as first-class abstractions, and seeing a small assembled circuit actually solved and validated against its analytic response.

This feature is the **first deliverable of phase-circuit-modeling**: it teaches **component modeling** (Constitution Principle XI — one concept at a time), the vocabulary that the later Phase-4 deliverables (passive tone stacks, diode clippers, op-amp stages) assemble and that the Phase-5/6 solvers adapt. Each story below is an independently testable slice.

### User Story 1 - Represent a component's physics, solver-neutrally (Priority: P1)

A circuit-model author declares a typed circuit element — a resistor, capacitor, inductor, voltage source, current source, or diode — as a value with its terminals bound to circuit nodes and its physical parameters set. Each element carries **its own constitutive relation**: a linear element exposes its admittance; a nonlinear element (diode) exposes a function from terminal voltages to `{current, conductance}`; a reactive element (C, L) exposes its continuous relation plus a companion hook. No solver-specific concept (matrix stamp, wave variable) appears on the element.

**Why this priority**: This is the MVP — the defining artifact of the feature and the one new concept the phase owns. Without the solver-neutral typed vocabulary there is nothing for Phase 4 to assemble or Phase 5/6 to adapt.

**Independent Test**: Construct each v1 component in isolation and query its physics directly — a resistor returns `i = v/R`; a diode returns the Shockley current and its small-signal conductance at a given bias; a capacitor returns the expected companion (equivalent conductance + history current) for a given timestep. No circuit, no solver required.

**Acceptance Scenarios**:

1. **Given** a resistor `R` between nodes `a` and `b`, **When** its admittance is queried, **Then** it reports conductance `1/R` with the correct nodal sign convention.
2. **Given** a diode with known saturation current `Is` and thermal parameters, **When** `evaluate(vAK)` is called at a forward bias, **Then** it returns the Shockley current and the analytic small-signal conductance `dI/dV` at that bias.
3. **Given** a capacitor `C` and a timestep `dt`, **When** its backward-Euler companion is requested, **Then** it returns the equivalent conductance `C/dt` and the history current derived from the previous node voltages.
4. **Given** any component, **When** it is inspected, **Then** it exposes no solver-specific method (no `stamp`, no `scatter`) — physics only.

---

### User Story 2 - Assemble a circuit as a typed netlist (Priority: P1)

The author collects components into a `Netlist<MaxNodes, MaxComponents>` that owns the topology — a count of nodes (ground ≡ node `0`) and a fixed-capacity, heap-free set of components. The netlist is built at `prepare()` time (allocation permitted there), and validated: a well-posed circuit passes; an ill-posed one (a floating node with no DC path to ground, a missing ground reference, or a component/node count exceeding the template capacity) raises a descriptive error at `prepare()`, never a silent fallback.

**Why this priority**: The vocabulary (US1) is only useful once elements can be composed into a circuit with a validated topology. Assembly + validation is the second half of the MVP.

**Independent Test**: Build a voltage-divider netlist and confirm it validates and reports the expected node/component counts; then build three ill-posed netlists (floating node, no ground, over-capacity) and confirm each raises a distinct, descriptive error at `prepare()`.

**Acceptance Scenarios**:

1. **Given** a set of components referencing nodes `0..N`, **When** the netlist is prepared, **Then** it reports the correct node and component counts and validates as well-posed.
2. **Given** a netlist with a node that has no conductive path to ground, **When** it is prepared, **Then** preparation raises a descriptive "floating node" error naming the node.
3. **Given** a netlist with no ground reference, **When** it is prepared, **Then** preparation raises a descriptive "missing ground" error.
4. **Given** more components (or nodes) than the template capacity `MaxComponents` (or `MaxNodes`), **When** they are added, **Then** the over-capacity condition raises a descriptive error — no heap growth, no truncation.

---

### User Story 3 - Solve and validate a linear circuit (reference solver, in the lab) (Priority: P2)

The lab reader assembles a linear circuit — a resistive voltage divider, an RC low-pass, an RLC network — and runs it through the **reference solver that lives in `core/labs/component-abstractions/`, not in the primitive**. The solver assembles a fixed-size linear system from the netlist (reading each component's admittance / backward-Euler companion) and solves it by Gaussian elimination with no heap allocation in the solve. The reader compares the result against the circuit's known analytic response.

**Why this priority**: This is what makes the vocabulary *runnable and validatable* before Phase 5's MNA exists, and it unblocks the rest of Phase 4. It is P2 because it depends on US1+US2 and is deliberately naive (superseded by MNA).

**Independent Test**: Solve a voltage divider and confirm the node voltage matches the exact ratio; sweep an RC low-pass and confirm the magnitude/phase match the analytic first-order response within tolerance; solve an RLC network and confirm it matches its analytic second-order response.

**Acceptance Scenarios**:

1. **Given** a resistive divider with resistors `R1`, `R2` from source to ground, **When** solved, **Then** the mid-node voltage equals `Vin·R2/(R1+R2)` to numerical precision.
2. **Given** an RC low-pass driven by a sinusoid, **When** solved across a frequency sweep with a backward-Euler timestep, **Then** the measured magnitude and phase track the analytic `1/(1+jωRC)` response within the documented tolerance (the tolerance accounts for backward-Euler's known damping, which is non-normative).
3. **Given** an RLC network, **When** solved, **Then** the response matches its analytic second-order transfer within tolerance.
4. **Given** any of the above, **When** the solve path runs, **Then** no heap allocation occurs during the per-sample solve (asserted).

---

### User Story 4 - Solve a single nonlinearity: the diode clipper (Priority: P2)

The lab reader assembles a diode clipper (a single diode, or an antiparallel diode pair, across an RC node) and runs it through the reference solver, which resolves the one nonlinearity with a **bounded fixed-iteration Newton** step. The reader compares the static input→output transfer against the known soft-clipping curve. The solver does **not** attempt circuits with multiple interacting nonlinearities — that boundary is explicit and enforced by scope, and is Phase 5's subject.

**Why this priority**: It demonstrates the nonlinear-component abstraction (`evaluate → {current, conductance}`) end-to-end and unblocks the diode-clippers Phase-4 deliverable, while holding the deliberate line that keeps the lab from becoming MNA.

**Independent Test**: Sweep a DC input across the clipper and confirm the output transfer matches the analytic single-diode / antiparallel-diode soft-clip curve within tolerance; confirm the Newton loop terminates within its iteration bound and reports its residual.

**Acceptance Scenarios**:

1. **Given** a single-diode clipper, **When** a DC sweep is applied, **Then** the output transfer matches the analytic soft-clip curve within tolerance.
2. **Given** an antiparallel-diode pair, **When** a symmetric sweep is applied, **Then** the output is the expected symmetric soft-clip.
3. **Given** a bias where Newton would not converge within the bound, **When** the solve runs, **Then** it terminates at the iteration bound and reports the final residual/status — never a silent fallback or fabricated output.
4. **Given** a netlist with two or more interacting nonlinear components, **When** it is handed to the reference solver, **Then** the solver refuses with a descriptive "out of reference-solver scope — deferred to Phase 5" error rather than attempting it.

---

### Edge Cases

- **Floating node / no DC path to ground** → descriptive `prepare()` error naming the node (US2.2).
- **No ground reference (node 0 unreferenced)** → descriptive `prepare()` error (US2.3).
- **Over-capacity** (nodes > `MaxNodes` or components > `MaxComponents`) → descriptive error, no heap growth, no truncation (US2.4).
- **Singular / near-singular system** at solve time → because topology is validated at `prepare()`, a well-posed circuit does not go singular in the linear solve; a detected numerical singularity (e.g. a zero-conductance pivot) is reported, not papered over.
- **Newton non-convergence** on the clipper → bounded iteration count, reported residual/status, no fallback (US4.3).
- **Multiple interacting nonlinearities** handed to the reference solver → explicit out-of-scope refusal (US4.4), not a naive attempt.
- **Reactive element at DC (ω→0)** → capacitor is an open (its companion contributes no steady conductance beyond history), inductor is a short; the companion formulation handles these limits without special-casing.
- **Zero-valued component** (R=0 short, R=∞ open, C=0, L=0) → represented honestly; a true `R=0` between distinct nodes is a topology the validator flags if it creates an ill-posed loop with an ideal source.

## Requirements *(mandatory)*

### Functional Requirements

**Component vocabulary (the primitive — `core/primitives/circuit/`)**

- **FR-001**: The primitive MUST define a node abstraction with an integer node handle where **ground ≡ node 0**, and a component's terminals MUST be bound to node handles.
- **FR-002**: The primitive MUST represent each circuit element as a **typed value struct** carrying element type, terminal→node mapping, and physical parameters — with **no inheritance and no virtual dispatch**.
- **FR-003**: Each **linear** element MUST expose its physics as an **admittance** (conductance contribution with correct nodal signs).
- **FR-004**: Each **nonlinear** element MUST expose `evaluate(terminalVoltages) → { current, conductance/Jacobian }` — its own constitutive relation, evaluated at a bias — with the diode implementing the **Shockley** equation.
- **FR-005**: Each **reactive** element (capacitor, inductor) MUST expose its **continuous-domain relation** plus a **companion hook** parameterized by a solver-supplied timestep — and MUST NOT hard-code a numerical integration method.
- **FR-006**: No component MUST expose any solver-specific operation (no matrix `stamp`, no wave `scatter`). Physics lives on the component; assembly lives in the solver.
- **FR-007**: The v1 inhabitant set MUST be exactly: **Resistor (R), Capacitor (C), Inductor (L), independent Voltage source (V), independent Current source (I), Diode (Shockley)**. Controlled sources (VCVS/VCCS/CCVS/CCCS), ideal op-amp, and nullor MUST be documented in the taxonomy as captured-but-deferred, and MUST NOT be materialized as inhabitants in this feature.
- **FR-008**: Components MUST be stored in a **heap-free, vtable-free** container over the fixed v1 type set (a tagged/variant value in a fixed-capacity array), dispatched without virtual calls.

**Topology & netlist**

- **FR-009**: The primitive MUST provide a **templated fixed-capacity** `Netlist<MaxNodes, MaxComponents>` that owns the circuit topology (node count + component set) with **no heap allocation** and no hard-coded global capacity ceiling.
- **FR-010**: The netlist MUST be assembled during `prepare()` (allocation permitted there) and MUST perform **topology validation** at `prepare()`, raising a **descriptive error** for: a floating node with no conductive path to ground, a missing ground reference, and an over-capacity condition. No silent fallback, no truncation.
- **FR-011**: After a successful `prepare()`, the per-sample **solve path MUST be well-posed and MUST NOT throw** and MUST NOT allocate.

**Reference solver (the lab — `core/labs/component-abstractions/`, NOT the primitive)**

- **FR-012**: The reference solver MUST live in `core/labs/component-abstractions/` and MUST NOT be part of the primitive. The primitive MUST remain solver-agnostic and independently usable without the lab.
- **FR-013**: The reference solver MUST solve **linear** circuits by assembling a fixed-size system from component admittances/companions and solving via **Gaussian elimination**, with no heap allocation in the solve.
- **FR-014**: The reference solver MUST discretize reactive elements with **backward Euler**, and MUST document this choice as **non-normative** (the naive lab choice, superseded by Phase 5 implicit integration).
- **FR-015**: The reference solver MUST resolve a **single** nonlinearity (single diode or antiparallel diode pair) via a **bounded fixed-iteration Newton** step, reporting its residual/status; on non-convergence within the bound it MUST report the residual and MUST NOT fall back or fabricate output.
- **FR-016**: The reference solver MUST **refuse** a netlist containing **two or more interacting nonlinear components** with a descriptive "out of reference-solver scope — deferred to Phase 5" error, rather than attempting a general nonlinear solve. It MUST NOT grow into MNA.

**Cross-cutting (constitution-derived)**

- **FR-017**: All headers MUST be **platform-independent** and **C++17-compatible** (no dependency on JUCE / libDaisy / Teensy; usable on the Teensy target which lacks C++20 concepts).
- **FR-018**: The `core/primitives/circuit/` category folder MUST be created **in the same commit as its first inhabitant** (inhabit-before-creating); no empty/`.gitkeep` category directory.
- **FR-019**: There MUST be **no fallbacks or mock data outside test code**; missing functionality raises a descriptive error.
- **FR-020**: Each source file MUST stay within **~300–500 lines**; split by concern (e.g. per-component headers, netlist, models) as needed.
- **FR-021**: The primitive taxonomy documentation (`core/primitives/README.md`) MUST be updated to register the new `circuit/` category and its inhabitants, consistent with the existing taxonomy discipline.
- **FR-022**: Component physics and the reference solve MUST compute in **`double`** precision internally, converting to/from the platform's `float` audio sample type only at the audio boundary. The primitive MUST NOT force `float` precision into the solve.

### Key Entities

- **NodeId / node set**: an integer node handle (ground ≡ 0) and the count of nodes a circuit spans; the coordinate system topology is expressed in.
- **Component (typed value struct)**: one circuit element — its type, its terminal→node binding, its physical parameters, and its own physics (admittance / `evaluate` / companion hook). The v1 types: Resistor, Capacitor, Inductor, VoltageSource, CurrentSource, Diode.
- **Netlist<MaxNodes, MaxComponents>**: the fixed-capacity, heap-free container owning topology (nodes + components); the unit that is `prepare()`-validated and handed to a solver.
- **Companion model**: the backward-Euler discretization of a reactive element for a given timestep (equivalent conductance + history current) — produced by the reactive component's companion hook, consumed by the solver.
- **Reference solver (lab)**: the deliberately-naive linear (Gaussian-elimination) + single-nonlinearity (bounded Newton) solver that makes circuits runnable in the lab; explicitly non-normative and Phase-5-superseded.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All six v1 components can be constructed and their physics queried in isolation; each matches its analytic reference (resistor conductance exact; diode Shockley current and small-signal conductance within tolerance of the closed form; capacitor/inductor companion exact for a given `dt`).
- **SC-002**: A resistive voltage divider solves to the exact analytic node voltage (to numerical precision).
- **SC-003**: An RC low-pass frequency sweep tracks the analytic first-order magnitude/phase within the documented tolerance; an RLC network matches its analytic second-order response within tolerance.
- **SC-004**: A single-diode and an antiparallel-diode clipper reproduce the analytic soft-clip static transfer within tolerance, with the Newton loop terminating inside its iteration bound.
- **SC-005**: Every ill-posed topology (floating node, missing ground, over-capacity) and every out-of-scope circuit (≥2 interacting nonlinearities) produces a **distinct, descriptive error** — verified by test — with **no fallback path** exercised.
- **SC-006**: The per-sample solve path performs **zero heap allocations** (asserted in test) and the primitive headers compile under a **C++17** configuration.
- **SC-007**: The primitive compiles and its component/netlist tests pass with the lab/reference-solver **absent** — proving the primitive is solver-independent (the seam holds).
- **SC-008**: `ls core/primitives/circuit/` shows only inhabited, documented headers; `core/primitives/README.md` registers the `circuit/` category and its six inhabitants.

## Assumptions

- **Reference solver is throwaway scaffolding, by design.** The lab's Gaussian-elimination + bounded-Newton solver exists to validate the vocabulary and unblock Phase 4; Phase 5 (MNA / Newton / implicit integration) supersedes it. It is intentionally naive and non-normative; "correctness" for it means "matches the analytic reference within a tolerance that accounts for backward-Euler damping," not "production-grade solver."
- **Backward Euler for reactive elements in the lab** (OQ1, resolved). The primitive stays integration-method-neutral; the method choice lives only in the lab.
- **Templated fixed capacity** `Netlist<MaxNodes, MaxComponents>` (OQ3, resolved) with operator-chosen sane default capacities for the lab's validation circuits; capacities are compile-time template parameters, not runtime limits.
- **v1 inhabitant set is R, C, L, V, I, diode** (OQ4, resolved); controlled sources / op-amp / nullor deferred to the op-amp-stages deliverable.
- **`std::variant` is the expected container** for the tagged value, but the choice between `std::variant` and a hand-rolled tagged union — and the code-size impact of templated-capacity instantiation on the Teensy target — is an **open measurement (OQ5)**, captured as a risk to measure during implementation, not pre-decided here.
- **Numeric precision** (clarified 2026-07-03): the physics and the reference solve compute in `double`; conversion to/from `float` happens only at the audio boundary (FR-022). The audio sample type remains the platform's `float`, consistent with the rest of `core/`.
- **Validation is against analytic references** where a closed form exists (divider, RC, RLC, diode transfer); no external SPICE dependency is assumed.

## Dependencies

- **Upstream (satisfied)**: `multi:feature/phase-dynamic-systems` (shipped) — the platform, three-layer structure, primitive taxonomy, and test-harness conventions this feature builds within.
- **Downstream (informed by this spec, not built here)**: the remaining Phase-4 deliverables (passive-tone-stacks, diode-clippers, opamp-stages) assemble this vocabulary; Phase 5 (MNA / Newton / implicit-integration) and Phase 6 (WDF) adapt it. Their needs shaped the solver-neutral seam but are out of scope for this feature.

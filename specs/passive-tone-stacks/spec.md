> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: passive-tone-stacks

**Feature Branch**: `passive-tone-stacks`

**Created**: 2026-07-04

**Status**: Draft

**Input**: Design record: `docs/superpowers/specs/2026-07-04-passive-tone-stacks-design.md` (approved 2026-07-04, design-to-spec exit gate 7/7). Roadmap item: `design:primitive/passive-tone-stacks`, part-of `multi:feature/phase-circuit-modeling`, depends-on `multi:feature/phase-dynamic-systems` (shipped). Second deliverable of Phase 4 (Circuit Modeling), following `component-abstractions`.

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is threefold, matching the platform's audience framing (as in `component-abstractions` / `svf-vertical-slice` / `tape-dynamics`):

- the **circuit-model author** — the developer who assembles a *named* passive tone stack (a Fender/Marshall/Vox stack, a Baxandall stack) from a bill of materials and a set of pot positions, and expects a `Netlist` describing the circuit's physics **without committing to a solver**;
- the **solver author** — who, in Phase 5 (MNA) and Phase 6 (WDF), will consume **the same builders** as an adapter target, reading each component's own physics to stamp a matrix or scatter waves;
- the **lab reader** — learning how a real tone control is modelled as a passive network, and seeing its **frequency response** actually computed and validated against the published (Duncan) analytic transfer function.

This feature is the **second deliverable of phase-circuit-modeling**, the first that composes the `component-abstractions` vocabulary into a musically recognizable circuit. It teaches **one concept** (Constitution Principle XI — one concept at a time): **the pot-driven passive frequency-shaping sub-circuit** — a parameterized, control-driven, reusable network, and how to read its frequency response. Each story below is an independently testable slice.

### User Story 1 - Assemble a named passive tone stack, solver-neutrally (Priority: P1)

A circuit-model author calls a named builder — `toneStackFMV` (the Fender/Marshall/Vox 3-band stack) or `toneStackBaxandall` (the passive Baxandall/James 2-band stack) — passing a bill of materials (resistor/capacitor values) and pot positions, and receives a `Netlist` of ordinary frozen-vocabulary components (`Resistor`, `Capacitor`, `VoltageSource`) describing the circuit. No solver-specific concept appears; the netlist can be handed to any solver (the lab AC solver now, Phase-5 MNA later).

**Why this priority**: This is the MVP — the defining artifact of the feature and the one new composition the phase owns. Without a solver-neutral tone-stack builder there is nothing for a solver author to adapt and nothing for the lab reader to solve.

**Independent Test**: Call each builder at several control settings and inspect the returned `Netlist` directly — it `prepare()`s cleanly (ground referenced, no floating node), its component and node counts match the topology's bill of materials, and it holds only frozen-vocabulary elements. No solver required.

**Acceptance Scenarios**:

1. **Given** a set of FMV component values and pot positions `{bass, mid, treble}`, **When** `toneStackFMV(values, controls, taper)` is called, **Then** it returns a `Netlist` that `prepare()`s without error and whose component/node counts match the FMV topology's bill of materials.
2. **Given** a set of Baxandall values and pot positions `{bass, treble}`, **When** `toneStackBaxandall(values, controls, taper)` is called, **Then** it returns a `prepare()`-valid `Netlist` for the passive James 2-band network.
3. **Given** any returned tone-stack `Netlist`, **When** it is inspected, **Then** every component is a member of the frozen `component-abstractions` vocabulary — the builders introduce **no** new element type and modify **no** existing one.
4. **Given** the builder's translation unit, **When** its includes are inspected, **Then** it includes nothing under `core/labs/` — the primitive does not depend on any lab.

---

### User Story 2 - Drive the tone stack with pot controls via taper laws (Priority: P2)

The author sweeps a pot from 0 to 1 and expects the network to change the way a real potentiometer does: a wiper splits a resistance track into two legs whose ratio follows the pot's **taper law** (linear, or the exponential "audio"/log taper), and never collapses to a dead short at the extremes (a real pot has a small contact/end resistance). This is the genuinely new physics content the deliverable owns.

**Why this priority**: The potentiometer *is* the tone stack's control surface; without a faithful, independently testable pot model the builder is not a tone control. It is separated from US1 so the pot math is provable in isolation, with no circuit and no solver.

**Independent Test**: Call `wiper(rTrack, pos, taper)` (and `rheostat(...)` for 2-terminal use) directly and check the returned leg resistances against hand values — no circuit assembled.

**Acceptance Scenarios**:

1. **Given** a track resistance `rTrack` and `pos = 0.5` with `Taper::Linear`, **When** `wiper` is called, **Then** it returns two equal legs summing to `rTrack` (within the end-resistance floor).
2. **Given** `Taper::Log` at a known position, **When** `wiper` is called, **Then** the leg split matches the reference exponential-taper fraction at that position.
3. **Given** `pos = 0` or `pos = 1`, **When** `wiper` is called, **Then** neither leg is `0` — each is floored at the fixed contact/end resistance, so an assembled netlist never contains a 0-Ω short at a pot extreme.
4. **Given** `pos` outside `[0, 1]` or a non-positive `rTrack`, **When** `wiper` is called, **Then** it raises a descriptive `std::invalid_argument` — no silent clamp, no fallback.

---

### User Story 3 - Read and validate the frequency response (Priority: P3)

The lab reader (and the future solver author) wants to see an assembled passive tone stack actually solved in the frequency domain: given a tone-stack `Netlist` and an angular frequency `ω`, compute the exact continuous-time transfer function `H(jω) = V_out / V_in`, sweep it across the audio band, and confirm the magnitude/phase curve matches the independently-derived analytic (Duncan) transfer function for that stack — including the recognizable FMV mid-scoop.

**Why this priority**: Frequency response is what makes a tone stack *recognizable*; validating it against independent hand analysis is what proves the whole chain (builder → assembled netlist → solve). It depends on US1 (a netlist to solve) and US2 (correct pot-driven values), so it is last.

**Independent Test**: Run the host-only lab AC solver on known sanity networks first (an RC low-pass must roll off at −20 dB/decade with phase → −90°; a resistive divider must be flat at `R2/(R1+R2)`), then on `toneStackFMV` / `toneStackBaxandall` outputs, comparing `|H(f)|` and `∠H(f)` to the analytic transfer function at several control settings.

**Acceptance Scenarios**:

1. **Given** an RC low-pass and a resistive divider built from the vocabulary, **When** `solveAC` is swept, **Then** the magnitude/phase match the closed-form response to numerical precision — proving the solver before it is trusted on a tone stack.
2. **Given** an FMV netlist at ≥3 control settings including a low mid setting, **When** `solveAC` is swept across the audio band, **Then** `|H(f)|` matches the analytic FMV transfer function within the stated tolerance, and the characteristic mid-scoop is present.
3. **Given** the FMV stack, **When** the mid pot is lowered, **Then** the scoop depth increases monotonically; **When** the treble pot is raised, **Then** the high-frequency magnitude increases monotonically.
4. **Given** a Baxandall netlist at ≥3 control settings, **When** `solveAC` is swept, **Then** `|H(f)|` matches its analytic curve, the bass/treble shelves move the expected asymptotes, and the center setting is near-flat.
5. **Given** a network that is singular at some `ω`, **When** `solveAC` is called, **Then** it raises a descriptive `std::runtime_error` naming `ω` — never a silent wrong `H`.

---

### Edge Cases

- **Pot at a hard extreme** (`pos = 0` / `pos = 1`): the end-resistance floor keeps every leg ≥ the fixed contact resistance, so `prepare()` never sees a 0-Ω short or a degenerate node.
- **All pots at the same extreme** (all bass/mid/treble at 0, or all at 1): the builder still returns a `prepare()`-valid netlist (ground referenced, every node reachable).
- **Singular network at a probe frequency**: `solveAC` throws naming `ω`; it does not fabricate a response.
- **Invalid control/BOM input** (`pos ∉ [0,1]`, non-positive `rTrack` or component value): descriptive `std::invalid_argument` on the build/control thread, never on an audio path (there is none here).
- **Lab deleted**: the primitive and its Tier-1 tests must still build and pass; only the assembled-response validations disappear.

## Requirements *(mandatory)*

### Functional Requirements

**Builders (portable primitive — `core/primitives/circuit/tone-stack/`)**

- **FR-001**: The system MUST provide `toneStackFMV(FMVValues, FMVControls, Taper)` returning a fixed-capacity `Netlist` of frozen-vocabulary components modelling the Fender/Marshall/Vox 3-band tone stack. It MUST produce topology only — **no** solved response and **no** audio-path / `process()` realization.
- **FR-002**: The system MUST provide `toneStackBaxandall(BaxandallValues, BaxandallControls, Taper)` returning a `Netlist` for the **passive James** 2-band Baxandall tone control (the active op-amp Baxandall is out of scope — it needs the deferred op-amp element).
- **FR-003**: The builders MUST use **only** the frozen `component-abstractions` vocabulary (`Resistor`, `Capacitor`, `VoltageSource`, and `Netlist`) — introducing no new element type and modifying no existing one — and MUST include nothing under `core/labs/`.
- **FR-004**: The builders MUST be **pure functions with no retained state**; a control change is expressed by rebuilding the netlist (control-rate). The returned `Netlist` MUST be heap-free (fixed compile-time capacities per topology; no `new`/`delete`/`std::vector`).
- **FR-005**: The builders MUST return a `Netlist` that `prepare()`s cleanly (ground referenced, no floating node) across the full control range, including all-pots-0, all-pots-1, mixed, and the design-center setting.

**Potentiometer as build-time parameterization (`taper.h`)**

- **FR-006**: The potentiometer MUST be modelled as build-time math, **not** a new vocabulary element: `wiper(rTrack, pos, Taper)` MUST map `pos ∈ [0,1]` through the taper law to two ordinary `Resistor` legs `{rTop, rBottom}` with `rTop + rBottom == rTrack` before flooring (a 3-terminal divider), and `rheostat(rTrack, pos, Taper)` MUST expose the single-leg (2-terminal) form.
- **FR-007**: The taper laws MUST include `Taper::Linear` and `Taper::Log` (exponential "audio" taper). `Antilog` MUST NOT be present — it MUST NOT exist as a dead/stub enum case (it is added only if a future stack needs mirrored controls; passive Baxandall uses linear pots, so no v1 exemplar needs it).
- **FR-008**: Each wiper leg MUST be floored at a **fixed 10 Ω** modelled contact/end resistance via `leg = max(computed, floor)`, applied **per leg**, so a returned netlist never contains a 0-Ω short at a pot extreme. The floor is a documented physical value, never a bug-hiding fallback; near an endpoint `rTop + rBottom` may exceed `rTrack` by up to one floor (physically correct series end resistance).
- **FR-009**: The following-stage load MUST be an **explicit** `rLoad` value in `FMVValues` and `BaxandallValues`, wired **output-node-to-ground** (the following stage's AC input impedance) — never a hidden constant.
- **FR-010**: Invalid input on the build/control thread — `pos ∉ [0,1]`, non-positive `rTrack`, or non-positive component values — MUST raise a descriptive `std::invalid_argument`. No silent clamp of `pos`, no fallback, no fabricated value.

**Frequency-response validation (host-only lab — `core/labs/passive-tone-stacks/`)**

- **FR-011**: The lab MUST provide `solveAC(netlist, ω, inNode, outNode)` computing the exact continuous-time `H(jω) = V_out/V_in` by stamping each component's admittance at `jω` (`R → 1/R`, `C → jωC`, `L → 1/(jωL)`) and solving by complex Gaussian elimination over `std::complex<double>`. It MUST **read** each component's admittance and MUST NOT re-derive a component's constitutive law (the FR-006-seam of `component-abstractions`, carried into the frequency domain).
- **FR-012**: `solveAC` MUST raise a descriptive `std::runtime_error` naming `ω` on a singular complex system — never a silent wrong `H`, mirroring `LinearSolver`'s zero-pivot policy.
- **FR-013**: The lab MUST NOT introduce Modified Nodal Analysis, a Newton loop, or trapezoidal/higher-order integration; it is a **linear complex `.ac` solve only** ("the lab must never grow into MNA" boundary carried forward from `component-abstractions`).
- **FR-014**: The lab MUST provide a host-only harness that prints PASS/FAIL lines with measured-vs-analytic `|H(f)|` and `∠H(f)` for FMV and Baxandall against the **independently derived** analytic (Duncan) transfer function, mirroring the doctest assertions in the Tier-2 test.
- **FR-015**: The lab MUST compute in `double` / `std::complex<double>` throughout; there is **no** `float` audio boundary in this deliverable (no audio path).

**Isolation & scope**

- **FR-016**: Deleting `core/labs/passive-tone-stacks/` MUST leave the tone-stack primitive and its Tier-1 tests compiling and passing; only the assembled-response validations disappear (mirrors `component-abstractions` SC-007). The primitive MUST have no include of anything under the lab.
- **FR-017**: Out of scope by construction (each captured for a later phase/feature, not a gap to fill here): no realtime `process()` / audio-path realization (Phase-6 WDF or a later lowering); no THD/nonlinearity (passive linear network); value-variant stacks (Marshall/Vox = FMV re-valued) and other topologies (Big Muff, Neve, tilt-EQ, active Baxandall) as named `design:feature/*` products; no DAW/hardware acceptance.

### Key Entities

- **Taper**: the pot's mechanical→electrical law; v1 = `{ Linear, Log }`.
- **WiperSplit**: the result of a divider wiper — `{ rTop, rBottom }` (two `Resistor` values).
- **FMVValues / FMVControls**: the FMV bill of materials (resistor/capacitor values + explicit `rLoad`) and its three pot positions `{ bass, mid, treble } ∈ [0,1]`.
- **BaxandallValues / BaxandallControls**: the passive James bill of materials (+ explicit `rLoad`) and its two pot positions `{ bass, treble } ∈ [0,1]`.
- **Tone-stack Netlist**: a fixed-capacity `Netlist<MaxNodes, MaxComponents>` of frozen-vocabulary components, capacities sized per topology.
- **AC transfer function `H(jω)`** *(lab)*: the complex ratio `V_out/V_in` from `solveAC`, swept to yield `|H(f)|` and `∠H(f)`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For both FMV and Baxandall, the builder returns a `prepare()`-valid `Netlist` at every one of the extreme control settings (all pots 0, all pots 1, a mixed setting) and the design-center setting — 100% of tested settings, zero `prepare()` failures.
- **SC-002**: The wiper legs sum to `rTrack` within the end-resistance floor; `pos = 0.5` with `Linear` yields equal legs; `Log` matches its reference fraction at the tested positions; at `pos = 0` and `pos = 1` each leg equals the 10 Ω floor, never 0.
- **SC-003**: `solveAC` matches the closed-form magnitude/phase of the RC low-pass (−20 dB/decade slope; phase → −90°) and the resistive divider (flat at `R2/(R1+R2)`) to a tight numeric tolerance (≈ 1e-9) — the solver is validated before being trusted on a tone stack.
- **SC-004**: FMV `|H(f)|` matches the analytic FMV transfer function within the stated dB tolerance across the audio band at ≥3 control settings including a low-mid (mid-scoop) setting; the scoop depth increases monotonically as the mid pot lowers and HF magnitude increases monotonically as the treble pot rises.
- **SC-005**: Baxandall `|H(f)|` matches its analytic curve within the stated tolerance at ≥3 control settings; the bass/treble shelves move the expected asymptotes and the center setting is near-flat.
- **SC-006**: With `core/labs/passive-tone-stacks/` deleted, the tone-stack primitive and its Tier-1 tests build and pass (isolation is itself a verified outcome).
- **SC-007**: No heap in the builder path — `core/primitives/circuit/tone-stack/` contains no `new`/`delete`/`std::vector`; capacities are compile-time `Netlist` template parameters.

## Assumptions

- **Frequency grid & tolerance (OQ4 — the one open numeric knob, a `/speckit-clarify` target).** Pending clarification, the analytic cross-check assumes a **log-spaced grid of ~10 points/decade over 20 Hz–20 kHz** and a **magnitude match tolerance on the order of 0.1 dB** — tight enough to catch a topology error, loose enough for honest floating-point. `/speckit-clarify` may refine both; nothing else in the spec depends on the exact values.
- **Analytic reference.** The published Duncan Tone Stack Calculator transfer functions (FMV) and the standard passive Baxandall/James response are the accepted independent cross-check; the analytic form is derived separately from the solver (not the solver graded against itself).
- **Baxandall variant = passive James** with **linear** pots; the active op-amp Baxandall is deferred to `opamp-stages` (needs the deferred op-amp element).
- **Canonical component values.** Representative FMV and Baxandall bills of materials are drawn from published references; the exact BOM constants are finalized in the plan/implementation, and the deliverable's correctness is defined by the analytic-match, not by any single vendor's part list.
- **Dependency.** Builds on the shipped `component-abstractions` primitive (`core/primitives/circuit/`, `Netlist`) and reuses the lab reference-solver structure (`core/labs/component-abstractions/solver/`) as the pattern for the new complex AC solve.
- **Load model.** The following-stage load is modelled as a single resistor (`rLoad`) from the output node to ground — the standard passive-tone-stack loading approximation.

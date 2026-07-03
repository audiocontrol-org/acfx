> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: tape-dynamics

**Feature Branch**: `tape-dynamics`

**Created**: 2026-07-03

**Status**: Draft

**Input**: Design record: `docs/superpowers/specs/2026-07-03-tape-dynamics-design.md` (approved 2026-07-03). Roadmap item: `design:feature/tape-dynamics`, part-of `multi:feature/phase-dynamic-systems`, depends-on `multi:feature/phase-nonlinear-dsp` (closed, validated).

## Clarifications

### Session 2026-07-03

- Q: OQ1 — Which numerical solver(s) ship in the first implementation cut? → A: All three — RK2, RK4, and Newton-Raphson — in the first cut (the selectable-solver tradeoff is the feature's core numerical-methods lesson; shipping all three delivers it fully).
- Q: OQ2 — Does the optional explicit envelope-driven trim (US6 / FR-011) land in the first cut? → A: Yes — included in the first cut (bit-exact no-op when disabled; completes the "both, layered" compression identity).
- Q: OQ4 — Which oversampling factors are exposed, and what is the default? → A: Menu {2×, 4×, 8×}, default 8× (exact sweet spot still tuned against the alias-sweep harness during implementation). *Note (2026-07-03, implementation): the menu was set to {2×, 4×, 8×} — the shipped `Oversampler<Factor>` `static_assert`s `Factor ∈ {2,4,8}`, so 16× is unavailable without modifying that separate validated primitive; operator elected to stay within the feature boundary rather than extend it.*
- Q: OQ3 — Concrete parameter ranges/mapping for the physics macros? → A: Deferred to implementation — musically useful ranges tuned against harness measurements (as the `saturation` voicings were); not an architecture-blocking decision.
- Q: OQ5 — Does `Hysteresis` get its own `tests/core/` unit file in addition to the lab harness? → A: Yes — a dedicated `tests/core/hysteresis-test.cpp`, mirroring `tests/core/delay-line-test.cpp`.

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is threefold, matching the platform's audience framing (as in `compressors` / `program-dependent-saturation`):

- the **DSP effect author / host integrator** — the developer wiring a tape-dynamics effect into a plugin or embedded target;
- the **primitive consumer** — who composes the graduated stateful `Hysteresis` building block into their own processors;
- the **lab reader** — learning the Jiles-Atherton hysteresis model, the numerical-solver tradeoff, and the emergent-compression measurement.

This feature is the **capstone of phase-dynamic-systems**: it teaches **nonlinearity with memory** (Constitution Principle XI — one concept at a time), the step beyond the static waveshaper (phase-nonlinear-dsp) and beyond envelope-tracked drive (`program-dependent-saturation`). Each story below is an independently testable slice.

### User Story 1 - Process a signal through magnetic hysteresis (Priority: P1)

An effect author routes audio through a tape-dynamics effect whose core is a **Jiles-Atherton magnetic-hysteresis** transfer: the output magnetization lags the applied field, so the transfer is *not single-valued* — it traces a history-dependent loop. The author sets `drive` (how hard the signal is pushed into the magnetics), `saturation`/`ceiling`, and `width`, and hears the tape-like nonlinearity that a memoryless waveshaper cannot produce.

**Why this priority**: This is the MVP — the defining behavior of the feature and the one new concept the phase owns. Without the hysteresis core there is no feature.

**Independent Test**: Feed a sinusoid through the effect at increasing `drive`; confirm the input→output relationship is history-dependent (a rising-vs-falling asymmetry / loop), stable, and finite. Verify against the `M`-vs-`H` loop-area test in US2.

**Acceptance Scenarios**:

1. **Given** a prepared effect at a known sample rate, **When** a full-scale sinusoid is processed at moderate `drive`, **Then** the output is a smoothly saturated, band-limited signal whose transfer traces a closed hysteresis loop (rising and falling branches differ).
2. **Given** `drive` = 0 (or bypass), **When** any signal is processed, **Then** output ≈ input at unity gain (defined passthrough; no pitch/level artifacts).
3. **Given** a hot transient that would destabilize a stiff integrator, **When** it is processed, **Then** the output remains finite (no NaN/Inf) and the effect recovers to stable operation (D9 guard).

---

### User Story 2 - Consume the graduated stateful `Hysteresis` primitive and learn it in a lab (Priority: P1)

A primitive consumer composes a **stateful** `Hysteresis` primitive (Jiles-Atherton `dM/dH`) into their own processor; a lab reader opens `core/labs/tape-dynamics/` and finds the hysteresis + solver theory (README), an RT-safe kernel, and a host-only harness. The kernel graduates into `core/primitives/nonlinear/hysteresis.h` as that category's **first stateful inhabitant** (contrasted with the stateless waveshaper family).

**Why this priority**: The three-layer graduation (Constitution Principle IX) is how the reusable building block is delivered and how the effect's core kernel is proven; it is structural to the deliverable and is exercised directly by US1 (the effect composes this primitive). Co-P1 with US1.

**Independent Test**: Confirm `core/labs/tape-dynamics/` contains README + kernel + host-only harness; confirm the graduated primitive lives at `core/primitives/nonlinear/hysteresis.h`; confirm `core/primitives/README.md` lists hysteresis as an inhabited member of `nonlinear/`; confirm the portability gate passes over the new paths; drive the primitive with a sinusoid and confirm the `M`-vs-`H` trace is a **closed loop with area > 0** (a static waveshaper traces a single-valued curve, area ≈ 0).

**Acceptance Scenarios**:

1. **Given** the graduation commit, **When** the tree is inspected, **Then** `core/primitives/nonlinear/hysteresis.h` exists and `core/primitives/README.md` documents it as an inhabited member of `nonlinear/`, noting it is the category's first *stateful* member.
2. **Given** the `Hysteresis` primitive driven by a sinusoidal field `H`, **When** the resulting magnetization `M` is plotted against `H` over a full cycle, **Then** the trace is a closed loop enclosing area > 0 (memory), and the loop widens/narrows with the `k` (coercivity) parameter.
3. **Given** the primitive, **When** `reset()` is called, **Then** all runtime state (`M`, previous `H`, `dH/dt`) returns to a defined initial condition and subsequent output is reproducible.
4. **Given** the lab, **When** the README is read, **Then** it explains `dM/dH`, the Langevin anhysteretic curve, the irreversible/reversible split, the explicit-vs-implicit solver tradeoff, and why ADAA antialiasing does not apply (state carries across samples) — so oversampling is the antialiasing route.

---

### User Story 3 - Choose the numerical solver (accuracy vs CPU) (Priority: P2)

The author (or primitive consumer) selects the ODE solver used to integrate `dM/dH` — **RK2**, **RK4** (explicit), or **Newton-Raphson** (implicit) — as a runtime quality setting, trading numerical accuracy against CPU cost. The lab teaches this as a numerical-methods lesson (order of accuracy, explicit vs implicit, stability under stiffness/oversampling).

**Why this priority**: The selectable-solver surface is a core teaching axis of the feature and a real quality/CPU control for embedded targets, but the MVP (US1/US2) is viable with any single working solver. P2.

**Independent Test**: Process an identical signal under each solver at a fixed oversampling factor; confirm the resulting hysteresis loops agree within a stated tolerance and that increasing the oversampling factor tightens the agreement; confirm none diverge on a hot transient.

**Acceptance Scenarios**:

1. **Given** the `Solver` control, **When** it is set to RK2, RK4, or Newton-Raphson, **Then** the effect/primitive integrates `dM/dH` with the selected method and produces a stable, finite output.
2. **Given** a fixed input and oversampling factor, **When** the loop produced by each solver is compared, **Then** the loops agree within tolerance, and the agreement improves as the oversampling factor increases.
3. **Given** an extreme transient, **When** processed under each solver, **Then** no solver produces NaN/Inf (the D9 stability guard holds for all).

---

### User Story 4 - Hear and measure emergent dynamic compression (Priority: P2)

The author drives the signal harder and hears the material "glue"/level as tape's magnetics saturate — a **dynamic compression that emerges from the physics**, not from a control-path compressor. The lab reader measures it: the input-vs-output level curve flattens above a threshold and the dynamic range narrows with `drive`, with **no** explicit trim engaged.

**Why this priority**: This is the second concept the design pairs with hysteresis and a distinguishing teaching point, but it is a *property* of the US1 core rather than new machinery — so it rides on US1/US2. P2.

**Independent Test**: Sweep input level at fixed `drive` with the explicit trim OFF; confirm the output-vs-input curve is monotonic and compressive above a threshold, and that a dynamic-range-reduction metric increases with `drive`.

**Acceptance Scenarios**:

1. **Given** the effect with the explicit trim disabled, **When** input level is swept across the saturation region, **Then** the output level curve is monotonic and compressive above a threshold (gain reduction increases with input level).
2. **Given** two `drive` settings, **When** the dynamic-range-reduction metric is measured at each, **Then** it is larger at the higher `drive`.
3. **Given** the emergent-compression behavior, **When** the parameter set is inspected, **Then** emergent compression is exposed only through the level curve / measurement — it is **not** a host-facing parameter.

---

### User Story 5 - Drive the host-facing effect wrapper safely (Priority: P2)

The host integrator uses `TapeDynamicsEffect` through the platform `Effect` concept — `prepare(ProcessContext)` then `process(AudioBlock)` — identically to `saturation` / `compressor` / `svf`. All state is allocated in `prepare()`; `process()` is real-time safe (zero heap, lock-free, O(1) bounded).

**Why this priority**: The host-facing wrapper is how the feature is actually consumed in a plugin/embedded target and is required for any real use, but it wraps the US1 core. P2.

**Independent Test**: Instantiate the effect, `prepare()` at a sample rate/block size, process blocks of varying size including silence and full-scale; confirm no allocation/locks occur in `process()` and output is well-formed.

**Acceptance Scenarios**:

1. **Given** an unprepared effect, **When** `prepare(ProcessContext)` is called, **Then** all buffers/state are allocated and the effect is ready; no allocation occurs in any later `process()`.
2. **Given** a prepared effect, **When** `process(AudioBlock)` is called across block sizes (including 1 sample and a large block), **Then** output is continuous and click-free across block boundaries.
3. **Given** a parameter change between blocks, **When** processing resumes, **Then** the change takes effect without discontinuity artifacts inconsistent with the sibling effects' conventions.

---

### User Story 6 - Apply the optional explicit envelope-driven trim (Priority: P3)

The author enables an **optional** explicit tape-style leveling trim layered on top of the magnetics — an envelope-driven gain stage composing the shipped `EnvelopeFollower` + `GainComputer` — with `attack`, `release`, and `amount` controls. When disabled, the signal path is exactly the US1 core (the trim adds nothing).

**Why this priority**: This is the second, *optional* compression layer; the design explicitly leaves its first-cut inclusion to a `/speckit-clarify` sequencing decision (OQ2). It reuses two already-taught primitives and is not required for the MVP. P3.

**Independent Test**: With the trim enabled, confirm envelope-driven gain reduction tracks the input envelope per the attack/release controls; with the trim disabled, confirm the output is bit-for-bit the US1 core path.

**Acceptance Scenarios**:

1. **Given** the trim disabled, **When** any signal is processed, **Then** output equals the core (magnetics-only) path exactly.
2. **Given** the trim enabled with set attack/release/amount, **When** a signal with dynamics is processed, **Then** an envelope-driven gain reduction is applied whose timing follows the attack/release controls (reusing `EnvelopeFollower` + `GainComputer`).

---

### User Story 7 - Control aliasing via oversampling factor (Priority: P2)

The author selects the oversampling factor at which the magnetics run (reusing the shipped `Oversampler<Factor>`), trading alias suppression against CPU. The per-sample Jiles-Atherton integration step is evaluated at the oversampled rate.

**Why this priority**: A saturating nonlinearity aliases; controlling it is necessary for acceptable quality and is a real CPU control on embedded targets. It rides on the existing oversampler (no new machinery). P2.

**Independent Test**: Process a high-frequency tone at increasing oversampling factors; confirm aliasing (measured via the existing alias-sweep harness) decreases as the factor rises.

**Acceptance Scenarios**:

1. **Given** the oversampling factor control, **When** it is increased, **Then** aliased spectral content (alias-sweep metric) decreases.
2. **Given** the composition, **When** the effect processes a sample, **Then** the Jiles-Atherton step runs as the `evalAtHighRate` callable of `Oversampler<Factor>::process(x, eval)` (the shipped oversampler is reused verbatim).

---

### Edge Cases

- **Hot transient / stiff divergence**: a large, fast input must not blow the integrator to NaN/Inf; the state is clamped/deNaN'd to a defined stable value and recovers (D9). This holds for every solver.
- **`drive` = 0 / bypass**: defined unity passthrough, no artifacts.
- **Extreme parameters**: `Ms`/`width`/`drive` at range extremes remain finite and stable (guarded).
- **Sample-rate changes / re-prepare**: `prepare()` at a new sample rate reconfigures the integrator step size; `reset()` clears memory state so there is no carryover click.
- **Block-size variation**: correct, click-free output for 1-sample and large blocks; no allocation in `process()`.
- **Solver × oversampling interaction**: low oversampling with an aggressive solver setting must still be stable (guard), even if less accurate.

## Requirements *(mandatory)*

### Functional Requirements

**Hysteresis primitive (the stateful core)**

- **FR-001**: The system MUST provide a stateful `Hysteresis` primitive at `core/primitives/nonlinear/hysteresis.h` implementing the Jiles-Atherton `dM/dH` model: anhysteretic magnetization via the Langevin function `L(x) = coth(x) − 1/x`, plus the irreversible + reversible magnetization split.
- **FR-002**: The primitive MUST expose the five physical parameters — `Ms` (saturation magnetization / ceiling), `a` (anhysteretic shape), `α` (inter-domain coupling), `k` (coercivity → loop width / memory), `c` (reversibility → loop openness) — via per-parameter setters.
- **FR-003**: The primitive MUST hold runtime state (magnetization `M`, previous field `H`, and the field rate `dH/dt` as required by the model) and provide `reset()` to return all state to a defined initial condition and `prepare(sampleRate)` to configure the integrator step size.
- **FR-004**: The primitive MUST provide a per-sample `float process(float H)` that advances the model by one step and returns the updated magnetization-derived output.
- **FR-005**: The primitive MUST support a selectable numerical solver via a `Solver` enum with **RK2**, **RK4** (explicit), and **Newton-Raphson** (implicit) members, selectable at runtime. All three MUST be implemented in the first cut (clarified 2026-07-03, OQ1).
- **FR-006**: The primitive MUST guard the stiff-solver state: on any step that would produce a non-finite or out-of-range result, it MUST resolve to a defined, stable state (clamp/deNaN) and MUST NOT propagate NaN/Inf.
- **FR-007**: The primitive MUST hold no platform dependencies (standard library only; no JUCE/libDaisy/Teensy) and MUST be RT-safe: no heap allocation or locks in `process()`, O(1) bounded work per sample, all state preallocated in `prepare()`.

**TapeDynamicsEffect (the host-facing effect)**

- **FR-008**: The system MUST provide a `TapeDynamicsEffect` under `core/effects/tape-dynamics/` (split into core / effect / parameters / presets) conforming to the platform `Effect` concept: `prepare(ProcessContext)` and `process(AudioBlock)`.
- **FR-009**: The effect MUST run the Jiles-Atherton integration under the shipped `Oversampler<Factor>`, passing the per-sample JA step as the `evalAtHighRate` callable of `Oversampler<Factor>::process(x, eval)` — reusing the existing oversampler without modification.
- **FR-010**: The effect MUST expose host-facing macro parameters that map to the physics: `drive` (input gain into the magnetics), `saturation`/`ceiling` (→ `Ms`), `width` (→ `k`), `solver` (RK2/RK4/Newton), `oversampling` (factor select), `mix` (dry/wet), and `output` (makeup gain). The `oversampling` parameter MUST expose the factor menu **{2×, 4×, 8×}** with a default of **8×** (clarified 2026-07-03, OQ4; bounded to the shipped `Oversampler`'s supported factors). Concrete numeric ranges/mappings for the physics macros are tuned during implementation against the analysis harness (OQ3, deferred).
- **FR-011**: The effect MUST provide an **optional** explicit envelope-driven trim composing the shipped `EnvelopeFollower` and `GainComputer`, controlled by `trim.enabled`, `trim.attack`, `trim.release`, and `trim.amount`. When `trim.enabled` is false, the signal path MUST equal the magnetics-only core path exactly. This layer is **included in the first cut** (clarified 2026-07-03, OQ2).
- **FR-012**: The effect MUST NOT expose emergent dynamic compression as a parameter; it is an inherent property of the saturating magnetics, surfaced only through the level response and lab measurement.
- **FR-013**: The effect MUST provide named presets (starting points) in `tape-dynamics-presets.h`.
- **FR-014**: The effect MUST be RT-safe (FR-007 conditions) and provide a defined unity passthrough at `drive` = 0 / bypass.

**Lab & graduation (three-layer vertical, Principle IX)**

- **FR-015**: The system MUST provide a lab at `core/labs/tape-dynamics/` containing a README (theory: JA hysteresis / `dM/dH`, the Langevin anhysteretic curve, the explicit-vs-implicit solver tradeoff and order-of-accuracy/stability under oversampling, emergent compression, and why ADAA does not apply here), an RT-safe kernel that graduates into the primitive, and a **host-only** measurement harness.
- **FR-016**: On graduation, `core/primitives/README.md` MUST list `nonlinear/hysteresis.h` as an inhabited member of `nonlinear/`, explicitly noting it is the category's first *stateful* member, with its lab and consumers referenced.
- **FR-017**: The graduation MUST pass the platform portability gate over the new primitive/effect paths (platform-independent core, Principle IV).

**Validation (host-side, Principle VIII/X)**

- **FR-018**: The system MUST validate the memory property: a sinusoidally driven `Hysteresis` traces a **closed** `M`-vs-`H` loop with area > 0 (distinguishing it from a single-valued static waveshaper).
- **FR-019**: The system MUST validate solver agreement and stability: RK2/RK4/Newton converge to the same loop within a stated tolerance as the oversampling factor rises, and none diverge on a hot transient.
- **FR-020**: The system MUST validate emergent compression: with the explicit trim disabled, the output-vs-input level curve is monotonic and compressive above a threshold, and a dynamic-range-reduction metric increases with `drive`.
- **FR-021**: The system MUST validate THD/aliasing versus the oversampling factor, reusing the existing analysis harness (`host/analysis/thdn.h`, `alias-sweep.h`).
- **FR-022**: The system MUST validate passthrough and guards: `drive` = 0 / bypass ≈ unity gain, and output remains NaN/Inf-free under extreme drive and parameter sweeps.
- **FR-024**: The system MUST provide a dedicated `tests/core/hysteresis-test.cpp` unit test for the graduated primitive (in addition to the lab harness), mirroring `tests/core/delay-line-test.cpp` (clarified 2026-07-03, OQ5).

**Out of scope (explicitly deferred — MUST NOT front-run later phases)**

- **FR-023**: This feature MUST NOT implement wow/flutter (already shipped as `WowFlutterStage`; integration is `tape-machine`'s responsibility), frequency-dependent HF/gap-loss via convolution (phase-convolution), or bias modeling / tape noise / named-deck EQ curves / the full record-repro path (`tape-machine`, phase-reference-hardware).

### Key Entities

- **Hysteresis (primitive)**: the stateful Jiles-Atherton magnetic model; attributes: physical params (`Ms`, `a`, `α`, `k`, `c`), selected `Solver`, runtime state (`M`, prev `H`, `dH/dt`), sample rate. Relationship: composed by `TapeDynamicsEffect` (under the oversampler) and by external primitive consumers.
- **Solver (enum)**: the numerical integration method (RK2 / RK4 / Newton-Raphson); an accuracy-vs-CPU selector on the primitive.
- **TapeDynamicsEffect**: the host-facing effect; composes `Hysteresis` under `Oversampler<Factor>` plus the optional `EnvelopeFollower` + `GainComputer` trim; holds the macro parameter set and presets.
- **TapeDynamicsParameters**: the parameter surface (drive, saturation/ceiling, width, solver, oversampling, trim.{enabled,attack,release,amount}, mix, output).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A sinusoidally driven `Hysteresis` produces a closed `M`-vs-`H` loop enclosing area strictly greater than a defined memory-threshold, while an equivalent static waveshaper encloses area below it — the memory property is objectively demonstrated.
- **SC-002**: Across RK2/RK4/Newton at a fixed oversampling factor, the produced hysteresis loops agree within a stated tolerance, and the agreement tolerance tightens monotonically as the oversampling factor increases.
- **SC-003**: With the explicit trim disabled, the measured output-vs-input level curve is monotonic and compressive above a threshold, and the dynamic-range-reduction metric at a high `drive` exceeds that at a low `drive`.
- **SC-004**: Aliased spectral content (alias-sweep metric) at a fixed hot tone decreases monotonically as the oversampling factor increases.
- **SC-005**: Under a full parameter sweep and hot transients, the primitive and effect produce zero non-finite (NaN/Inf) output samples; at `drive` = 0 / bypass the output equals the input within unity-gain tolerance.
- **SC-006**: The three-layer graduation is complete and verifiable: `core/primitives/nonlinear/hysteresis.h` exists, `core/primitives/README.md` lists it (noting first stateful member), the lab contains README + kernel + host-only harness, and the portability gate passes over the new paths.
- **SC-007**: `process()` performs no heap allocation and takes no locks (verified host-side), consistent with the platform's RT-safety convention.

## Assumptions

- **Progressive-learning boundary (Constitution XI)**: wow/flutter, convolution-based HF/gap loss, bias, tape noise, and named-deck EQ are out of scope and belong to later phases / `tape-machine` (FR-023). The design record fixed this boundary.
- **Reuse basis (already shipped, assumed available)**: `core/primitives/oversampling/oversampler.h` (`Oversampler<Factor>::process(x, evalAtHighRate)`), `core/primitives/dynamics/envelope-follower.h`, `core/primitives/dynamics/gain-computer.h`, and the `host/analysis/` measurement harness (`thdn.h`, `alias-sweep.h`).
- **Placement**: the primitive is the first *stateful* inhabitant of `core/primitives/nonlinear/` (alongside the stateless waveshaper family); ADAA antialiasing is inapplicable because state carries across samples, so oversampling is the antialiasing route.
- **Host-side validation only**: no hardware/DAW dependency in the acceptance path (Principle VIII); DAW/hardware acceptance, if any, is a separate downstream step.
- **Parameter mapping/ranges** use reasonable musical defaults to be tuned against harness measurements during implementation (as the `saturation` voicings were), pending the clarify pass below.

### Open questions — resolved in the 2026-07-03 clarify session

All five sequencing open questions are now resolved (see `## Clarifications`); recorded here for traceability:

- **OQ1** → **Resolved**: all three solvers (RK2 + RK4 + Newton) ship in the first cut (FR-005).
- **OQ2** → **Resolved**: the optional explicit-trim layer is included in the first cut (FR-011, US6).
- **OQ3** → **Deferred to implementation**: physics-macro ranges/mappings tuned against the analysis harness (FR-010); not architecture-blocking.
- **OQ4** → **Resolved**: oversampling menu {2×, 4×, 8×}, default 8× (FR-010; 16× dropped — shipped `Oversampler` caps at Factor 8).
- **OQ5** → **Resolved**: a dedicated `tests/core/hysteresis-test.cpp` in addition to the lab harness (FR-024).

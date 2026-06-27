> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: SVF Vertical Slice — proving the acfx cross-platform spine

**Feature Branch**: `platform-foundation`

**Created**: 2026-06-25

**Status**: Draft

**Input**: User description: "acfx Milestone 1 — the vertical slice that proves the cross-platform DSP spine end-to-end using a State-Variable Filter (SVF), driven from the approved platform design at `docs/superpowers/specs/2026-06-25-acfx-platform-design.md`."

## User Scenarios & Testing *(mandatory)*

The "user" of this milestone is the **DSP author** — the person who writes an
effect once and expects it to run, unchanged, on every target. The milestone
proves the spine with a single real effect (a State-Variable Filter) so that
every effect after it is "just more of the same."

### User Story 1 - Sketch and hear an effect on the desktop (Priority: P1)

The author writes the SVF once, then opens the desktop workbench, feeds it audio,
and hears it process in real time. They adjust the filter's controls (cutoff,
resonance, mode), drive those same controls from a MIDI controller, and A/B the
processed signal against the dry signal. When they change the DSP and rebuild, the
workbench relaunches and they are hearing the new version within a few seconds.

**Why this priority**: This is the MVP and the whole point of the platform — the
fast edit→hear loop. If only this story ships, the author already has a useful,
demonstrable sketch-and-hear environment for one real effect. Everything else
(plugin, hardware) reuses the same effect and parameter declaration.

**Independent Test**: Build and launch the workbench with the SVF; route audio
(live input or the built-in player) through it; sweep cutoff, change resonance,
switch mode low→high→band; confirm the audible result matches expectation and that
A/B toggles between dry and processed. Change a DSP constant, rebuild, relaunch,
and confirm the change is audible.

**Acceptance Scenarios**:

1. **Given** the workbench is running with the SVF loaded and audio flowing, **When** the author lowers the cutoff control, **Then** high frequencies are audibly attenuated in low-pass mode and the change tracks the control in real time.
2. **Given** a MIDI controller is connected and a CC is bound to cutoff, **When** the author turns the physical knob, **Then** the workbench's cutoff control and the audible filter both follow it.
3. **Given** the author edits the SVF source and rebuilds, **When** the workbench relaunches, **Then** audio restarts and the edited behavior is in effect within a few seconds (rebuild-and-relaunch, not hot-reload).
4. **Given** the workbench exposes an A/B control, **When** the author toggles it, **Then** the output switches between the dry input and the filtered signal.

---

### User Story 2 - Run the same effect as a DAW plugin (Priority: P2)

Without changing the effect's source, the author builds it as a plugin and loads
it in a DAW. The plugin exposes the same parameters as host-automatable controls,
generated from the single parameter declaration, and processes audio identically
to the workbench.

**Why this priority**: Proves the "one core, many adapters" claim across the two
desktop adapters that share almost all their code, and delivers the first
releasable artifact shape. It depends on the same core and parameter model proven
by US1.

**Independent Test**: Build the plugin; load it in a plugin host in each exported
format; confirm the parameters appear as automatable host parameters with correct
names/ranges; automate cutoff from the host and confirm the audible sweep matches
the workbench's behavior for the same settings.

**Acceptance Scenarios**:

1. **Given** the plugin is built, **When** it is loaded in a host, **Then** it instantiates in VST3, AU, and CLAP formats and processes audio.
2. **Given** the plugin is loaded, **When** the host inspects its parameters, **Then** cutoff, resonance, and mode appear as host-automatable parameters with names, ranges, and defaults derived from the same declaration the workbench uses.
3. **Given** identical parameter settings, **When** the same audio is processed by the plugin and by the workbench, **Then** the output is equivalent (same core, same result).

---

### User Story 3 - Build the same effect for hardware (Priority: P3)

Without changing the effect's source, the author builds it for the two
microcontroller targets. The same SVF compiles and links for Daisy and for Teensy,
with the platform's parameter model mapped to physical controls (ADC pins /
encoders). On-hardware listening is a checkpoint for when a board is in hand; the
milestone bar is a clean cross-compile and link.

**Why this priority**: Makes the cross-platform claim real rather than aspirational
and validates the real-time-safety constraints (no heap allocation, no virtual
calls in the hot path) that the MCU targets demand. It reuses the exact effect and
parameter declaration from US1/US2.

**Independent Test**: Run the Daisy preset and the Teensy preset; confirm each
produces a linked firmware artifact for the same SVF source with parameter-mapping
glue present; confirm no JUCE or desktop-only code is pulled into either MCU build.

**Acceptance Scenarios**:

1. **Given** the Daisy preset, **When** the author builds, **Then** the same SVF source compiles and links into a Daisy firmware artifact with its audio callback invoking the effect and ADC/encoder inputs mapped to the parameters.
2. **Given** the Teensy preset, **When** the author builds, **Then** the same SVF source compiles and links into a Teensy artifact with analog/MIDI inputs mapped to the parameters.
3. **Given** either MCU build, **When** its dependency graph is inspected, **Then** it contains the core plus only that target's adapter — no JUCE, no desktop-only stubs.

---

### Edge Cases

- **Sample-rate / block-size change**: when the host or device re-prepares the effect at a new sample rate or maximum block size, the filter retunes correctly and `reset` clears state without artifacts carried across the change.
- **Denormals / NaN**: sustained near-silence or self-oscillation at high resonance must not produce denormal slowdowns or NaN/Inf in the output.
- **Discrete parameter bounds**: setting mode at or beyond the enum's first/last value resolves to a valid mode, never an out-of-range read.
- **Skewed parameter at extremes**: a normalized cutoff of 0.0 and 1.0 maps to the intended min/max frequency with the documented log/skew curve, with no discontinuity at the ends.
- **Channel-count mismatch**: the effect handles the channel configuration the adapter provides without runtime allocation or out-of-bounds access.
- **Audio source absent (workbench)**: if no input device or playable source is available, the workbench raises a descriptive error rather than silently feeding zeros or mock audio.

## Requirements *(mandatory)*

### Functional Requirements

**The spine (core abstractions)**

- **FR-001**: The platform MUST define an effect contract that an effect satisfies as a plain type, with operations to prepare (sample rate, max block size, channels), process a block in place, reset internal state, expose its parameter descriptors, and set a parameter by id to a normalized value.
- **FR-002**: The platform MUST provide a fixed-size audio-block abstraction passed to `process` that requires no runtime allocation to use.
- **FR-003**: An effect MUST declare its parameters exactly once as constant descriptor data — each with id, name, unit, minimum, maximum, default, and a scaling/skew curve (e.g. logarithmic for frequency) — and that single declaration MUST be the source consumed by every adapter (plugin automation, workbench GUI/MIDI, hardware control mapping).
- **FR-004**: The platform MUST provide a host-boundary adapter that lets any effect be held polymorphically by desktop hosts while confining polymorphism to at most one virtual call per audio block; the DSP within the block MUST remain non-virtual.

**The proving effect (SVF)**

- **FR-005**: The platform MUST include a State-Variable Filter effect exposing three parameters: a continuous skewed/logarithmic **cutoff**, a continuous linear **resonance**, and a discrete **mode** (low-pass / high-pass / band-pass).
- **FR-006**: The SVF MUST hold internal state across blocks and MUST clear it on `reset`/`prepare` such that retuning at a new sample rate produces correct, artifact-free output.

**Targets / adapters**

- **FR-007**: The same SVF source MUST compile, unmodified, into all four targets: desktop workbench, desktop plugin, Daisy, and Teensy.
- **FR-008**: The desktop workbench MUST provide sketch-and-hear: route audio from an input device or a built-in loop/file player through the effect, auto-render controls for the declared parameters, bind MIDI CCs to parameters, and offer dry/processed A/B.
- **FR-009**: The desktop plugin MUST export VST3, AU, and CLAP formats and MUST register host-automation parameters generated from the parameter descriptors; plugin and workbench MUST share the same core and host-boundary adapter.
- **FR-010**: The Daisy and Teensy adapters MUST each invoke the effect from the target's audio callback and map the target's physical inputs (ADC pins / encoders / analog / MIDI) to the declared parameters; neither MCU build may include desktop-only code.

**Build, test, CI**

- **FR-011**: The build system MUST provide named, one-command-per-target configurations for desktop, Daisy, Teensy, and host tests.
- **FR-012**: External dependencies MUST be pinned to exact, reproducible versions; an MCU build MUST NOT pull in the desktop plugin framework.
- **FR-013**: The platform-independent core MUST be unit-tested host-side with no hardware, covering: parameter scaling/skew curves, SVF correctness against known-good impulse/frequency-response references, and stability (no NaN/denormal blow-ups).
- **FR-014**: There MUST be an automated test asserting that `process` performs **zero heap allocations**.
- **FR-015**: Continuous integration MUST build and run the desktop and host-test configurations on every change.

**Constraints carried from the constitution**

- **FR-016**: No `process` / audio-callback path may allocate heap memory, take locks, or perform unbounded work (Constitution VI).
- **FR-017**: Outside test code, missing functionality or data MUST raise a descriptive error — no fallbacks, no mock data, no desktop-side hardware stubs (Constitution IV, V).
- **FR-018**: The core MUST compile under both C++17 and C++20; the contract is a compile-time concept where the toolchain supports it and degrades to a plain duck-typed template where it does not, never losing the code on any target.
- **FR-019**: Source files MUST stay within ~300–500 lines, split when larger (Constitution VII).

### Key Entities *(include if feature involves data)*

- **Effect**: a unit of DSP that transforms an audio block in place; satisfies the effect contract; owns its parameter declaration and internal state.
- **Parameter descriptor**: the single declared definition of one control — id, name, unit, range, default, scaling/skew — consumed by every adapter.
- **Audio block**: a fixed-size, non-allocating view of multichannel audio handed to `process`.
- **Process context**: the prepared run conditions — sample rate, maximum block size, channel count.
- **Host-boundary node**: the adapter that wraps an effect so a desktop host can hold it polymorphically at one virtual call per block.
- **Target adapter**: the thin per-target shell (workbench, plugin, Daisy, Teensy) that feeds the core audio and parameters.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The same SVF source file builds, with zero modifications, in all four target configurations.
- **SC-002**: After editing the DSP, the author hears the changed behavior in the workbench within a few seconds of starting a rebuild (rebuild-and-relaunch loop).
- **SC-003**: The automated allocation test confirms **zero** heap allocations occur during `process`.
- **SC-004**: The SVF's measured frequency response matches the known-good reference within the documented tolerance for all three modes.
- **SC-005**: Adding the SVF to any one target requires **no** change to the SVF source or its parameter declaration — only the target's adapter glue.
- **SC-006**: A single declared parameter set drives the workbench controls, the plugin's host-automation parameters, and the hardware control mapping, with consistent names, ranges, and defaults across all of them.
- **SC-007**: Each MCU build's dependency graph contains only the core plus that target's adapter (no desktop plugin framework, no desktop-only stubs).
- **SC-008**: CI builds and passes the desktop and host-test configurations on every change.

## Assumptions

- The "user" is the project author building DSP for personal use and eventual release; there is no multi-user, account, or permission dimension in this milestone.
- On-hardware audio verification (flashing and listening on a physical Daisy/Teensy) is a checkpoint performed when a board is in hand; the milestone's hardware bar is a clean cross-compile **and link**, plus the parameter-mapping glue being in place.
- The following are deferred to the planning phase rather than blocking this spec (each has a reasonable default in the approved design): the exact C++-standard level Teensy's toolchain supports; the pinned versions of the desktop framework, the DSP-primitive library, the MCU SDKs, and the test framework; whether the SVF wraps an existing primitive or is an in-house implementation; and whether the workbench's audio source is live input, a built-in player, or both.
- Out of scope for this milestone (captured for later roadmap cycles): additional effects, generators (synth/drum/sampler), an on-hardware audio-verification harness, hot-reload of the DSP core, and any branded/custom plugin GUI beyond auto-generated controls.
- This milestone runs on the existing `platform-foundation` branch with a descriptively named spec directory (`specs/svf-vertical-slice`), per Constitution Principle III — no numeric spec-dir prefix.

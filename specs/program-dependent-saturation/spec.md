> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Program-Dependent Saturation — Envelope-Modulated Nonlinearity + Dynamics-Modulator Primitive

**Feature Branch**: `program-dependent-saturation`

**Created**: 2026-07-03

**Status**: Draft

**Roadmap item**: `design:feature/program-dependent-saturation` (`depends-on: multi:feature/phase-nonlinear-dsp`; `part-of: multi:feature/phase-dynamic-systems`)

**Design record**: `docs/superpowers/specs/2026-07-02-program-dependent-saturation-design.md` (operator-approved, `design-to-spec` gate 7/7; source of truth)

**Input**: User description: "Program-dependent saturation — the first dynamic saturator of phase-dynamic-systems: the full three-layer vertical (lab + graduated stateless DynamicsModulator primitive + ProgramDependentSaturationEffect) composing the shipped, unchanged SaturationCore with the shipped EnvelopeFollower so the saturation character tracks the signal's dynamics. Capture everything the design record specifies (drive/bias/tone/mix modulation matrix; signed depth + linear/log/exp curve; feedforward+feedback; opto/vari-mu/tape-comp presets; external sidechain; sidechain HPF; stereo linking); insert no scope cuts — first-cut sequencing and the parameterization detail are a later /speckit-clarify pass." (full brief in the design record)

## Clarifications

### Session 2026-07-03

- Q: First graduated/shipped cut — which of the captured catalog lands in this feature's implementation vs stays captured-for-later? → A: Full captured catalog — all four targets (drive/bias/tone/mix), signed depth + all three curves (linear/log/exp), both topologies (feedforward/feedback), the opto/vari-mu/tape-comp presets, external sidechain, sidechain HPF, and stereo linking all land in the first cut (matching the compressors full-catalog precedent).
- Q: What units does the `DynamicsModulator` consume, and how does signed depth scale into each target's native range? → A: The `EnvelopeFollower`'s dB-domain envelope is normalized to 0..1 over a reference window (default −60..0 dBFS), and signed depth scales that normalized value into each target's native range (drive dB, bias/tone ±1, mix 0..1) via the parameter descriptor's min/max span — consistent across all four targets.
- Q: In feedback topology, which output sample does the detector read? → A: The previous **final output sample** `y` (post-mix, post-output-trim) — the only tap that keeps `SaturationCore` composed **unchanged** (it exposes no intermediate tap); this also means the feedback path inherently accounts for the oversampled tier's wet-path latency (it reads the realized output).
- Q: At what rate is the modulation applied, given `SaturationCore`'s tone setter recomputes SVF coefficients? → A: **drive/bias/mix** are modulated **per-sample** (cheap scalar setters); **tone** is modulated **per-block** (control-rate) so the SVF coefficient recompute does not run every sample — keeping the audio path MCU-viable while preserving per-sample dynamics on the cheap targets.

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is a **DSP effect author / host integrator** — the developer wiring a dynamic saturator into a plugin or embedded target — plus the **primitive consumer** who composes the graduated `DynamicsModulator` into other envelope-driven effects, and the **lab reader** learning the envelope-modulates-nonlinearity theory. Each story is an independently testable slice.

### User Story 1 - Make saturation track level with dynamic drive (Priority: P1)

An effect author instantiates a `ProgramDependentSaturationEffect`, chooses a static base voicing/drive, sets a positive **drive depth** and attack/release times, and processes an audio block. Louder input pushes the signal deeper into the nonlinearity (more harmonic saturation on transients / loud passages); quieter input stays cleaner. The dynamic character follows the detected envelope at the configured ballistics. This is the irreducible program-dependent saturator.

**Why this priority**: The MVP. A working envelope-driven **drive** modulation in the feedforward, single-detector configuration is, on its own, a usable dynamic saturator — the "hit it harder = more saturation" behavior that motivates the whole feature. Every other story extends this core.

**Independent Test**: Feed a tone stepped through levels and measure harmonic content (THD) vs input level; assert that with positive drive depth the harmonic content rises with level along the analytic `depth·curve(env)` drive-offset model, and that on a level step the drive offset reaches ~63% of its target within the configured attack time.

**Acceptance Scenarios**:

1. **Given** a dynamic saturator with a fixed base drive, positive drive depth, feedforward detection, **When** the input level rises from quiet to loud, **Then** the measured harmonic content (THD) increases monotonically with level per the analytic drive-offset model within tolerance.
2. **Given** the same configuration with **drive depth = 0**, **When** any input is applied, **Then** the output equals the static saturator at that base drive (no dynamic change) — see US3.
3. **Given** attack = 10 ms, **When** the input steps from quiet to loud, **Then** the drive offset reaches ~63% of its steady value within 10 ms (± tolerance) and recovers at the release time on the reverse step.
4. **Given** a fully-silent input, **When** processed, **Then** the output is silence with no NaN/Inf and no drift.

---

### User Story 2 - Consume the graduated DynamicsModulator primitive and learn it in a lab (Priority: P1)

A primitive consumer composes a stateless `DynamicsModulator` (envelope in → signed parameter offset out) into their own envelope-driven effect; a lab reader opens `core/labs/program-dependent-saturation/` and finds the envelope-modulates-nonlinearity theory (README), an RT-safe kernel, and a host-only harness. The kernel graduates into `core/primitives/dynamics/dynamics-modulator.h` as the category's **third inhabitant** (after `envelope-follower`, `gain-computer`).

**Why this priority**: The three-layer graduation (Principle IX) is how the reusable modulation-mapper building block is delivered and how the dynamic saturator's core kernel is proven; it is structural to the deliverable and is exercised directly by US1 (the effect composes this primitive). Co-P1 with US1 because the MVP dynamic saturator cannot exist without the modulator it composes.

**Independent Test**: Confirm `core/labs/program-dependent-saturation/` contains README + kernel + host-only harness; confirm the graduated primitive lives at `core/primitives/dynamics/dynamics-modulator.h`; confirm `core/primitives/README.md` lists the modulation mapper as inhabited; confirm the portability gate passes over both new paths; unit-test `modulate(envelope)` against the analytic `depth·curve(env)` model for each curve.

**Acceptance Scenarios**:

1. **Given** the graduation commit, **When** the tree is inspected, **Then** `core/primitives/dynamics/dynamics-modulator.h` exists and `core/primitives/README.md` documents the modulation mapper as an inhabited member of `dynamics/`.
2. **Given** the lab folder, **Then** it contains a README (envelope-modulates-nonlinearity theory), the kernel, and a host-only harness, and no portable unit includes a harness.
3. **Given** the `DynamicsModulator` primitive, **When** `modulate(envelope)` is called across an envelope sweep, **Then** it returns the analytic signed offset (`depth·curve(envelope)`) for the configured depth/curve and holds **no runtime state** (identical output for identical input, call-order independent).

---

### User Story 3 - Preserve the static saturator exactly at zero depth (orthogonality) (Priority: P1)

An author who leaves every modulation depth at zero gets **exactly** the shipped static `SaturationEffect` — same voicing, drive, tone, mix, output, bias, quality behavior. The dynamic layer is strictly additive on top of a static base; it never alters the sound until a depth is dialed in.

**Why this priority**: This orthogonality is the load-bearing contract that makes composing (not modifying) `SaturationCore` safe: the static saturator remains available untouched, and the dynamic layer cannot silently color a "clean" setting. It underwrites every other story and must hold from the MVP.

**Independent Test**: With all modulation depths = 0, feed a battery of stimuli (tones, sweeps, noise, transients) through both the `ProgramDependentSaturationEffect` and the static `SaturationEffect` configured with the same static parameters; assert the outputs are identical within a tight tolerance (byte-for-byte where the paths coincide).

**Acceptance Scenarios**:

1. **Given** all depths = 0 and matching static parameters, **When** identical input is processed by both effects, **Then** the outputs match within the stated tolerance across every stimulus.
2. **Given** a single non-zero depth on one target, **When** processed, **Then** only that target's parameter is modulated; the other three targets remain at their static base values.

---

### User Story 4 - Modulate the full matrix (drive, bias, tone, mix) (Priority: P2)

Beyond drive, the author independently modulates **bias** (level-dependent asymmetry → shifting even-harmonic content), **tone** (dynamic brightness), and **mix** (dynamic parallel blend / self-compression feel) — each an independent target with its own depth, sharing the single detected envelope.

**Why this priority**: Dynamic bias/tone/mix are the behaviors that distinguish tube/transformer/tape character from a plain dynamic clipper; the design captured all four targets. The drive-only MVP (US1) is complete on its own, so the additional targets are extensions.

**Independent Test**: For each target in {bias, tone, mix}, hold the others at depth 0, apply a level step, and assert the corresponding `SaturationCore` parameter is offset by the analytic `depth·curve(env)` amount (bias asymmetry via even-harmonic measurement; tone via spectral tilt; mix via wet/dry ratio), while the untargeted parameters stay at their static bases.

**Acceptance Scenarios**:

1. **Given** a positive bias depth and a rising level, **When** processed, **Then** the measured even-harmonic content shifts with level per the analytic bias-offset model.
2. **Given** a tone depth, **When** the level rises, **Then** the spectral tilt changes in the modeled direction (brighter or darker per the depth sign).
3. **Given** a mix depth, **When** the level rises, **Then** the wet/dry blend moves per the analytic mix-offset model.
4. **Given** independent depths on multiple targets, **When** processed, **Then** each target is offset by its own modulator from the shared envelope with no cross-talk.

---

### User Story 5 - Choose direction and response curve (signed depth, linear/log/exp) (Priority: P2)

The author sets, per target, a **signed depth** (positive = louder pushes the parameter up; negative = louder pulls it down — dynamic softening / ducking) and a **response curve** (`linear` / `logarithmic` / `exponential`) that shapes how abruptly the modulation arrives with level.

**Why this priority**: Signed depth expresses both "tube pushing" and "dynamic softening" from one mechanism, and the curve sets the character's onset; both were captured. The linear, positive-depth path (US1) is a complete processor, so direction/curve is an extension.

**Independent Test**: For a fixed envelope sweep, assert that a positive depth increases the target parameter with level and a negative depth decreases it (mirror-image offsets); and that each curve (`linear`/`log`/`exp`) produces the analytic offset shape vs envelope for the same depth.

**Acceptance Scenarios**:

1. **Given** a target with **negative** drive depth, **When** the level rises, **Then** the drive offset (and measured harmonic content) **decreases** with level — the softening/ducking behavior.
2. **Given** the same depth magnitude with opposite signs, **When** swept, **Then** the two offset trajectories are mirror images about the static base within tolerance.
3. **Given** `linear` vs `logarithmic` vs `exponential` curves at equal depth, **When** the envelope sweeps, **Then** each offset-vs-envelope curve matches its analytic shape and the three are distinguishable.

---

### User Story 6 - Choose the detection topology (feedforward vs feedback) (Priority: P2)

The author selects **feedforward** (the detector reads the input; predictable, the default) or **feedback** (the detector reads the previous post-saturation **output** sample; the smoother, self-regulating vintage tube/tape character).

**Why this priority**: Feedback is the topology behind the self-limiting "feel" of vintage-voiced saturators and was a required capture, but the feedforward P1 path is a complete dynamic saturator, so this is an extension.

**Independent Test**: For a steady input, assert the feedback topology settles to a stable steady state (the fixed point of the detector→modulation→saturation loop) and does not oscillate/diverge; compare feedforward vs feedback modulation trajectories for the same input.

**Acceptance Scenarios**:

1. **Given** feedback topology and a steady input, **When** the loop settles, **Then** the steady-state modulation is the fixed point of the output-detection loop within tolerance, and the loop is stable (no oscillation/divergence) for bounded input.
2. **Given** feedback cold start (no prior output), **When** the first sample is processed, **Then** the detector reads a defined initial output (silence/floor) — never uninitialized state.

---

### User Story 7 - Select the detector mode and ballistics (Priority: P2)

The author selects the detector mode (**peak** / **rms** / **peakHold**) and the smoother topology (**branching** / **decoupled**) and attack/release times used to derive the envelope, delegating to the composed `EnvelopeFollower`.

**Why this priority**: The detector/ballistics choice shapes how the modulation "feels" (snappy vs smooth, peak vs average), and all of it is inherited directly from the shipped `EnvelopeFollower`; a sensible default detector (US1) is a complete processor, so exposing the full detector catalog is an extension.

**Independent Test**: Assert that selecting each detector mode and ballistics topology changes the envelope (and thus the modulation) as characterized by the shipped `EnvelopeFollower` tests; assert attack/release times produce the ~63%-in-τ step response measured on the modulation.

**Acceptance Scenarios**:

1. **Given** peak vs rms detection on the same transient, **When** processed, **Then** the modulation responds faster/sharper for peak and smoother/slower for rms, matching the `EnvelopeFollower` characterization.
2. **Given** a configured attack/release, **When** a level step is applied, **Then** the modulation reaches ~63% of its steady value in the attack time and recovers at the release time within tolerance.

---

### User Story 8 - Drive the host-facing effect wrapper safely (Priority: P2)

A host integrator drives the `ProgramDependentSaturationEffect` through the standard `Effect` contract: `parameters()` exposes a single constexpr descriptor table; `setParameter(id, normalized)` is callable from any thread and is consumed lock-free on the audio thread at the top of `process()`; `prepare()`/`reset()`/`process()` are allocation-free.

**Why this priority**: The wrapper is how the effect is actually used by a host and is the idiom every shipped effect follows; it is co-required with US1 for a usable product, but the DSP correctness (US1) is the substance, so P2.

**Independent Test**: Confirm `ProgramDependentSaturationEffect` satisfies the `Effect` concept; confirm a parameter edit published from a non-audio thread takes effect on the next `process()` without a data race; confirm a no-allocation test passes across `prepare`/`process`/`reset`.

**Acceptance Scenarios**:

1. **Given** the `Effect` concept, **When** `ProgramDependentSaturationEffect` is checked against it, **Then** it satisfies every required member (`prepare`, `process`, `reset`, `parameters`, `setParameter`).
2. **Given** a `setParameter` call from a non-audio thread, **When** the next `process()` runs, **Then** the new value is applied with no lock and no torn read (lock-free atomic handoff consumed at the top of `process()`).
3. **Given** the descriptor table, **When** the build compiles, **Then** a `static_assert` rejects any malformed descriptor (e.g., a discrete parameter whose label count ≠ choice count).

---

### User Story 9 - Recall a named dynamic character (opto / vari-mu / tape-comp) (Priority: P2)

The author selects a named **dynamic-character preset** (`opto`, `variMu`, `tapeComp`, or `none`) that configures the modulation matrix (target depths, curves, topology, detector/ballistics) to a recognizable program-dependent character in one move, then optionally edits from there.

**Why this priority**: The named characters are the recognizable entry points (opto/vari-mu/tape) that motivate program-dependence and are a thin, zero-cost mapping onto the same matrix; the raw matrix (US4/US5) is complete without them, so presets are a convenience extension.

**Independent Test**: For each preset, assert the effect's realized modulation-matrix configuration equals the documented preset definition (each preset is a fixed, testable set of matrix parameters — not new DSP); assert `none` leaves the matrix at its neutral defaults.

**Acceptance Scenarios**:

1. **Given** the `opto` preset, **When** selected, **Then** the modulation matrix (depths/curves/topology/ballistics) equals the documented `opto` configuration.
2. **Given** any preset followed by a manual matrix edit, **When** the edit is applied, **Then** the edited parameter overrides the preset value (presets are a starting point, not a lock) — *(exact override semantics is a Deferred Decision)*.

---

### User Story 10 - Filter the sidechain (HPF / tilt) (Priority: P3)

The author enables a pre-detector highpass (or tilt) on the detection signal so low-frequency energy does not dominate the envelope (bass need not dictate drive). This composes the shipped `SvfPrimitive` into the detection path only; the main saturation path is unaffected.

**Why this priority**: A standard, useful control that prevents bass-driven modulation pumping, but the saturator functions without it (full-band detection), so it is an add-on.

**Independent Test**: With the sidechain HPF engaged at cutoff `fc`, apply a low-frequency tone below `fc` and assert it produces little/no modulation, whereas the same level above `fc` drives the expected modulation; confirm the main saturation path is otherwise unchanged.

**Acceptance Scenarios**:

1. **Given** a sidechain HPF at 120 Hz, **When** a 60 Hz tone is applied, **Then** the modulation is substantially less than for a 1 kHz tone at the same level.
2. **Given** the sidechain HPF at 0 Hz (bypass), **Then** detection is full-band (identical to no filter).

---

### User Story 11 - Key the saturation from an external sidechain (Priority: P3)

The author routes a separate key/sidechain signal into the detector instead of the main input, so the saturation character is driven by the key (ducking the saturation from another source) while the nonlinearity is applied to the main path.

**Why this priority**: External keying enables useful creative routing but requires an additional input the host must supply; the internal-detection dynamic saturator is complete without it.

**Independent Test**: Supply a main signal and an independent key signal; assert the modulation tracks the **key** level (not the main level) while the saturation is applied to the main path; assert that with no external key the detector falls back to the main input.

**Acceptance Scenarios**:

1. **Given** an external key that is loud while the main signal is quiet, **When** processed, **Then** the saturation character follows the key's level (driven modulation) applied to the main path.
2. **Given** no external key provided, **Then** the detector reads the main input (internal sidechain).

---

### User Story 12 - Link detection across channels (Priority: P3)

The author selects **linked** stereo/multichannel detection (a single detector value — the max across linked channels — drives a common modulation applied to all channels) versus **per-channel** (independent detection/modulation per channel). Linking keeps the dynamic character and stereo image stable.

**Why this priority**: Linking prevents image shift and character drift across channels on stereo material, but per-channel operation is a valid default and the mono case is unaffected, so this is an extension.

**Independent Test**: Feed a stereo signal with a transient in one channel only; assert linked mode applies the **same** modulation to both channels while per-channel mode modulates only the transient channel.

**Acceptance Scenarios**:

1. **Given** linked detection and a transient in the left channel only, **When** processed, **Then** both channels receive the same modulation (driven by the cross-channel max).
2. **Given** per-channel detection on the same input, **Then** only the left channel's saturation character changes.

---

### Edge Cases

- **Silence / zero input**: output is silence; no NaN/Inf; the dB-domain detection floor (−120 dBFS, inherited from `EnvelopeFollower`) prevents −∞ propagating into the modulation.
- **Zero depth on every target**: the effect is exactly the static `SaturationEffect` (US3) — the dynamic layer engages no state that alters the signal.
- **Extreme depth**: a modulated parameter offset is clamped so the resulting `drive'/bias'/tone'/mix'` stays within `SaturationCore`'s valid ranges — no out-of-range parameter, no NaN/Inf, bounded loudness (the core's gain-compensation stays engaged).
- **Feedback cold start**: the feedback detector's first sample (no prior output) uses a defined initial output (silence/floor); the loop must not read uninitialized state or diverge.
- **Feedback interaction with oversampled quality**: when the composed `SaturationCore` runs the oversampled tier (wet-path latency), the feedback tap reads a defined output sample — *(exact tap point is a Deferred Decision)* — and stays stable/bounded.
- **Response curve at envelope extremes**: `logarithmic`/`exponential` curves stay finite and bounded at envelope = 0 and at full scale (no `log(0)`/overflow) — guarded like the `EnvelopeFollower` floor.
- **Parameter change mid-stream** (depth/curve/attack/detector/…): recomputes any cached coefficients off the audio thread's pending queue without allocation; click-free enough for control-rate use; no state reset unless `reset()` is called.
- **Mono input to a linked/stereo config**: linking across one channel degenerates to per-channel (the max over one channel is itself); no special-case failure.
- **Preset then edit**: selecting a preset sets the matrix; a subsequent manual edit takes precedence — *(override semantics is a Deferred Decision)*.
- **Very short attack/release at low MCU sample rates**: envelope coefficients stay finite and bounded to `[0, 1)` (inherited `EnvelopeFollower` guards); modulation behavior characterized.

## Requirements *(mandatory)*

### Functional Requirements

**DynamicsModulator primitive (the stateless mapper)**

- **FR-001**: The system MUST provide a stateless `DynamicsModulator` primitive in `core/primitives/dynamics/dynamics-modulator.h` with per-parameter `set*` methods (depth, curve) and a pure `float modulate(float envelope) const` returning a **signed** parameter offset. It MUST hold **no runtime state**: identical inputs yield identical outputs regardless of call order.
- **FR-002**: `DynamicsModulator` MUST support a **signed depth** in `[-1, +1]` where the sign selects direction (positive = the offset grows with envelope; negative = the offset falls with envelope) and magnitude scales the offset. The modulator MUST consume a **0..1 normalized envelope** (the composed `EnvelopeFollower`'s dB-domain envelope normalized over a reference window, default −60..0 dBFS, clamped to `[0,1]`); signed depth MUST scale the shaped normalized value into the caller-supplied **native span** of the target parameter (drive dB, bias/tone ±1, mix 0..1) so a given depth reads consistently across all four targets. (Clarified 2026-07-03; the reference-window endpoints are a tunable default.)
- **FR-003**: `DynamicsModulator` MUST support an enum-selected `ModCurve` covering **linear**, **logarithmic**, and **exponential** response shaping of the (normalized) envelope→offset map; the curve MUST be bounded and allocation-free and MUST stay finite at the envelope's extremes (envelope = 0 and = 1). (The exact log/exp law and whether it is shared or per-target is a Deferred Decision.)

**ProgramDependentSaturationCore (composition kernel)**

- **FR-004**: The system MUST provide a per-channel `ProgramDependentSaturationCore` that composes the shipped, **unchanged** `SaturationCore` (the nonlinearity + its `setDrive`/`setBias`/`setTone`/`setMix`/`setVoicing` setters and static voicings), the shipped `EnvelopeFollower` (detection + ballistics + dB domain), and the shipped `SvfPrimitive` (sidechain highpass) — it MUST NOT re-derive the nonlinearity, detection, or filtering.
- **FR-005**: `ProgramDependentSaturationCore` MUST implement the per-sample signal chain: derive the detection source (external key or main input) → optional sidechain highpass → topology fork (feedforward reads the source; feedback reads the previous output) → detect the envelope via `EnvelopeFollower` → for each target compute a signed offset via a `DynamicsModulator` from the **shared** envelope → add each offset to that target's static base to form `drive'/bias'/tone'/mix'` → push those to the composed `SaturationCore` → run `SaturationCore::process(x)` → retain the output for the feedback tap.
- **FR-006**: The system MUST support modulating each of the four targets — **drive**, **bias**, **tone**, **mix** — independently, each via its own `DynamicsModulator` fed by the single shared envelope, with no cross-talk between targets.
- **FR-007**: The dynamic layer MUST be **strictly orthogonal**: with every target depth = 0, `ProgramDependentSaturationCore` MUST produce output identical (within a tight tolerance, byte-for-byte where the paths coincide) to the static `SaturationEffect`/`SaturationCore` at the same static parameters. Modulation MUST be additive on top of a static base, never a replacement of it.
- **FR-008**: The system MUST support an enum-selected `Detection` topology of **feedForward** (detector reads the input/key) and **feedBack** (detector reads the previous **final output sample `y`** — post-mix, post-output-trim, as returned by the composed `SaturationCore::process()`; Clarified 2026-07-03). Reading the realized final output is the only tap that keeps `SaturationCore` composed **unchanged** (FR-004/FR-026) and inherently accounts for the oversampled tier's wet-path latency. The feedback path MUST use a defined initial output (silence/floor) at cold start and MUST remain stable (no divergence/oscillation) for bounded input.
- **FR-009**: The system MUST support selecting the detector **mode** (peak / rms / peakHold), the **ballistics** topology (branching / decoupled), and the **attack/release** times used to derive the envelope, delegating to the composed `EnvelopeFollower`.
- **FR-010**: The system MUST clamp each modulated parameter (`drive'/bias'/tone'/mix'`) into the composed `SaturationCore`'s valid range for that parameter, so no modulation can drive an out-of-range parameter, produce NaN/Inf, or defeat the core's loudness bounding.
- **FR-010a**: The modulation MUST be applied at these rates (Clarified 2026-07-03): **drive**, **bias**, and **mix** offsets are recomputed and pushed to `SaturationCore` **per-sample** (its `setDrive`/`setBias`/`setMix` are cheap scalar setters); the **tone** offset is applied **per-block** (control-rate) so `SaturationCore::setTone`'s SVF-coefficient recompute does not run per-sample, keeping the audio path MCU-viable. Per-block tone application MUST NOT introduce audible zippering under the intended use (a smoothing/​ramp is a Deferred planning detail).
- **FR-011**: The system MUST support a pre-detector **sidechain highpass** (cutoff in Hz; 0 = bypass) applied to the detection signal only, via the composed `SvfPrimitive`, leaving the main saturation path unaffected.
- **FR-012**: The system MUST support an **external sidechain** (key) input: when supplied, detection reads the key; when absent, detection reads the main input.
- **FR-013**: The system MUST support **stereo/multichannel linking**: **linked** detection drives a single common modulation from the max detector value across linked channels; **perChannel** detection is independent per channel. Linking over a single channel degenerates to per-channel.
- **FR-014**: The system MUST support named **dynamic-character presets** (`none`, `opto`, `variMu`, `tapeComp`): each preset configures the modulation matrix (per-target depths and curves, topology, detector/ballistics) to a documented, testable configuration. A preset MUST be a convenience configuration of the existing parameters — **not** new DSP. `none` leaves the matrix at its neutral (all-depths-0) defaults.

**ProgramDependentSaturationEffect (host-facing wrapper)**

- **FR-015**: The system MUST provide a `ProgramDependentSaturationEffect` in `core/effects/program-dependent-saturation/` satisfying the `Effect` contract (`prepare`, `process`, `reset`, static `parameters()`, `setParameter`), mirroring the shipped `SaturationEffect`/`SvfEffect` idiom, with **no base class and no vtable on the audio path**.
- **FR-016**: `ProgramDependentSaturationEffect` MUST expose a single **constexpr `ParameterDescriptor` table** as the sole source of parameter truth, covering the captured set: the static passthrough {drive, voicing, tone, mix, output, bias, quality}; the detector {detector: peak|rms|peakHold, ballistics: branching|decoupled, attack, release, detection: feedForward|feedBack}; the modulation matrix {for each of drive/bias/tone/mix: a signed depth + a curve: linear|log|exp}; the character {dynamicPreset: none|opto|variMu|tapeComp}; and the sidechain/linking {externalSidechain, scHpfHz, stereoLink}. A build-time `static_assert` MUST reject any malformed descriptor.
- **FR-017**: `setParameter(id, normalized)` MUST be callable from any thread and MUST publish a **lock-free atomic** pending value consumed by the audio thread at the top of `process()`; parameter edits MUST NOT race `process()` and MUST NOT mutate core coefficients off the audio thread.
- **FR-018**: `prepare()`/`reset()` MAY mutate core coefficients directly and are NOT synchronized against `process()` (called only while the stream is stopped, per the established effect lifecycle).

**Real-time safety & portability**

- **FR-019**: No audio-path method (`process`) MUST perform heap allocation, take locks, or do unbounded work; all coefficients MUST recompute in `set*`/`prepare`. The `DynamicsModulator` MUST be bounded arithmetic; the only per-sample transcendentals (`sqrt`/`log`) MUST stay confined to the composed `EnvelopeFollower`.
- **FR-020**: The `DynamicsModulator` primitive, the `ProgramDependentSaturationCore`, the lab kernel, and the `ProgramDependentSaturationEffect` MUST compile and run with no JUCE / libDaisy / Teensy knowledge and MUST be suitable for MCU targets; the design MUST use a **single shared detector** feeding N stateless mappers (not N detectors) to stay MCU-viable.
- **FR-021**: All parameter setters MUST guard degenerate inputs (out-of-range ids, non-positive times/cutoff, out-of-range depths) so that no `process()` output is NaN/Inf and every internal coefficient stays within its valid bound.

**Structure, graduation & gating**

- **FR-022**: The concept MUST be authored as `core/labs/program-dependent-saturation/` (README envelope-modulates-nonlinearity theory + RT-safe kernel + host-only harness) and the stateless modulation mapper MUST then be graduated (`git mv` the kernel) into `core/primitives/dynamics/dynamics-modulator.h`, refining in place — never re-derived.
- **FR-023**: The graduated `dynamics-modulator.h` MUST be added to the existing `core/primitives/dynamics/` category and `core/primitives/README.md` MUST document the modulation mapper as an inhabited member (moved from prospectus) in the same atomic commit as the graduation.
- **FR-024**: `scripts/check-portability.sh` (run in CI, never as a git hook) MUST be extended to cover `core/labs/program-dependent-saturation/**`, `core/primitives/dynamics/dynamics-modulator.h`, and `core/effects/program-dependent-saturation/**` for harness-isolation, dependency-direction, platform-independence, and file-size checks.
- **FR-025**: Every source file introduced MUST stay within ~300–500 lines and use strict typing (no unchecked casts). If the captured parameter set pushes `program-dependent-saturation-effect.h` past the guideline, the wrapper MUST be split into cooperating files (e.g. a parameter-table header + an apply/denormalize header) rather than exceeding the limit.

**Scope boundary (Principle XI — one concept at a time)**

- **FR-026**: The feature MUST deliver the generic **envelope-modulates-nonlinearity** mechanism: the `DynamicsModulator` primitive, the program-dependent-saturation lab, and the `ProgramDependentSaturationEffect` composing the **unchanged** `SaturationCore`. The **physical tape magnetic model** (hysteresis / Jiles-Atherton, tape bias interactions, wow/flutter dynamics), **multiband dynamic saturation**, and **dynamic EQ** MUST NOT be implemented here — they are owned by sibling/future items (`design:feature/tape-dynamics` and future multiband/dynamic-EQ items). This feature MUST add **no new nonlinearity kernel**: `SaturationCore` is composed as-is.

### Key Entities

- **DynamicsModulator**: the stateless modulation-mapper primitive. Attributes: signed depth `[-1,+1]`, response curve (linear/log/exp). Behavior: `modulate(envelope) → signed parameter offset`. No runtime state. Composed by envelope-driven effects.
- **ProgramDependentSaturationCore**: the per-channel composition kernel. Composes the unchanged `SaturationCore`, an `EnvelopeFollower`, an optional `SvfPrimitive` (sidechain HPF), and four `DynamicsModulator` instances (drive/bias/tone/mix targets); holds detection topology, per-channel runtime state, and the feedback tap.
- **ProgramDependentSaturationEffect**: the host-facing wrapper. Owns per-channel `ProgramDependentSaturationCore` state, the constexpr parameter descriptor table, and the lock-free cross-thread parameter handoff.
- **ModCurve**: enumerated response curve — linear, logarithmic, exponential.
- **Detection**: enumerated detection topology — feedForward, feedBack.
- **ModTarget**: the four modulated `SaturationCore` parameters — drive, bias, tone, mix.
- **DynamicPreset**: enumerated named character — none, opto, variMu, tapeComp.
- **StereoLink**: enumerated channel-linking mode — perChannel, linked.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For a tone stepped across levels with a positive **drive** depth, the measured harmonic content (THD) rises with input level along the analytic `depth·curve(env)` drive-offset model within a stated tolerance (US1).
- **SC-002**: With **all modulation depths = 0**, the `ProgramDependentSaturationEffect` output matches the static `SaturationEffect` at the same static parameters within a tight tolerance (byte-for-byte where the paths coincide) across a battery of stimuli (the orthogonality contract, US3).
- **SC-003**: For each target {drive, bias, tone, mix}, a non-zero depth offsets **only** that target's `SaturationCore` parameter by the analytic `depth·curve(env)` amount, with the other three targets unchanged (US4).
- **SC-004**: A **negative** depth decreases the target parameter with rising level (mirror-image of the positive-depth trajectory within tolerance), and the three response curves (linear/log/exp) each match their analytic offset-vs-envelope shape and are distinguishable (US5).
- **SC-005**: For a level step, the modulation reaches 1 − 1/e (~63%) of its steady value within the configured attack time and recovers at the release time, within tolerance, across detector modes (US1/US7).
- **SC-006**: In **feedback** topology, a steady input settles to the analytic detector→modulation→saturation fixed point within tolerance, and the loop is stable (no oscillation/divergence) for bounded input, including a defined cold start (US6).
- **SC-007**: The `DynamicsModulator` primitive returns the analytic signed offset for each curve and holds no runtime state (identical output for identical input, call-order independent) (US2).
- **SC-008**: Each named preset (`opto`/`variMu`/`tapeComp`) realizes its documented modulation-matrix configuration; `none` leaves the matrix at all-depths-0 defaults (US9).
- **SC-009**: With the sidechain HPF engaged at `fc`, a tone below `fc` produces substantially less modulation than a tone above `fc` at the same level; at 0 Hz detection is full-band (US10).
- **SC-010**: With an external key supplied, the modulation tracks the key level (not the main level) while the saturation is applied to the main path; with no key, detection reads the main input (US11).
- **SC-011**: Linked detection applies the same modulation to all linked channels (driven by the cross-channel max), keeping a one-channel transient's character/image stable; per-channel detection modulates only the affected channel (US12).
- **SC-012**: `ProgramDependentSaturationEffect` satisfies the `Effect` concept; a parameter edit from a non-audio thread is applied on the next `process()` with no lock or torn read; a malformed descriptor fails the build via `static_assert` (US8).
- **SC-013**: A no-allocation test confirms zero heap allocation on the `process()` path (after `prepare()`) across all targets, curves, topologies, and feature combinations.
- **SC-014**: No input (silence, DC, impulse, level step, feedback cold start, extreme depth, low-sample-rate short-τ) produces a NaN/Inf output in any configuration, and every modulated parameter stays within `SaturationCore`'s valid range.
- **SC-015**: The portability gate passes over `core/labs/program-dependent-saturation/**`, `core/primitives/dynamics/dynamics-modulator.h`, and `core/effects/program-dependent-saturation/**` (harness isolation, dependency direction, platform independence, file size), the modulation mapper is documented as inhabited in `core/primitives/README.md`, and no portable unit includes a harness.
- **SC-016**: The `DynamicsModulator` graduates via `git mv` (lab → primitive) with the README + kernel + host-only harness present in the lab, in a single atomic commit with the README-inhabited update.

## Assumptions

- **Consumers call `process()` per block** through the `Effect` contract (the established idiom); `ProgramDependentSaturationCore::process()` is the per-sample loop body — matching `SaturationEffect`/`SvfEffect`.
- **The composed units are shipped and reused as-is, unchanged**: `SaturationCore` (`core/effects/saturation/saturation-core.h`), `EnvelopeFollower` (`core/primitives/dynamics/envelope-follower.h`), and `SvfPrimitive` (`core/primitives/filters/svf-primitive.h`). Detection ballistics, dB-domain handling, and the −120 dBFS floor are inherited from `EnvelopeFollower`; the nonlinearity, voicings, gain-compensation, and mix/output stages are inherited from `SaturationCore`.
- **A single shared detector feeds all four modulators** (MCU-viable); per-band detection (multiband) is out of scope for this feature (FR-026).
- **The full captured catalog IS the first-cut scope** (Clarified 2026-07-03): all four targets (drive/bias/tone/mix), signed depth + all three curves, both topologies (feedforward/feedback), the opto/vari-mu/tape-comp presets, external sidechain, sidechain HPF, and stereo linking all land in this feature's implementation — nothing is deferred to a follow-on, matching the compressors full-catalog precedent.
- **Validation reuses the shipped measurement (stimulus/response) and harmonic-analysis infrastructure** (`tests/core/measurement-*`, `measurement-support.h`, `tests/support/svf-reference.h`) for orthogonality, modulation-offset, THD-vs-level, step-response, and feedback-convergence assertions, following the `svf-reference` named-tolerance pattern.
- **Default configuration** on `prepare()` is the neutral static saturator: `dynamicPreset = none`, all target depths = 0, feedforward detection, a default detector mode/ballistics, no sidechain HPF, no external key, per-channel linking — so a host that sets no dynamic parameters gets exactly the static saturator (US3), and dialing a single depth introduces dynamics.
- **The static parameters are a per-target "base" the modulation offsets from**; the modulated value pushed to `SaturationCore` is `base + offset`, clamped to the core's valid range (FR-010).
- **Dependency**: `multi:feature/phase-nonlinear-dsp` is complete; the `SaturationCore`, `EnvelopeFollower`, and `SvfPrimitive` units, the measurement + harmonic-analysis infrastructure, and the three-layer/portability tooling are shipped.

## Deferred Decisions *(for `/speckit-clarify` and `/speckit-plan`)*

Four of the design-record open questions were resolved in the 2026-07-03 clarification session (see
`## Clarifications` and the folded FR-002/FR-008/FR-010a and Assumptions updates: first-cut = full
catalog, modulator consumes a normalized-dB-window envelope scaled by depth into each target's native
span, feedback tap = final output `y`, and drive/bias/mix modulate per-sample while tone modulates
per-block). The remaining items are parameterization/structure detail parked for `/speckit-plan` —
**captured, not discarded, and not scope-affecting**; the FRs above cover the full captured capability:

- **Response-curve law.** The exact `logarithmic`/`exponential` curve forms, whether they are shared across targets or per-target, and whether an optional modulation attack/release asymmetry (beyond the `EnvelopeFollower`'s own) is warranted.
- **Preset definitions.** The concrete matrix configurations for `opto`/`variMu`/`tapeComp` (which targets, depths, curves, topology, detector/ballistics) and whether a preset is a hard selection or an editable starting point (the US9 override semantics).
- **Reference-window endpoints.** The default −60..0 dBFS normalization window is a tunable default (FR-002); whether it is fixed, per-target, or a user parameter is a planning/tuning decision.
- **Per-block tone smoothing.** Whether the per-block tone application (FR-010a) needs an explicit ramp/smoother to avoid zippering on fast material, or whether block granularity is sufficient.
- **Stereo-link detail.** Whether linking is max-only or also offers sum/average, and whether the sidechain HPF / external key is per-channel or shared across a linked group.
- **Static-parameter application seam.** The exact cheap fast-path by which per-sample drive/bias/mix offsets reach `SaturationCore` without redundant coefficient work — an implementation-structure detail flowing from FR-010a.
- **Effect-wrapper file split.** Whether the large captured parameter set pushes `program-dependent-saturation-effect.h` past the ~300–500 line guideline and how it splits (parameter-table header + apply/denormalize header) — an FR-025 planning decision.

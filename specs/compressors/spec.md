> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Compressors — Gain Computer + Compressor Effect

**Feature Branch**: `compressors`

**Created**: 2026-07-02

**Status**: Draft

**Roadmap item**: `design:feature/compressors` (`depends-on: multi:feature/phase-nonlinear-dsp`; `part-of: multi:feature/phase-dynamic-systems`)

**Design record**: `docs/superpowers/specs/2026-07-02-compressors-design.md` (operator-approved; source of truth)

**Input**: User description: "Compressors — a dynamics processor for the acfx platform-independent DSP core: the full three-layer vertical (lab + graduated stateless GainComputer primitive + CompressorEffect composing the shipped EnvelopeFollower, SvfPrimitive, and DelayLine). Capture everything the design record specifies (compress/limit/expand/gate; feedforward+feedback; level+gain ballistics sites; external sidechain; sidechain HPF; lookahead; auto-makeup; stereo linking); insert no scope cuts — first-cut sequencing is a later /speckit-clarify pass." (full brief in the design record)

## Clarifications

### Session 2026-07-02

- Q: First graduated/shipped cut — which of the captured catalog lands in this feature's implementation vs stays captured-for-later? → A: Full captured catalog — all modes (compress/limit/expand/gate), both topologies (feedforward/feedback), both ballistics sites (level/gain), external sidechain, sidechain HPF, lookahead, auto-makeup, and stereo linking all land in the first cut (matching the envelope-followers full-catalog precedent).
- Q: Feedback topology — which signal should the feedback detector read? → A: The post-makeup, pre-mix compressed signal — after the gain multiply and makeup, but before the dry/wet mix and output trim (so mix and output trim stay outside the detection loop).
- Q: Soft-knee formula — one form across all modes, or per-mode? → A: A single unified quadratic (C¹-continuous) knee straddling the threshold, applied by the same machinery in every mode (above-threshold for compress/limit, below-threshold for expand/gate), reducing exactly to the hard knee at knee = 0.
- Q: Auto-makeup — what reference model derives the makeup gain? → A: Closed-form `makeup = −computeGainDb(0 dBFS)` (compensate the reduction the curve applies at a 0 dBFS reference), recomputed once per parameter change (no runtime tracking); auto-makeup is 0 for expand/gate (downward-only).

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is a **DSP effect author / host integrator** — the developer wiring a compressor into a plugin or embedded target — and the **primitive consumer** who composes the graduated `GainComputer` into other dynamics processors, plus the **lab reader** learning the gain-computer and topology theory. Each story is an independently testable slice.

### User Story 1 - Compress a signal above a threshold (Priority: P1)

An effect author instantiates a `CompressorEffect`, sets a threshold, a ratio, and attack/release times, and processes an audio block. Signal above the threshold is attenuated by the ratio (with a configurable soft/hard knee) at the configured attack/release ballistics; signal below the threshold passes unchanged. This is the irreducible downward compressor.

**Why this priority**: The MVP. A working downward compressor (threshold + ratio + knee + attack/release) in the feedforward, level-smoothed configuration is, on its own, a usable, shippable dynamics processor. Every other story extends this core.

**Independent Test**: Feed a steady tone stepped through levels crossing the threshold; assert the measured input→output level map matches the analytic static curve (unity below threshold; `thr + (level−thr)/ratio` above) within tolerance, and that a step above threshold reaches ~63% of its target gain reduction within the configured attack time.

**Acceptance Scenarios**:

1. **Given** a compressor with threshold −20 dBFS, ratio 4:1, hard knee, **When** a steady −10 dBFS tone is applied, **Then** the output level settles to −17.5 dBFS (−20 + (−10−−20)/4) within tolerance.
2. **Given** the same compressor, **When** a steady −30 dBFS tone (below threshold) is applied, **Then** the output equals the input within tolerance (no gain reduction).
3. **Given** attack = 10 ms, **When** the input steps from below to above threshold, **Then** the gain reduction reaches ~63% of its steady value within 10 ms (± tolerance); on the reverse step it recovers at the release time.
4. **Given** a fully-silent input, **When** processed, **Then** the output is silence with no NaN/Inf and no drift.

---

### User Story 2 - Consume the graduated GainComputer primitive and learn it in a lab (Priority: P1)

A primitive consumer composes a stateless `GainComputer` (the static curve: level in → gain-reduction-dB out) into their own dynamics processor; a lab reader opens `core/labs/compressor/` and finds the gain-computer and topology theory (README), an RT-safe kernel, and a host-only harness. The kernel graduates into `core/primitives/dynamics/gain-computer.h` as the category's **second inhabitant**.

**Why this priority**: The three-layer graduation (Principle IX) is how the reusable static-curve building block is delivered and how the compressor's core kernel is proven; it is structural to the deliverable and is exercised directly by US1 (the effect composes this primitive). Co-P1 with US1 because the MVP compressor cannot exist without the gain computer it composes.

**Independent Test**: Confirm `core/labs/compressor/` contains README + kernel + host-only harness; confirm the graduated primitive lives at `core/primitives/dynamics/gain-computer.h`; confirm `core/primitives/README.md` lists "gain computers" as inhabited; confirm the portability gate passes over both new paths; unit-test `computeGainDb(levelDb)` against the analytic curve for each mode.

**Acceptance Scenarios**:

1. **Given** the graduation commit, **When** the tree is inspected, **Then** `core/primitives/dynamics/gain-computer.h` exists and `core/primitives/README.md` documents the gain computer as an inhabited member of `dynamics/`.
2. **Given** the lab folder, **Then** it contains a README (gain-computer/topology theory), the kernel, and a host-only harness, and no portable unit includes a harness.
3. **Given** the `GainComputer` primitive, **When** `computeGainDb(levelDb)` is called across a level sweep, **Then** it returns the analytic gain reduction (≤ 0 dB) for the configured mode/threshold/ratio/knee/range and holds **no runtime state** (identical output for identical input, call order independent).

---

### User Story 3 - Limit with a brickwall ratio (Priority: P2)

The author selects **limit** mode (or equivalently the ratio → ∞ end of the range); output above the threshold is held at the threshold (within the knee), giving brickwall limiting behavior.

**Why this priority**: Limiting is the second workhorse dynamics behavior and is the same static-curve machinery as compression at an extreme ratio; the P1 compressor already works, so this is an extension.

**Independent Test**: Drive levels well above threshold; assert the output level asymptotes to the threshold (within the knee-rounded curve) rather than continuing to rise.

**Acceptance Scenarios**:

1. **Given** limit mode with threshold −6 dBFS, **When** a −1 dBFS tone is applied, **Then** the output settles at ≈ −6 dBFS within tolerance.
2. **Given** limit mode, **When** the input sits at exactly the threshold, **Then** the gain reduction is ≈ 0 dB (continuous with the below-threshold region).

---

### User Story 4 - Shape a soft knee (Priority: P2)

The author sets a non-zero knee width; the transition from unity gain to full ratio is a smooth (C¹-continuous) curve straddling the threshold rather than a hard corner. A zero knee gives the hard-corner curve of US1.

**Why this priority**: The soft knee is standard compressor voicing and materially changes the sound near the threshold, yet the hard-knee curve (US1) is a complete processor on its own.

**Independent Test**: Sweep input level across the knee region; assert the input→output curve is continuous and has a continuous first derivative (no corner) across the full knee width, and that it reduces to the hard-knee curve when knee = 0.

**Acceptance Scenarios**:

1. **Given** a soft knee of width W around the threshold, **When** the level sweeps across `[thr − W/2, thr + W/2]`, **Then** the gain-reduction curve is C¹-continuous (no discontinuity in value or slope).
2. **Given** knee = 0, **When** the same sweep is applied, **Then** the curve matches the hard-knee analytic curve exactly.

---

### User Story 5 - Choose the detection topology (feedforward vs feedback) (Priority: P2)

The author selects **feedforward** (the detector reads the input; predictable, the default) or **feedback** (the detector reads the previous **output** sample, post-gain-reduction; the smoother, self-regulating vintage character).

**Why this priority**: Feedback is the topology behind classic optical/FET compressor "feel" and is a required capture, but the feedforward P1 path is a complete compressor, so this is an extension.

**Independent Test**: For a steady input above threshold, assert the feedback topology settles to the analytic feedback fixed point (where detected-output level maps through the curve back to the applied gain) and that this converges to a stable steady state; compare the feedforward and feedback gain-reduction curves for the same input.

**Acceptance Scenarios**:

1. **Given** feedback topology and a steady above-threshold input, **When** the loop settles, **Then** the steady-state output level is the fixed point of the detector→curve→gain loop within tolerance, and the loop is stable (no oscillation/divergence).
2. **Given** feedforward vs feedback on the same input, **Then** the two produce the characterized (different) gain-reduction trajectories, each matching its analytic model.

---

### User Story 6 - Choose the ballistics site (level-smoothed vs gain-smoothed) (Priority: P2)

The author selects where the attack/release ballistics act: **level** (smooth the detected level before the static curve — the `EnvelopeFollower` supplies the ballistics; the curve is instantaneous) or **gain** (the static curve maps the ~instantaneous level, then a second smoother applies attack/release to the **gain-reduction** signal).

**Why this priority**: The two sites voice audibly differently and the design captured both; the level site is the P1 default (direct reuse of the `EnvelopeFollower`'s ballistics), so the gain site is the extension.

**Independent Test**: Apply a step above threshold; assert that in each site the gain reduction reaches ~63% of its steady value in the configured attack time (measured on the level signal for the level site, on the gain-reduction signal for the gain site), matching the site's analytic model.

**Acceptance Scenarios**:

1. **Given** the level site, **When** a step above threshold is applied, **Then** the smoothed **level** reaches ~63% of its target in the attack time and the instantaneous curve maps it to gain.
2. **Given** the gain site, **When** the same step is applied, **Then** the **gain-reduction** signal reaches ~63% of its steady value in the attack time while the curve itself runs on the ~instantaneous level.

---

### User Story 7 - Expand and gate below a threshold (Priority: P3)

The author selects **expand** (downward expansion below threshold by the ratio) or **gate** (attenuate toward a `range` floor below threshold). These are the below-threshold reflections of the static curve, with the same knee machinery.

**Why this priority**: Expansion/gating round out the dynamics catalog and reuse the same curve/knee/range machinery, but compressors/limiters (US1/US3) are the primary deliverable, so these follow.

**Independent Test**: Sweep input below threshold; assert expand attenuates by the configured ratio down to the `range` floor, and gate attenuates to the `range` floor below the (knee-rounded) threshold; assert both pass unity above threshold.

**Acceptance Scenarios**:

1. **Given** expand mode, threshold −40 dBFS, ratio 2:1, range −20 dB, **When** a −50 dBFS tone is applied, **Then** the output is attenuated per the downward-expansion curve, not exceeding the −20 dB range floor.
2. **Given** gate mode with a range floor, **When** the input falls below the threshold minus the knee, **Then** the output is attenuated to the range floor; **When** it rises above threshold, **Then** the output passes at unity.

---

### User Story 8 - Filter the sidechain (HPF / tilt) (Priority: P3)

The author enables a pre-detector highpass (or tilt) on the sidechain so low-frequency energy does not dominate detection (the classic bus-compressor "sidechain HPF"). This composes the shipped `SvfPrimitive` into the detection path only; the main path is unaffected.

**Why this priority**: A standard, useful control that prevents bass-driven pumping, but the compressor functions without it (full-band detection), so it is an add-on.

**Independent Test**: With the sidechain HPF engaged at cutoff `fc`, drive a low-frequency tone below `fc` above the nominal threshold and assert it produces little/no gain reduction, whereas the same level above `fc` triggers the expected reduction; confirm the main-path signal is otherwise unchanged.

**Acceptance Scenarios**:

1. **Given** a sidechain HPF at 120 Hz, **When** a 60 Hz tone above threshold is applied, **Then** the gain reduction is substantially less than for a 1 kHz tone at the same level.
2. **Given** the sidechain HPF at 0 Hz (bypass), **Then** detection is full-band (identical to no filter).

---

### User Story 9 - Key the compressor from an external sidechain (Priority: P3)

The author routes a separate key/sidechain signal into the detector instead of the main input (ducking, de-essing, keyed gating). Gain reduction is driven by the key while the gain is applied to the main path.

**Why this priority**: External keying enables important use cases (ducking/de-essing) but requires an additional input the host must supply; the internal-detection compressor is complete without it.

**Independent Test**: Supply a main signal and an independent key signal; assert gain reduction tracks the **key** level (not the main level) and the reduction is applied to the main path; assert that with no external key the detector falls back to the main input.

**Acceptance Scenarios**:

1. **Given** an external key above threshold and a quiet main signal, **When** processed, **Then** the main signal is attenuated according to the key's level.
2. **Given** no external key provided, **Then** the detector reads the main input (internal sidechain).

---

### User Story 10 - Look ahead to catch transients (Priority: P3)

The author enables lookahead; the main path is delayed by N samples so the detector "sees ahead" and the gain reduction is in place before a fast transient arrives (brickwall limiting). The introduced latency is reported to the host.

**Why this priority**: Lookahead is essential for transparent peak limiting but adds latency and buffering the host must account for; the zero-latency compressor is the default.

**Independent Test**: Enable a lookahead of L ms; assert the reported latency equals `round(L·fs)` samples and that a fast transient is attenuated from its very first sample (no overshoot past the threshold that a zero-lookahead limiter would let through).

**Acceptance Scenarios**:

1. **Given** lookahead L ms, **When** the effect is prepared, **Then** the reported latency equals `round(L·fs)` samples.
2. **Given** lookahead engaged in limit mode, **When** an instantaneous transient above threshold arrives, **Then** it is limited from its first sample (the pre-delay lets the detector act ahead of it).

---

### User Story 11 - Make up gain (manual and auto) and blend in parallel (Priority: P3)

The author applies makeup gain (manual dB, or **auto** derived from threshold/ratio so perceived level stays roughly constant), a dry/wet **mix** for parallel ("New York") compression, and a final output trim.

**Why this priority**: Makeup and parallel mix are standard finishing controls that improve usability but are conveniences over the core gain reduction; the compressor reduces gain correctly without them.

**Independent Test**: With auto-makeup on, assert the effective makeup equals the closed-form `−computeGainDb(0 dBFS)` and is applied as a **constant** gain (so a below-threshold signal is lifted by that makeup amount — the standard constant-makeup behavior, not gated to only the compressed region); with manual makeup of M dB assert the output rises by M dB; with mix = 0 assert full dry passthrough and mix = 1 assert fully compressed.

**Acceptance Scenarios**:

1. **Given** manual makeup M dB, **When** applied, **Then** the output level is M dB above the un-made-up compressed level.
2. **Given** auto-makeup, **When** a below-threshold signal is processed, **Then** the output is lifted by the constant closed-form makeup gain `−computeGainDb(0 dBFS)` (a constant makeup gain compensates the reduction at the 0 dBFS reference and is applied uniformly, so below-threshold content is boosted by that amount — the standard makeup-gain behavior; it is NOT gated to unity below threshold). *(Reconciled during implementation 2026-07-02: the original "≈ unity below threshold" wording contradicted the operator's chosen closed-form constant-makeup model, which mathematically lifts all signal; the model is authoritative and the acceptance is corrected to match it.)*
3. **Given** mix = 0, **Then** the output equals the dry input; **Given** mix = 1, **Then** the output is the fully compressed signal.

---

### User Story 12 - Link detection across channels (Priority: P3)

The author selects **linked** stereo/multichannel detection (a single detector value — the max across linked channels — drives a common gain applied to all channels) versus **per-channel** (independent detection per channel). Linking keeps the stereo image stable under gain reduction.

**Why this priority**: Stereo linking prevents image shift on stereo material and is expected on bus compressors, but per-channel operation is a valid default and the mono case is unaffected, so this is an extension.

**Independent Test**: Feed a stereo signal with a transient in one channel only; assert linked mode applies the **same** gain to both channels (image stable) while per-channel mode attenuates only the transient channel.

**Acceptance Scenarios**:

1. **Given** linked detection and a transient in the left channel only, **When** processed, **Then** both channels receive the same gain reduction (driven by the max across channels).
2. **Given** per-channel detection on the same input, **Then** only the left channel is attenuated.

---

### User Story 13 - Drive the host-facing effect wrapper safely (Priority: P2)

A host integrator drives the `CompressorEffect` through the standard `Effect` contract: `parameters()` exposes a single constexpr descriptor table; `setParameter(id, normalized)` is callable from any thread and is consumed lock-free on the audio thread at the top of `process()`; `prepare()`/`reset()`/`process()` are allocation-free.

**Why this priority**: The wrapper is how the compressor is actually used by a host and is the idiom every shipped effect follows; it is co-required with US1 for a usable product, but the DSP correctness (US1) is the substance, so P2.

**Independent Test**: Confirm `CompressorEffect` satisfies the `Effect` concept; confirm a parameter edit published from a non-audio thread takes effect on the next `process()` without a data race; confirm a no-allocation test passes across `prepare`/`process`/`reset`.

**Acceptance Scenarios**:

1. **Given** the `Effect` concept, **When** `CompressorEffect` is checked against it, **Then** it satisfies every required member (`prepare`, `process`, `reset`, `parameters`, `setParameter`).
2. **Given** a `setParameter` call from a non-audio thread, **When** the next `process()` runs, **Then** the new value is applied with no lock and no torn read (lock-free atomic handoff, values consumed at the top of `process()`).
3. **Given** the descriptor table, **When** the build compiles, **Then** a `static_assert` rejects any malformed descriptor (e.g., a discrete parameter whose label count ≠ choice count).

---

### Edge Cases

- **Silence / zero input**: output is silence; no NaN/Inf; the dB-domain detection floor (−120 dBFS, inherited from `EnvelopeFollower`) prevents −∞ in the level→gain map.
- **Input exactly at threshold**: gain reduction is continuous (≈ 0 dB just below, smoothly increasing above); no discontinuity, hard or soft knee.
- **Ratio at extremes**: ratio 1:1 → no compression (unity curve); ratio → ∞ / limit mode → brickwall at threshold; the curve stays finite and monotone.
- **Feedback cold start**: the feedback detector's first sample (no prior output) uses a defined initial output (silence/floor); the loop must not read uninitialized state or diverge.
- **Lookahead = 0**: zero added latency, no delay buffer engaged; reported latency is 0.
- **Auto-makeup with expander/gate modes**: auto-makeup is 0 (disabled) for expand/gate — it is defined only for downward compression/limiting (FR-016) — never undefined.
- **Sidechain HPF at/above Nyquist or ≤ 0 Hz**: guarded to a defined, stable filter state (bypass at ≤ 0; clamp near Nyquist) — no NaN/Inf, consistent with `SvfPrimitive` guards.
- **Parameter change mid-stream** (threshold/ratio/attack/…): recomputes coefficients off the audio thread's pending-queue without allocation; click-free enough for control-rate use; no state reset unless `reset()` is called.
- **Mono input to a linked/stereo config**: linking across one channel degenerates to per-channel (the max over one channel is itself); no special-case failure.
- **Very short attack/release at low MCU sample rates**: coefficients stay finite and bounded to `[0, 1)` (inherited `EnvelopeFollower` / one-pole guards); behavior characterized.

## Requirements *(mandatory)*

### Functional Requirements

**GainComputer primitive (the static curve)**

- **FR-001**: The system MUST provide a stateless `GainComputer` primitive in `core/primitives/dynamics/gain-computer.h` with per-parameter `set*` methods (mode, threshold, ratio, knee, range) and a pure `float computeGainDb(float levelDb) const` that returns the gain change in dB (≤ 0 = attenuation). It MUST hold **no runtime state**: identical inputs yield identical outputs regardless of call order.
- **FR-002**: `GainComputer` MUST support an enum-selected `GainMode` covering **compress**, **limit**, **expand**, and **gate**.
- **FR-003**: In **compress** mode above threshold the curve MUST be `out = thr + (level − thr)/ratio` (gain reduction = `out − level`); below threshold the gain change MUST be 0 dB (unity).
- **FR-004**: In **limit** mode the curve MUST behave as ratio → ∞ (output held at the threshold within the knee).
- **FR-005**: In **expand** mode below threshold the curve MUST apply downward expansion by the ratio, bounded by the `range` floor (maximum attenuation); above threshold it MUST be unity.
- **FR-006**: In **gate** mode below the (knee-rounded) threshold the curve MUST attenuate toward the `range` floor; above threshold it MUST be unity.
- **FR-007**: The system MUST support a configurable **knee** width via a **single unified quadratic (C¹-continuous) knee** straddling the threshold, applied by the same machinery in every mode (above-threshold for compress/limit, below-threshold for expand/gate): knee = 0 gives a hard corner; knee > 0 gives a value- and slope-continuous transition, reducing exactly to the hard-knee curve as knee → 0. (Clarified 2026-07-02 — one form across all modes, not per-mode.)

**CompressorCore (composition kernel)**

- **FR-008**: The system MUST provide a per-channel `CompressorCore` that composes the shipped `EnvelopeFollower` (detection + ballistics + dB domain), `SvfPrimitive` (sidechain highpass), and `DelayLine` (lookahead) — it MUST NOT re-derive detection, filtering, or delay.
- **FR-009**: `CompressorCore` MUST implement the per-sample signal chain: derive the key (external sidechain or main input) → optional sidechain highpass → detect level → static-curve gain reduction → optional gain-site smoothing → convert to linear gain with makeup → apply to the (optionally delayed) main path → dry/wet mix → output trim.
- **FR-010**: The system MUST support an enum-selected `Detection` topology of **feedForward** (detector reads the input) and **feedBack** (detector reads the previous **post-makeup, pre-mix** output sample — after the gain multiply and makeup gain but **before** the dry/wet mix and output trim, so those stay outside the detection loop; Clarified 2026-07-02); the feedback path MUST use a defined initial output at cold start and MUST remain stable (no divergence/oscillation) for bounded input.
- **FR-011**: The system MUST support an enum-selected `BallisticsSite` of **level** (attack/release applied to the detected level via the `EnvelopeFollower`; the static curve is instantaneous) and **gain** (the static curve maps the ~instantaneous level; a second smoother applies attack/release to the gain-reduction signal).
- **FR-012**: The system MUST support selecting the detector mode (**peak** / **rms**) used for detection, delegating to the composed `EnvelopeFollower`.
- **FR-013**: The system MUST support a pre-detector **sidechain highpass** (cutoff in Hz; 0 = bypass) applied to the key signal only, via the composed `SvfPrimitive`, leaving the main path unaffected.
- **FR-014**: The system MUST support an **external sidechain** (key) input: when supplied, detection reads the key; when absent, detection reads the main input.
- **FR-015**: The system MUST support **lookahead**: the main path is delayed by `round(lookaheadMs·fs)` samples via the composed `DelayLine` (buffer sized in `prepare()`), and the resulting latency MUST be reported to the host; lookahead = 0 adds no latency and engages no delay.
- **FR-016**: The system MUST support **makeup** gain — manual (dB) and an **auto** mode computed as the closed-form `makeup = −computeGainDb(0 dBFS)` (compensating the gain reduction the curve applies at a 0 dBFS reference), recomputed once per parameter change with no runtime tracking; auto-makeup MUST be 0 in expand/gate modes (downward-only). (Clarified 2026-07-02.) It MUST also support a dry/wet **mix** (parallel compression; 0 = dry, 1 = fully compressed) and a final **output** trim (dB).
- **FR-017**: The system MUST support **stereo/multichannel linking**: **linked** detection drives a single common gain from the max detector value across linked channels; **perChannel** detection is independent per channel. Linking over a single channel degenerates to per-channel.

**CompressorEffect (host-facing wrapper)**

- **FR-018**: The system MUST provide a `CompressorEffect` in `core/effects/compressor/` satisfying the `Effect` contract (`prepare`, `process`, `reset`, static `parameters()`, `setParameter`), mirroring the shipped `SaturationEffect`/`SvfEffect` idiom, with **no base class and no vtable on the audio path**.
- **FR-019**: `CompressorEffect` MUST expose a single **constexpr `ParameterDescriptor` table** as the sole source of parameter truth, covering the captured set: threshold, ratio, knee, attack, release, mode {compress|limit|expand|gate}, detection {feedForward|feedBack}, detector {peak|rms}, ballisticsSite {level|gain}, range, sidechain-HPF Hz, lookahead ms, makeup, auto-makeup, stereoLink {perChannel|linked}, mix, output. A build-time `static_assert` MUST reject any malformed descriptor.
- **FR-020**: `setParameter(id, normalized)` MUST be callable from any thread and MUST publish a **lock-free atomic** pending value consumed by the audio thread at the top of `process()`; parameter edits MUST NOT race `process()` and MUST NOT mutate core coefficients off the audio thread.
- **FR-021**: `prepare()`/`reset()` MAY mutate core coefficients directly and are NOT synchronized against `process()` (called only while the stream is stopped, per the established effect lifecycle); the reported lookahead latency MUST be established in `prepare()`.

**Real-time safety & portability**

- **FR-022**: No audio-path method (`process`) MUST perform heap allocation, take locks, or do unbounded work; all coefficients MUST recompute in `set*`/`prepare`, and the lookahead buffer MUST be sized in `prepare()`, never in `process()`.
- **FR-023**: The `GainComputer` primitive, the `CompressorCore`, the lab kernel, and the `CompressorEffect` MUST compile and run with no JUCE / libDaisy / Teensy knowledge and MUST be suitable for MCU targets; per-sample transcendental cost MUST stay confined to the composed `EnvelopeFollower` (the `GainComputer` MUST be branch-only arithmetic).
- **FR-024**: All parameter setters MUST guard degenerate inputs (non-positive ratio/times/cutoff, out-of-range ids) so that no `process()` output is NaN/Inf and every internal coefficient stays within its valid bound.

**Structure, graduation & gating**

- **FR-025**: The concept MUST be authored as `core/labs/compressor/` (README gain-computer/topology theory + RT-safe kernel + host-only harness) and the stateless gain computer MUST then be graduated (`git mv` the kernel) into `core/primitives/dynamics/gain-computer.h`, refining in place — never re-derived.
- **FR-026**: The graduated `gain-computer.h` MUST be added to the existing `core/primitives/dynamics/` category and `core/primitives/README.md` MUST document the gain computer as an inhabited member (moved from prospectus) in the same atomic commit as the graduation.
- **FR-027**: `scripts/check-portability.sh` (run in CI, never as a git hook) MUST be extended to cover `core/labs/compressor/**`, `core/primitives/dynamics/gain-computer.h`, and `core/effects/compressor/**` for harness-isolation, dependency-direction, platform-independence, and file-size checks.
- **FR-028**: Every source file introduced MUST stay within ~300–500 lines and use strict typing (no unchecked casts). If the captured parameter set pushes `compressor-effect.h` past the guideline, the wrapper MUST be split into cooperating files (e.g. a parameter-table header + an apply/denormalize header) rather than exceeding the limit.

**Scope boundary (Principle XI — one concept at a time)**

- **FR-029**: The feature MUST deliver the detector-fed **static** dynamics processor: the `GainComputer` primitive, the compressor lab, and the `CompressorEffect`. **Program-dependent / "auto" attack-release** (opto / vari-mu emulation), **multiband dynamics**, and **dynamic EQ** MUST NOT be implemented here — they are owned by sibling/future items (`design:feature/program-dependent-saturation`, `design:feature/tape-dynamics`). The gain application here MUST be a per-sample multiply folded into `CompressorCore`, NOT a separately graduated stateful VCA-envelope primitive.

### Key Entities

- **GainComputer**: the stateless static-curve primitive. Attributes: mode (compress/limit/expand/gate), threshold, ratio, knee width, range floor. Behavior: `computeGainDb(levelDb) → gainReductionDb (≤ 0)`. No runtime state. Composed by dynamics processors.
- **CompressorCore**: the per-channel composition kernel. Composes an `EnvelopeFollower`, a `GainComputer`, an optional `SvfPrimitive` (sidechain HPF), and an optional `DelayLine` (lookahead); holds detection topology, ballistics site, makeup/mix/output, and per-channel runtime state.
- **CompressorEffect**: the host-facing wrapper. Owns per-channel `CompressorCore` state, the constexpr parameter descriptor table, and the lock-free cross-thread parameter handoff; reports lookahead latency.
- **GainMode**: enumerated static-curve mode — compress, limit, expand, gate.
- **Detection**: enumerated detection topology — feedForward, feedBack.
- **BallisticsSite**: enumerated ballistics placement — level, gain.
- **StereoLink**: enumerated channel-linking mode — perChannel, linked.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For a steady tone stepped across the threshold, the measured input→output level map matches the analytic static curve (unity below threshold; `thr + (level−thr)/ratio` above) within a stated tolerance, for compress and limit modes.
- **SC-002**: For a step from below to above threshold, the gain reduction reaches 1 − 1/e (~63%) of its steady value within the configured attack time (and recovers at the release time) within tolerance, for each ballistics site.
- **SC-003**: The soft-knee gain-reduction curve is C¹-continuous (continuous value and slope) across the full knee width and reduces exactly to the hard-knee curve when knee = 0.
- **SC-004**: In feedback topology, a steady above-threshold input settles to the analytic detector→curve→gain fixed point within tolerance, and the loop is stable (no oscillation/divergence) for bounded input.
- **SC-005**: In expand and gate modes, a below-threshold sweep matches the analytic downward curve and never exceeds the configured `range` floor; both pass at unity above threshold.
- **SC-006**: With the sidechain HPF engaged at `fc`, a tone below `fc` produces substantially less gain reduction than a tone above `fc` at the same level; at 0 Hz the detection is full-band.
- **SC-007**: With an external key supplied, gain reduction tracks the key level (not the main level); with no key, detection reads the main input.
- **SC-008**: With lookahead L ms, the reported host latency equals `round(L·fs)` samples, and a first-sample transient is limited from its first sample (no threshold overshoot that a zero-lookahead limiter would pass).
- **SC-009**: Manual makeup of M dB raises the output by M dB; auto-makeup applies the closed-form constant makeup `−computeGainDb(0 dBFS)` uniformly (a below-threshold signal is lifted by that makeup amount, per the chosen constant-makeup model — not gated to unity); mix = 0 yields dry passthrough and mix = 1 yields the fully compressed signal.
- **SC-010**: Linked detection applies the same gain to all linked channels (driven by the cross-channel max), keeping a one-channel transient's image stable; per-channel detection attenuates only the affected channel.
- **SC-011**: `CompressorEffect` satisfies the `Effect` concept; a parameter edit from a non-audio thread is applied on the next `process()` with no lock or torn read; a malformed descriptor fails the build via `static_assert`.
- **SC-012**: A no-allocation test confirms zero heap allocation on the `process()` path (after `prepare()`) across all modes, topologies, sites, and feature combinations.
- **SC-013**: No input (silence, DC, impulse, threshold-crossing, feedback cold start, low-sample-rate short-τ) produces a NaN/Inf output in any configuration.
- **SC-014**: The portability gate passes over `core/labs/compressor/**`, `core/primitives/dynamics/gain-computer.h`, and `core/effects/compressor/**` (harness isolation, dependency direction, platform independence, file size), the gain computer is documented as inhabited in `core/primitives/README.md`, and no portable unit includes a harness.
- **SC-015**: The `GainComputer` graduates via `git mv` (lab → primitive) with the README + kernel + host-only harness present in the lab, in a single atomic commit with the README-inhabited update.

## Assumptions

- **Consumers call `process()` per block** through the `Effect` contract (the established idiom); `CompressorCore::process()` is the per-sample loop body — matching `SaturationEffect`/`SvfEffect`.
- **The composed primitives are shipped and reused as-is**: `EnvelopeFollower` (`core/primitives/dynamics/envelope-follower.h`), `SvfPrimitive` (`core/primitives/filters/svf-primitive.h`), and `DelayLine` (`core/primitives/delays/delay-line.h`). Detection ballistics, dB-domain handling, and the −120 dBFS floor are inherited from `EnvelopeFollower`.
- **Validation reuses the shipped measurement (stimulus/response) infrastructure** (`tests/core/measurement-*`, `measurement-support.h`, `tests/support/svf-reference.h`) for static-curve, step-response, and latency assertions, following the `svf-reference` named-tolerance pattern.
- **The full captured catalog IS the first-cut scope** (Clarified 2026-07-02): all modes (compress/limit/expand/gate), both topologies (feedforward/feedback), both ballistics sites (level/gain), external sidechain, sidechain HPF, lookahead, auto-makeup, and stereo linking all land in this feature's implementation — nothing is deferred to a follow-on, matching the envelope-followers full-catalog precedent.
- **Default configuration** on `prepare()` is a downward compressor: feedforward, level-smoothed, a default detector mode, hard knee, manual (0 dB) makeup, mix = 1, per-channel linking, no sidechain HPF, no external key, no lookahead — so a host that only sets threshold/ratio/attack/release gets a working compressor.
- **The gain application is a per-sample linear multiply** folded into `CompressorCore`; no separate stateful VCA-envelope primitive is graduated in this feature (that taxonomy entry remains available for a later item).
- **Dependency**: `multi:feature/phase-nonlinear-dsp` is complete; the `EnvelopeFollower`/`SvfPrimitive`/`DelayLine` primitives, the measurement infrastructure, and the three-layer/portability tooling are shipped.

## Deferred Decisions *(for `/speckit-clarify` and `/speckit-plan`)*

Four of the design-record open questions were resolved in the 2026-07-02 clarification session
(see `## Clarifications` and the folded FR-007/FR-010/FR-016 and Assumptions updates: first-cut =
full catalog, feedback tap = post-makeup/pre-mix, unified quadratic knee across modes, auto-makeup =
closed-form at 0 dBFS / off for expand-gate). The remaining items are parameterization/structure
detail parked for `/speckit-plan` — captured, not discarded, and not scope-affecting:

- **Effect-wrapper file split.** Whether the ~17-parameter `compressor-effect.h` exceeds the ~300–500 line guideline and how it splits (parameter-table header + apply/denormalize header) — an FR-028 planning decision.
- **Ratio / limit parameterization.** Given `limit` is a distinct `GainMode` (FR-002/FR-004), the ratio parameter's skew mapping (log/skewed 1:1…∞:1) for usable control and how `range` and `knee` interact for the gate.
- **Stereo-link detail.** Whether linking offers only max or also sum/average, and whether the sidechain HPF / external key is per-channel or shared across a linked group.
- **Detector defaults per topology.** Whether feedback defaults to a different detector mode / ballistics than feedforward, given feedback's inherent smoothing.

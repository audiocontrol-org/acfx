> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Modulated Delay — feedback-filtered delay with movement, warble, and tape wow & flutter

**Feature Branch**: `main`

**Created**: 2026-06-28

**Status**: Draft

**Input**: User description: "I want to develop a delay effect with a state-variable filter SVF on the feedback loop. I want to be able to modulate the SVF parameters as well as the delay time to give the delay tail some movement and warble. I also want to be able to create a wow-and-flutter effect on the input signal—which I imagine will require its own delay line."

## Clarifications

### Session 2026-06-28

- Q: How should the modulation source(s) for delay-time and feedback-filter movement be structured? → A: Independent LFO per destination — delay time, filter cutoff, and filter resonance each get their own LFO with its own rate AND depth, so warble and filter movement can run at different speeds.
- Q: What modulation waveform(s) should drive the delay-time/filter LFO movement? → A: Selectable shape set — each LFO offers {sine, triangle, saw, random} via a discrete shape control.
- Q: What maximum delay time should the preallocated buffer support? → A: 2 seconds (per-channel buffer sized for 2.0 s at the prepared sample rate).
- Q: How much control should the wow & flutter stage expose? → A: Independent rate AND depth for the wow component and for the flutter component (four controls).

## User Scenarios & Testing *(mandatory)*

The "user" of this feature is the **DSP author** — the person who writes an
effect once and expects it to run, unchanged, on every target (desktop
workbench, DAW plugin, embedded hardware), exactly as the shipped SVF effect
does. This feature is the second real effect on the cross-platform spine, and it
deliberately reuses the SVF that proved the spine.

### User Story 1 - A delay whose echoes are shaped by a filter in the feedback loop (Priority: P1)

The author writes a delay effect once, opens the desktop workbench, feeds it
audio, and hears discrete echoes. They set the delay time, the feedback amount,
and the dry/wet mix. Crucially, a state-variable filter sits **inside the
feedback loop**, so each successive echo is filtered again — letting the author
make the tail progressively darken (low-pass), thin out to a telephone band
(band-pass), or hollow out the lows (high-pass), with an optional resonant
emphasis. They adjust the filter's cutoff, resonance, and mode and hear the tail
re-shape in real time.

**Why this priority**: This is the MVP and the heart of the request — a delay
with a filtered feedback path. If only this story ships, the author already has a
distinctive, useful, demonstrable effect on the same spine as the SVF, and every
later story (modulation, wow & flutter) layers onto it. It also proves the new
DSP building block this feature introduces: an allocation-free delay line with a
filter in its feedback path.

**Independent Test**: Build and launch the workbench with the modulated-delay
effect; route audio through it; set a clearly audible delay time and a high
feedback; sweep the feedback-filter cutoff and switch its mode low→high→band;
confirm successive echoes are audibly re-filtered each pass and that delay time,
feedback, and mix all track their controls in real time without clicks.

**Acceptance Scenarios**:

1. **Given** the effect is loaded with audio flowing and feedback set high enough for several audible repeats, **When** the author lowers the feedback-filter cutoff in low-pass mode, **Then** each successive echo is darker than the previous one and the tail audibly decays in brightness as well as level.
2. **Given** feedback is set so echoes repeat several times, **When** the author switches the feedback filter to band-pass, **Then** the tail collapses toward a narrow band and successive repeats sound increasingly "telephone-like".
3. **Given** the dry/wet mix control, **When** the author moves it from fully dry to fully wet, **Then** the balance between the unprocessed input and the delayed/filtered signal changes smoothly without clicks or dropouts.
4. **Given** a feedback amount within the stable range, **When** audio plays continuously, **Then** the feedback path does not run away into uncontrolled self-oscillation or clipping beyond what the author commanded.
5. **Given** the author changes the delay time control, **When** the new value takes effect, **Then** the delay retimes without producing clicks, pops, or zipper noise.

---

### User Story 2 - Modulate the delay time and the feedback filter to give the tail movement and warble (Priority: P2)

Without changing how the delay is wired, the author enables modulation. A
low-frequency modulation source can be routed to the **delay time** (producing
pitch/time warble in the tail as the read position drifts) and to the
**feedback-filter cutoff and resonance** (producing a sweeping, moving timbre in
the echoes). The author sets a modulation rate and a per-destination depth and
hears the once-static tail come alive — chorus-like warble from delay-time
modulation, and a filter that breathes across the repeats.

**Why this priority**: This is the "movement and warble" the author explicitly
asked for, and it is what makes the delay expressive rather than clinical. It
builds directly on Story 1's delay+filter and is independently demonstrable.

**Independent Test**: With Story 1 working, enable modulation; route it to delay
time and set a modest depth — confirm the tail develops a periodic pitch/time
warble at the set rate; then route modulation to the feedback-filter cutoff and
confirm the echoes sweep tonally in time with the rate; vary depth from zero
(static, identical to Story 1) up to a strong setting and confirm the effect
scales smoothly with no clicks or instability.

**Acceptance Scenarios**:

1. **Given** modulation routed to delay time at a moderate depth and a slow rate, **When** audio with sustained content plays through the delay, **Then** the tail exhibits a periodic pitch/time wobble (warble) synchronized to the modulation rate.
2. **Given** modulation routed to the feedback-filter cutoff, **When** echoes repeat, **Then** the brightness of the tail rises and falls periodically at the modulation rate.
3. **Given** any modulation depth set to zero, **When** audio plays, **Then** the corresponding destination is held static and the result is indistinguishable from Story 1 with no modulation.
4. **Given** the modulation rate control, **When** the author increases it, **Then** the movement/warble speeds up correspondingly, across the full supported rate range, without aliasing artifacts or clicks.
5. **Given** modulation routed to delay time, **When** the modulation drives the delay time toward its bounds, **Then** the read stays within the allocated delay buffer and never reads out of range.

---

### User Story 3 - Wow & flutter on the input signal for tape/analog instability (Priority: P3)

The author enables a wow-and-flutter stage that sits on the **input** path,
before the main delay. It runs its own dedicated modulated delay line and imparts
the pitch instability of tape/analog machines: a slow "wow" drift plus a faster
"flutter". The author sets the amount of wow and the amount of flutter and hears
the dry signal (and everything downstream, including the delay tail) take on a
tape-like, slightly unstable character.

**Why this priority**: This is a distinct character layer the author explicitly
asked for, requiring its own delay line. It is valuable but optional relative to
the core filtered delay, and it is independently demonstrable on its own.

**Independent Test**: With the effect loaded, set the main delay mix to mostly
dry so the input is audible; enable wow & flutter and raise the wow amount —
confirm a slow periodic pitch drift on the signal; raise the flutter amount —
confirm a faster, shallower pitch shimmer layered on top; set both amounts to
zero and confirm the input passes through with no pitch modulation.

**Acceptance Scenarios**:

1. **Given** wow & flutter enabled with a non-zero wow amount, **When** sustained-pitch audio (e.g. a held note) plays, **Then** the perceived pitch drifts slowly and periodically (the "wow").
2. **Given** a non-zero flutter amount, **When** the same audio plays, **Then** a faster, shallower pitch shimmer is layered on top of any wow.
3. **Given** both wow and flutter amounts at zero, **When** audio plays, **Then** the input is passed through with no audible pitch modulation from this stage.
4. **Given** wow & flutter active ahead of the main delay, **When** the delay tail is audible, **Then** the tape-like instability is present in the delayed signal as well, because it was imparted on the input before the delay.
5. **Given** wow & flutter active at any supported amount, **When** audio plays continuously, **Then** its dedicated delay line read stays in range and introduces no clicks, dropouts, or runaway artifacts.

---

### Edge Cases

- **Delay time changes (stepped vs swept)**: When the delay time is changed abruptly by a control edit (not by modulation), the delay must retime without clicks/zipper. Whether a fast control change glides or jumps is a behavior to define.
- **Modulation driving delay time to its limits**: When delay-time modulation (or wow/flutter) pushes the effective read position toward zero or toward the maximum allocated delay, reads must stay strictly within the preallocated buffer — never out of range, never wrapping incorrectly.
- **Feedback near or at unity**: High feedback combined with a resonant feedback filter can build energy. The effect must stay within a defined, controllable bound (the author commands the level; the effect does not silently runaway or hard-clip in a way the author did not ask for).
- **Maximum delay time and buffer sizing**: The maximum supported delay time must be a defined value so the preallocated buffer is sized correctly at `prepare()` time; requesting beyond it is bounded to the maximum.
- **Sample-rate independence**: Modulation rates and delay/wow/flutter times must behave consistently across sample rates (e.g. 44.1k, 48k, 96k) since the same effect runs unchanged on every target.
- **Block-rate vs sample-rate modulation**: Whether modulation is applied per-sample or per-block affects smoothness of fast modulation; the chosen granularity must avoid audible stepping on the supported rate range.
- **Mono vs multi-channel**: The effect processes the channel count given at `prepare()` (consistent with the SVF effect's per-channel handling); behavior at 1 and 2 channels must be defined, including whether modulation is correlated across channels.
- **Reset/prepare while stopped**: As with the SVF effect, buffer allocation and state clears happen in `prepare()`/`reset()` while the audio stream is stopped; `process()` and cross-thread parameter edits remain the only audio-thread-and-any-thread interaction.

## Requirements *(mandatory)*

### Functional Requirements

#### Core filtered-feedback delay (Story 1)

- **FR-001**: The effect MUST satisfy the existing `Effect` contract (`prepare`, `process`, `reset`, static `parameters()`, `setParameter`) so it runs unchanged across the workbench, DAW plugin, and embedded targets, exactly as the SVF effect does.
- **FR-002**: The effect MUST provide a delay with author-controllable **delay time**, **feedback amount**, and **dry/wet mix** parameters.
- **FR-003**: The effect MUST place a state-variable filter **inside the feedback loop**, so each pass of the feedback signal is filtered before being summed back into the delay input.
- **FR-004**: The feedback filter MUST expose **cutoff** (log Hz), **resonance** (linear), and **mode** (lowpass / highpass / bandpass) controls, semantically consistent with the existing SVF effect.
- **FR-005**: The feedback filter MUST reuse the existing proven per-sample SVF math (the `SvfPrimitive` wrapper over DaisySP's `Svf`) rather than reimplementing filter math.
- **FR-006**: All delay buffers MUST be preallocated in `prepare()`; `process()` MUST perform no heap allocation and take no locks (Constitution VI, real-time safety).
- **FR-007**: Delay-buffer reads MUST stay strictly within the allocated buffer for every supported delay-time and modulation setting; out-of-range reads MUST be impossible.
- **FR-008**: Changing the delay time MUST NOT produce clicks, pops, or zipper noise; the implementation MUST use fractional (interpolated) delay-line reads so swept/modulated delay times are smooth.
- **FR-009**: The effect MUST support a **maximum delay time of 2 seconds**; the per-channel delay buffer is sized for 2.0 s at the prepared sample rate, and requests beyond the maximum MUST be bounded to the maximum (no out-of-range allocation or read).
- **FR-010**: The feedback path MUST remain bounded under author control — feedback at the supported maximum MUST NOT silently diverge into uncommanded runaway or clipping beyond the author's setting.

#### Modulation of delay time and feedback filter (Story 2)

- **FR-011**: The effect MUST provide **three independent low-frequency modulation sources** — one each for **delay time**, **feedback-filter cutoff**, and **feedback-filter resonance** — so the three destinations can move independently.
- **FR-012**: Each modulation source MUST expose its own **rate** AND **depth** control, so each destination can move at a different speed and by a different amount (including zero), e.g. tail warble at one rate while the filter breathes at another.
- **FR-012a**: Each modulation source MUST offer a selectable **waveform shape** from the set {sine, triangle, saw, random}.
- **FR-013**: A modulation depth of zero for a destination MUST hold that destination static, producing a result indistinguishable from Story 1 (no modulation), regardless of that source's rate or shape.
- **FR-014**: Modulation of delay time MUST produce smooth pitch/time warble (no clicks) and MUST keep the effective read within the allocated buffer at all rates and depths (ties to FR-007).
- **FR-015**: Modulation MUST be sample-rate independent — a given rate produces the same musical movement at 44.1k, 48k, and 96k.
- **FR-016**: Modulation MUST be free of audible stepping/zipper across the supported rate and depth ranges (the chosen per-sample/per-block granularity MUST meet this bar).

#### Wow & flutter on the input (Story 3)

- **FR-017**: The effect MUST provide a **wow & flutter** stage on the **input** path, ahead of the main delay, running on its **own dedicated delay line** (separate from the feedback delay).
- **FR-018**: The wow & flutter stage MUST expose **independent rate and depth controls for the wow component AND for the flutter component** (four controls total), so the slow wow drift and the faster flutter shimmer can be shaped independently.
- **FR-019**: With wow and flutter amounts at zero, the input MUST pass through this stage with no audible pitch modulation.
- **FR-020**: Because wow & flutter is applied to the input before the delay, its character MUST be present in the delayed/feedback signal as well as the dry path.
- **FR-021**: The wow & flutter delay line MUST be preallocated in `prepare()`, read in range at all amounts, and free of clicks/dropouts (same RT-safety bar as the main delay).

#### Cross-cutting (all stories)

- **FR-022**: All author-facing controls MUST be declared in a **single constexpr parameter table** (the single source of parameter truth) that drives every adapter, exactly as the SVF effect's table does.
- **FR-023**: Cross-thread parameter edits MUST stay lock-free: `setParameter` (callable from any thread) publishes a pending value that the audio thread consumes at the top of `process()`; parameter edits MUST never race `process()`.
- **FR-024**: The core MUST contain no platform headers (no JUCE / libDaisy / Teensy); dependencies point only inward (Constitution IV).
- **FR-025**: Source modules MUST stay within the project size budget (~300–500 lines) with strict typing (no `any`-equivalent / unchecked casts), decomposing the delay line, modulation source, wow/flutter stage, and effect wiring into separate small units as needed.
- **FR-026**: The core MUST be testable host-side (Constitution VIII) — delay timing, in-range reads, feedback-filter shaping, modulation movement, and wow/flutter behavior MUST be verifiable in host-side tests without hardware.
- **FR-027**: The effect MUST process the channel count provided at `prepare()` consistently with the SVF effect's per-channel handling; behavior at 1 and 2 channels MUST be defined.

### Key Entities *(include if feature involves data)*

- **Modulated Delay Effect**: The top-level effect satisfying the `Effect` contract; owns the parameter table, the main feedback delay, the feedback filter, the modulation source(s), and the wow & flutter stage; wires the signal flow input → wow&flutter → delay (with filtered feedback) → mix.
- **Delay Line (interpolated)**: An allocation-free circular buffer supporting fractional read positions for smooth/modulated delay times; used by both the main feedback delay and (a separate instance) the wow & flutter stage.
- **Feedback Filter**: The state-variable filter placed in the feedback path; reuses `SvfPrimitive` (cutoff/resonance/mode), one instance per channel.
- **Modulation Source (LFO)**: A low-frequency modulation generator with its own rate, depth, and selectable waveform shape {sine, triangle, saw, random}. Three independent instances drive delay time, feedback-filter cutoff, and feedback-filter resonance respectively.
- **Wow & Flutter Stage**: An input-path processor with its own interpolated delay line and two independent modulation components (a slow wow and a faster flutter, each with its own rate and depth) producing tape-style pitch instability.
- **Parameter Table**: The single constexpr `ParameterDescriptor` table enumerating every author-facing control — delay time, feedback, mix, filter cutoff/resonance/mode, per-destination modulation rate/depth/shape (delay, cutoff, resonance), and wow/flutter rate+depth — the single source of truth for all adapters.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The DSP author can write the effect once and load it unchanged in the desktop workbench, hearing a delay whose echoes are progressively re-filtered by the feedback-loop filter (Story 1 demonstrable end-to-end).
- **SC-002**: Across the full supported range of delay time, feedback, mix, and filter controls, no control change produces an audible click, pop, or zipper, and the feedback never diverges into uncommanded runaway.
- **SC-003**: With delay-time modulation enabled, the author can produce an audible, periodic pitch/time warble in the tail whose speed tracks the modulation rate; with the depth at zero the result is indistinguishable from the unmodulated delay.
- **SC-004**: With feedback-filter modulation enabled, the author can produce an audible, periodic tonal sweep across successive echoes whose speed tracks the modulation rate.
- **SC-005**: With wow & flutter enabled, the author can produce an audible slow pitch drift (wow) and a faster shimmer (flutter) on the input signal; with both amounts at zero the input passes through with no pitch modulation.
- **SC-006**: For every combination of supported parameter values and modulation settings, all delay-buffer reads remain in range (verified by host-side tests) — there are zero out-of-range reads.
- **SC-007**: The `process()` path performs zero heap allocations and takes zero locks under all parameter and modulation settings (verifiable host-side).
- **SC-008**: All author-facing controls are enumerated in exactly one parameter table, and the same effect source runs unchanged on every supported target (parity with the SVF effect).
- **SC-009**: The effect behaves consistently at 44.1 kHz, 48 kHz, and 96 kHz — a given modulation rate and delay time produce the same musical result independent of sample rate.

## Assumptions

- **Reuse of the SVF**: The feedback filter reuses the existing `SvfPrimitive`/`SvfEffect` design and DaisySP `Svf` math; cutoff is log Hz, resonance is linear 0..1, mode is the same {lowpass, highpass, bandpass} set.
- **Free-running times, no host-tempo sync**: Delay time and modulation rate are expressed in absolute units (ms / Hz), free-running; synchronizing delay time or modulation rate to host/DAW tempo is an adapter-level concern and is out of scope for this core effect.
- **Wow vs flutter as two components**: "Wow" is the slow pitch-drift component and "flutter" is the faster component; each has its own independent rate and depth (clarified 2026-06-28), consistent with tape-machine terminology.
- **Modulation shapes**: Each LFO offers a selectable waveform from {sine, triangle, saw, random} (clarified 2026-06-28); "random" denotes a smoothly-varying/sample-and-hold style source suitable for organic warble rather than a pure periodic wave.
- **Per-channel processing**: Like the SVF effect, the effect processes up to the channel count provided at `prepare()`; stereo modulation correlation (identical vs offset per channel) defaults to identical across channels unless a later refinement changes it.
- **Single long-lived branch**: Per the program convention, work proceeds on the existing branch with a descriptive (non-numeric) spec dir; the active spec dir is resolved via the `CLAUDE.md` SPECKIT marker, not the branch name.
- **Bounded maximum delay**: The maximum delay time is 2 seconds (clarified 2026-06-28); the per-channel buffer is sized for 2.0 s at the prepared sample rate.
- **No new external dependencies**: The feature builds on DaisySP (already pinned) and the existing core DSP primitives; no new third-party libraries are assumed.

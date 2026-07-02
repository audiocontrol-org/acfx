> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Envelope Followers — Dynamics Level-Detector Primitive

**Feature Branch**: `envelope-followers`

**Created**: 2026-07-01

**Status**: Draft

**Roadmap item**: `design:primitive/envelope-followers` (`depends-on: multi:feature/phase-nonlinear-dsp`; `part-of: multi:feature/phase-dynamic-systems`)

**Design record**: `docs/superpowers/specs/2026-07-01-envelope-followers-design.md` (operator-approved; source of truth)

**Input**: User description: "Envelope followers — the dynamics level-detector primitive for the acfx platform-independent DSP core … capture everything it specifies, insert no scope cuts." (full brief in the design record)

## User Scenarios & Testing *(mandatory)*

The "user" of this primitive is a **DSP effect author / downstream primitive consumer** — the
developer building the compressor, limiter, gate, or level meter that composes this detector — and
the **lab reader** learning the ballistics. Each story is an independently testable slice.

### User Story 1 - Track signal level with a peak detector and attack/release ballistics (Priority: P1)

A consumer feeds an audio signal, one sample at a time, into an `EnvelopeFollower` configured for
peak detection with a chosen attack time and release time, and receives a smoothed linear-amplitude
envelope that rises quickly on transients (at the attack rate) and falls slowly (at the release
rate). This is the irreducible level detector every dynamics processor builds on.

**Why this priority**: The MVP. A working peak detector with attack/release ballistics returning a
linear envelope is, on its own, a usable sidechain front end for a limiter or gate. Every other
story extends this core.

**Independent Test**: Drive a unit step (0 → 1) and assert the envelope reaches 1 − 1/e (~63%) of
the target in the configured attack time within tolerance; drive a 1 → 0 step and assert the
release time likewise; drive a sine of amplitude A and assert the peak envelope settles to ≈A.

**Acceptance Scenarios**:

1. **Given** a follower in peak mode with attack = 10 ms, **When** a unit step is applied, **Then** the envelope crosses ~63% of the step within 10 ms (± tolerance) and asymptotes to the peak.
2. **Given** a settled envelope at 1.0 with release = 100 ms, **When** the input drops to 0, **Then** the envelope decays through ~37% of its start value within 100 ms (± tolerance).
3. **Given** a fully-silent input, **When** processed, **Then** the envelope is 0 (no NaN/Inf, no drift).

---

### User Story 2 - Detect program level with an RMS detector (Priority: P2)

The consumer selects RMS mode and receives an envelope tracking the signal's root-mean-square
(program) level rather than its instantaneous peak — the level measure a compressor uses for
musical "feel" and a VU-style meter uses for perceived loudness.

**Why this priority**: RMS is the second workhorse detector and is essential for program-dependent
dynamics; without it the primitive is incomplete for its primary compressor consumer.

**Independent Test**: Drive a sine of amplitude A and assert the RMS envelope settles to ≈ A/√2
within tolerance; assert the steady-state ripple stays under a named bound.

**Acceptance Scenarios**:

1. **Given** RMS mode, **When** a steady sine of amplitude A is applied, **Then** the envelope converges to A/√2 (± tolerance).
2. **Given** RMS mode on a steady sine, **When** the envelope has settled, **Then** its peak-to-peak ripple is below the specified bound.

---

### User Story 3 - Hold peaks with a peak-hold detector (Priority: P3)

The consumer selects peak-hold mode with a hold time; the envelope latches a detected peak and
holds it for the configured duration before the release ballistics begin to pull it down — the
behavior a PPM meter or a transient-aware limiter wants.

**Why this priority**: Peak-hold rounds out the metering catalog (PPM ballistics) and is a distinct,
testable behavior, but compressors/limiters function without it, so it follows the two workhorses.

**Independent Test**: Apply a single-sample impulse to a peak of value P, then silence; assert the
envelope stays at ≈P for the hold time before decaying at the release rate.

**Acceptance Scenarios**:

1. **Given** peak-hold with hold = 50 ms, **When** a peak of P is detected then the input goes silent, **Then** the envelope holds ≈P for ~50 ms and only then begins releasing.
2. **Given** peak-hold, **When** a new higher peak arrives during the hold window, **Then** the held value updates upward and the hold window restarts from the new peak.

---

### User Story 4 - Choose the ballistics topology (branching vs decoupled, smooth variant) (Priority: P2)

The consumer selects the smoothing topology — **branching** (single state; attack coefficient while
the rectified input rises, release coefficient while it falls) or **decoupled** (a release smoother
feeds an attack smoother) — and optionally the **smooth** variant (attack coefficient applied in
both stages). Decoupled eliminates the branching detector's release-then-attack tracking artifact;
branching is the cheaper single-state option for MCU targets and simple gates.

**Why this priority**: The topology choice materially changes tracking behavior and is required by
the downstream compressor (which wants decoupled-smooth), yet the P1 peak detector already works
with the default branching topology — so this is an extension, not the core.

**Independent Test**: Apply a fast transient followed by a slow decay; assert the branching detector
exhibits the known release-then-attack artifact while the decoupled detector tracks the decay
monotonically, each within its characterized envelope shape.

**Acceptance Scenarios**:

1. **Given** branching topology, **When** a transient is followed by a slower decaying tail, **Then** the envelope shows the characteristic branching response.
2. **Given** decoupled topology on the same input, **Then** the envelope tracks the decay without the branching artifact.
3. **Given** the smooth variant of either topology, **Then** the attack coefficient governs both smoothing stages and the response matches the characterized smooth curve.

---

### User Story 5 - Detect in the decibel domain for level-independent time constants (Priority: P3)

The consumer selects decibel-domain detection; the detector converts the rectified level to dB
before smoothing so that the effective attack/release time constants are independent of the input
amplitude — the property a well-behaved compressor relies on for consistent feel across levels.
`process()` then returns the envelope in dB.

**Why this priority**: A real, literature-backed improvement for the compressor consumer, but the
linear-domain envelope (the base contract) is sufficient for limiters, gates, and meters, so the dB
mode is an added peer rather than the core.

**Independent Test**: Apply steps of two different amplitudes (e.g., differing by 20 dB); assert the
dB-domain envelope reaches ~63% of each step's dB change in the same configured attack time (time
constant independent of level), whereas the linear-domain envelope's measured time varies with
level.

**Acceptance Scenarios**:

1. **Given** decibel domain with a fixed attack time, **When** two steps of different amplitude are applied, **Then** the measured attack time (to ~63% of the dB change) is the same for both within tolerance.
2. **Given** decibel domain, **When** the input is at or below the detection floor, **Then** the returned dB value is clamped to the floor rather than −∞ (see Deferred Decisions for the floor level).

---

### User Story 6 - Learn the ballistics in a lab and consume the graduated primitive (Priority: P2)

A lab reader opens `core/labs/envelope-follower/` and finds the ballistics theory (README), an
RT-safe kernel, and a host-only harness that measures attack/release behavior; the kernel then
graduates into `core/primitives/dynamics/` as the reusable primitive, creating the `dynamics/`
category with its first inhabitant.

**Why this priority**: The three-layer graduation (Principle IX) is how this concept is delivered as
a reusable primitive and how the `dynamics/` category comes into existence; it is structural to the
deliverable, though the detector behavior (US1–US5) is what consumers exercise.

**Independent Test**: Confirm `core/labs/envelope-follower/` contains README + kernel + host-only
harness; confirm the graduated primitive lives at `core/primitives/dynamics/`; confirm
`core/primitives/README.md` lists `dynamics/` as inhabited; confirm the portability gate passes over
both new paths.

**Acceptance Scenarios**:

1. **Given** the graduation commit, **When** the tree is inspected, **Then** `core/primitives/dynamics/` exists with the envelope-follower primitive and `dynamics/` is documented as inhabited (moved from prospectus) in `core/primitives/README.md`, in the same atomic commit.
2. **Given** the lab folder, **Then** it contains a README (ballistics theory), the kernel, and a host-only harness, and no portable unit includes a harness.

---

### Edge Cases

- **Silence / zero input**: linear envelope must settle to exactly 0 with no NaN/Inf; dB-domain envelope must clamp to the floor, not −∞.
- **First sample after `init()`/`reset()`**: state is defined and deterministic (envelope starts from 0); no uninitialized read.
- **Very short time constants at low sample rate** (e.g., attack shorter than a sample period at 32 kHz on MCU): the coefficient must remain finite and stable (bounded to [0, 1)); behavior is characterized (see Deferred Decisions).
- **DC input**: peak → |DC|; RMS → |DC|; a DC-blocked path is NOT part of this primitive (that is the consumer's concern).
- **Parameter change mid-stream** (`setAttack`/`setRelease`/`setMode`/…): recomputes coefficients without allocation, click-free enough for control-rate use; no state reset unless `reset()` is called.
- **Peak-hold with hold = 0**: degenerates to plain peak detection (release begins immediately).
- **Negative or zero sample rate / times passed to setters**: guarded to a defined, finite result (no divide-by-zero, no NaN).

## Requirements *(mandatory)*

### Functional Requirements

**Interface & core behavior**

- **FR-001**: The system MUST provide an `EnvelopeFollower` primitive with an allocation-free lifecycle: `init(float sampleRate)`, per-parameter `set*` methods, `reset()`, and `float process(float)`, matching the established `SvfPrimitive`/`Waveshaper` primitive idiom.
- **FR-002**: `process(float)` MUST accept one input sample and return the current envelope value: a **linear amplitude** by default, or a **decibel** value when the detection domain is decibel.
- **FR-003**: The system MUST support selecting the detection mode among **peak** (absolute value), **RMS** (moving root-mean-square), and **peak-hold** (peak latched for a hold time before releasing), via an enum-selected `setMode`.
- **FR-004**: The system MUST support selecting the ballistics topology among **branching** (single state; attack coefficient on rising rectified input, release coefficient on falling) and **decoupled** (release smoother feeding an attack smoother), via an enum-selected `setBallistics`.
- **FR-005**: The system MUST support a **smooth** variant (attack coefficient applied in both smoothing stages) toggled via `setSmooth(bool)`, applicable to both topologies.
- **FR-006**: The system MUST support selecting the detection domain among **linear** (base contract) and **decibel** (convert to dB before smoothing) via an enum-selected `setDomain`.
- **FR-007**: The system MUST accept attack and release times in **seconds** (`setAttack`, `setRelease`), interpreted as the time to reach **1 − 1/e (~63%)** of a step target.
- **FR-008**: The system MUST accept a **hold** time in seconds (`setHold`), effective in peak-hold mode and ignored in other modes.
- **FR-009**: The system MUST accept an **RMS window / averaging** control (`setRmsWindow`) governing the RMS mean-square accumulation (exact averaging model deferred — see Deferred Decisions).
- **FR-010**: `reset()` MUST clear all detector and smoother state to a defined initial condition (envelope 0), safe to call while the stream is stopped.

**Signal processing**

- **FR-011**: In peak mode the detector MUST rectify to `|x|`; in RMS mode it MUST square to `x²`, accumulate a moving mean-square, and take the square root to return amplitude (in the linear domain); in peak-hold mode it MUST latch `|x|` peaks and apply the hold timer before release.
- **FR-012**: In the decibel domain the detector MUST convert the detected level to dB **before** the ballistics smoothing, and return the smoothed dB value (with a defined floor near zero — see Deferred Decisions).
- **FR-013**: Attack/release one-pole coefficients MUST be computed as `coeff = exp(−1/(τ·sampleRate))` (or an equivalent characterized mapping) and cached in the `set*` methods; they MUST NOT be recomputed per sample.
- **FR-014**: The branching topology MUST use the attack coefficient when the (rectified/converted) input exceeds the current envelope and the release coefficient otherwise; the decoupled topology MUST route the level through a release smoother and then an attack smoother.
- **FR-015**: The peak-hold hold duration MUST be realized as a sample counter derived once (in `setHold`/`init`), not recomputed per sample; a new higher peak during the hold window MUST update the held value and restart the hold window.

**Real-time safety & portability**

- **FR-016**: No method on the audio path (`process`) MUST perform heap allocation, take locks, or do unbounded work; any RMS averaging state MUST be sized/prepared in `init()`, never in `process()`.
- **FR-017**: The primitive and lab kernel MUST compile and run with no JUCE / libDaisy / Teensy (platform) knowledge, and MUST be suitable for MCU targets (Daisy/Teensy) — favoring cheap per-sample math and making the decibel conversion opt-in.
- **FR-018**: All parameter setters MUST guard against degenerate inputs (non-positive sample rate or times) so that no `process()` output is NaN/Inf and every coefficient stays within `[0, 1)`.

**Structure, graduation & gating**

- **FR-019**: The concept MUST be authored as `core/labs/envelope-follower/` (README ballistics theory + RT-safe kernel + host-only harness) and then graduated (`git mv` the kernel) into `core/primitives/dynamics/`, refining in place — never re-derived.
- **FR-020**: The `core/primitives/dynamics/` category directory and its first inhabitant MUST be created in **one atomic commit** (inhabit-before-creating), and `core/primitives/README.md` MUST move `dynamics/` from a prospectus family to an inhabited category in that same commit.
- **FR-021**: `scripts/check-portability.sh` (run in CI, never as a git hook) MUST be extended to cover `core/labs/envelope-follower/**` and `core/primitives/dynamics/**` for harness-isolation, dependency-direction, platform-independence, and file-size checks.
- **FR-022**: Every source file introduced MUST stay within ~300–500 lines and use strict typing (no unchecked casts).

**Scope boundary (Principle XI — one concept at a time)**

- **FR-023**: The feature MUST deliver the level detector and its lab **only**. The static gain computer (threshold/ratio/knee/makeup), the VCA / gain application, sidechain EQ/filtering, and lookahead MUST NOT be implemented here — they are owned by `design:feature/compressors` and its siblings. The boundary MUST be recorded so the downstream compressor item does not read as overlapping.

### Key Entities

- **EnvelopeFollower**: the stateful level-detector primitive. Attributes (no implementation detail): detection mode, ballistics topology, smooth flag, detection domain, attack time, release time, hold time, RMS window, sample rate; current envelope state. Consumed by dynamics effects and meters.
- **DetectMode**: enumerated detector selection — peak, rms, peak-hold.
- **Ballistics**: enumerated smoothing topology — branching, decoupled.
- **DetectDomain**: enumerated detection domain — linear, decibel.
- **Envelope signal**: the control-rate output — a linear amplitude or a dB value tracking the input level.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For a unit step, the peak-mode envelope reaches 1 − 1/e (~63%) of the target within the configured attack time, within a stated tolerance (e.g., ≤ 10% of the attack time), across each supported ballistics topology.
- **SC-002**: For a 1 → 0 step, the envelope decays to ~37% of its start value within the configured release time, within the stated tolerance.
- **SC-003**: For a steady sine of amplitude A, the peak-mode envelope settles to A within tolerance and the RMS-mode envelope settles to A/√2 within tolerance.
- **SC-004**: In RMS mode on a steady sine, the settled envelope ripple stays below a stated peak-to-peak bound.
- **SC-005**: In peak-hold mode, a detected peak is held within tolerance for the configured hold time (± one control period) before the release begins.
- **SC-006**: In the decibel domain, the measured attack time (to ~63% of the dB change) is equal across two input levels differing by ≥ 20 dB, within tolerance — demonstrating level-independent time constants; the linear domain does not exhibit this equality.
- **SC-007**: A no-allocation test confirms zero heap allocation on the `process()` path across all modes, topologies, and domains.
- **SC-008**: No input (silence, DC, impulse, low-sample-rate short-τ) produces a NaN/Inf output in any configuration; the linear envelope of silence is exactly 0 and the dB envelope of silence equals the defined floor.
- **SC-009**: The portability gate passes over `core/labs/envelope-follower/**` and `core/primitives/dynamics/**` (harness isolation, dependency direction, platform independence, file size), and `dynamics/` is documented as inhabited in `core/primitives/README.md`.
- **SC-010**: The graduation lands the category directory and its first inhabitant in a single atomic commit, with the lab (README + kernel + host-only harness) present and no portable unit including a harness.

## Assumptions

- **Consumers call `process()` per sample** (the established primitive contract); block processing, if any, is the consumer's loop — not this primitive's concern.
- **Validation reuses the shipped measurement (stimulus/response) infrastructure** (`tests/core/measurement-*`, `measurement-support.h`, `tests/support/svf-reference.h`) for step/response and sine-envelope assertions, following the `svf-reference` named-tolerance pattern.
- **The full mode/topology/domain catalog is captured in this spec.** Which subset lands in the *first graduated cut* is a planning/sequencing decision, not a scope cut (see Deferred Decisions); the design record captures everything per the capture-over-YAGNI house rule.
- **Time-constant convention** is the 1 − 1/e (~63%) seconds convention for the one-pole smoothers; whether it is applied identically to RMS (whose effective constant is shaped by the mean-square stage) is a Deferred Decision.
- **Default configuration** on `init()` is peak mode, branching topology, non-smooth, linear domain, with implementation-chosen default attack/release — so a consumer that only calls `init()` + `process()` gets a working peak follower (US1).
- **Dependency**: `multi:feature/phase-nonlinear-dsp` is complete; the measurement infrastructure and the three-layer/portability tooling it relies on are shipped.

## Deferred Decisions *(for `/speckit-clarify`)*

Captured (not resolved) per the capture-over-YAGNI house rule; the design record parks these and
`/speckit-clarify` is the sanctioned pass to resolve them before planning:

- **First graduated cut** — which `DetectMode`s and `Ballistics` topologies land in the first graduated primitive vs stay captured-for-later.
- **RMS averaging model** — one-pole leaky integrator (release-derived constant) vs an explicit windowed average; whether `setRmsWindow` is an independent parameter or derived from release.
- **dB-domain floor** — the floor level (e.g., −120 dB) and −∞ guard near zero, and its interaction with attack tracking from silence.
- **Peak-hold × decoupled composition** — whether peak-hold composes with the two-stage decoupled smoother or is defined for the branching path only in the first cut.
- **1 − 1/e convention for RMS** — whether the seconds→coefficient mapping is identical for RMS or adjusted for the mean-square stage.
- **Low-sample-rate coefficient accuracy** — whether `exp(−1/(τ·fs))` needs a higher-order correction for very short time constants at MCU sample rates (≤ 32 kHz).

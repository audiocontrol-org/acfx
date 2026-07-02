---
title: Envelope Followers — Dynamics Level-Detector Primitive — Design Record
date: 2026-07-01
roadmap-item: design:primitive/envelope-followers
status: approved
---

# Envelope Followers — Dynamics Level-Detector Primitive

Establish the **first primitive of `phase-dynamic-systems`** and the **first
inhabitant of the `dynamics/` primitive category**: a level detector that turns an
audio signal into a control-rate amplitude envelope. This is the foundational
sidechain building block every dynamics processor (compressor, limiter, gate) and
every level meter (VU, PPM) composes. It is the **second concept to walk the
three-layer graduation pattern greenfield** — waveshaping proved the forward
lab→primitive path first; envelope-following is the second, in a new category.

## Problem domain

Constitution **Principle IX (Progressive Layered Architecture)** and the program
prospectus declare `phase-dynamic-systems`, whose foundational deliverable is the
level detector. `core/primitives/README.md` already names the **`dynamics/`**
prospectus family — *"Amplitude-envelope processors. Intended inhabitants:
peak/RMS detectors, gain computers, VCA envelopes for compressors, limiters,
gates."* This item materializes that category on disk with its **first
inhabitant**, the detector.

On disk today:

- `core/primitives/` has `filters/`, `delays/`, `modulation/`, `nonlinear/`,
  `oversampling/`, `analysis/` — **no `dynamics/` category exists yet.** Per the
  inhabit-before-creating rule (FR-008, SC-006) the folder is created only in the
  same atomic commit as its first primitive.
- `core/labs/` holds `state-variable-filter/`, `waveshaping/`, `saturation/`,
  `oversampling/`. Waveshaping was the first greenfield lab→primitive graduation;
  envelope-following is the second, and the first in the `dynamics/` category.
- The **measurement infrastructure** (shipped) provides a stimulus/response harness
  and analysis tooling. A level detector's correctness is a **time-domain step /
  envelope-tracking** property (attack/release time, RMS accuracy, ripple) — exactly
  what a stimulus/response harness measures, distinct from the harmonic tooling the
  nonlinear labs used.

A **level detector** is the sidechain front end: it rectifies or squares the input
and applies attack/release ballistics to produce an envelope. Per **Principle XI
(one concept at a time)** the scope is the **detector only** — NOT the static gain
computer (threshold/ratio/knee/makeup) and NOT the VCA/gain application, both of
which the `dynamics/` taxonomy lists as *separate* inhabitants and which
`design:feature/compressors` owns. Drawing that boundary here keeps the compressor
item's charter intact.

### Forces and constraints

- **Real-time safety (Principle VI):** no heap allocation, locks, or unbounded work
  in `process()`. Time-constant coefficients are recomputed only in the `set*`
  methods; any RMS averaging state is sized/prepared in `init()`, never in
  `process()`.
- **Platform-independent core, thin adapters (Principle IV):** the kernel and
  primitive compile with no JUCE / libDaisy / Teensy knowledge and must run on MCU
  targets (Daisy / Teensy), where per-sample transcendentals are costly — motivating
  a cheap branching topology and an optional (not mandatory) dB conversion.
- **Evolve, don't discard (Principle IX):** the lab kernel relocates into
  `primitives/dynamics/` and refines in place; it is never re-derived.
- **Explicit gates, never hooks (Commandment II / Principle II):** enforcement
  extends `scripts/check-portability.sh` (already in CI), never a git hook.
- **Strict typing, small modules (Principle VII):** no `any`/unchecked casts; files
  within ~300–500 lines.
- **Measurable engineering (Principle X):** every mode/topology is validated by
  objective time-domain measurement (attack/release time to 1−1/e, RMS accuracy,
  ripple) reusing the shipped stimulus/response infrastructure, not by ear.
- **One concept at a time (Principle XI):** the level detector + its lab only; no
  gain computer, no VCA, no sidechain EQ, no lookahead.

## Solution space

Five decisions were explored against alternatives. The chosen positions compose
into `## Decisions`; the rejected alternatives and their reasons are recorded here.
The operator selected the capture-everything option at every fork, consistent with
the capture-over-YAGNI house rule.

### Chosen — Lab + graduated primitive (full pattern, end-to-end)

Author `core/labs/envelope-follower/` (README ballistics theory + RT-safe kernel +
host-only harness) and graduate the kernel into `core/primitives/dynamics/`. Walks
the full Theory→Lab→Primitive pattern greenfield for the second time, in a new
category, creating `dynamics/` with its first inhabitant in one atomic commit.

### Rejected — Primitive only (skip the lab)

Author `core/primitives/dynamics/` directly. **Rejected:** breaks the graduation
discipline waveshaping just proved greenfield; the ballistics math (branching vs
decoupled detectors, time-constant derivation) is genuinely worth a lab that
teaches it before it is frozen into a primitive.

### Rejected — Lab only, defer graduation

Author the lab now; graduate later once the catalog is proven. **Rejected:** leaves
`dynamics/` empty and the phase's first deliverable undelivered as a reusable
primitive; graduation is the proof and should not be deferred without cause.

### Chosen — Peak + RMS + peak-hold detection catalog

An enum-selected `DetectMode` capturing three detectors: **peak** (absolute value),
**RMS** (running mean-square → sqrt), and **peak-hold** (peak with a hold time
before release begins). Covers meter ballistics (VU≈RMS, PPM≈peak-hold) and the
compressor/limiter/gate needs downstream. Matches the `SvfMode`/`Shape` enum idiom.
Planning sequences which modes land in the first graduated cut.

### Rejected — Peak + RMS only

Drop peak-hold. **Rejected:** peak-hold is the standard PPM-metering and
transient-limiter behavior; excluding it merely pushes the same complexity
downstream to be re-discovered.

### Rejected — Peak only

Single absolute-value detector. **Rejected:** RMS (program-level) detection is
essential for compressor "feel" and would be the compressor item's first
addition — the detector primitive would ship incomplete.

### Chosen — Branching AND decoupled topologies, both smooth-capable

An enum-selected `Ballistics` capturing both canonical smoothing topologies from
the Reiss et al. compressor tutorial: **branching** (one state variable; attack
coefficient when the rectified input rises, release coefficient when it falls) and
**decoupled** (a release smoother feeds an attack smoother, eliminating the
branching detector's release-then-attack tracking artifact). A `setSmooth(bool)`
flag selects the "smooth" variant of each (attack coefficient applied in both
stages). Captures the full literature catalog; the decoupled-smooth topology is the
modern compressor reference, while branching remains the cheap MCU/gate option.

### Rejected — Branching only

Single one-pole, coefficient by direction. **Rejected:** the branching detector's
well-documented artifact (a fast transient followed by decay tracks poorly) is
exactly what the decoupled topology fixes; compressors will need the decoupled path.

### Rejected — Decoupled-smooth only

Ship only the modern reference topology. **Rejected:** discards the cheaper
single-state branching detector that MCU targets and simple gates may prefer; it
would be re-added later under the same charter.

### Chosen — Capture both detection domains; linear is the base contract

Detection returns a **linear amplitude** envelope by default (the base contract). A
`DetectDomain::decibel` mode is captured as a peer that converts to dB before
smoothing, because dB-domain smoothing yields **amplitude-independent** attack/
release time constants — the property real compressors rely on for consistent feel
across input levels. Enum-selected; planning cuts the first set.

### Rejected — Linear only

Detect and smooth purely in linear amplitude; downstream converts to dB.
**Rejected:** discards the level-independent-time-constant property, which the
compressor item would then re-add — the dB-domain smoother belongs to the detector
concept.

### Rejected — Log/dB only

Convert to dB immediately and always smooth in dB. **Rejected:** forces a per-sample
`log` even on consumers (limiters, gates, meters) that want a cheap linear
envelope, and on MCU targets that per-sample transcendental is a real cost.

## Decisions

1. **Three-layer graduation, greenfield #2.** Author `core/labs/envelope-follower/`
   (`README.md` ballistics theory + walkthrough naming `primitives/dynamics/` as the
   graduation target; RT-safe kernel; host-only `harness/`), then `git mv` the kernel
   into `core/primitives/dynamics/` and refine in place. The `dynamics/` category
   directory and its first inhabitant are created in **one atomic commit**
   (inhabit-before-creating, SC-006); `core/primitives/README.md` moves `dynamics/`
   from prospectus to inhabited in the same commit.

2. **Interface — enum-selected stateful primitive (the `SvfPrimitive`/`Waveshaper`
   idiom).**
   ```cpp
   enum class DetectMode   : std::uint8_t { peak, rms, peakHold };
   enum class Ballistics   : std::uint8_t { branching, decoupled };
   enum class DetectDomain : std::uint8_t { linear, decibel };

   class EnvelopeFollower {
     void  init(float sampleRate) noexcept;    // preps RMS/detector state; no alloc in process()
     void  setMode(DetectMode) noexcept;
     void  setBallistics(Ballistics) noexcept;
     void  setSmooth(bool) noexcept;           // smooth variant: attack coeff applied in both stages
     void  setDomain(DetectDomain) noexcept;
     void  setAttack(float seconds) noexcept;  // time to reach 1 − 1/e (~63%) of a step
     void  setRelease(float seconds) noexcept;
     void  setHold(float seconds) noexcept;    // peakHold only; ignored in other modes
     void  setRmsWindow(float seconds) noexcept;
     void  reset() noexcept;                   // clears detector + smoother state
     float process(float x) noexcept;          // returns envelope (linear amplitude, or dB if domain=decibel)
   };
   ```
   Allocation-free `init/set*/reset/process` shape mirroring the established
   primitive idiom.

3. **Per-sample signal chain.**
   ```
   process(x):
     d = detect(x)          // peak → |x| ; rms → x² ; peakHold → |x| with hold counter
     d = (rms) ? runningMeanSquare(d) : d      // one-pole / windowed mean-square accumulation
     s = (domain==decibel) ? toDecibels(d) : d // optional pre-smoothing dB conversion
     e = ballistics(s)      // branching: one state, coeff by rise/fall
                            // decoupled: release smoother → attack smoother
                            // smooth:    attack coeff in both stages
     e = (rms && domain==linear) ? sqrt(e) : e // complete the RMS in the linear domain
     return e
   ```
   The RMS `sqrt` and the dB conversion are the only non-trivial per-sample math and
   both are bounded; no allocation, no locks.

4. **Time-constant convention.** Attack/release are specified in **seconds to reach
   1 − 1/e (~63%)** of a step target; the one-pole coefficient
   `coeff = exp(−1 / (τ · sampleRate))` is computed in the `set*` methods and cached,
   never per-sample. The peak-hold `hold` time is a sample counter derived once in
   `setHold()`/`init()`.

5. **Validation reuses the shipped measurement (stimulus/response) infrastructure.**
   The harness drives the existing stimulus/response tooling to assert: attack-time
   and release-time accuracy (time to 1 − 1/e of a step, per topology), sine-envelope
   accuracy (`peak → A`, `rms → A/√2` for a sine of amplitude A), RMS ripple bound,
   and peak-hold dwell. Assertions use analytic truths + named tolerances (the
   `svf-reference` pattern) and the `no-allocation-test` covers RT-safety.

6. **Portability gate extended.** `scripts/check-portability.sh` (in CI, never a
   hook) learns `core/labs/envelope-follower/**` and `core/primitives/dynamics/**`
   so its harness-isolation, dependency-direction, platform-independence, and
   file-size checks cover the new units.

7. **Scope discipline (Principle XI).** This item delivers the level detector + its
   lab only. **Excluded:** the static gain computer (threshold/ratio/knee/makeup),
   the VCA / gain application, sidechain EQ/filtering, and lookahead — all owned by
   `design:feature/compressors` and its siblings. The boundary is recorded so the
   compressor item does not read as overlapping.

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later
scoping pass (`/speckit-clarify` / planning), **not** discarded:

- **First graduated cut.** Which `DetectMode`s and `Ballistics` topologies land in
  the first graduated primitive vs stay captured-for-later. A sequencing decision,
  not a design blocker.
- **RMS averaging model.** Whether the running mean-square is a one-pole leaky
  integrator (release-derived time constant) or an explicit windowed average, and
  whether the RMS window is an independent parameter or derived from release.
- **dB-domain floor.** How `toDecibels` handles values near/at zero (−∞ guard, floor
  level such as −120 dB) and the interaction of that floor with attack tracking from
  silence.
- **Hold-time interaction with the decoupled topology.** Whether peak-hold composes
  cleanly with the two-stage decoupled smoother or is defined only for the branching
  path in the first cut.
- **Time-constant convention across modes.** Whether the 1 − 1/e seconds convention
  is applied identically to RMS (whose effective time constant is shaped by the
  mean-square stage) or adjusted per mode.
- **Coefficient accuracy at low sample rates.** Whether the `exp(−1/(τ·fs))` one-pole
  needs a higher-order correction for very short time constants at low sample rates
  (MCU targets sometimes run 32 kHz or below).

## Provenance

- **Roadmap item:** `design:primitive/envelope-followers` (status `planned` →
  `designing`), `depends-on: multi:feature/phase-nonlinear-dsp` (complete),
  `part-of: multi:feature/phase-dynamic-systems`. Design pointer set via
  `stackctl workflow link-design` to this file.
- **Constitution:** Principle IX (Progressive Layered Architecture), with supporting
  Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII (Strict
  Typing & Small Modules), X (Measurable Engineering), XI (One Concept at a Time).
  `.specify/memory/constitution.md`.
- **Program vision:**
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
  (`phase-dynamic-systems`; the `dynamics/` primitive category; the four-stage
  graduation model).
- **Pattern precedent:**
  `docs/superpowers/specs/2026-06-30-waveshapers-design.md` (the greenfield
  lab→primitive graduation, enum-selected stateful primitive, portability-gate
  extension) and
  `docs/superpowers/specs/2026-06-29-three-layer-structure-design.md`.
- **Reused infrastructure:**
  `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` and the
  shipped stimulus/response tooling (`tests/core/measurement-*`,
  `measurement-support.h`, `tests/support/svf-reference.h`).
- **Current code surveyed:** `core/primitives/{filters/svf-primitive,delays/
  delay-line,modulation/lfo,nonlinear/waveshaper}.h`, `core/primitives/README.md`
  (the `dynamics/` prospectus entry), `core/labs/`, `core/dsp/`,
  `scripts/check-portability.sh`.
- **Design method:** `superpowers:brainstorming` driven in-session under the
  `/stack-control:design` frontend; house-rules block injected (capture-over-YAGNI,
  ≥2 solution-space alternatives, required sections, operator-approval marker,
  handoff to `/stack-control:define`).
- **Decisions driven by the operator** across five forks: scope boundary
  (detector only), detection modes (peak + RMS + peak-hold), ballistics (branching +
  decoupled, both smooth-capable), detection domain (both, linear base + dB peer),
  altitude (lab + graduated primitive). The operator consistently chose the
  capture-everything option, aligning with the capture-over-YAGNI house rule.
- **Reference:** J. D. Reiss et al., *"Digital Dynamic Range Compressor Design — A
  Tutorial and Analysis"* (the branching vs decoupled, level vs smooth detector
  topology taxonomy).
- **Next step:** operator records the `design-approved:` marker on the roadmap node;
  on a met `design-to-spec` gate, hand off to `/stack-control:define` to author the
  Spec Kit spec.

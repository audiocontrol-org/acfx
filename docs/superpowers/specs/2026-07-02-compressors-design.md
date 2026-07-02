---
title: Compressors — Dynamics Gain-Computer Primitive + CompressorEffect — Design Record
date: 2026-07-02
roadmap-item: design:feature/compressors
status: proposed
---

# Compressors — Gain Computer + Compressor Effect

Deliver the **first dynamics *processor*** of `phase-dynamic-systems`: a
compressor that composes the shipped `EnvelopeFollower` level detector with a new
**gain-computer primitive** (the static curve) and a gain-application stage,
wrapped as a host-facing `Effect`. This is the concept the envelope-follower
primitive was built to feed — the level detector produces the sidechain envelope;
this item turns that envelope into gain reduction and applies it.

The item walks the **full three-layer vertical in one cut**: a lab teaches the
gain-computer math and the compressor composition, the stateless gain computer
graduates into `core/primitives/dynamics/` as the category's **second
inhabitant**, and a `CompressorEffect` ships in `core/effects/compressor/` mirroring
the shipped `saturation/` effect idiom exactly.

## Problem domain

Constitution **Principle IX (Progressive Layered Architecture)** and the program
prospectus declare `phase-dynamic-systems`. Its foundational primitive — the level
detector — shipped as `core/primitives/dynamics/envelope-follower.h`. That design
record (`docs/superpowers/specs/2026-07-01-envelope-followers-design.md`, Decision 7)
**explicitly carved out for this item**: the static gain computer
(threshold/ratio/knee/makeup), the VCA / gain application, sidechain EQ/filtering,
and lookahead. This item builds exactly that carve-out.

On disk today:

- `core/primitives/dynamics/` exists with **one inhabitant** (`envelope-follower.h`).
  `core/primitives/README.md` names the `dynamics/` family — *"peak/RMS detectors,
  gain computers, VCA envelopes for compressors, limiters, gates."* The **gain
  computer** and **VCA envelope** are named-but-unbuilt; this item materializes the
  gain computer as the second inhabitant.
- `core/effects/` holds `svf/`, `saturation/`, `modulated-delay/`. `SaturationEffect`
  and `SvfEffect` are the **effect idiom** to mirror: a constexpr
  `ParameterDescriptor` table as the single source of parameter truth, a lock-free
  atomic cross-thread parameter handoff, allocation-free `prepare/process/reset`,
  per-channel core state, and a `static_assert` descriptor-validity guard.
- The composable building blocks the compressor needs are **already shipped**:
  `EnvelopeFollower` (detection + ballistics + dB domain), `SvfPrimitive`
  (sidechain highpass), `DelayLine` (lookahead). This item is primarily a
  **composition**, not new DSP kernels — with one genuinely new kernel, the static
  gain computer.
- The **measurement infrastructure** (shipped stimulus/response harness + analysis)
  measures exactly the compressor's correctness properties: static-curve accuracy
  (gain reduction vs input level), attack/release time, knee continuity, latency.

A **compressor** is: detect the sidechain level → map that level through a static
curve to a gain-reduction value → smooth it with attack/release ballistics → apply
it to the main path. The level detector already exists; this item owns the **curve**,
the **ballistics placement**, the **topology (feedforward/feedback)**, the
**sidechain conditioning**, the **lookahead**, and the **gain application**.

### Forces and constraints

- **Real-time safety (Principle VI):** no heap allocation, locks, or unbounded work
  in `process()`. All coefficients recompute in `set*`/`prepare`; the lookahead
  buffer is sized in `prepare()`, never in `process()`. The gain computer is pure
  branch-only arithmetic; the only per-sample transcendentals (`sqrt`, `log`) live
  inside the already-shipped `EnvelopeFollower`.
- **Platform-independent core, thin adapters (Principle IV):** the kernel, the
  primitive, and the effect compile with no JUCE / libDaisy / Teensy knowledge and
  run on MCU targets. The parameter-count and per-sample cost must stay MCU-viable —
  motivating a stateless gain computer and optional (not mandatory) advanced
  features.
- **Compose, don't re-derive (Principle IX):** reuse the shipped `EnvelopeFollower`,
  `SvfPrimitive`, and `DelayLine` primitives rather than re-implementing detection,
  filtering, or delay inside the compressor.
- **Explicit gates, never hooks (Commandment II / Principle II):** enforcement
  extends `scripts/check-portability.sh` (already in CI), never a git hook.
- **Strict typing, small modules (Principle VII):** no `any`-equivalents / unchecked
  casts; each file within ~300–500 lines (the large captured parameter set may push
  the effect wrapper toward a split — see open questions).
- **Measurable engineering (Principle X):** every mode/topology validated by
  objective time-domain measurement (static-curve accuracy, attack/release time,
  knee continuity, feedback convergence, lookahead latency) against analytic truths
  and named tolerances — not by ear.
- **One concept at a time (Principle XI):** the compressor concept (detector-fed
  static dynamics processing). **Excluded:** program-dependent / "auto" ballistics
  (opto / vari-mu character), multiband, dynamic EQ — separate sibling/future items.

## Solution space

Five decisions were explored against alternatives. The chosen positions compose
into `## Decisions`; the rejected alternatives and their reasons are recorded here.
The operator selected the capture-everything option at every fork, consistent with
the capture-over-YAGNI house rule.

### Chosen — Full vertical (lab + graduated gain-computer primitive + CompressorEffect)

Author `core/labs/compressor/` (README theory + kernel + host-only harness),
graduate a stateless `gain-computer.h` into `core/primitives/dynamics/`, and ship a
`CompressorEffect` (with its `CompressorCore` composition kernel) in
`core/effects/compressor/`. Delivers the whole vertical — the reusable static-curve
primitive AND the usable host-facing processor — in one item, exactly as saturation
shipped (lab → effect) alongside envelope-follower's lab → primitive graduation.

### Rejected — Primitive only (gain-computer primitive, defer the effect)

Deliver only the graduated gain-computer primitive; make the `CompressorEffect` a
follow-on. **Rejected:** leaves the phase's first *processor* undelivered and the
just-shipped `EnvelopeFollower` still without a consumer that proves the
composition; the effect is the proof the primitives compose.

### Rejected — Effect only (no new primitive)

Inline the gain-computer math inside the effect core; ship no graduated primitive.
**Rejected:** traps the reusable static-curve building block inside the effect,
where limiters, gates, and future multiband processors cannot reach it; the gain
computer is a first-class `dynamics/` inhabitant the taxonomy already names.

### Chosen — Full dynamics catalog (compress + limit + expand + gate), soft/hard knee

One enum-selected `GainMode` gain computer covering downward compression, limiting
(ratio → ∞), downward expansion, and gating, each with a configurable soft/hard
knee. Captures the whole static-curve family the `dynamics/` taxonomy implies,
mirroring how `EnvelopeFollower` captured its full detector/ballistics catalog in
one primitive.

### Rejected — Compressor + limiter only

Downward-only (compression + limiting as the ratio → ∞ case), no expander/gate.
**Rejected:** the expander/gate curve is the same static-map machinery reflected
below threshold; excluding it merely pushes the identical knee/range complexity to a
sibling to be re-discovered.

### Rejected — Compressor only

Threshold + ratio + knee → gain reduction; limiter is just a high ratio, expander
and gate are separate items. **Rejected:** limiting-as-a-mode (hard `range`/knee
behavior) and gating are standard dynamics-processor expectations; a
"compressor-only" curve would ship the primitive visibly incomplete.

### Chosen — Both feedforward and feedback topologies (enum-selected)

An enum-selected `Detection` capturing **feedforward** (detector reads the input;
modern, predictable — the default) and **feedback** (detector reads the previous
*output* sample, post-gain-reduction; the smoother vintage optical/FET character).
Captures both, mirroring `EnvelopeFollower` capturing branching + decoupled. Feedback
is the topology behind the 1176/LA-2A "feel."

### Rejected — Feedforward only

Detector always reads the input. **Rejected:** discards the feedback character that
defines a whole class of reference compressors; it would be re-added later under the
same charter, and the feedback path is a small state dependency, not a new kernel.

### Chosen — Capture both ballistics sites (level-smoothed AND gain-smoothed), enum-selected

An enum-selected `BallisticsSite` capturing both architectures from the Reiss
tutorial: **level** (attack/release smooth the detected level in the
`EnvelopeFollower`; the gain computer sees an already-smoothed level) and **gain**
(the `EnvelopeFollower` runs ~instantaneous, the static curve maps the raw level, and
a second smoother applies attack/release to the *gain-reduction* signal). They voice
audibly differently; capture both.

### Rejected — Gain-smoothed only

The Reiss-recommended reference architecture (static curve on the instantaneous
level, ballistics on the gain-reduction). **Rejected:** discards the level-smoothed
voicing and under-uses the `EnvelopeFollower`'s own ballistics, which the level site
reuses directly.

### Rejected — Level-smoothed only

Ballistics live entirely in the `EnvelopeFollower` on the detected level.
**Rejected:** simplest reuse, but cannot independently smooth the gain-reduction
signal and voices differently from most reference compressors; the gain site would
be re-added under the same charter.

### Chosen — Capture the full sidechain/advanced feature set

Capture, for later sequencing (not YAGNI-cut): **external sidechain input** (key
signal for ducking / de-essing / keyed gating), **sidechain filter** (pre-detector
SVF highpass/tilt, composing the shipped `SvfPrimitive`), **lookahead** (main-path
`DelayLine` so the detector sees ahead, reporting latency to the host), and
**auto-makeup + stereo/multichannel linking** (derive makeup from threshold/ratio;
link detection across channels via max so the stereo image does not shift).

### Rejected — Minimal effect (main-input detection only, manual makeup, per-channel)

Ship the compressor with only internal-input feedforward detection and manual
makeup. **Rejected:** the envelope-follower design *already parked* sidechain
filtering and lookahead specifically for this item; dropping them here would strand
that carve-out and force a near-immediate follow-on. Sequencing which land in the
first graduated cut is an open question, not a reason to omit them from the design.

## Decisions

1. **Full three-layer vertical, one item.** Author `core/labs/compressor/`
   (`README.md`: gain-computer curve math incl. soft-knee formulas, feedforward vs
   feedback, the level-vs-gain ballistics-site tradeoff, and gain-reduction theory
   from the Reiss tutorial; an RT-safe kernel; a host-only `harness/`). Graduate the
   stateless gain computer via `git mv` into
   `core/primitives/dynamics/gain-computer.h` (the `dynamics/` category's **second
   inhabitant**; `core/primitives/README.md` moves "gain computers" from prospectus
   to inhabited in the same commit). Ship `CompressorCore` + `CompressorEffect` in
   `core/effects/compressor/`.

2. **`GainComputer` primitive — stateless, pure static curve.**
   ```cpp
   enum class GainMode : std::uint8_t { compress, limit, expand, gate };

   class GainComputer {
     void  setMode(GainMode) noexcept;
     void  setThreshold(float dB) noexcept;
     void  setRatio(float ratio) noexcept;      // limit ≈ ratio → ∞
     void  setKnee(float dB) noexcept;          // 0 = hard knee; >0 = soft width
     void  setRange(float dB) noexcept;         // expander/gate floor (max attenuation)
     float computeGainDb(float levelDb) const noexcept;  // ≤ 0 attenuation; pure fn of level+params
   };
   ```
   No ballistics, no runtime state — a pure function of level + parameters. That is
   what makes it a first-class reusable `dynamics/` primitive (limiters, gates, and
   future multiband processors compose it). `compress`: above threshold,
   `out = thr + (level − thr)/ratio`; soft knee is a quadratic (C¹-continuous) blend
   across the knee width. `limit`: ratio → ∞ within the knee. `expand`/`gate`: below
   threshold, downward by ratio (expand) or toward the `range` floor (gate).

3. **`CompressorCore` — the composition kernel (per channel).**
   ```
   process(x, sc?):
     key   = externalSC ? sc : x
     key   = (scHpf > 0) ? svfHighpass(key) : key         // composes SvfPrimitive
     level = envelopeFollower.process(key)                 // composes dynamics/EnvelopeFollower (dB domain)
                                                           // feedBack: key uses the previous output sample
     grDb  = gainComputer.computeGainDb(level)             // static curve
     grDb  = (site == gain) ? smoothGain(grDb) : grDb      // gain-site attack/release (2nd smoother)
     gain  = dbToLin(grDb + makeupDb)
     main  = (lookahead > 0) ? delayLine.read(x) : x       // composes DelayLine; reports latency
     y     = main * gain
     y     = mix * y + (1 − mix) * x                       // parallel / NY compression
     y     = y * outputGain
   ```
   `Detection { feedForward, feedBack }` and `BallisticsSite { level, gain }` are
   enum forks. In the **level** site the `EnvelopeFollower` runs its own
   attack/release; in the **gain** site it runs ~instantaneous and a second smoother
   (a one-pole / second `EnvelopeFollower` instance) applies the ballistics to the
   gain-reduction signal. Stereo/multichannel **linking** computes one detector value
   across linked channels (max) and applies a common gain so the image is stable.

4. **`CompressorEffect` — the host wrapper (the `SaturationEffect` idiom, exactly).**
   Constexpr `ParameterDescriptor` table as the single source of parameter truth;
   lock-free atomic cross-thread parameter handoff (publish on any thread, consume at
   the top of `process()`); allocation-free `prepare/process/reset`; per-channel
   `CompressorCore` state; `static_assert` descriptor-validity guard. Captured
   parameter set: `threshold, ratio, knee, attack, release, mode {compress|limit|
   expand|gate}, detection {feedForward|feedBack}, detector {peak|rms}, ballisticsSite
   {level|gain}, range, scHpfHz, lookaheadMs, makeup, autoMakeup, stereoLink
   {perChannel|linked}, mix, output`. Lookahead latency is reported to the host.

5. **Validation reuses the shipped measurement (stimulus/response) infrastructure.**
   Assert: **static-curve accuracy** (gain reduction vs input level at fixed
   ratio/knee, per mode, against the analytic curve), **knee continuity** (C¹ across
   the soft knee), **attack/release time** to 1 − 1/e per ballistics-site,
   **feedback convergence** (feedback topology settles to the feedforward fixed point
   for a steady input), **lookahead latency == N samples**, **stereo-link image
   stability**, and **auto-makeup unity** (a below-threshold signal passes at unity).
   Assertions use analytic truths + named tolerances (the `svf-reference` pattern);
   the `no-allocation-test` covers RT-safety.

6. **Portability gate extended.** `scripts/check-portability.sh` (in CI, never a
   hook) learns `core/labs/compressor/**`, `core/primitives/dynamics/gain-computer.h`,
   and `core/effects/compressor/**` so its harness-isolation, dependency-direction,
   platform-independence, and file-size checks cover the new units.

7. **Scope discipline (Principle XI).** This item delivers the detector-fed static
   dynamics processor: the gain-computer primitive + the compressor lab + the
   `CompressorEffect`. **Excluded:** program-dependent / "auto" attack-release
   (opto/vari-mu emulation) — owned by `design:feature/program-dependent-saturation`
   / `design:feature/tape-dynamics`; multiband dynamics and dynamic EQ — separate
   future items. The gain application here is a simple per-sample multiply folded into
   `CompressorCore`, not a separately graduated "VCA envelope" primitive (that
   taxonomy entry remains available for a later item if a stateful VCA is warranted).

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later scoping
pass (`/speckit-clarify` / planning), **not** discarded:

- **First graduated cut.** Which of the large captured parameter/feature set lands in
  the first graduated `CompressorEffect` vs stays captured-for-later: feedback
  topology, the gain ballistics-site, expander/gate modes, external sidechain,
  sidechain HPF, lookahead, auto-makeup, and stereo linking are all captured; a
  sequencing decision, not a design blocker.
- **File-size split.** The captured parameter set (~17 parameters) may push
  `compressor-effect.h` past the ~300–500 line guideline; whether the wrapper splits
  (e.g. a parameter-table header + an apply/denormalize header) is a planning
  decision.
- **Feedback path definition.** Exactly which output the feedback detector reads —
  the post-makeup, post-mix, or pre-mix output sample — and how the one-sample delay
  in the feedback loop interacts with the ballistics site and with lookahead.
- **Ratio/limit parameterization.** Whether `limit` is a distinct `GainMode` or the
  `ratio` parameter's ∞ end, and how the ratio parameter's skew maps (a log/skewed
  1:1…∞:1 range) for usable UI control.
- **Soft-knee formula across modes.** Whether the same quadratic soft-knee blend
  applies identically to expand/gate (where the knee sits below threshold) or needs a
  per-mode form; and how `range` and `knee` interact for the gate.
- **Auto-makeup model.** How auto-makeup is derived (e.g. gain reduction at a
  reference level such as −20 dBFS, or a threshold/ratio closed form) and whether it
  tracks parameters live or is computed once per parameter change.
- **Stereo-link detail.** Whether linking is max-only or also offers sum/average, and
  whether the sidechain HPF/external-key is per-channel or shared across a linked
  group.
- **Detector defaults per topology.** Whether feedback defaults to a different
  detector mode / ballistics than feedforward, given feedback's inherent smoothing.

## Provenance

- **Roadmap item:** `design:feature/compressors` (status `planned` → `designing`),
  `depends-on: multi:feature/phase-nonlinear-dsp` (complete),
  `part-of: multi:feature/phase-dynamic-systems`. Design pointer set via
  `stackctl workflow link-design` to this file.
- **Constitution:** Principle IX (Progressive Layered Architecture), with supporting
  Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII (Strict
  Typing & Small Modules), X (Measurable Engineering), XI (One Concept at a Time).
  `.specify/memory/constitution.md`.
- **Direct predecessor (the carve-out this item fills):**
  `docs/superpowers/specs/2026-07-01-envelope-followers-design.md` — its Decision 7
  and Open Questions explicitly assign the gain computer, VCA/gain application,
  sidechain EQ/filtering, and lookahead to `design:feature/compressors`.
- **Pattern precedent:** `docs/superpowers/specs/2026-06-30-waveshapers-design.md`
  (greenfield lab → primitive graduation, enum-selected primitive, portability-gate
  extension) and the shipped `saturation/` effect (lab → effect; the
  `ParameterDescriptor`-table / atomic-handoff wrapper idiom this item mirrors).
- **Composed primitives (all shipped):**
  `core/primitives/dynamics/envelope-follower.h` (detection + ballistics + dB
  domain), `core/primitives/filters/svf-primitive.h` (sidechain highpass),
  `core/primitives/delays/delay-line.h` (lookahead).
- **Reused infrastructure:**
  `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` and the
  shipped stimulus/response tooling (`tests/core/measurement-*`,
  `measurement-support.h`, `tests/support/svf-reference.h`).
- **Current code surveyed:** `core/primitives/dynamics/envelope-follower.h`,
  `core/effects/saturation/{saturation-effect,saturation-core}.h`, `core/dsp/effect.h`,
  `core/primitives/{filters/svf-primitive,delays/delay-line}.h`,
  `core/primitives/README.md`, `scripts/check-portability.sh`.
- **Design method:** `superpowers:brainstorming` driven in-session under the
  `/stack-control:design` frontend; house-rules block injected (capture-over-YAGNI,
  ≥2 solution-space alternatives, required sections, operator-approval marker, handoff
  to `/stack-control:define`).
- **Decisions driven by the operator** across five forks: altitude (full vertical),
  gain-computer catalog (compress + limit + expand + gate, soft/hard knee), detection
  topology (feedforward + feedback), ballistics site (level + gain), and the
  sidechain/advanced feature set (external SC + SC HPF + lookahead + auto-makeup +
  stereo linking — all captured). The operator consistently chose the
  capture-everything option, aligning with the capture-over-YAGNI house rule.
- **Reference:** J. D. Reiss et al., *"Digital Dynamic Range Compressor Design — A
  Tutorial and Analysis"* (the feedforward vs feedback topology, the level vs
  smoothed-gain ballistics site, the soft-knee static-curve formulas).
- **Next step:** operator records the `design-approved:` marker on the roadmap node;
  on a met `design-to-spec` gate, hand off to `/stack-control:define` to author the
  Spec Kit spec.

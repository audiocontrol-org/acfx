---
title: Program-Dependent Saturation — Envelope-Modulated Nonlinearity + Dynamics-Modulator Primitive — Design Record
date: 2026-07-02
roadmap-item: design:feature/program-dependent-saturation
status: approved
---

# Program-Dependent Saturation — Envelope-Modulated Nonlinearity

Deliver the **first program-dependent (dynamic) saturator** of
`phase-dynamic-systems`: an effect that composes the shipped `EnvelopeFollower`
level detector with the shipped `SaturationCore` nonlinearity and a **new
stateless modulation-mapper primitive**, so the saturation character tracks the
signal's dynamics. Where the compressor turned an envelope into *gain reduction*,
this turns an envelope into **saturation-parameter offsets** (drive, bias, tone,
mix). It is the saturation analog of the compressor, and it fills the boundary the
static `SaturationEffect` explicitly drew for it.

The item walks the **full three-layer vertical in one cut**: a lab teaches the
envelope-modulates-nonlinearity math and the composition, a stateless
`DynamicsModulator` graduates into `core/primitives/dynamics/` as the category's
**third inhabitant**, and a `ProgramDependentSaturationEffect` ships in
`core/effects/program-dependent-saturation/` mirroring the shipped `saturation/`
and `compressor/` effect idiom exactly.

## Problem domain

Constitution **Principle IX (Progressive Layered Architecture)** and the program
prospectus declare `phase-dynamic-systems`. Its foundational primitive — the level
detector — shipped as `core/primitives/dynamics/envelope-follower.h`; its first
*processor* — the compressor — shipped composing that detector with a graduated
`GainComputer`. This item is the phase's **first dynamic *saturator***: the same
detector, feeding not a gain computer but a **modulation of the nonlinearity's
operating point**.

The static `SaturationEffect` **explicitly carved out this item.** Its header
boundary note (`core/effects/saturation/saturation-effect.h`, FR-024) reads:

> This effect is the STATIC-CHARACTER composed saturation;
> program-dependent/dynamic behavior (envelope-tracking drive, dynamic bias,
> tape-style compression) is the separate
> `design:feature/program-dependent-saturation` item (Phase-3 dynamic-systems),
> NOT built here.

This item builds exactly that carve-out.

On disk today:

- `core/primitives/dynamics/` holds **two inhabitants** (`envelope-follower.h`,
  `gain-computer.h`). `core/primitives/README.md` names the `dynamics/` family. The
  **modulation mapper** — an envelope-to-parameter-offset shaper — is a natural
  third inhabitant: it is to *modulation* what `GainComputer` is to *static gain*.
- `core/effects/` holds `svf/`, `saturation/`, `compressor/`, `modulated-delay/`.
  `SaturationEffect` / `CompressorEffect` / `SvfEffect` are the **effect idiom** to
  mirror: a constexpr `ParameterDescriptor` table as the single source of parameter
  truth, a lock-free atomic cross-thread parameter handoff, allocation-free
  `prepare/process/reset`, per-channel core state, and a `static_assert`
  descriptor-validity guard.
- The composable building blocks are **already shipped**: `SaturationCore`
  (`core/effects/saturation/saturation-core.h`) exposes exactly the operating-point
  setters an envelope can drive — `setDrive`, `setBias`, `setTone`, `setMix` — plus
  the 4 static voicings; `EnvelopeFollower` supplies detection + ballistics + dB
  domain; `SvfPrimitive` supplies the optional sidechain highpass. This item is
  primarily a **composition**, with one genuinely new kernel — the stateless
  modulation mapper.
- The **measurement infrastructure** (shipped stimulus/response harness + analysis,
  incl. harmonic analysis) measures exactly this effect's correctness properties:
  zero-depth orthogonality against the static saturator, modulation-offset accuracy,
  signed-direction harmonic behavior vs level, attack/release time, feedback
  convergence.

A **program-dependent saturator** is: detect the signal level → map that level
through a per-target depth+curve to a **signed parameter offset** → add it to the
static operating point → run the (unchanged) saturation nonlinearity with the
modulated parameters. The detector already exists; this item owns the **modulation
mapper**, the **modulation matrix** (which parameters, with what depth/curve), the
**detector topology (feedforward/feedback)**, the **static-voicing orthogonality**,
the **named dynamic characters**, and the **sidechain conditioning**.

### Forces and constraints

- **Real-time safety (Principle VI):** no heap allocation, locks, or unbounded work
  in `process()`. The modulation mapper is pure arithmetic; the only per-sample
  transcendentals (`sqrt`, `log`) live inside the already-shipped `EnvelopeFollower`,
  and any log/exp response curve is bounded and allocation-free. The feedback
  topology is a one-sample output dependency, not a new kernel. All coefficients
  recompute in `set*`/`prepare`.
- **Platform-independent core, thin adapters (Principle IV):** the kernel, the
  primitive, and the effect compile with no JUCE / libDaisy / Teensy knowledge and
  run on MCU targets. Per-sample cost stays MCU-viable — a shared single detector
  feeding N stateless mappers, not N detectors (multiband is captured-for-later).
- **Compose, don't re-derive (Principle IX):** reuse the **unchanged**
  `SaturationCore`, `EnvelopeFollower`, and `SvfPrimitive` rather than
  re-implementing the nonlinearity, detection, or filtering. The dynamics layer is
  strictly orthogonal — at zero depth the effect is byte-for-byte the static
  saturator.
- **Explicit gates, never hooks (Commandment II / Principle II):** enforcement
  extends `scripts/check-portability.sh` (already in CI), never a git hook.
- **Strict typing, small modules (Principle VII):** no `any`-equivalents / unchecked
  casts; each file within ~300–500 lines (the large captured parameter set may push
  the effect wrapper toward a split — see open questions).
- **Measurable engineering (Principle X):** every mode/topology validated by
  objective measurement (zero-depth orthogonality, modulation-offset accuracy,
  signed-direction THD-vs-level, attack/release time, feedback convergence, preset
  equivalence) against analytic truths and named tolerances — not by ear.
- **One concept at a time (Principle XI):** the concept is
  *envelope-modulates-nonlinearity*. **Excluded:** the physical tape magnetic model
  (hysteresis / Jiles-Atherton, wow/flutter dynamics) — owned by
  `design:feature/tape-dynamics`; multiband dynamic saturation — captured-but-deferred.

## Solution space

Six decisions were explored against alternatives. The chosen positions compose into
`## Decisions`; the rejected alternatives and their reasons are recorded here. The
operator selected the capture-everything option at every fork, consistent with the
capture-over-YAGNI house rule (and, for the one fork the operator was unsure on,
capture is the house-rule default because it is cheap and reversible while scoping
is a separate later pass).

### Chosen — Full vertical with a new generic modulation primitive

Author `core/labs/program-dependent-saturation/` (README theory + kernel +
host-only harness), graduate a stateless `dynamics-modulator.h` into
`core/primitives/dynamics/`, and ship a `ProgramDependentSaturationEffect` (with its
`ProgramDependentSaturationCore` composition kernel) in
`core/effects/program-dependent-saturation/`. Delivers the whole vertical — the
reusable envelope-to-parameter mapper AND the usable host-facing dynamic saturator —
in one item, exactly as compressors shipped (reuse the detector, add one new kernel,
wrap as an effect). The new primitive is voicing-agnostic and reusable by any
envelope-driven effect (dynamic filter, auto-wah, ducking).

### Rejected — Full vertical, no new primitive (pure composition inside the effect)

Ship the lab + effect but fold the envelope-to-parameter mapping inside the effect
core; graduate no primitive. **Rejected:** traps the reusable modulation-mapper
building block inside the effect, where dynamic filters, auto-wah, and ducking
processors cannot reach it — the mapper is to *modulation* what the just-graduated
`GainComputer` is to *static gain*, a first-class `dynamics/` inhabitant.

### Rejected — Extend the static SaturationEffect in place

Add dynamic drive/bias params directly onto `core/effects/saturation/`.
**Rejected:** the static effect deliberately drew this boundary
(`saturation-effect.h` FR-024); mixing static and dynamic behavior in one wrapper
muddies both contracts and deletes an intentional separation-of-concerns.

### Chosen — Full modulation matrix (drive + bias + tone + mix), each an independent target

The envelope can modulate **all four** `SaturationCore` operating-point knobs —
`drive` (hit-it-harder = more saturation), `bias` (level-dependent asymmetry / even
harmonics), `tone` (dynamic brightness), and `mix` (dynamic parallel blend /
self-compression feel) — each an independent target with its own signed depth and
curve. Captures the whole modulation family in one mechanism, mirroring how
`EnvelopeFollower` captured its full detector/ballistics catalog.

### Rejected — Drive-only modulation

Modulate only `drive` (the essential "louder = more saturation"). **Rejected:**
dynamic bias (level-dependent even-harmonic character) and dynamic tone/mix are the
behaviors that distinguish tube/transformer/tape voicings from a plain dynamic
clipper; excluding them merely pushes identical depth+curve machinery to a sibling to
be re-discovered.

### Chosen — Signed depth + selectable response curve (linear / log / exp)

Each modulation target carries a **signed** depth (positive = louder pushes the
parameter up; negative = louder pulls it down, i.e. dynamic softening / ducking) and
a selectable response curve (`linear` / `logarithmic` / `exponential`). One
mechanism expresses both "tube pushing" and "dynamic softening," and the curve sets
how abruptly the character arrives.

### Rejected — Up-only, fixed curve

Louder always means *more*, single fixed response. **Rejected:** cannot express
dynamic softening / ducking (a whole class of program-dependent effects), and the
fixed curve forecloses the voicing difference between a gentle and an aggressive
onset; both would be re-added under the same charter.

### Rejected — Signed depth, linear only

Both directions via signed depth but only a linear map. **Rejected:** the response
curve is a cheap, bounded per-target shaping that materially changes the character;
dropping it strands the log/exp voicings the mapper is meant to provide.

### Chosen — Both feedforward and feedback detector topologies (enum-selected)

An enum-selected `Detection` capturing **feedforward** (detector reads the input;
predictable, modern — the default) and **feedback** (detector reads the previous
post-saturation *output* sample; self-regulating, the smoother vintage tube/tape
"feel"). Captures both, mirroring the compressor's feedforward/feedback fork and
`EnvelopeFollower`'s branching/decoupled fork. Feedback is the topology behind the
self-limiting character of vintage-voiced saturators.

### Rejected — Feedforward only

Detector always reads the input. **Rejected:** discards the feedback character that
defines a class of vintage-voiced dynamic saturators; the feedback path is a small
one-sample state dependency, not a new kernel, and would be re-added under the same
charter.

### Chosen — Static voicings unchanged + orthogonal dynamics layer + named character presets

Keep `SaturationCore`'s four static voicings (`softClip`/`tape`/`console`/
`tubePreamp`) **unchanged**; add program-dependent modulation as a strictly
**orthogonal** layer on top (at zero depth the effect equals the static saturator);
AND capture named **dynamic-character presets** (`opto`, `variMu`, `tapeComp`) as
convenience configurations of the modulation matrix. Captures both the raw matrix
(for sound designers) and the recognizable characters (for quick recall).

### Rejected — Orthogonal layer only, no named presets

Add the dynamics layer but expose only the raw matrix (depths/curves/ballistics),
no named characters. **Rejected:** the named characters are a thin, zero-cost
convenience mapping onto the same matrix and are the recognizable entry points
(opto/vari-mu/tape); omitting them ships the feature visibly incomplete for the very
voicings that motivate program-dependence.

### Chosen — Capture the full sidechain / advanced feature set

Capture, for later sequencing (not YAGNI-cut): **external sidechain key** (drive the
envelope from another source — ducking the saturation), **sidechain pre-filter**
(pre-detector `SvfPrimitive` highpass/tilt so bass does not dictate drive),
**multiband program-dependence** (per-band detectors and modulation — bass stays
clean while highs push), and **stereo/multichannel linking** (link detection across
channels so the dynamic character keeps the image stable). The operator was unsure
which of these belong in the first cut; per the capture-over-YAGNI house rule they
are captured at design time (cheap, reversible) and the first-cut sequencing is an
explicit open question.

### Rejected — Minimal effect (internal-input feedforward detection only)

Ship with only internal-input feedforward detection and the modulation matrix; no
external key, sidechain filter, multiband, or linking. **Rejected:** the compressor
design already parked the sidechain-key / sidechain-filter / linking machinery for
the dynamics family; dropping these here strands that carve-out and forces a
near-immediate follow-on. Which land in the first graduated cut is an open question,
not a reason to omit them from the design.

## Decisions

1. **Full three-layer vertical, one item.** Author
   `core/labs/program-dependent-saturation/` (`README.md`: envelope-modulates-
   nonlinearity theory — dynamic drive/bias/tone/mix, signed depth and response
   curves, feedforward vs feedback, and the opto/vari-mu/tape-compression characters;
   an RT-safe kernel; a host-only `harness/`). Graduate the stateless modulation
   mapper via `git mv` into `core/primitives/dynamics/dynamics-modulator.h` (the
   `dynamics/` category's **third inhabitant**; `core/primitives/README.md` moves the
   mapper from prospectus to inhabited in the same commit). Ship
   `ProgramDependentSaturationCore` + `ProgramDependentSaturationEffect` in
   `core/effects/program-dependent-saturation/`.

2. **`DynamicsModulator` primitive — stateless, pure mapping.**
   ```cpp
   enum class ModCurve : std::uint8_t { linear, logarithmic, exponential };

   class DynamicsModulator {
     void  setDepth(float signedDepth) noexcept;   // [-1,+1]; sign = direction
     void  setCurve(ModCurve) noexcept;            // response shaping
     float modulate(float envelope) const noexcept; // -> signed param offset; pure fn of env + params
   };
   ```
   No ballistics, no runtime state — a pure function of the envelope + parameters.
   The ballistics live in the `EnvelopeFollower` that feeds it; keeping the mapper
   stateless is exactly what makes it a first-class reusable `dynamics/` primitive
   (dynamic filters, auto-wah, ducking sends all compose it), mirroring the stateless
   `GainComputer`. `modulate` returns a signed offset added to the target's static
   base value.

3. **`ProgramDependentSaturationCore` — the composition kernel (per channel).**
   ```
   process(x, key?):
     src    = externalSC ? key : x
     src    = (scHpf > 0) ? svfHighpass(src) : src        // composes SvfPrimitive (captured)
     det    = (detection == feedBack) ? prevOutput : src  // topology fork
     env    = envelopeFollower.process(det)               // composes EnvelopeFollower (detector + ballistics + dB)
     drive' = staticDrive + driveMod.modulate(env)        // orthogonal dynamics layer:
     bias'  = staticBias  + biasMod.modulate(env)         //   static base value + signed
     tone'  = staticTone  + toneMod.modulate(env)         //   modulator offset, per target
     mix'   = staticMix   + mixMod.modulate(env)
     y      = saturationCore.process(x)  // with (drive',bias',tone',mix') pushed via its setters
     prevOutput = y
     return y
   ```
   The **unchanged** `SaturationCore` runs the nonlinearity; the four
   `DynamicsModulator` instances share the single `EnvelopeFollower` envelope. At all
   depths == 0 the modulated parameters equal the static bases, so the core reduces
   byte-for-byte to the static saturator (the zero-depth orthogonality contract).
   `Detection { feedForward, feedBack }` is an enum fork; feedback reads the previous
   output sample (one-sample state, exactly which output sample is an open question).

4. **`ProgramDependentSaturationEffect` — the host wrapper (the `SaturationEffect`
   idiom, exactly).** Constexpr `ParameterDescriptor` table as the single source of
   parameter truth; lock-free atomic cross-thread parameter handoff (publish on any
   thread, consume at the top of `process()`); allocation-free `prepare/process/reset`;
   per-channel `ProgramDependentSaturationCore` state; `static_assert`
   descriptor-validity guard. Captured parameter set: the static passthrough
   `{drive, voicing, tone, mix, output, bias, quality}`; the detector
   `{detector: peak|rms|peakHold, ballistics: branching|decoupled, attack, release,
   detection: feedForward|feedBack}`; the **modulation matrix** — for each target in
   `{drive, bias, tone, mix}` a signed `depth` and a `curve: linear|log|exp`; the
   character `{dynamicPreset: none|opto|variMu|tapeComp}`; and captured-for-later
   `{externalSidechain, scHpfHz, stereoLink}` (multiband captured as a note).

5. **Static-voicing orthogonality + named dynamic characters.** The static voicing
   axis is untouched — program-dependence is a layer over any voicing. The named
   presets (`opto`, `variMu`, `tapeComp`) are convenience configurations that set the
   modulation matrix (target depths, curves, topology, ballistics) to recognizable
   characters; each preset is a documented, testable matrix configuration, not new
   DSP.

6. **Validation reuses the shipped measurement (stimulus/response) infrastructure.**
   Assert: **zero-depth orthogonality** (all depths 0 ⇒ output equals the static
   `SaturationEffect`, within tolerance / byte-for-byte where the paths coincide),
   **modulation-offset accuracy** (measured drive/bias/tone/mix offset vs the analytic
   `depth·curve(env)` at fixed envelope), **signed-direction behavior** (positive
   depth ⇒ harmonic content / THD rises with input level; negative ⇒ falls — via the
   shipped harmonic-analysis tooling), **attack/release time** of the modulation to
   1 − 1/e (reusing the `EnvelopeFollower` characterization), **feedback convergence**
   (feedback topology settles to the feedforward fixed point for a steady input),
   and **preset equivalence** (each named character equals its documented matrix
   config). Assertions use analytic truths + named tolerances; the `no-allocation-test`
   covers RT-safety.

7. **Portability gate extended.** `scripts/check-portability.sh` (in CI, never a
   hook) learns `core/labs/program-dependent-saturation/**`,
   `core/primitives/dynamics/dynamics-modulator.h`, and
   `core/effects/program-dependent-saturation/**` so its harness-isolation,
   dependency-direction, platform-independence, and file-size checks cover the new
   units.

8. **Scope discipline (Principle XI).** This item delivers the generic
   *envelope-modulates-nonlinearity* mechanism: the `DynamicsModulator` primitive +
   the lab + the `ProgramDependentSaturationEffect`. **Excluded:** the physical tape
   magnetic model — hysteresis (Jiles-Atherton), tape bias interactions, wow/flutter
   dynamics — owned by `design:feature/tape-dynamics`; multiband dynamic saturation
   and dynamic EQ — captured-but-deferred future items. The `SaturationCore` is
   composed **unchanged** — this item adds no new nonlinearity kernel, only the
   modulation layer above the existing one.

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later scoping
pass (`/speckit-clarify` / planning), **not** discarded:

- **First graduated cut.** Which of the captured set lands in the first
  `ProgramDependentSaturationEffect` vs stays captured-for-later: the feedback
  topology, the full 4-target matrix vs drive+bias first, the named presets, external
  sidechain, sidechain HPF, multiband, and stereo linking are all captured; a
  sequencing decision, not a design blocker. (The operator was explicitly unsure on
  the sidechain/advanced set — captured by default, sequenced later.)
- **Envelope normalization + modulation scaling.** What units the modulator consumes
  (a 0..1 normalized envelope, a linear amplitude, or the `EnvelopeFollower`'s dB
  value) and how the signed `depth` scales into each target's native range (dB for
  drive, −1..+1 for bias/tone, 0..1 for mix) so a given depth reads consistently
  across targets.
- **Feedback path definition.** Exactly which output the feedback detector reads —
  post-output-gain, post-mix, or the pre-mix wet sample — and how the one-sample delay
  in the feedback loop interacts with the detector ballistics and with the
  oversampled quality tier's wet-path latency.
- **Response-curve law.** The exact `logarithmic` / `exponential` curve forms and
  whether they are shared across targets or per-target, plus whether an optional
  attack/release *asymmetry* on the modulation (beyond the `EnvelopeFollower`'s own)
  is warranted.
- **Preset definitions.** The concrete matrix configurations for `opto`, `variMu`,
  and `tapeComp` (which targets, depths, curves, topology, detector/ballistics), and
  whether presets are a hard selection or a starting point the user can then edit.
- **Modulation update rate.** Whether the modulation offsets are recomputed
  per-sample or per–small-block (control-rate) for MCU budget, and how that interacts
  with `SaturationCore`'s existing per-sample setters (which currently expect
  block-boundary parameter application).
- **Stereo-link detail.** Whether linking is max-only or also offers sum/average, and
  whether the sidechain HPF / external key is per-channel or shared across a linked
  group (mirrors the compressor's open question).
- **Static-parameter application seam.** `SaturationCore`'s drive/bias/tone/mix
  setters recompute coefficients; driving them per-sample from the modulation layer
  needs a cheap fast-path (or a per-block modulation apply) so the dynamic layer does
  not defeat the "coefficient work off the audio path" discipline.

## Provenance

- **Roadmap item:** `design:feature/program-dependent-saturation` (status `planned`
  → `designing`), `depends-on: multi:feature/phase-nonlinear-dsp` (complete),
  `part-of: multi:feature/phase-dynamic-systems`. Design pointer set via
  `stackctl workflow link-design` to this file (before any content was written, so
  phase-derivation reports `designing`).
- **Constitution:** Principle IX (Progressive Layered Architecture), with supporting
  Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII (Strict
  Typing & Small Modules), X (Measurable Engineering), XI (One Concept at a Time).
  `.specify/memory/constitution.md`.
- **The carve-out this item fills:** `core/effects/saturation/saturation-effect.h`
  (FR-024 boundary note) assigns "envelope-tracking drive, dynamic bias, tape-style
  compression" to this item; `docs/superpowers/specs/2026-07-02-compressors-design.md`
  (Decision 7 / scope) assigns "program-dependent / 'auto' attack-release
  (opto/vari-mu emulation)" to `design:feature/program-dependent-saturation` and
  `design:feature/tape-dynamics`.
- **Pattern precedent:** `docs/superpowers/specs/2026-07-02-compressors-design.md`
  (reuse the shipped `EnvelopeFollower` detector, add one new stateless
  `dynamics/` primitive, wrap as an effect mirroring the `SaturationEffect` idiom,
  extend the portability gate) and `docs/superpowers/specs/2026-06-30-saturation-design.md`
  (lab → effect; `SaturationCore` composition).
- **Composed units (all shipped, unchanged):**
  `core/effects/saturation/saturation-core.h` (the nonlinearity + its drive/bias/
  tone/mix/voicing setters), `core/primitives/dynamics/envelope-follower.h`
  (detection + ballistics + dB domain), `core/primitives/filters/svf-primitive.h`
  (sidechain highpass — captured).
- **Reused infrastructure:**
  `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` and
  `docs/superpowers/specs/2026-07-01-harmonic-analysis-design.md` with the shipped
  stimulus/response + harmonic tooling (`tests/core/measurement-*`,
  `measurement-support.h`, `tests/support/svf-reference.h`).
- **Current code surveyed:** `core/effects/saturation/{saturation-effect,saturation-core}.h`,
  `core/primitives/dynamics/{envelope-follower,gain-computer}.h`,
  `core/effects/compressor/*`, `core/primitives/{filters/svf-primitive,delays/delay-line}.h`,
  `core/primitives/README.md`, `scripts/check-portability.sh`, `ROADMAP.md`.
- **Design method:** `superpowers:brainstorming` driven in-session under the
  `/stack-control:design` frontend; house-rules block injected (capture-over-YAGNI,
  ≥2 solution-space alternatives, required sections, operator-approval marker, handoff
  to `/stack-control:define`). Compass consulted (`--intent design`) → `on-course`.
- **Decisions driven by the operator** across six forks: altitude (full vertical with
  a new generic modulation primitive), modulation targets (drive + bias + tone + mix —
  full matrix), response shape (signed depth + selectable curve), detector topology
  (feedforward + feedback), character layer (orthogonal dynamics layer + named
  opto/vari-mu/tape-comp presets), and the sidechain/advanced feature set (external
  key + SC HPF + multiband + stereo linking — all captured; operator unsure, captured
  by house-rule default). The operator consistently chose the capture-everything
  option.
- **Reference:** the opto (LA-2A) and vari-mu (Fairchild) program-dependent character
  literature and J. D. Reiss et al., *"Digital Dynamic Range Compressor Design — A
  Tutorial and Analysis"* (the feedforward vs feedback detector topology, the level
  detector this item reuses).
- **Next step:** operator records the `design-approved:` marker on the roadmap node;
  on a met `design-to-spec` gate, hand off to `/stack-control:define` to author the
  Spec Kit spec.

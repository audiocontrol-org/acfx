> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Saturation — Composed Production Effect

**Feature Branch**: `saturation`

**Created**: 2026-06-30

**Status**: Draft

**Roadmap item**: `design:feature/saturation` (`part-of: multi:feature/phase-nonlinear-dsp`)

**Design record**: `docs/superpowers/specs/2026-06-30-saturation-design.md` (operator-approved; source of truth)

**Input**: User description: "Saturation — the first Production Effect (stage 4 of the four-stage graduation model) of phase-nonlinear-dsp, a voiced saturation effect that COMPOSES the shipped Waveshaper primitive and SvfPrimitive filter, authored from the operator-approved design record. Capture everything; do not cut scope."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Apply a voiced saturation effect to audio (Priority: P1)

An effect author runs an audio signal through the saturation effect and, with control over
input drive, output makeup, and a dry/wet blend, obtains a musically saturated signal whose
harmonic character comes from a nonlinear stage placed between frequency-shaping filters. The
effect is built entirely by composing already-shipped primitives (the nonlinear waveshaper and
the state-variable filter); it introduces no new nonlinearity or filter of its own.

**Why this priority**: This is the irreducible core of the feature — a working composed
saturation processor with gain-staging and a dry/wet blend. Every other story builds on it; on
its own it already delivers a usable production effect that a host or the workbench can drive.

**Independent Test**: Drive a known sine tone through the effect at a fixed drive with a chosen
voicing and assert, via the existing harmonic (THD) measurement, that the produced harmonic
series reflects the nonlinear stage within a named tolerance, that increasing drive increases
measured distortion monotonically, and that a fully-dry blend reproduces the input within
tolerance.

**Acceptance Scenarios**:

1. **Given** the effect configured with a voicing, unity-ish drive, and a fully-wet blend, a
   unit-amplitude sine stimulus, **When** the signal is processed, **Then** the measured
   harmonic series exhibits the nonlinear stage's expected harmonic content (within the named
   tolerance) and the output contains no DC offset within tolerance.
2. **Given** a fully-dry blend (`mix` at the dry extreme), **When** the signal is processed,
   **Then** the output matches the input within the named tolerance (the wet path contributes
   nothing).
3. **Given** increasing drive across a sweep at a fixed voicing, **When** the signal is
   processed at each drive, **Then** the measured total harmonic distortion increases
   monotonically (within tolerance) and, with gain-compensation active, the output loudness
   stays within a named band.
4. **Given** silence (all-zero input), **When** processed, **Then** the output is silence
   (within tolerance), with no denormal or NaN/Inf generation.

---

### User Story 2 - Select among documented voicings (Priority: P1)

The effect exposes a discrete **voicing** selector — Soft Clip, Tape, Console, Tube Preamp —
where each voicing fixes a nonlinear shape plus a pre-emphasis and post-de-emphasis filter
curve, giving each a distinct spectral and harmonic fingerprint. The author switches voicing at
runtime to change character without rebuilding.

**Why this priority**: The breadth of voicings is the substance of "saturation"; a single
character would make the effect barely more than the primitive it wraps. Each voicing's
pre/post emphasis is precisely what distinguishes tape from console from tube. Co-equal P1 with
Story 1 because the effect is only meaningful against a set of voicings.

**Independent Test**: For each voicing, drive an identical stimulus through the effect and
assert the measured harmonic + spectral signature differs from the other voicings by at least a
named margin and matches that voicing's documented signature within tolerance — with no
residual state from the previously-selected voicing.

**Acceptance Scenarios**:

1. **Given** each of the four voicings in turn at identical drive/tone/mix, **When** an
   identical stimulus is processed, **Then** each voicing produces its documented harmonic +
   spectral signature within tolerance, and the four signatures are mutually distinguishable by
   at least the named margin.
2. **Given** the effect set to one voicing and then switched to another at runtime, **When** the
   signal is processed after each selection, **Then** the output reflects the currently selected
   voicing with no residual state (filter or DC) from the previous voicing.
3. **Given** any voicing, **When** its character is inspected, **Then** the voicing fixes the
   nonlinear **shape** and the **pre/post-emphasis** curves only — the asymmetry **bias** is a
   separate user control, not baked into the voicing.

---

### User Story 3 - Shape the result with the musical control surface (Priority: P2)

An author shapes the saturated signal with a **tone** tilt, a user **bias** (asymmetry / even-
harmonic dial), a **mix** dry/wet blend for parallel saturation, and an **output** makeup trim.
Every control is adjustable from a non-audio thread (a UI, a MIDI callback, an MCU main loop)
without glitching or racing the audio processing.

**Why this priority**: These controls are what make the effect musically usable rather than a
fixed transform, and the thread-safe parameter handoff is the production-effect contract the
codebase already establishes. P2 because Stories 1–2 define the effect's identity; this story
completes its expressive surface and its host-integration contract.

**Independent Test**: Publish parameter edits from a non-audio thread while the effect
processes, and assert the audio thread applies them at block boundaries with no allocation, no
lock, and no torn/`NaN` values; assert `mix` blends dry/wet as specified and `bias` introduces
the expected even-harmonic content.

**Acceptance Scenarios**:

1. **Given** parameter edits (`drive`, `voicing`, `tone`, `mix`, `output`, `bias`, `quality`)
   published from a non-audio thread, **When** the audio thread processes the next block,
   **Then** the edits take effect at the block boundary with no heap allocation and no lock on
   the processing path.
2. **Given** a nonzero user `bias`, **When** the signal is processed, **Then** even-harmonic
   content appears in the predicted direction AND no DC offset reaches the output beyond the
   named tolerance.
3. **Given** the `mix` control swept from dry to wet, **When** the signal is processed, **Then**
   the output is the specified blend of the dry input and the wet (saturated) path across the
   range, with the blend's gain law behaving as documented.
4. **Given** the `tone` tilt and `output` trim, **When** each is adjusted, **Then** the output
   spectrum tilts and the output level scales as specified, independently of the nonlinear
   character.

---

### User Story 4 - Choose an anti-aliasing quality (Priority: P2)

An author who hears aliasing from aggressive saturation selects the **quality** control's
anti-aliased mode (ADAA) instead of the naive mode, reducing aliased energy without changing
the effect's parameter surface. A future oversampled quality tier is reserved but not yet
active.

**Why this priority**: Nonlinear saturation aliases, and anti-aliasing materially improves
audible quality for aggressive settings. P2 because the naive path is independently complete and
the ADAA path is strictly additive; the oversampled tier is deferred to the sibling primitive.

**Independent Test**: Process a high-frequency tone (whose harmonics exceed Nyquist) through the
effect in naive versus ADAA quality; assert the ADAA mode exhibits measurably lower aliased
(inharmonic) energy by at least a named margin, with an identical parameter surface.

**Acceptance Scenarios**:

1. **Given** a high-frequency stimulus that drives the effect into aliasing, **When** processed
   in naive versus ADAA quality, **Then** the ADAA mode shows lower inharmonic/aliased energy by
   at least the named margin.
2. **Given** the effect's parameter surface, **When** the quality mode is switched, **Then** the
   set of user parameters is unchanged — quality selects the internal anti-aliasing path only.
3. **Given** the reserved oversampled quality tier, **When** the effect is inspected, **Then**
   it is present as a documented but **unwired** seam that adds no dependency on the (not-yet-
   built) oversampling sibling; selecting it behaves as a defined, bounded no-op fallback rather
   than a partial/aliased path.

---

### User Story 5 - Learn the composition from the saturation laboratory (Priority: P2)

A learner opens the `saturation` laboratory and finds the theory + walkthrough (how gain-
staging, per-voicing pre/post emphasis, voicing, and naive-vs-ADAA anti-aliasing compose into a
musical effect), an RT-safe composition kernel, and a host-only harness producing objective
harmonic evidence — the first concept to walk the Theory→Lab→**Effect** graduation, relocating
its kernel into the effects layer.

**Why this priority**: The progressive-curriculum mission (Constitution Principle IX) requires
the lab + graduation, and this is the first greenfield exercise of the pattern's final
Production-Effect stage. P2 because the effect can be validated before the lab prose is final,
but graduation is the proof the pattern reaches stage four.

**Independent Test**: From the lab harness alone, regenerate the per-voicing harmonic evidence
and the naive-vs-ADAA aliasing comparison, and confirm the kernel it drives is the same code
that, post-graduation, lives in the effects layer.

**Acceptance Scenarios**:

1. **Given** the `saturation` lab, **When** its harness is run host-side, **Then** it emits
   per-voicing harmonic evidence and a naive-vs-ADAA aliasing comparison using the shared
   measurement infrastructure.
2. **Given** the lab kernel, **When** the concept graduates, **Then** the composition kernel is
   relocated (not re-derived) into the effects layer, where the effect-contract wrapper is added
   around it, and the lab persists as theory + harness now driving the graduated effect.
3. **Given** the portability gate, **When** it runs in CI, **Then** it confirms no portable code
   includes a lab harness, the dependency direction holds (effect composes primitives; nothing
   portable includes a harness), and the new lab/effect locations meet the platform-independence
   and file-size checks.

---

### Edge Cases

- **Extreme drive** pushing the input far outside the nominal range: output stays bounded (no
  NaN/Inf), and the nonlinear stage's clipping/fold behavior matches the selected voicing's
  definition.
- **Fully-dry and fully-wet extremes** of the `mix` control: dry reproduces the input within
  tolerance; wet passes only the saturated path; the blend gain law is defined at both extremes.
- **DC / near-DC input** with an asymmetric (biased) setting: the wet path introduces no DC
  offset to the output beyond tolerance (the composed waveshaper's DC-blocker handles it).
- **Denormal-prone decays** (signal trailing to silence): no denormal stalls; output settles to
  clean silence.
- **Voicing switch mid-stream**: no stale filter or DC state carried from the prior voicing; any
  audible change is the inherent voicing difference only.
- **Quality switch mid-stream** (naive ↔ ADAA): bounded, no NaN/Inf; any latency difference
  between the paths is defined and documented (relevant to dry/wet phase alignment).
- **Selecting the reserved oversampled tier** before the sibling lands: a defined, bounded
  fallback (documented), never a partial or silently-aliased path.
- **Channel count / block size** variation: per-channel state is correct up to the supported
  channel maximum; processing is allocation-free regardless of block size.

## Requirements *(mandatory)*

### Functional Requirements

**Composed effect core**

- **FR-001**: The system MUST provide a saturation effect that **composes already-shipped
  primitives** — the nonlinear waveshaper (with its anti-aliasing) and the state-variable filter
  — and MUST NOT introduce a new nonlinearity or a new filter primitive of its own.
- **FR-002**: The per-channel signal chain MUST be: a **pre-emphasis** filter, then the
  **nonlinear waveshaping** stage driven by `drive` and user `bias`, then a **post-de-emphasis**
  filter, then a user **tone** tilt, then a **dry/wet blend** (`mix`) against the unprocessed
  input, then an **output** makeup trim.
- **FR-003**: The effect MUST hold independent per-channel state up to a supported channel
  maximum, and MUST NOT carry state across channels.
- **FR-004**: The nonlinear stage's internal gain-compensation MUST be active so that perceived
  loudness stays within a named band as `drive` increases, with `output` as the user trim on
  top.

**Voicings**

- **FR-005**: The effect MUST expose a discrete **voicing** selector including, captured in full
  (numeric per-voicing tuning is a planning decision, not a scope cut): **Soft Clip**, **Tape**,
  **Console**, and **Tube Preamp**.
- **FR-006**: Each voicing MUST fix a nonlinear **shape** and a **pre-emphasis** and
  **post-de-emphasis** filter curve, producing a documented, mutually-distinguishable harmonic +
  spectral signature.
- **FR-007**: A voicing MUST NOT bake the **bias** control; `bias` remains a user-facing control
  applied within the nonlinear stage (voicing owns the fixed spectral/shape identity, `bias`
  owns the live asymmetry — design Decision 5).
- **FR-008**: Switching voicing at runtime MUST carry no stale filter or DC state from the prior
  voicing; `reset` MUST clear all carried state.

**Parameter surface (the Effect contract)**

- **FR-009**: The effect MUST expose the parameters **drive**, **voicing**, **tone**, **mix**,
  **output**, **bias**, and **quality**, defined by a single source of parameter truth (a
  descriptor table) that is the same for every host adapter.
- **FR-010**: Parameter edits MUST be publishable from any thread and MUST be applied by the
  audio thread at a block boundary; publishing an edit MUST NOT directly mutate audio-thread
  state, allocate heap memory, or take a lock.
- **FR-011**: The effect MUST provide preparation (with sample rate and channel count), `reset`,
  per-block processing, and per-parameter setting, consistent with the established production-
  effect contract (no base-class/vtable on the processing path).
- **FR-012**: The **mix** control MUST blend the unprocessed (dry) input with the saturated (wet)
  path across its range, with a documented gain law and a documented policy for any latency
  difference between the dry and wet paths.

**Anti-aliasing**

- **FR-013**: The effect MUST expose a **quality** control selecting at least a **naive** and an
  **ADAA (antiderivative anti-aliasing)** mode, delegating to the composed waveshaper's
  anti-aliasing; switching quality MUST NOT change the user parameter surface.
- **FR-014**: The ADAA mode MUST reduce aliased (inharmonic) energy relative to the naive mode
  for stimuli that drive the effect into aliasing, by at least a named margin.
- **FR-015**: The effect MUST reserve an **oversampled** quality tier as a **documented but
  unwired seam** that adds **no dependency** on the not-yet-built oversampling sibling; until the
  sibling lands, selecting it MUST behave as a defined, bounded fallback (never a partial or
  silently-aliased path). This feature MUST NOT build a reusable oversampling primitive.

**Validation / measurement**

- **FR-016**: Each voicing MUST be validated by **objective harmonic measurement** (harmonic
  distortion / harmonic signatures from a pure-tone stimulus) asserted against named tolerances,
  reusing the shipped measurement infrastructure (single-bin harmonic analysis + sine stimulus) —
  listening tests complement but never replace measurements.
- **FR-017**: The validation suite MUST assert: **drive→THD monotonicity** (rising drive raises
  measured distortion), **gain-compensation** holding output loudness within a named band across
  drive, **mix** dry/wet balance (dry reproduces input; wet is the saturated path), and the
  **naive-vs-ADAA** aliasing comparison.
- **FR-018**: The validation suite MUST assert real-time-safety invariants on the processing
  path: silence-in→silence-out, no DC offset reaching the output for biased settings, and no
  NaN/Inf/denormal generation under stress (including extreme drive and voicing/quality switches).

**Layering, portability, real-time safety**

- **FR-019**: The concept MUST be authored as a `saturation` laboratory (theory + walkthrough
  README naming the graduation target; an RT-safe composition kernel held to the primitive bar; a
  host-only harness) and then **graduated** by relocating the kernel (not re-deriving it) into the
  **effects** layer, where the effect-contract wrapper is added around it.
- **FR-020**: No code on the per-block/per-sample processing path may allocate heap memory or take
  locks; all per-sample work MUST be bounded, and any table/coefficient computation MUST occur at
  preparation time, never during processing.
- **FR-021**: The portable kernel and effect MUST contain no platform-specific (desktop or
  embedded host framework) headers and MUST be compilable for the embedded targets; host-only
  harness/visualization code MUST never be included by portable code.
- **FR-022**: Enforcement MUST extend the existing explicit portability check (run in CI, never a
  git hook) to cover the new lab (`core/labs/saturation/**`) and effect (`core/effects/
  saturation/**`) locations for harness-isolation, dependency-direction, platform-independence,
  and module-size — consistent with the three-layer-structure precedent.
- **FR-023**: Source modules MUST stay within the project's small-module size guidance
  (~300–500 lines); the composition kernel, the effect-contract wrapper, and the voicing table are
  organized as separate units.

**Boundaries**

- **FR-024**: This effect is the **static-character** composed saturation. Program-dependent /
  dynamic behavior (envelope-tracking drive, dynamic bias, tape-style compression) is the separate
  `design:feature/program-dependent-saturation` item and MUST NOT be built here; the boundary MUST
  be documented so the later item does not read as a duplicate.

### Key Entities

- **Saturation effect**: a per-channel, runtime-configurable production effect composing a
  nonlinear waveshaping stage between pre/post-emphasis filters, with a dry/wet blend and makeup
  trim.
- **Voicing**: a named character (Soft Clip / Tape / Console / Tube Preamp) fixing a nonlinear
  shape plus pre/post-emphasis curves; the unit the harness produces a signature for.
- **Composition kernel**: the RT-safe DSP core (the lab kernel that graduates into the effects
  layer), independent of host-thread/parameter-plumbing concerns.
- **Effect-contract wrapper**: the parameter descriptor table (single source of parameter truth)
  plus the thread-safe parameter handoff and prepare/reset/process surface added at the effect
  layer.
- **Quality mode**: the anti-aliasing selection (naive / ADAA now; oversampled reserved-unwired).
- **Saturation laboratory**: theory + walkthrough + RT-safe composition kernel + host-only
  harness; the origin of the graduated effect.
- **Harmonic evidence**: per-voicing harmonic signatures and the naive-vs-ADAA aliasing
  comparison, derived from the shared measurement infrastructure.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Each of the four voicings produces its documented harmonic + spectral signature
  within the named per-voicing tolerance for a standard pure-tone stimulus, and the four
  signatures are mutually distinguishable by at least the named margin.
- **SC-002**: Increasing `drive` at a fixed voicing increases measured total harmonic distortion
  monotonically (within tolerance), while gain-compensation holds output loudness within the named
  band.
- **SC-003**: A fully-dry `mix` reproduces the input within the named tolerance, and a fully-wet
  `mix` yields the saturated path; intermediate settings follow the documented blend law.
- **SC-004**: For at least one aggressive (aliasing-prone) voicing/setting, the ADAA quality mode
  reduces measured aliased/inharmonic energy by at least the named margin versus the naive mode.
- **SC-005**: For every biased setting, the output DC offset is held within the named tolerance of
  zero, and the processing path performs zero heap allocations and holds no locks under the
  measurement suite's checks, generating no NaN/Inf/denormal under stress (including
  silence-in→silence-out and voicing/quality switches).
- **SC-006**: The portable kernel and effect compile for the embedded target toolchain(s) with no
  platform-framework headers, and the portability check passes for the new lab and effect
  locations.
- **SC-007**: The saturation lab's harness regenerates the per-voicing harmonic evidence and the
  naive-vs-ADAA comparison host-side, and the graduated effect is the relocated lab kernel
  (verified by the portability/graduation checks), proving the Theory→Lab→Effect pattern reaches
  the Production-Effect stage.

## Assumptions

- The composed primitives — the nonlinear waveshaper (with drive/bias/shape/DC-block/gain-comp,
  closed-form + LUT backends, and an ADAA variant) and the state-variable filter — are reused
  **as-is**; this feature adds no new nonlinearity or filter primitive.
- The shipped measurement infrastructure (single-bin/Goertzel harmonic analysis, sine stimulus,
  allocation sentinel, analytic-bound assertion pattern) is reused as-is; this feature adds no new
  general spectral engine.
- "Named tolerance / named margin / named band" means an explicit threshold chosen per voicing/
  metric during planning/clarification (the existing reference-bound philosophy), not a fabricated
  exact figure.
- The production-effect contract (single source-of-truth parameter descriptor table, lock-free
  cross-thread parameter handoff, prepare/reset/process/setParameter, per-channel state) follows
  the established effect precedent in the codebase.
- Embedded-target compilation is validated to the extent the existing build/CI exercises those
  toolchains; full on-hardware measurement is a separate, later concern.
- The per-voicing numeric tuning (emphasis curves, shape/drive/bias defaults) is a **planning**
  decision; the spec captures the four voicings without cutting them.
- Oversampling machinery, program-dependent/dynamic saturation, multi-stage/cascaded topologies,
  and deeper nonlinear-specific harmonic-analysis tooling are **out of scope** here (separate
  roadmap items), per one-concept-at-a-time.

## Open Questions *(carried from the design record — captured, not blockers)*

- Per-voicing emphasis curves and preset numbers (the actual pre/post-emphasis frequencies/gains
  and shape/drive/bias defaults per voicing) — a tuning pass validated by the harmonic harness.
- Oversampled quality arm: wiring `quality::oversampled` once the oversampling sibling ships —
  fixed factor vs selectable, and whether the effect exposes the factor.
- Multi-stage / cascaded saturation topology (console/tube character often comes from cascaded
  stages) — a richer topology deferred from this single-stage design.
- `tone` control law: single tilt vs a small shelf pair; center-detent behavior; interaction with
  the per-voicing baked emphasis.
- `mix` compensation: whether the parallel blend needs phase-/delay-matching between the dry tap
  and the (possibly ADAA-delayed) wet path, and the blend gain law (equal-power vs linear).
- Effect-level makeup law: whether the effect adds its own makeup model atop the waveshaper's
  internal gain-compensation, or relies solely on it plus the user `output`.
- Additional/extreme voicings (e.g. a wavefold-based character) beyond the four named ones.
- Harness standardized output contract (e.g. CSV harmonic spectra) so nonlinear labs/effects are
  comparable — shared with the open question recorded for the lab layer.

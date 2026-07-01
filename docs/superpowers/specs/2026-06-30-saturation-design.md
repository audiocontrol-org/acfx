---
title: Saturation — Composed Production Effect — Design Record
date: 2026-06-30
roadmap-item: design:feature/saturation
status: proposed
---

# Saturation — Composed Production Effect

Establish the **first Production Effect** (stage 4 of the four-stage graduation
model) of `phase-nonlinear-dsp`: a voiced saturation effect that **composes the
shipped `Waveshaper` primitive** (nonlinear shaping + ADAA) with the shipped
`SvfPrimitive` (pre/post emphasis + tone) into a user-facing effect under
`core/effects/saturation/`. The waveshapers design explicitly deferred "the
composed saturation effect (later item)" — this is that item.

It is also the **first concept to walk the lab→*effect* graduation** — every prior
graduation (SVF, waveshapers) targeted a *primitive*. Saturation graduates a
composition kernel into the effects layer, exercising the four-stage model's final
Production-Effect stage for the first time.

## Problem domain

Constitution **Principle IX (Progressive Layered Architecture)** and the program
prospectus declare the four-stage model — Theory → Laboratory → Reusable Primitive
→ **Production Effect** — and name **Saturation** as a Phase-2 deliverable, listing
four voicings (Soft Clip, Tape, Console, Tube Preamp). The prospectus states each
effect "documents which primitives it uses, making the implementation transparent
and educational" — i.e. an effect's value is the *composition itself*.

On disk today:

- `core/effects/` already holds two Production Effects — `svf/` (the cross-platform
  spine, `SvfEffect`) and `modulated-delay/`. **No `saturation/` effect exists.**
- The **`Waveshaper` primitive** (`core/primitives/nonlinear/`) shipped and closed:
  memoryless `acfx::shape::*` catalog, a stateful `Waveshaper` wrapper
  (drive/bias/shape/DC-block/optional gain-comp, closed-form + LUT backends), and a
  separate opt-in `ADAAWaveshaper` (antiderivative anti-aliasing). Saturation's
  nonlinearity is this primitive — it is not re-derived here.
- The **`SvfPrimitive`** filter (`core/primitives/filters/`) is shipped and is the
  natural building block for the frequency-dependent pre-emphasis / de-emphasis
  that distinguishes tape from console from tube. Saturation composes it.
- The **measurement infrastructure** (Goertzel-based THD / harmonic analysis + sine
  stimulus) shipped and already validates the waveshaper; it is exactly the tool to
  validate each saturation voicing's harmonic signature.
- The **`oversampling` primitive** (`design:primitive/oversampling`) is a Phase-2
  sibling that is **still `planned` (not built)**. The waveshapers design framed
  saturation as "waveshaper inside oversampler", but the roadmap gives saturation
  **no depends-on edge to oversampling**. The effect must therefore anti-alias with
  what is shipped (the `Waveshaper`'s naive/ADAA modes) and leave a documented seam
  for oversampling — not block on an unbuilt sibling.
- **Program-dependent / dynamic saturation** is a *separate* roadmap item
  (`design:feature/program-dependent-saturation`, Phase-3 `phase-dynamic-systems`).
  This item is the **static/memoryless-character** composed effect; the dynamic
  behavior belongs to that later item, and the boundary is drawn explicitly.

Per **Principle XI (One Concept at a Time)** the scope is the composed voiced
saturation effect built from *already-shipped* primitives — not a new nonlinearity
(waveshapers owns that), not the anti-aliasing machinery (oversampling sibling),
not program-dependent dynamics (a later item).

### Forces and constraints

- **Real-time safety (Principle VI):** no heap allocation, locks, or unbounded work
  in `process()`. The effect only composes RT-safe primitives; any LUT / filter
  coefficient work happens in `prepare()`, never in `process()`.
- **Platform-independent core, thin adapters (Principle IV):** the effect kernel
  and the `Effect`-contract wrapper compile with no JUCE / libDaisy / Teensy
  knowledge and must run on MCU targets (Daisy / Teensy).
- **Evolve, don't discard (Principle IX):** the lab composition kernel relocates
  into `core/effects/saturation/` and refines in place; it is never re-derived.
- **Composition over invention (Principle IX / prospectus):** saturation invents no
  new DSP primitive. It composes `Waveshaper` + `SvfPrimitive`; the composition and
  its voicing rationale are the deliverable's substance.
- **Effect contract, no inheritance (existing `SvfEffect` idiom):** a constexpr
  `ParameterDescriptor` table as the single source of parameter truth, lock-free
  atomic cross-thread parameter handoff consumed at the top of `process()`, and the
  `prepare()/reset()/process(AudioBlock&)/setParameter()` surface — no base class,
  no vtable on the hot path.
- **Explicit gates, never hooks (Commandment II / Principle II):** enforcement
  extends `scripts/check-portability.sh` (already in CI), never a git hook.
- **Strict typing, small modules (Principle VII):** no `any`/unchecked casts; files
  within ~300–500 lines. The lab kernel, the effect wrapper, and the voicing table
  are separate units.
- **Measurable engineering (Principle X):** every voicing is validated by objective
  harmonic measurement (THD / harmonic spectra) reusing the shipped measurement
  infrastructure, not by listening alone.

## Solution space

Five forks were explored against alternatives. The chosen positions compose into
`## Decisions`; the rejected alternatives and their reasons are recorded here.

### Fork 1 — Effect identity / scope

**Chosen — Voiced effect, capture-everything.** Define the full voiced effect
(character selector across the prospectus's four voicings + tone + mix + output +
per-voicing baked character) now, and record the richer deferred members
(multi-stage, oversampled arm, program-dependent) as captured open questions.
Matches the capture-over-YAGNI stance the operator held throughout the waveshapers
design.

- **Rejected — One voiced effect, minimal.** Voicing selector + drive/mix/output
  only, no per-voicing tone character. Rejected: the voicings would be nearly
  indistinguishable, which is the whole point of naming four of them.
- **Rejected — Generic composed effect, voicings later.** Expose the raw
  `Waveshaper` shape directly (drive/shape/mix/output) and defer all four voicings
  to later named effects. Rejected: barely more than the primitive it wraps; the
  prospectus explicitly frames Saturation as a voiced effect.

### Fork 2 — Anti-aliasing vs the oversampling sibling

**Chosen — Ship on ADAA now, oversampling seam later.** Saturation uses the
`Waveshaper`'s built-in anti-aliasing (a `quality` control selecting naive / ADAA)
and does **not** block on the `oversampling` sibling. A documented
`quality::oversampled` integration seam is captured but left unwired; the sibling
fills it when it lands. No new roadmap depends-on edge.

- **Rejected — Depend on oversampling, sequence it first.** Add a depends-on edge
  and build the oversampling primitive first, so `saturation = Oversampler<Waveshaper>`
  is the literal architecture. Rejected: blocks a ready item on another unbuilt
  primitive and re-shapes the roadmap frontier for a textbook-purity gain the ADAA
  path already substantially delivers.
- **Rejected — Throwaway in-effect resampler now.** Ship naive/ADAA plus an
  explicit throwaway (non-reusable, never-graduated) up/down-sampler for an
  oversampled arm immediately. Rejected (captured as the seam instead): duplicates
  the oversampling sibling's charter and risks a one-concept-at-a-time violation at
  the boundary; the seam captures the intent without building throwaway DSP.

### Fork 3 — Tone / frequency shaping

**Chosen — Per-voicing baked pre/post emphasis + user tone tilt.** Each voicing
carries a fixed pre-emphasis and post-de-emphasis filter (composed `SvfPrimitive`)
that defines its spectral character, PLUS one user-facing `tone` tilt. Reuses the
shipped filter primitive and makes the voicings genuinely distinct; it is also the
clearest demonstration of the "effects compose primitives" thesis.

- **Rejected — Single user tone tilt only.** One shared tilt, voicings differ only
  by shape + gain-staging. Rejected: pre/post emphasis is precisely what separates
  tape / console / tube; without it the voicings collapse together. Captured as an
  open question only for the *numeric* emphasis curves, not the mechanism.
- **Rejected — No tone filtering this item.** Pure `Waveshaper` + drive/mix/output.
  Rejected: contradicts the voiced-effect identity chosen in Fork 1 and makes the
  effect barely more than the primitive.

### Fork 4 — Layer / lab (three-layer altitude)

**Chosen — New composition lab that graduates into the effect.** Author
`core/labs/saturation/` (README teaching musical gain-staging + pre/post emphasis +
voicing + anti-aliasing composition; RT-safe kernel; host-only harness) and
graduate the kernel into `core/effects/saturation/`. Exercises the four-stage
model's final Production-Effect stage for the first time; the graduation target is
an **effect**, not a primitive (a first — prior graduations were lab→primitive).

- **Rejected — Production effect only + documented composition.** No lab; the effect
  lives only at `core/effects/saturation/` with a README documenting the
  composition, on the grounds that waveshaping and SVF already have labs.
  Rejected by the operator: the *composition itself* (gain-staging, emphasis,
  voicing) is a teachable concept the prospectus explicitly values, and this is the
  program's first chance to walk lab→effect greenfield.

### Fork 5 — Parameter surface

**Chosen — `drive, voicing, tone, mix, output, bias`.** Full surface: `drive`
(input gain into the nonlinearity), `voicing` (discrete four-way), `tone` (tilt),
`mix` (dry/wet for parallel saturation), `output` (makeup trim), and `bias`
(user-facing asymmetry / even-harmonic dial). The `Waveshaper`'s internal
gain-compensation is on so perceived loudness stays roughly constant as drive
rises; `output` is the user trim on top.

- **Rejected — Drop `mix` (series only).** Smaller surface but omits parallel
  saturation, a common and cheap musical control; captured as an open question is
  strictly worse than including a one-line dry/wet blend.
- **Rejected — Omit user `bias` (bake it per-voicing only).** Simpler surface, but
  the operator chose to expose asymmetry as a live control. See Decision 5 for how
  user `bias` is reconciled with the per-voicing baked character (voicing bakes
  *shape + emphasis*, not bias; `bias` stays a user control).

## Decisions

1. **Composed Production Effect, shipped primitives only.** `saturation` composes
   the shipped `Waveshaper` (nonlinearity + ADAA) and `SvfPrimitive` (pre/post
   emphasis + tone tilt). It invents **no new DSP primitive**; the composition and
   its voicing rationale are the deliverable. Per-channel state; allocation-free
   `process()`.

2. **Lab→effect graduation (a first).** Author `core/labs/saturation/` — `README.md`
   (theory: gain-staging, pre/post emphasis, voicing, naive-vs-ADAA anti-aliasing,
   naming `core/effects/saturation/` as the graduation target), an RT-safe
   composition kernel (`SaturationCore`), and a host-only `harness/`. Then `git mv`
   the kernel into `core/effects/saturation/` and refine in place. The graduation
   target is an **effect**, not a primitive — the first time the four-stage model's
   Production-Effect stage is walked greenfield.

3. **Two-tier structure, mirroring the primitive/effect split.**
   - **`SaturationCore` (lab kernel → effect DSP core)** — the pure, RT-safe
     per-channel composition: `preEmphasis → Waveshaper(drive, bias, shape) →
     postDeEmphasis → toneTilt`, with a parallel dry tap and a `mix` blend, then
     `output`. No sample-rate-thread or cross-thread concerns; unit-testable and
     teachable.
   - **`SaturationEffect` (effect-layer wrapper)** — adds the `Effect` contract on
     top of the core: the constexpr `ParameterDescriptor` table (single source of
     parameter truth), lock-free atomic cross-thread parameter handoff consumed at
     the top of `process()`, and `prepare()/reset()/process(AudioBlock&)/
     setParameter()`. Mirrors how `SvfEffect` wraps `SvfPrimitive`.

   **Signal chain (per channel):**
   ```
   process(x):
     wet = preEmphasis[voicing](x)          // SvfPrimitive, per-voicing fixed curve
     wet = waveshaper(drive·wet + bias)      // shipped Waveshaper; shape[voicing];
                                             //   internal gainComp = on
     wet = postDeEmphasis[voicing](wet)      // SvfPrimitive, per-voicing fixed curve
     wet = toneTilt(wet)                     // SvfPrimitive, user 'tone'
     y   = mix·wet + (1−mix)·x               // parallel dry/wet blend
     y   = output · y                        // user makeup trim
     return y
   ```

4. **Anti-aliasing: `quality {naive, adaa}` now, `oversampled` seam captured.** The
   `quality` parameter selects the `Waveshaper`'s naive or ADAA evaluation. A
   documented `quality::oversampled` value is reserved and a wiring seam described,
   but left **unwired**; it is filled when `design:primitive/oversampling` ships.
   **No depends-on edge is added** — saturation ships independently of the sibling.

5. **Voicing bakes shape + emphasis; `bias` stays a user control.** The `voicing`
   selector fixes each voicing's spectral fingerprint — its `Waveshaper` shape and
   its pre-emphasis / post-de-emphasis `SvfPrimitive` curves. The `bias` parameter
   is **user-facing, not per-voicing baked**: it is the live asymmetry / even-
   harmonic dial applied inside the `Waveshaper` (`shape(drive·x + bias)`). This
   reconciles "per-voicing baked character" (Fork 3) with "expose bias" (Fork 5)
   without a contradiction — voicing owns the fixed spectral identity, bias owns the
   live asymmetry.

6. **Parameter surface (six params).** `drive` [dB, log], `voicing` [discrete:
   softClip / tape / console / tubePreamp], `tone` [−1..1 tilt], `mix` [0..1],
   `output` [dB], `bias` [−1..1]. Plus a `quality` discrete [naive / adaa] per
   Decision 4. Exact ranges/skews and per-voicing preset numbers are a planning /
   tuning pass (open question), not a design blocker.

7. **Program-dependent boundary (one-concept-at-a-time).** This effect is the
   **static-character** composed saturation. Program-dependent / dynamic behavior
   (envelope-tracking drive, dynamic bias, tape-style compression) is
   `design:feature/program-dependent-saturation` (Phase-3 `phase-dynamic-systems`).
   The design records the boundary so the later item does not read as a duplicate.

8. **Validation reuses the shipped measurement infrastructure.** The harness drives
   the existing Goertzel/THD analyzer + sine stimulus to produce per-voicing
   harmonic signatures, a drive→THD monotonicity check, a gain-compensation
   unity-at-low-signal check, a `mix` dry/wet balance check, and the naive-vs-ADAA
   aliasing comparison. Assertions use analytic harmonic truths + named tolerances
   (the `svf-reference` pattern). Deeper nonlinear-specific harmonic tooling is the
   `harmonic-analysis` sibling's charter — boundary noted.

9. **Portability gate extended.** `scripts/check-portability.sh` (in CI, never a
   hook) learns `core/labs/saturation/**` and `core/effects/saturation/**` so its
   harness-isolation, dependency-direction, platform-independence, and file-size
   checks cover the new units. A lab harness may consume the kernel/effect; nothing
   portable may include a harness.

10. **Scope discipline (Principle XI).** This item delivers the voiced,
    static-character composed saturation effect built from shipped primitives, plus
    its lab. Excluded: any new nonlinearity (waveshapers owns it), the oversampling
    machinery (sibling), program-dependent dynamics (later item), and deeper
    harmonic-analysis tooling (sibling).

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later
scoping pass (`/speckit-clarify` / planning), **not** discarded:

- **Per-voicing emphasis curves + preset numbers.** The actual pre-emphasis /
  de-emphasis filter frequencies, gains, and `Waveshaper` shape/drive/bias defaults
  for each of the four voicings — a tuning pass validated by the harmonic harness.
- **Oversampled quality arm.** Wiring `quality::oversampled` once
  `design:primitive/oversampling` ships: fixed factor vs selectable, and whether the
  effect exposes the factor.
- **Multi-stage / cascaded saturation.** Whether a later revision stacks multiple
  nonlinear stages (console/tube character often comes from cascaded stages) — a
  richer topology deferred from this single-stage design.
- **`tone` control law.** Single tilt vs a small shelf pair; center-detent
  behavior; interaction with the per-voicing baked emphasis.
- **`mix` compensation.** Whether parallel blend needs phase-/delay-matching between
  the dry tap and the (ADAA-delayed) wet path, and gain-law of the blend
  (equal-power vs linear).
- **Gain-compensation law at the effect level.** Whether the effect adds its own
  makeup model on top of the `Waveshaper`'s internal gain-comp, or relies solely on
  it plus the user `output`.
- **Wavefolder / extreme voicings.** Whether additional voicings (e.g. a
  wavefold-based "fuzz") join the four named ones.
- **Harness output contract.** Whether the harness emits the standardized CSV
  harmonic-spectra artifact flagged by the measurement-infrastructure and
  three-layer-structure designs, so nonlinear labs/effects are comparable.

## Provenance

- **Roadmap item:** `design:feature/saturation` (status `planned` → `designing`),
  `depends-on: multi:feature/phase-digital-fundamentals`,
  `part-of: multi:feature/phase-nonlinear-dsp`. Design pointer set via
  `stackctl workflow link-design` to this file (before any content was written).
- **Compass:** `stackctl workflow compass design:feature/saturation --intent design`
  returned `on-course` (`designing` is the legitimate next move from `planned`).
- **Constitution:** Principle IX (Progressive Layered Architecture), with supporting
  Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII (Strict
  Typing & Small Modules), X (Measurable Engineering), XI (One Concept at a Time).
  `.specify/memory/constitution.md`.
- **Program vision:**
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` (Phase 2 —
  Nonlinear DSP; Saturation with Soft Clip / Tape / Console / Tube Preamp voicings;
  the four-stage graduation model; "each effect documents which primitives it
  uses").
- **Primitive precedent (the nonlinearity being composed):**
  `docs/superpowers/specs/2026-06-30-waveshapers-design.md` and the shipped
  `Waveshaper` / `ADAAWaveshaper` (`core/primitives/nonlinear/`), which explicitly
  deferred "the composed saturation effect (later item)" to this item.
- **Effect precedent (the contract being followed):** `core/effects/svf/svf-effect.h`
  (`SvfEffect` — constexpr `ParameterDescriptor` table, lock-free atomic parameter
  handoff, `prepare/reset/process(AudioBlock&)/setParameter`, per-channel state).
- **Pattern precedent (graduation seam):**
  `docs/superpowers/specs/2026-06-29-three-layer-structure-design.md` (lab kernel +
  host-only harness; `git mv` graduation; portability-gate extension as the
  enforcement seam).
- **Reused infrastructure:**
  `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` and the
  shipped Goertzel/THD analyzer + sine stimulus.
- **Current code surveyed:** `core/effects/{svf/svf-effect,modulated-delay}`,
  `core/primitives/nonlinear/{waveshaper,adaa-waveshaper,waveshaper-shapes,
  waveshaper-lut}.h`, `core/primitives/filters/svf-primitive.h`, `core/dsp/`,
  `scripts/check-portability.sh`.
- **Design method:** `superpowers:brainstorming` driven in-session under the
  `/stack-control:design` frontend; house-rules block injected (capture-over-YAGNI,
  ≥2 solution-space alternatives, required sections, operator-approval marker,
  handoff to `/stack-control:define`).
- **Decisions driven by the operator** across five forks: identity (voiced effect,
  capture-everything), anti-aliasing (ADAA now + captured oversampling seam), tone
  (per-voicing baked emphasis + user tilt), layer (composition lab that graduates
  into the effect), parameter surface (`drive, voicing, tone, mix, output, bias`).
  The operator consistently chose the capture-everything option, aligning with the
  capture-over-YAGNI house rule. The design reconciled the operator's Fork-3/Fork-5
  answers (voicing bakes shape + emphasis; `bias` is a user control — Decision 5).
- **Next step:** operator records the `design-approved:` marker on the roadmap node;
  on a met `design-to-spec` gate, hand off to `/stack-control:define` to author the
  Spec Kit spec.

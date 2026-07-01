---
description: "Task list for saturation feature implementation"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Saturation — Composed Production Effect

**Input**: Design documents from `specs/saturation/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/saturation-api.md, quickstart.md

**Tests**: INCLUDED. Constitution X makes objective measurement the acceptance evidence, and every
user story's Independent Test is measurement-based — so test tasks are first-class and authored
before the implementation they validate.

**Organization**: Tasks grouped by user story (US1–US5 from spec.md) for independent implementation
and testing. Pre-graduation, the three portable headers (`saturation-core.h`,
`saturation-voicings.h`, `saturation-effect.h`) and the host-only harness live under
`core/labs/saturation/`; Phase US5 graduates the three headers into `core/effects/saturation/`,
where they become the Production Effect (the lab persists as README + harness). This keeps the
dependency direction clean throughout: everything portable is co-located in the lab, the harness
consumes it, and nothing backwards (`effects/` → `labs/`) is ever created.

**Composition, not invention**: the effect composes the SHIPPED `Waveshaper`/`ADAAWaveshaper`
(`core/primitives/nonlinear/`) and `SvfPrimitive` (`core/primitives/filters/`) — no new DSP
primitive is authored (FR-001).

**Per-voicing tuning + oversampled wiring (design Open Questions, sequencing-only)**: US1 lands the
composition with a single first voicing (`softClip`); US2 completes the full four-voicing table. The
per-voicing emphasis/shape *numbers* are a tuning pass validated by the harness — no scope is cut.
The `oversampled` quality tier is reserved-unwired in US4 (FR-015); its wiring waits on the
oversampling sibling.

## Format: `[ID] [P?] [Story] [tier:label] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1–US5; Setup/Foundational/Polish carry no story label
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via `.stack-control/config.yaml` `tier_map`

## Path Conventions

- Lab (pre-graduation): `core/labs/saturation/` (three portable headers + `harness/`, `README.md`)
- Effect (post-graduation target): `core/effects/saturation/`
- Composed (shipped, unchanged): `core/primitives/nonlinear/`, `core/primitives/filters/`
- Tests: `tests/core/` (doctest); shared helpers: `tests/core/measurement-support.h`
- Gate: `scripts/check-portability.sh`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: directories, build wiring, and the lab skeleton everything else builds on.

- [ ] T001 [tier:fast] Create the lab directory `core/labs/saturation/` (with `harness/`) and the empty effect target directory `core/effects/saturation/`; record `saturation` in the effects taxonomy doc (`core/effects/README.md` if present, else note it in the lab README).
- [ ] T002 [tier:balanced] Wire CMake (`cmake/acfx-effect-targets.cmake` / `CMakeLists.txt` as appropriate) to register the new `tests/core/saturation-*-test.cpp` doctest suites and a host-only harness target `acfx_lab_saturation_harness`.
- [ ] T003 [P] [tier:fast] Author the `core/labs/saturation/README.md` skeleton: theory placeholder + walkthrough outline (gain-staging, per-voicing pre/post emphasis, voicing, naive-vs-ADAA anti-aliasing) naming `core/effects/saturation/` as the graduation target (filled in T021).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the composition-kernel surface, the voicing-table surface, and the shared test helpers
all stories depend on.

**⚠️ No user-story work begins until this phase is complete.**

- [ ] T004 [tier:balanced] Define the composition-kernel surface in `core/labs/saturation/saturation-core.h`: the `SaturationCore` class declaration per `contracts/saturation-api.md` (`prepare/reset/setVoicing/setQuality/setDrive/setBias/setTone/setMix/setOutput/process`) with its composed members declared (three `SvfPrimitive`, one `Waveshaper`/`ADAAWaveshaper`) — no bodies yet.
- [ ] T005 [P] [tier:balanced] Define the voicing-table surface in `core/labs/saturation/saturation-voicings.h`: `enum class SaturationVoicing`, `enum class SaturationQuality` (incl. reserved `oversampled`), and a `VoicingConfig` struct (shape + pre/post-emphasis SVF params) with a `voicingConfig(SaturationVoicing)` selector (declarations + a first `softClip` entry stub).
- [ ] T006 [P] [tier:powerful] Extend `tests/core/measurement-support.h` with saturation helpers reused across stories: per-voicing harmonic-signature capture (Goertzel/THD), drive→THD series, inharmonic-energy (aliasing) measure, mix dry/wet balance measure, and a DC-offset measure — analytic-bound assertion style (no fabricated numbers).

---

## Phase 3: User Story 1 — Apply a voiced saturation effect to audio (Priority: P1) 🎯 MVP

**Goal**: a working `SaturationCore` composing pre-emphasis → waveshaper(drive,bias) →
post-de-emphasis → tone → mix → output, with a DC-free output and a functioning dry/wet blend, for
the first voicing.

**Independent test**: drive a sine through the core; assert the chain order, drive→THD monotonicity,
fully-dry `mix` reproduces input, fully-wet is the saturated path, and the output is DC-free (US1
acceptance scenarios).

- [ ] T007 [US1] [tier:balanced] Write `tests/core/saturation-core-test.cpp`: signal-chain order (pre → shaper(`drive·x+bias`) → post → tone → `mix·wet+(1−mix)·x` → output), fully-dry reproduces input within tolerance, fully-wet is the wet path, silence-in→silence-out, asymmetric-bias DC-free output, no-stale-state-on-reset (FR-002/003/004, SC-003/005).
- [ ] T008 [US1] [tier:balanced] Write `tests/core/saturation-harmonics-test.cpp` (US1 slice): rising `drive` raises measured THD monotonically for `softClip`, and gain-compensation holds output loudness within the named band, via the T006 helpers (SC-001/002).
- [ ] T009 [US1] [tier:powerful] Implement `SaturationCore` in `core/labs/saturation/saturation-core.h`: the per-channel composition wiring the shipped `Waveshaper` (gainComp on) between two `SvfPrimitive` emphasis stages, plus the tone `SvfPrimitive`, the parallel dry/wet blend, and the output trim; RT-safe (`noexcept`, no alloc/lock); coefficients built in `prepare()`.
- [ ] T010 [US1] [tier:balanced] Wire the first `softClip` voicing end-to-end (shape + placeholder-but-documented pre/post emphasis from T005) and the default mix/gain-staging so T007/T008 pass; assert RT-safety via the allocation sentinel.

**Checkpoint**: US1 is an independently demonstrable MVP — a usable single-voicing saturation effect core.

---

## Phase 4: User Story 2 — Select among documented voicings (Priority: P1)

**Goal**: the full four-voicing table (Soft Clip, Tape, Console, Tube Preamp), each fixing a shape +
pre/post-emphasis, runtime-selectable on the core; `bias` remains a user control (not baked).

**Independent test**: each voicing matches its documented shape+emphasis signature and the four are
mutually distinguishable by ≥ the named margin; a voicing switch carries no stale state (US2
acceptance scenarios).

- [ ] T011 [US2] [tier:balanced] Write `tests/core/saturation-voicings-test.cpp`: per-voicing harmonic + spectral signature within tolerance, mutual distinguishability by ≥ the named margin, runtime-switch no-stale-filter/DC-state, and the assertion that `bias` is NOT baked per-voicing (Decision 5) (SC-001, FR-006/007/008).
- [ ] T012 [US2] [tier:powerful] Complete the voicing table in `core/labs/saturation/saturation-voicings.h`: the `tape`, `console`, `tubePreamp` entries (each a `Waveshaper` shape + pre-emphasis + post-de-emphasis SVF config), with the per-voicing numbers documented as the tuning pass (split a second file only if the size budget nears 500 lines — FR-023).
- [ ] T013 [US2] [tier:balanced] Wire all four voicings into `SaturationCore::setVoicing` dispatch; ensure a voicing switch reconfigures the emphasis + shape with no stale state; document each voicing's character in the README (T021) and a header comment.

**Checkpoint**: US1 + US2 deliver the full voiced effect core.

---

## Phase 5: User Story 3 — Shape the result with the musical control surface (Priority: P2)

**Goal**: the `SaturationEffect` host-facing wrapper — the constexpr `ParameterDescriptor` table
(single source of truth) and lock-free atomic cross-thread parameter handoff — exposing
drive/voicing/tone/mix/output/bias/quality with `prepare/reset/process(AudioBlock&)/setParameter`.

**Independent test**: parameter edits published from a non-audio thread take effect at the block
boundary with no alloc/lock; `mix` blends per the documented law; user `bias` yields even harmonics
DC-free (US3 acceptance scenarios).

- [ ] T014 [US3] [tier:balanced] Write `tests/core/saturation-effect-test.cpp`: cross-thread parameter publish → block-boundary apply with the allocation sentinel (no alloc/lock), `mix` dry/wet balance per the documented gain law, user-`bias` even-harmonic + DC-free assertions, and the compile-time descriptor-table invariants (FR-009/010/011/012, SC-005).
- [ ] T015 [US3] [tier:powerful] Implement `SaturationEffect` in `core/labs/saturation/saturation-effect.h`: the `Param` enum + constexpr `kParams` `ParameterDescriptor` table (drive/voicing/tone/mix/output/bias/quality) with the compile-time `isValidDescriptor` static_assert, per-channel `SaturationCore` state, lock-free `pendingBits_/pendingDirty_` atomics, and `prepare/reset/process(AudioBlock&)/setParameter` — mirroring `core/effects/svf/svf-effect.h`.
- [ ] T016 [US3] [tier:balanced] Wire `applyPending()` to denormalize each parameter and drive the matching `SaturationCore` setter (drive/voicing/tone/mix/output/bias/quality); confirm no direct core mutation in `setParameter` (audio-thread-only application) and make T014 pass.

---

## Phase 6: User Story 4 — Choose an anti-aliasing quality (Priority: P2)

**Goal**: the `quality` control selecting `naive` vs `adaa` (delegating to the composed
`Waveshaper`/`ADAAWaveshaper`), with the `oversampled` tier reserved as a documented, bounded,
unwired seam.

**Independent test**: ADAA inharmonic energy lower than naive by ≥ named margin; the parameter
surface is unchanged by the quality switch; the reserved `oversampled` selection yields the
documented bounded fallback (US4 acceptance scenarios).

- [ ] T017 [US4] [tier:balanced] Write `tests/core/saturation-aliasing-test.cpp`: high-frequency stimulus, naive-vs-ADAA inharmonic-energy comparison (≥ named margin, SC-004); assert the user parameter surface is identical across quality modes; assert selecting `oversampled` produces the defined bounded fallback (not a partial/aliased path, FR-015 / Constitution V).
- [ ] T018 [US4] [tier:balanced] Implement `quality` in `SaturationCore`: switch the nonlinear stage between the naive `Waveshaper` and the `ADAAWaveshaper` per `SaturationQuality`, with no stale state across the switch; implement the reserved `oversampled` bounded fallback (documented — e.g. transparent bypass to `adaa` with a recorded note) and make T017 pass.

---

## Phase 7: User Story 5 — Learn from the lab + graduate the kernel (Priority: P2)

**Goal**: the lab harness produces per-voicing harmonic evidence; the three portable headers
graduate into `core/effects/saturation/` as the Production Effect; the portability gate covers the
new locations.

**Independent test**: the harness regenerates per-voicing harmonic evidence + naive-vs-ADAA
comparison host-side; the graduated effect is the relocated lab kernel; the gate passes (US5
acceptance scenarios).

- [ ] T019 [US5] [tier:balanced] Implement `core/labs/saturation/harness/saturation-harness.cpp`: drive each voicing, emit per-voicing harmonic signatures and the naive-vs-ADAA aliasing comparison via the measurement infra; host-only target (`acfx_lab_saturation_harness`).
- [ ] T020 [US5] [tier:balanced] Extend `scripts/check-portability.sh` to cover `core/labs/saturation/**` and `core/effects/saturation/**` for harness-isolation, dependency-direction (effect composes primitives; nothing portable includes a harness), platform-independence, and file-size (FR-022); run it green.
- [ ] T021 [US5] [tier:balanced] Complete `core/labs/saturation/README.md`: theory + walkthrough + the measured evidence + the composition rationale ("which primitives it uses, and why"), naming the graduation target.
- [ ] T022 [US5] [tier:powerful] Graduate: `git mv` the three portable headers (`saturation-core.h`, `saturation-voicings.h`, `saturation-effect.h`) from `core/labs/saturation/` into `core/effects/saturation/`; add `core/effects/saturation/README.md` (composition rationale); update `#include` paths in tests + harness; confirm the lab persists as README + harness now driving the graduated effect; full suite + gate green (SC-006/007).

---

## Phase 8: Polish & Cross-Cutting Concerns

- [ ] T023 [P] [tier:fast] (Open question) Optional CSV harmonic-spectrum dump from the harness for cross-lab/effect comparison; gate it behind a `--csv` flag so the default run stays assertion-only.
- [ ] T024 [P] [tier:fast] Finalize the taxonomy/README cross-references (effect-consumes-primitive convention note) and the program-dependent-saturation boundary statement (FR-024 — this effect is static-character; dynamic behavior is the separate item).
- [ ] T025 [tier:balanced] Verify the full `ctest --preset test` suite, `scripts/check-portability.sh`, and the `daisy`/`teensy` cross-compiles are green (SC-006); confirm zero unpushed commits.

---

## Dependencies & Execution Order

- **Setup (T001–T003)** → **Foundational (T004–T006)** block everything.
- **US1 (P1, MVP)** depends only on Foundational. **US2 (P1)** depends on Foundational + the
  `SaturationCore` composition and `setVoicing` dispatch from US1. **US3 (P2)** depends on US1
  (the core it wraps). **US4 (P2)** depends on US1 (the nonlinear stage it switches). **US5 (P2)**
  depends on US1–US4 existing (the harness exercises them) and performs the graduation last.
- **Polish (P8)** last.

**Story completion order**: US1 → US2 → (US3 ∥ US4) → US5 → Polish.

## Parallel Opportunities

- Setup: T003 ∥ T001/T002 wiring.
- Foundational: T005 ∥ T006 (different files); T004 first (the surface US1 builds on).
- US3 and US4 are largely independent once US1+US2 land (wrapper plumbing vs nonlinear-stage
  switching) — their phases can proceed in parallel.
- Within a story, the test task is authored first, then implementation; the per-voicing entries in
  T012 are internally parallelizable by voicing.

## Implementation Strategy

- **MVP = US1** (Phase 3): a usable single-voicing composed saturation core — independently
  demonstrable.
- **Incremental delivery**: US2 completes the four voicings; US3 adds the host-facing Effect
  contract (param table + thread-safe handoff); US4 adds the anti-aliasing quality control; US5
  produces the lab evidence and graduates the kernel into the effects layer. Commit and push per task.

## Total

- **25 tasks**: Setup 3, Foundational 3, US1 4, US2 3, US3 3, US4 2, US5 4, Polish 3.

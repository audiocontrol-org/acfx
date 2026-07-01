---
description: "Task list for the oversampling primitive feature"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Oversampling — Reusable Anti-Aliasing Primitive

**Input**: Design documents in `specs/oversampling/` (plan.md, spec.md, research.md,
data-model.md, contracts/oversampling-api.md, quickstart.md)

**Tests**: INCLUDED — the acfx core is validated host-side by measurement (Constitution VIII, X);
test tasks are first-class here, not optional.

**Organization**: Tasks are grouped by user story (spec.md priorities) for independent
implementation and testing. The primitive is authored directly under
`core/primitives/oversampling/`; `core/labs/oversampling/` is its theory+harness companion.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1..US4 (user-story phases only)
- Every task names exact file path(s)

## Path Conventions

Single C++ core: primitive under `core/primitives/oversampling/`, lab under
`core/labs/oversampling/`, tests under `tests/core/` + `tests/support/`, gate at
`scripts/check-portability.sh`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the directory skeletons and make the portability gate ready before code lands.

- [ ] T001 Create the primitive + lab directory skeletons: `core/primitives/oversampling/` (with a placeholder `README.md`) and `core/labs/oversampling/{,harness/,tools/}` (with a placeholder `README.md`), per plan.md Project Structure.
- [ ] T002 [P] Extend `scripts/check-portability.sh` to cover `core/primitives/oversampling/**` (platform-free) and `core/labs/oversampling/**` (kernel headers platform-free + harness-free), following the existing `core/effects/saturation` gate block; must pass vacuously while the trees are empty.

**Checkpoint**: Directories exist; the gate recognizes the new trees.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The halfband coefficients + stages + shared measurement helper that every user story
builds on. ⚠️ No user story can proceed until this phase is complete.

- [ ] T003 Author the offline halfband-coefficient generator (host-only, NOT on any build/audio path) in `core/labs/oversampling/tools/gen-halfband.*` with documented design parameters per research.md Decision 6 (transition band, ≥ 80 dB stopband, ≤ 0.1 dB passband ripple).
- [ ] T004 Generate and author `core/primitives/oversampling/halfband-coefficients.h`: `static constexpr` symmetric (linear-phase) halfband FIR taps + tap count, with an inline provenance comment recording the generator invocation and design parameters (research.md Decision 5; contract "Coefficients").
- [ ] T005 Implement `HalfbandUpsampler` (1→2) and `HalfbandDownsampler` (2→1) in `core/primitives/oversampling/halfband-stage.h`: polyphase halfband FIR, fixed-size `std::array` delay lines, `init()/reset()`, per-stage `static constexpr latency()`; no heap (data-model "Halfband*", contract "HalfbandUpsampler/Downsampler").
- [ ] T006 [P] Factor the aliasing measurement into a shared helper under `tests/support/measurement/` (extract `aliasingMeasure` currently in `tests/core/saturation-aliasing-test.cpp`) so both the saturation and oversampler suites use one implementation (research.md Decision 8, FR-022); update the saturation test include.

**Checkpoint**: Coefficients + stages compile and are measurement-ready; the shared aliasing helper exists.

---

## Phase 3: User Story 1 — Oversample a nonlinearity transparently (Priority: P1) 🎯 MVP

**Goal**: A working, transparent `Oversampler<Factor>` that wraps a caller nonlinearity and
suppresses aliasing.

**Independent Test**: Identity-callback round-trip reproduces the input (delayed by reported
latency) within passband ripple; a driven nonlinearity shows lower inharmonic energy than the
naive path.

- [ ] T007 [P] [US1] Author `tests/core/oversampler-transparency-test.cpp`: identity `eval` round-trip ≈ input delayed by `latencySamples()` within the named passband-ripple tolerance; silence-in → silence-out (no NaN/Inf/denormal); `reset()` restores fresh behavior (FR-007/011/016, SC-001).
- [ ] T008 [P] [US1] Author `tests/core/oversampler-aliasing-test.cpp`: wrap a hard nonlinearity producing supra-Nyquist harmonics on a high-fundamental sine; assert oversampled inharmonic power is below the base-rate (naive) path by ≥ the named margin, using the shared `aliasingMeasure` (FR-008, SC-002).
- [ ] T009 [US1] Implement `Oversampler<int Factor>` in `core/primitives/oversampling/oversampler.h`: `static_assert` Factor ∈ {2,4,8}; compose the `log2(Factor)` up/down stage cascade with a fixed-size scratch; `init(sampleRate)`, `reset()`, `oversampledRate()`, and the templated `process(float x, Eval&&)` wrap-and-decimate (invoking `eval` exactly `Factor`× per call); a `static_assert` that `eval` is `noexcept` (contract "Oversampler", data-model "Oversampler").
- [ ] T010 [US1] Implement `latencySamples()` in `oversampler.h` by summing the cascade's per-stage `latency()` converted to base-rate samples (needed by the transparency test's delay alignment) (FR-006).
- [ ] T011 [US1] Make T007 + T008 pass at the MVP factor; fix any transparency/aliasing gaps.

**Checkpoint**: MVP — a transparent, alias-suppressing oversampler works and is measured.

---

## Phase 4: User Story 2 — Choose the oversampling factor (Priority: P1)

**Goal**: 2×/4×/8× each correct — transparent, alias-suppressing, well-defined latency.

**Independent Test**: For each factor, transparency + anti-aliasing pass; a higher factor never
worsens residual aliasing.

- [ ] T012 [P] [US2] Author `tests/core/oversampler-response-test.cpp`: assert halfband stopband rejection ≥ and passband ripple ≤ the named tolerances against analytic FIR truth, using the `svf-reference` assertion pattern (FR-009, SC-003).
- [ ] T013 [US2] Parameterize `oversampler-transparency-test.cpp` and `oversampler-aliasing-test.cpp` over Factor ∈ {2,4,8}; assert each factor passes and that residual aliasing is monotone-or-better with factor (FR-003/010, SC-007).
- [ ] T014 [US2] Verify/adjust the `Oversampler` cascade wiring so 4× = two stages and 8× = three stages produce correct results (fine→coarse up, reverse down); no code path assumes a single stage (data-model "Oversampler").

**Checkpoint**: All supported factors validated.

---

## Phase 5: User Story 3 — Report processing latency (Priority: P2)

**Goal**: `latencySamples()` equals the measured group delay exactly, per factor.

**Independent Test**: Measured impulse/reference group delay == reported latency for each factor.

- [ ] T015 [P] [US3] Author `tests/core/oversampler-latency-test.cpp`: measure group delay (impulse or known reference) for each factor and assert it equals `latencySamples()` to the sample; assert latency-aligned identity output matches input within ripple (consistency with US1) (FR-012, SC-004).
- [ ] T016 [US3] Reconcile any off-by-one between the analytic per-stage `latency()` sum and the measured group delay; document the latency derivation in a comment in `oversampler.h`.

**Checkpoint**: Latency reporting is exact and documented.

---

## Phase 6: User Story 4 — Close the reserved saturation oversampled tier (Priority: P1)

**Goal**: The saturation `oversampled` tier becomes a real oversampled path and is user-selectable
(closes saturation FR-015).

**Independent Test**: Saturation `oversampled` shows measurably lower inharmonic energy than
`naive` and is a selectable quality option.

- [ ] T017 [US4] In `core/effects/saturation/saturation-core.h`: add an `Oversampler<4>` member and an `oversampledShaper_` (`Waveshaper`); in `prepare()` prepare `oversampledShaper_` at `oversampler_.oversampledRate()`; extend `applyDrive()/applyBias()/configureShapers()` fan-out to keep it parameter-identical (research.md Decision 7, data-model client entity, FR-018).
- [ ] T018 [US4] In `saturation-core.h` `process()`: replace the interim ADAA mapping in the `oversampled` case with the real path — pre-emphasis (base) → `oversampler_.process(wet, [&](float s){ return oversampledShaper_.process(s); })` → post-de-emphasis (base) (FR-017).
- [ ] T019 [US4] In `core/effects/saturation/saturation-effect.h`: add `oversampled` to `kQualityLabels` and the discrete bucket→enum mapping so it is user-selectable (FR-019).
- [ ] T020 [US4] De-reserve the FR-015 seam comments in `saturation-core.h` and `core/effects/saturation/saturation-voicings.h` (the tier is now wired, not reserved), keeping them accurate.
- [ ] T021 [US4] Modify `tests/core/saturation-aliasing-test.cpp`: replace the `oversampled == adaa` assertion with `oversampled` inharmonic power < `naive` by ≥ the named margin; add an RT-safe runtime quality-switch check (naive↔adaa↔oversampled) asserting no stale-state artifact / NaN-Inf beyond a bounded transient (FR-020/021, SC-006).

**Checkpoint**: The primitive is proven end-to-end through a real effect; saturation FR-015 closed.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: RT-safety proof, lab documentation, and the green-gate confirmation.

- [ ] T022 [P] Extend `tests/core/no-allocation-test.cpp`: wrap `Oversampler::process()` for each factor in the allocation sentinel; assert zero heap allocations and no locks (FR-013, SC-005).
- [ ] T023 [P] Author `core/labs/oversampling/harness/oversampling-harness.cpp` (host-only): render transparency + aliasing-reduction evidence (spectra/sweeps) by `#include`ing and driving the primitive; ensure it is excluded from the portability kernel-header scan.
- [ ] T024 [P] Author `core/labs/oversampling/README.md` (theory: sampling/aliasing, linear-phase halfband FIR, polyphase decomposition, group-delay/latency) and `core/primitives/oversampling/README.md` (building blocks used + the caller RT-safety/`noexcept` contract).
- [ ] T025 Run `scripts/check-portability.sh` and the full `ctest --preset test`; confirm all gates + suites are green (quickstart.md).

---

## Dependencies & Story Completion Order

- **Setup (T001–T002)** → **Foundational (T003–T006)** block everything.
- **US1 (P1, T007–T011)** is the MVP; depends only on Foundational.
- **US2 (P1, T012–T014)** depends on US1 (extends the Oversampler + its tests).
- **US3 (P2, T015–T016)** depends on US1 (`latencySamples()` exists) — independently testable.
- **US4 (P1, T017–T021)** depends on US1 (needs a working `Oversampler`) — independently testable via saturation.
- **Polish (T022–T025)** depends on all prior phases.

## Parallel Opportunities

- Setup: T002 ∥ T001 follow-on.
- Foundational: T006 ∥ T003–T005.
- Within US1: T007 ∥ T008 (distinct test files) before T009–T011.
- Cross-story tests are distinct files: T012, T015 can be drafted in parallel once US1 lands.
- Polish: T022 ∥ T023 ∥ T024.

## Implementation Strategy

- **MVP = Phase 1 + Phase 2 + Phase 3 (US1)** — a transparent, alias-suppressing oversampler,
  measured. Ship/commit at this checkpoint.
- Then **US2** (factors) and **US4** (saturation proof, closes FR-015) — both P1 — followed by
  **US3** (latency, P2) and **Polish**.
- Commit atomically per task and push promptly (Commandment 1).

## Independent Test Criteria (per story)

- **US1**: identity round-trip transparency + driven-nonlinearity aliasing reduction (single factor).
- **US2**: transparency + aliasing + response pass for 2×/4×/8×; higher factor never worse.
- **US3**: measured group delay == `latencySamples()` per factor.
- **US4**: saturation `oversampled` < `naive` inharmonic energy, user-selectable, RT-safe switch.

## Suggested MVP Scope

Phases 1–3 (Setup + Foundational + US1): the smallest slice that delivers a working, measured,
transparent oversampling primitive.

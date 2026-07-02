---

description: "Task list for envelope-followers — dynamics level-detector primitive"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Envelope Followers — Dynamics Level-Detector Primitive

**Input**: Design documents from `specs/envelope-followers/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/envelope-follower-api.md, quickstart.md

**Tests**: INCLUDED. acfx is measurement-driven (Constitution VIII — test the core host-side; X —
measurable engineering); the spec's SC-001..010 are objective test obligations, so each user story
carries doctest tasks written FIRST (must fail before implementation).

**Organization**: Tasks are grouped by user story. `core/primitives/dynamics/envelope-follower.h` is
a single header-only primitive; the kernel is authored in the lab, then **graduated** (atomic `git mv`)
into the new `dynamics/` category, and refined in place. All `process()` code is RT-safe (no heap, no
locks, bounded — Constitution VI).

## Format: `[ID] [P?] [Story] [tier:label] Description`
- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1..US6 (maps to spec.md user stories); Setup/Foundational/Polish carry no story label
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via `.stack-control/config.yaml` `tier_map` (haiku/sonnet/opus). `fast` = mechanical (1–2 files, complete spec); `balanced` = standard impl/integration/tests; `powerful` = subtle correctness / broad-context.

---

## Phase 1: Setup (lab scaffold + build wiring)

**Purpose**: create the lab, the kernel skeleton, and the gate/build wiring every story builds on

- [x] T001 [tier:balanced] Create `core/labs/envelope-follower/README.md` — ballistics theory (peak / RMS / peak-hold; branching vs decoupled + smooth variant; one-pole `a = exp(−1/(τ·fs))` with the 1 − 1/e convention; −120 dBFS dB floor) and a walkthrough naming `core/primitives/dynamics/` as the graduation target.
- [x] T002 [tier:balanced] Create `core/labs/envelope-follower/envelope-follower.h` — kernel skeleton: `namespace acfx`, enums `DetectMode{peak,rms,peakHold}` / `Ballistics{branching,decoupled}` / `DetectDomain{linear,decibel}`, and the `EnvelopeFollower` class **declaration** with all methods from `contracts/envelope-follower-api.md` as `noexcept` stubs; includes limited to `<cmath>`/`<cstdint>`/`core/dsp/`.
- [x] T003 [P] [tier:fast] Create `core/labs/envelope-follower/harness/envelope-follower-harness.cpp` (host-only stub) and wire its build target in `CMakeLists.txt`, mirroring the existing lab harness targets.
- [x] T004 [P] [tier:fast] Register the five test files in `tests/CMakeLists.txt`: `envelope-follower-test.cpp`, `envelope-follower-ballistics-test.cpp`, `envelope-follower-rms-test.cpp`, `envelope-follower-hold-test.cpp`, `envelope-follower-db-test.cpp`.
- [x] T005 [P] [tier:balanced] Extend `scripts/check-portability.sh` with coverage markers `C-EF-PRIM` (`core/primitives/dynamics/**` gate-ready: platform-free, harness-free) and `C-EF-LAB` (`core/labs/envelope-follower/*.h` kernel headers harness-free), per FR-021.

---

## Phase 2: Foundational (BLOCKING — kernel substrate + graduation)

**Purpose**: the shared class substrate and the category graduation that ALL user stories depend on

**⚠️ CRITICAL**: no user-story work begins until this phase completes

- [x] T006 [tier:powerful] Implement `init(float)` / `reset()` / state members and the guarded coefficient helper in the kernel: `aAtk`,`aRel`,`aRms = exp(−1/(τ·fs))` bounded to `[0,1)`, `holdSamples = round(hold·fs)`; every `set*` recomputes-and-caches, never per-sample; degenerate inputs (≤0 fs/time) guarded to finite results (FR-013, FR-016, FR-018; data-model state table).
- [x] T007 [tier:balanced] Implement the `process(float)` dispatch skeleton wiring detect(mode) → domain(linear/dB) → smooth(topology) with per-branch stubs returning a defined value (FR-002); establishes the per-sample chain from `data-model.md`.
- [x] T008 [tier:balanced] **GRADUATION (one atomic commit)**: `git mv core/labs/envelope-follower/envelope-follower.h core/primitives/dynamics/envelope-follower.h` (creating the `dynamics/` category dir with its first inhabitant); move `dynamics/` from a prospectus family to an **inhabited** category in `core/primitives/README.md`; update the harness + test `#include` paths to the graduated location — all in the SAME commit (FR-019, FR-020, SC-010).

**Checkpoint**: `core/primitives/dynamics/envelope-follower.h` exists with a working skeleton; `dynamics/` is inhabited; the gate is wired.

---

## Phase 3: User Story 1 — Peak detector with attack/release ballistics (Priority: P1) 🎯 MVP

**Goal**: a working peak follower (branching, linear) whose envelope rises at the attack rate and falls at the release rate.

**Independent Test**: unit step → ~63% within attack time; 1→0 step → ~37% within release time; sine A → peak ≈ A.

### Tests (write FIRST — must FAIL)
- [x] T009 [P] [US1] [tier:balanced] `tests/core/envelope-follower-test.cpp` — interface + default config (peak/branching/linear after `init` only), `reset()` clears to 0, silence → exactly 0, no NaN/Inf on DC/impulse (SC-008, Assumptions).
- [x] T010 [P] [US1] [tier:powerful] `tests/core/envelope-follower-ballistics-test.cpp` — branching attack-time (step → 1−1/e within `attackSeconds`) and release-time (→ 1/e within `releaseSeconds`) via the measurement stimulus/response infra + `svf-reference` named tolerances (SC-001, SC-002).

### Implementation
- [x] T011 [US1] [tier:fast] Implement the peak detector `|x|` in `core/primitives/dynamics/envelope-follower.h` (FR-011).
- [x] T012 [US1] [tier:balanced] Implement the branching ballistics smoother (attack coeff when level > env, release coeff otherwise) (FR-014).
- [x] T013 [US1] [tier:balanced] Return the linear-domain envelope and make T009/T010 pass (SC-001/002/003-peak).
- [x] T014 [US1] [tier:fast] Extend `tests/core/no-allocation-test.cpp` to assert 0 heap allocations in `process()` for peak/branching/linear (SC-007).

**Checkpoint**: US1 is a fully functional, independently testable peak level detector (MVP).

---

## Phase 4: User Story 2 — RMS detector (Priority: P2)

**Goal**: program-level RMS detection tracking A/√2 for a sine of amplitude A.

**Independent Test**: steady sine A → RMS envelope ≈ A/√2; settled ripple below the named bound.

### Tests (write FIRST — must FAIL)
- [x] T015 [P] [US2] [tier:balanced] `tests/core/envelope-follower-rms-test.cpp` — sine A → A/√2 (± tol) and settled peak-to-peak ripple below bound (SC-003-rms, SC-004).

### Implementation
- [x] T016 [US2] [tier:powerful] Implement RMS mode: one-pole mean-square accumulate (`aRms` from `setRmsWindow`, independent of attack/release) → `sqrt` in the linear domain (FR-009, FR-011).
- [x] T017 [US2] [tier:fast] Make T015 pass; extend `no-allocation-test.cpp` for the rms config (SC-007).

**Checkpoint**: peak (US1) and RMS (US2) both work independently.

---

## Phase 5: User Story 4 — Ballistics topology choice: decoupled + smooth (Priority: P2)

**Goal**: enum-selectable decoupled topology (no release-then-attack artifact) and the smooth variant.

**Independent Test**: transient-then-decay → branching shows the artifact, decoupled tracks monotonically; smooth variant matches the characterized curve.

### Tests (write FIRST — must FAIL)
- [x] T018 [P] [US4] [tier:powerful] Add decoupled + smooth cases to `tests/core/envelope-follower-ballistics-test.cpp` — decoupled tracks a decaying tail without the branching artifact; `setSmooth(true)` applies the attack coeff in both stages (US4 acceptance).

### Implementation
- [x] T019 [US4] [tier:powerful] Implement the decoupled smoother (release stage feeding an attack stage) selectable via `setBallistics` (FR-004, FR-014).
- [x] T020 [US4] [tier:balanced] Implement `setSmooth(bool)` smooth variant (attack coeff in both smoothing stages) for both topologies (FR-005).
- [x] T021 [US4] [tier:fast] Make T018 pass; extend `no-allocation-test.cpp` for decoupled + smooth (SC-007).

**Checkpoint**: both topologies, both smooth-capable, work under peak and RMS.

---

## Phase 6: User Story 3 — Peak-hold detector (Priority: P3)

**Goal**: latch a detected peak for a hold time before release; topology-independent.

**Independent Test**: impulse to P then silence → holds ≈ P for `holdSeconds`, then releases; a higher peak during hold restarts the window; works under branching AND decoupled.

### Tests (write FIRST — must FAIL)
- [x] T022 [P] [US3] [tier:balanced] `tests/core/envelope-follower-hold-test.cpp` — dwell ≈ `holdSeconds` (± one control period), restart-on-higher-peak, and identical hold behavior under both topologies (SC-005, FR-015).

### Implementation
- [x] T023 [US3] [tier:balanced] Implement peak-hold at the detector/latch stage (upstream of smoothing): latch `|x|`, `holdCounter` countdown, restart on a higher peak; topology-independent by construction (FR-015).
- [x] T024 [US3] [tier:fast] Make T022 pass; extend `no-allocation-test.cpp` for peakHold (SC-007).

**Checkpoint**: all three modes work under both topologies.

---

## Phase 7: User Story 5 — Decibel-domain detection + floor (Priority: P3)

**Goal**: dB-domain smoothing for level-independent time constants, with a −120 dBFS floor.

**Independent Test**: two steps ≥ 20 dB apart → equal dB attack time (± tol) while linear differs; silence → −120 dB (never −∞).

### Tests (write FIRST — must FAIL)
- [x] T025 [P] [US5] [tier:powerful] `tests/core/envelope-follower-db-test.cpp` — level-independent attack time across a ≥20 dB pair (dB equal, linear differs); silence/sub-floor → −120 dB, no −∞/NaN (SC-006, SC-008).

### Implementation
- [x] T026 [US5] [tier:balanced] Implement decibel domain: clamp detected level to −120 dBFS, `20·log10` before smoothing, return the smoothed dB value, selectable via `setDomain` (FR-006, FR-012).
- [x] T027 [US5] [tier:fast] Make T025 pass; extend `no-allocation-test.cpp` for the dB config (SC-007).

**Checkpoint**: full mode × topology × domain catalog functional (the first graduated cut).

---

## Phase 8: User Story 6 — Lab teaching + structural verification (Priority: P2)

**Goal**: the lab teaches the ballistics and the graduated primitive is a properly-inhabited category.

**Independent Test**: `core/labs/envelope-follower/` has README (theory) + host-only harness; `core/primitives/dynamics/` holds the primitive; `core/primitives/README.md` lists `dynamics/` inhabited; the portability gate passes over both paths.

### Implementation
- [x] T028 [US6] [tier:balanced] Fill `core/labs/envelope-follower/harness/envelope-follower-harness.cpp` — drive step/impulse/sine stimuli and emit attack/release + RMS/hold measurement evidence (host-only; never included by a portable unit).
- [x] T029 [US6] [tier:balanced] Finalize `core/labs/envelope-follower/README.md` theory to match the shipped primitive; confirm the lab persists (README + harness) and `dynamics/` is documented as inhabited (moved from prospectus) in `core/primitives/README.md` (US6 acceptance, FR-020).
- [x] T030 [US6] [tier:fast] Run `scripts/check-portability.sh` — PASS over `core/labs/envelope-follower/**` and `core/primitives/dynamics/**` (harness isolation, dependency direction, platform independence, file size) (SC-009).

**Checkpoint**: the graduation is complete, gate-clean, and taught.

---

## Phase 9: Polish & Cross-Cutting

- [x] T031 [P] [tier:balanced] Verify the ~300–500-line module budget (VII, FR-022); if `envelope-follower.h` exceeds it, split a detector/ballistics helper header out under `core/primitives/dynamics/` and update includes.
- [x] T032 [tier:fast] Run `quickstart.md` end-to-end (`make test` + `scripts/check-portability.sh`) and confirm every validation-scenario outcome.
- [x] T033 [P] [tier:fast] Final `no-allocation-test.cpp` sweep asserting 0 allocations across ALL modes × topologies × domains (SC-007).
- [x] T034 [tier:powerful] Characterize the deferred low-sample-rate coefficient accuracy (research Decision 7) at ≤ 32 kHz with short τ; apply a higher-order correction only if the ballistics test flags timing drift beyond tolerance.

---

## Dependencies & Execution Order

- **Setup (Phase 1)**: no dependencies — start immediately; T003/T004/T005 are `[P]`.
- **Foundational (Phase 2)**: after Setup; T006 → T007 → **T008 (graduation)**. BLOCKS all user stories (they reference the graduated path).
- **User stories (Phases 3–8)**: after Foundational. Dependency-aware order: US1 (P1) → US2 (P2) → US4 (P2, decoupled) → US3 (P3) → US5 (P3) → US6 (P2, verification). US6's structural graduation was performed in T008; its phase completes the lab's teaching deliverable and verifies the gate.
- **Polish (Phase 9)**: after all desired stories.

### Story independence notes
- US1 is the standalone MVP. US2/US3/US5 each add an orthogonal mode/domain and are independently testable. US4 extends the smoother selection and is testable against US1's peak path. US6 is structural/teaching and depends only on the primitive existing (T008) plus whatever behavior has landed.

### Within each story
- Test tasks are written FIRST and must FAIL before implementation (TDD). Detector before smoother before domain. Commit after each task or logical group; push promptly (Commandment I).

---

## Parallel Opportunities

- Setup: T003, T004, T005 in parallel.
- Per story, the test-authoring task `[P]` can be written while the previous story's implementation lands (different files).
- `no-allocation-test.cpp` extensions are appended per story (same file — NOT `[P]` across stories).

## Parallel Example: Phase 1
```bash
Task: "T003 harness stub + build target in CMakeLists.txt"
Task: "T004 register 5 test files in tests/CMakeLists.txt"
Task: "T005 extend scripts/check-portability.sh (C-EF-PRIM, C-EF-LAB)"
```

---

## Implementation Strategy

- **MVP** = Phase 1 + Phase 2 + Phase 3 (US1): a working, gate-clean, independently-tested peak level
  detector living at `core/primitives/dynamics/envelope-follower.h` with the `dynamics/` category
  materialized. STOP and validate here if desired.
- **Incremental**: add US2 (RMS) → US4 (topologies) → US3 (peak-hold) → US5 (dB) → US6 (lab/verify),
  each a testable increment that does not break prior stories. The first graduated cut is the **full
  catalog** (2026-07-02 clarification), so all stories are in-scope for this feature.

## Notes
- `[P]` = different files, no incomplete-task dependency. `[Story]` labels give traceability.
- The graduation (T008) is the single atomic commit that creates the category — do not split it.
- One design open question (low-fs coefficient accuracy) is characterized in T034, not a scope item.

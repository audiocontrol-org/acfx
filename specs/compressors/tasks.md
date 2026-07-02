---
description: "Task list for compressors — gain computer + compressor effect"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Compressors — Gain Computer + Compressor Effect

**Input**: Design documents from `specs/compressors/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md,
contracts/gain-computer-api.md, contracts/compressor-effect-api.md, quickstart.md

**Tests**: INCLUDED. acfx is measurement-driven (Constitution VIII — test the core host-side; X —
measurable engineering); the spec's SC-001..015 are objective test obligations, so each user story
carries doctest tasks written FIRST (must fail before implementation).

**Organization**: Tasks are grouped by user story. The stateless `GainComputer` kernel is authored in
`core/labs/compressor/`, then **graduated** (atomic `git mv`) into `core/primitives/dynamics/` as the
category's second inhabitant and refined in place. `CompressorCore` + `CompressorEffect` compose the
shipped `EnvelopeFollower` / `SvfPrimitive` / `DelayLine` primitives in `core/effects/compressor/`,
mirroring `core/effects/saturation/` exactly. All `process()` code is RT-safe (no heap, no locks,
bounded — Constitution VI).

## Format: `[ID] [P?] [Story] [tier:label] Description`
- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1..US13 (maps to spec.md user stories); Setup/Foundational/Polish carry no story label
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via
  `.stack-control/config.yaml` `tier_map` (haiku/sonnet/opus). `fast` = mechanical (1–2 files, complete
  spec); `balanced` = standard impl/integration/tests; `powerful` = subtle correctness / broad-context.

---

## Phase 1: Setup (lab scaffold + skeletons + build wiring)

**Purpose**: create the lab, the kernel/effect skeletons, and the gate/build wiring every story builds on

- [ ] T001 [tier:balanced] Create `core/labs/compressor/README.md` — gain-computer curve theory (compress/limit/expand/gate; the unified quadratic C¹ soft knee straddling threshold; `out = thr + (level−thr)/ratio`), feedforward vs feedback (post-makeup/pre-mix tap), the level-vs-gain ballistics-site tradeoff, closed-form auto-makeup, and a walkthrough naming `core/primitives/dynamics/gain-computer.h` as the graduation target.
- [ ] T002 [tier:balanced] Create `core/labs/compressor/gain-computer.h` — kernel skeleton: `namespace acfx`, `enum class GainMode{compress,limit,expand,gate}`, and the `GainComputer` class **declaration** with all methods from `contracts/gain-computer-api.md` as `noexcept` stubs; includes limited to `<cmath>`/`<cstdint>`/`core/dsp/`.
- [ ] T003 [P] [tier:balanced] Create `core/effects/compressor/compressor-core.h` — `CompressorCore` skeleton: enums `Detection{feedForward,feedBack}` / `BallisticsSite{level,gain}`, composed-primitive members (EnvelopeFollower detector + gainSmoother, GainComputer, SvfPrimitive, DelayLine), and all methods from `contracts/compressor-effect-api.md` as `noexcept` stubs.
- [ ] T004 [P] [tier:balanced] Create `core/effects/compressor/compressor-effect.h` — `CompressorEffect` skeleton: `enum class StereoLink{perChannel,linked}`, the `Param` id enum, and the `Effect`-contract method stubs, mirroring `core/effects/saturation/saturation-effect.h` structure.
- [ ] T005 [P] [tier:fast] Create `core/labs/compressor/harness/compressor-harness.cpp` (host-only stub) and wire its build target in `CMakeLists.txt`, mirroring the existing lab harness targets.
- [ ] T006 [P] [tier:fast] Register the compressor test files in `tests/CMakeLists.txt`: `gain-computer-test.cpp`, `compressor-test.cpp`, `compressor-topology-test.cpp`, `compressor-sidechain-test.cpp`, `compressor-lookahead-test.cpp`, `compressor-makeup-link-test.cpp`, `compressor-effect-test.cpp`.
- [ ] T007 [P] [tier:balanced] Extend `scripts/check-portability.sh` with coverage markers for `core/primitives/dynamics/gain-computer.h` (primitive: platform-free, harness-free, effect-free), `core/labs/compressor/*.h` (kernel headers harness-free), and `core/effects/compressor/**` (effect: no platform/harness headers; may include shipped primitives), per FR-027.

---

## Phase 2: Foundational (BLOCKING — GainComputer + graduation + core/effect substrate)

**Purpose**: the stateless gain-computer curve, its category graduation, and the composition/wrapper substrate ALL user stories depend on

**⚠️ CRITICAL**: no user-story work begins until this phase completes

- [ ] T008 [tier:powerful] Implement `GainComputer` fully in `core/labs/compressor/gain-computer.h`: the piecewise map for all four `GainMode`s with the **single unified quadratic C¹ knee** straddling threshold (hard corner at knee=0), `range`-bounded expand/gate, `ratio` guarded ≥1, branch-only arithmetic, no runtime state, no NaN/Inf (FR-001..007, FR-024; `contracts/gain-computer-api.md`).
- [ ] T009 [tier:powerful] **GRADUATION (one atomic commit)**: `git mv core/labs/compressor/gain-computer.h core/primitives/dynamics/gain-computer.h` (second inhabitant of the existing `dynamics/` category); move "gain computers" from a prospectus family to an **inhabited** member in `core/primitives/README.md`; update the lab harness + test `#include` paths to the graduated location — all in the SAME commit (FR-025, FR-026, SC-015).
- [ ] T010 [tier:powerful] Implement `CompressorCore::prepare/reset` + state and the per-sample `process(x, key)` chain in `core/effects/compressor/compressor-core.h`: key select → sidechain HPF (SvfPrimitive) → detect (EnvelopeFollower, dB domain) → `GainComputer.computeGainDb` → gain-site smoother → makeup → lookahead (DelayLine) → VCA multiply → feedback tap (post-makeup/pre-mix `prevOutput`) → mix → output; coefficients recomputed in setters, lookahead buffer sized in `prepare()`, allocation-free/bounded (FR-008..017, FR-022, FR-024; `data-model.md` chain).
- [ ] T011 [tier:powerful] Implement the `CompressorEffect` substrate in `core/effects/compressor/compressor-effect.h`: the constexpr `ParameterDescriptor` table (17 params per `data-model.md`) as the single source of truth, the `static_assert` descriptor-validity guard, `prepare/reset/process` wiring per-channel `CompressorCore`s, the lock-free atomic pending-parameter handoff, and `latencySamples()` — mirroring `saturation-effect.h` (FR-018..022). If the file exceeds the ~300–500-line budget, split the table + denormalize into `core/effects/compressor/compressor-parameters.h` (FR-028).

**Checkpoint**: `core/primitives/dynamics/gain-computer.h` exists and is inhabited/gate-ready; `CompressorCore` and `CompressorEffect` compile with a working (feedforward, level-site, compress) signal path.

---

## Phase 3: User Story 1 — Compress a signal above a threshold (Priority: P1) 🎯 MVP

**Goal**: a working downward compressor (threshold/ratio/hard-knee, feedforward, level site, attack/release).

**Independent test**: `tests/core/compressor-test.cpp` — analytic static level map (unity below, `thr+(level−thr)/ratio` above) + attack ~63% within the attack time.

### Tests (write FIRST — must FAIL)
- [ ] T012 [US1] [tier:balanced] `tests/core/compressor-test.cpp` — assert the compress static map (thr −20, ratio 4 → −10 dBFS in ≈ −17.5 dBFS out; −30 dBFS passes at unity) and attack/release time to ~63% within tolerance, reusing the measurement stimulus/response + `svf-reference` named-tolerance pattern (SC-001, SC-002).

### Implementation
- [ ] T013 [US1] [tier:balanced] Verify/complete the feedforward, level-site compress path end-to-end in `CompressorCore` (attack/release delegated to the level detector `EnvelopeFollower`; makeup=0, mix=1, output=0 defaults) so T012 passes (FR-003, FR-009, FR-011).

**Checkpoint**: US1 is a fully functional, independently testable downward compressor (MVP).

---

## Phase 4: User Story 2 — GainComputer primitive + lab (Priority: P1)

**Goal**: the stateless gain computer is proven standalone and taught in the lab (graduation done in T009).

**Independent test**: `tests/core/gain-computer-test.cpp` — pure `computeGainDb` sweeps vs the analytic curve per mode; statelessness (call-order independence); portability gate over the new paths.

### Tests (write FIRST — must FAIL)
- [ ] T014 [US2] [P] [tier:balanced] `tests/core/gain-computer-test.cpp` — sweep `computeGainDb(levelDb)` across levels for each `GainMode`; assert it matches the analytic curve, is ≤ 0 dB, respects `range` (expand/gate), and is call-order independent (stateless) (SC-001/005, US2 independent test).

### Implementation
- [ ] T015 [US2] [P] [tier:balanced] Complete `core/labs/compressor/README.md` teaching content and add the graduated-primitive walkthrough; verify `core/primitives/README.md` documents the gain computer as inhabited and the harness/tests reference the graduated path (FR-026, SC-014).
- [ ] T016 [US2] [tier:balanced] Run `scripts/check-portability.sh`; confirm PASS over `core/primitives/dynamics/gain-computer.h`, `core/labs/compressor/**`, and `core/effects/compressor/**` (harness isolation, dependency direction, platform independence, file size) (SC-014).

**Checkpoint**: the gain computer works standalone, is gate-clean, and is taught.

---

## Phase 5: User Story 3 — Limit with a brickwall ratio (Priority: P2)

**Goal**: `limit` mode holds output at the threshold (ratio → ∞) within the knee.

### Tests (write FIRST — must FAIL)
- [ ] T017 [US3] [tier:balanced] Extend `tests/core/gain-computer-test.cpp` and `tests/core/compressor-test.cpp` — limit mode (thr −6; −1 dBFS → ≈ −6 dBFS out); continuity at threshold (SC-001).

### Implementation
- [ ] T018 [US3] [tier:balanced] Confirm/refine the `limit` branch in `GainComputer` (slope 0 above threshold within the knee) so the limit tests pass (FR-004).

**Checkpoint**: compress (US1) and limit (US3) both work.

---

## Phase 6: User Story 4 — Shape a soft knee (Priority: P2)

**Goal**: the unified quadratic knee is C¹-continuous across its full width and reduces to the hard knee at knee=0.

### Tests (write FIRST — must FAIL)
- [ ] T019 [US4] [tier:powerful] Extend `tests/core/gain-computer-test.cpp` — sample the GR curve densely across `[thr−W/2, thr+W/2]`; assert value- and slope-continuity (C¹) for every mode and exact match to the hard-knee curve at knee=0 (SC-003).

### Implementation
- [ ] T020 [US4] [tier:powerful] Verify/refine the unified quadratic knee interpolation in `GainComputer` so the C¹ continuity test passes across all modes (FR-007).

**Checkpoint**: hard and soft knees correct for all modes.

---

## Phase 7: User Story 5 — Detection topology feedforward vs feedback (Priority: P2)

**Goal**: `feedBack` reads the post-makeup/pre-mix previous output; settles to the analytic fixed point; stable.

### Tests (write FIRST — must FAIL)
- [ ] T021 [US5] [tier:powerful] `tests/core/compressor-topology-test.cpp` — feedback: steady above-threshold input settles to the detector→curve→gain fixed point (± tol) and is stable (no divergence/oscillation); feedforward vs feedback trajectories differ per their analytic models (SC-004).

### Implementation
- [ ] T022 [US5] [tier:powerful] Complete the `feedBack` path in `CompressorCore` (detector reads `prevOutput`, the post-makeup/pre-mix tap; defined cold-start) so T021 passes; confirm stability guard (FR-010).

**Checkpoint**: both topologies functional and stable.

---

## Phase 8: User Story 6 — Ballistics site level vs gain (Priority: P2)

**Goal**: `level` smooths the detected level; `gain` smooths the gain-reduction signal via the second smoother.

### Tests (write FIRST — must FAIL)
- [ ] T023 [US6] [tier:powerful] Extend `tests/core/compressor-topology-test.cpp` — for each `BallisticsSite`, a step above threshold reaches ~63% of steady GR within the attack time (measured on the level signal for `level`, on the gain-reduction signal for `gain`), per the site's analytic model (SC-002).

### Implementation
- [ ] T024 [US6] [tier:powerful] Complete the `gain`-site path in `CompressorCore` (level detector ~instantaneous; the `gainSmoother` `EnvelopeFollower`/one-pole applies attack/release to `grDb`) so T023 passes (FR-011).

**Checkpoint**: both ballistics sites voiced correctly under both topologies.

---

## Phase 9: User Story 13 — Host-facing effect wrapper (Priority: P2)

**Goal**: `CompressorEffect` satisfies the `Effect` contract with a lock-free param handoff and a validated table.

### Tests (write FIRST — must FAIL)
- [ ] T025 [US13] [tier:balanced] `tests/core/compressor-effect-test.cpp` — `static_assert`/concept check that `CompressorEffect` satisfies `Effect`; a `setParameter` from a non-audio thread is applied on the next `process()` (no lock, no torn read); a deliberately malformed descriptor fails the build (compile-time guard documented) (SC-011).

### Implementation
- [ ] T026 [US13] [tier:powerful] Finalize the `CompressorEffect` parameter denormalization + per-channel apply (dB→gain, index→enum, direct linear) and the `applyPending`/`applyAll` handoff, mirroring `saturation-effect.h`, so T025 passes (FR-018..021).

**Checkpoint**: the effect is host-drivable and thread-safe.

---

## Phase 10: User Story 7 — Expand and gate (Priority: P3)

**Goal**: below-threshold downward expansion/gating bounded by `range`; unity above.

### Tests (write FIRST — must FAIL)
- [ ] T027 [US7] [tier:balanced] Extend `tests/core/gain-computer-test.cpp` — expand (thr −40, ratio 2, range −20; −50 dBFS attenuated per curve, ≥ −20 dB floor) and gate (below thr−knee → range floor; unity above) (SC-005).

### Implementation
- [ ] T028 [US7] [tier:balanced] Confirm/refine the `expand`/`gate` branches in `GainComputer` (downward slope / range-floor attenuation with the unified knee) so the tests pass (FR-005, FR-006).

**Checkpoint**: all four modes correct.

---

## Phase 11: User Story 8 — Sidechain filter HPF (Priority: P3)

**Goal**: a pre-detector highpass on the key attenuates low-frequency detection; 0 Hz = bypass; main path unaffected.

### Tests (write FIRST — must FAIL)
- [ ] T029 [US8] [tier:balanced] `tests/core/compressor-sidechain-test.cpp` — scHpf 120 Hz: a 60 Hz tone yields much less GR than a 1 kHz tone at the same level; 0 Hz = full-band; main-path signal otherwise unchanged (SC-006).

### Implementation
- [ ] T030 [US8] [tier:balanced] Wire the composed `SvfPrimitive` highpass into the key path of `CompressorCore` (cutoff 0 = bypass; guarded near Nyquist / ≤0) so T029 passes (FR-013).

**Checkpoint**: sidechain filtering works; main path clean.

---

## Phase 12: User Story 9 — External sidechain key (Priority: P3)

**Goal**: detection tracks an external key when supplied; falls back to the main input otherwise.

### Tests (write FIRST — must FAIL)
- [ ] T031 [US9] [tier:balanced] Extend `tests/core/compressor-sidechain-test.cpp` — external key above threshold + quiet main → main attenuated per key level; no key → detection reads the main input (SC-007).

### Implementation
- [ ] T032 [US9] [tier:balanced] Route the external key through `CompressorEffect::process` into `CompressorCore::process(x, key)` (keyless callers pass `x` as `key`), threading the host's sidechain buffer per the `AudioBlock` convention, so T031 passes (FR-014).

**Checkpoint**: keyed detection (ducking/de-essing) works.

---

## Phase 13: User Story 10 — Lookahead (Priority: P3)

**Goal**: the main path is delayed N samples; reported latency == round(L·fs); first-sample transient limiting.

### Tests (write FIRST — must FAIL)
- [ ] T033 [US10] [tier:balanced] `tests/core/compressor-lookahead-test.cpp` — reported `latencySamples()` == round(L·fs); in limit mode a first-sample transient is limited from its first sample (no threshold overshoot a zero-lookahead limiter would pass) (SC-008).

### Implementation
- [ ] T034 [US10] [tier:balanced] Wire the composed `DelayLine` into the main path of `CompressorCore` (buffer sized in `prepare()` from max lookahead; 0 = bypass, 0 latency) and report latency from `CompressorEffect::prepare` so T033 passes (FR-015, FR-021).

**Checkpoint**: lookahead limiting works; latency reported.

---

## Phase 14: User Story 11 — Makeup, auto-makeup, mix, output (Priority: P3)

**Goal**: manual makeup; closed-form auto-makeup `−computeGainDb(0 dBFS)` (0 for expand/gate); parallel mix; output trim.

### Tests (write FIRST — must FAIL)
- [ ] T035 [US11] [tier:balanced] `tests/core/compressor-makeup-link-test.cpp` — manual makeup M dB → +M dB output; auto-makeup → below-threshold signal ≈ unity; auto-makeup == 0 in expand/gate; mix 0 = dry passthrough, mix 1 = fully compressed; output trim scales (SC-009).

### Implementation
- [ ] T036 [US11] [tier:balanced] Implement makeup (manual + closed-form auto per FR-016, recomputed on threshold/ratio/knee/mode change, 0 for expand/gate), dry/wet `mix`, and `output` trim in `CompressorCore`/`CompressorEffect` so the makeup tests pass (FR-016).

**Checkpoint**: gain staging (makeup/mix/output) correct.

---

## Phase 15: User Story 12 — Stereo/multichannel linking (Priority: P3)

**Goal**: `linked` drives a common gain from the cross-channel max; `perChannel` is independent; mono degenerates cleanly.

### Tests (write FIRST — must FAIL)
- [ ] T037 [US12] [tier:powerful] Extend `tests/core/compressor-makeup-link-test.cpp` — linked: a transient in L only → both channels get the same GR (image stable); perChannel: only L attenuated; mono config degenerates to per-channel (SC-010).

### Implementation
- [ ] T038 [US12] [tier:powerful] Implement stereo linking in `CompressorEffect::process` (compute one detector value = max across linked channels, apply a common gain; `perChannel` independent) so T037 passes (FR-017).

**Checkpoint**: the full captured catalog is functional (the first cut).

---

## Phase 16: Polish & Cross-Cutting

- [ ] T039 [P] [tier:balanced] Extend `tests/core/no-allocation-test.cpp` — assert zero heap allocation on `CompressorEffect::process()` across all modes/topologies/sites/feature combinations (SC-012); assert no NaN/Inf on silence/DC/impulse/threshold-crossing/feedback-cold-start (SC-013).
- [ ] T040 [P] [tier:balanced] Complete the host-only `core/labs/compressor/harness/compressor-harness.cpp` — drive level-swept / step / impulse stimuli and emit static-curve + attack/release + latency measurement evidence; confirm no portable unit includes the harness (Constitution VIII, SC-014).
- [ ] T041 [tier:balanced] Final `scripts/check-portability.sh` PASS over all new paths; confirm file-size budget (split `compressor-effect.h` → `compressor-parameters.h` if needed, FR-028); `make test` green end-to-end (quickstart.md validation table).

---

## Dependencies & Execution Order

- **Setup (Phase 1)**: no dependencies — start immediately; T003/T004/T005/T006/T007 are `[P]`.
- **Foundational (Phase 2)**: after Setup; T008 → T009 (graduation) → T010/T011. BLOCKS all user stories (they reference the graduated path + the core/effect substrate).
- **User stories (Phases 3–15)**: after Foundational. Priority order: US1 (P1, MVP) → US2 (P1) → US3/US4/US5/US6/US13 (P2) → US7/US8/US9/US10/US11/US12 (P3). Most stories touch distinct code paths/tests and are independently testable; US3/US4/US7 refine `GainComputer` branches, US5/US6/US8/US9/US10/US11/US12 refine `CompressorCore`/`CompressorEffect` paths.
- **Polish (Phase 16)**: after all desired stories.

### Story independence notes
- US1 is the standalone MVP (feedforward, level-site, compress). US2 proves the graduated `GainComputer` standalone. US3/US7 add modes; US4 the knee; US5 the feedback topology; US6 the gain ballistics site; US8/US9 the sidechain; US10 lookahead; US11 gain staging; US12 stereo linking; US13 the host wrapper. Each carries its own doctest(s) and is independently testable against the analytic model.

### Within each story
- Tests (write FIRST, must fail) → implementation → checkpoint. `[P]` marks distinct-file tasks with no incomplete dependency.

## Parallel Opportunities
- Phase 1: T003, T004, T005, T006, T007 run in parallel (distinct files).
- Test-authoring tasks across different suites (T014, T029, T033, T035) are `[P]` where they touch new files.
- Polish T039/T040 are `[P]` (distinct files).

## Parallel Example: Phase 1
```
T003 compressor-core.h skeleton   ┐
T004 compressor-effect.h skeleton ├─ run together (distinct files)
T005 harness stub + CMake         │
T006 register tests in CMake      │
T007 extend check-portability.sh  ┘
```

## Implementation Strategy
- **MVP** = Phase 1 + Phase 2 + Phase 3 (US1): a working, gate-clean, independently-tested downward
  compressor composing the graduated `GainComputer`. Ship-checkpoint after US1.
- **Incremental delivery**: each subsequent story adds one orthogonal, independently-testable
  capability. The full captured catalog (2026-07-02 clarification) is complete at Phase 15; Phase 16
  hardens RT-safety, the lab, and the portability gate.

## Notes
- Every `process()` path is RT-safe: no heap, no locks, bounded; coefficients recompute in setters/
  `prepare`; the only buffer (lookahead) is sized in `prepare()` (Constitution VI, FR-022).
- Compose-don't-re-derive: detection/filter/delay come from the shipped `EnvelopeFollower` /
  `SvfPrimitive` / `DelayLine`; only the `GainComputer` curve is new (Constitution IX, FR-008).
- Descriptor **shapes** are normative; exact numeric ranges are a tuning-pass placeholder (mirroring
  `SaturationEffect`), tracked in `data-model.md` and research Deferred.

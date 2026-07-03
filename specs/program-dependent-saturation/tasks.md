---
description: "Task list for program-dependent-saturation — dynamics modulator + envelope-modulated saturator"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Program-Dependent Saturation — Dynamics Modulator + Envelope-Modulated Saturator

**Input**: Design documents from `specs/program-dependent-saturation/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md,
contracts/dynamics-modulator-api.md, contracts/program-dependent-saturation-effect-api.md, quickstart.md

**Tests**: INCLUDED. acfx is measurement-driven (Constitution VIII — test the core host-side; X —
measurable engineering); the spec's SC-001..016 are objective test obligations, so each user story
carries doctest tasks written FIRST (must fail before implementation).

**Organization**: Tasks are grouped by user story. The stateless `DynamicsModulator` kernel is authored
in `core/labs/program-dependent-saturation/`, then **graduated** (atomic `git mv`) into
`core/primitives/dynamics/` as the category's **third inhabitant** and refined in place.
`ProgramDependentSaturationCore` + `ProgramDependentSaturationEffect` compose the shipped **unchanged**
`SaturationCore` / `EnvelopeFollower` / `SvfPrimitive` in `core/effects/program-dependent-saturation/`,
mirroring `core/effects/saturation/` exactly. All `process()` code is RT-safe (no heap, no locks,
bounded — Constitution VI).

## Format: `[ID] [P?] [Story] [tier:label] Description`
- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1..US12 (maps to spec.md user stories); Setup/Foundational/Polish carry no story label
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via
  `.stack-control/config.yaml` `tier_map` (haiku/sonnet/opus). `fast` = mechanical (1–2 files, complete
  spec); `balanced` = standard impl/integration/tests; `powerful` = subtle correctness / broad-context.

---

## Phase 1: Setup (lab scaffold + skeletons + build wiring)

**Purpose**: create the lab, the kernel/effect skeletons, and the gate/build wiring every story builds on

- [x] T001 [tier:balanced] Create `core/labs/program-dependent-saturation/README.md` — envelope-modulates-nonlinearity theory (dynamic drive/bias/tone/mix; signed depth + `linear`/`log`/`exp` response curves anchored at (0,0)/(1,1); the normalized-dB-window envelope mapping, default −60..0 dBFS; feedforward vs feedback with the final-output tap; the opto/vari-mu/tape-comp characters), and a walkthrough naming `core/primitives/dynamics/dynamics-modulator.h` as the graduation target.
- [x] T002 [tier:balanced] Create `core/labs/program-dependent-saturation/dynamics-modulator.h` — kernel skeleton: `namespace acfx`, `enum class ModCurve{linear,logarithmic,exponential}`, and the `DynamicsModulator` class **declaration** with all methods from `contracts/dynamics-modulator-api.md` as `noexcept` stubs; includes limited to `<cmath>`/`<cstdint>`/`core/dsp/`.
- [x] T003 [P] [tier:balanced] Create `core/effects/program-dependent-saturation/program-dependent-saturation-core.h` — `ProgramDependentSaturationCore` skeleton: enums `ModTarget{drive,bias,tone,mix}` / `Detection{feedForward,feedBack}`, composed-unit members (unchanged `SaturationCore`, one shared `EnvelopeFollower`, `SvfPrimitive`, four `DynamicsModulator`), and all methods from `contracts/program-dependent-saturation-effect-api.md` as `noexcept` stubs.
- [x] T004 [P] [tier:balanced] Create `core/effects/program-dependent-saturation/program-dependent-saturation-effect.h` — `ProgramDependentSaturationEffect` skeleton: `enum class DynamicPreset{none,opto,variMu,tapeComp}` / `StereoLink{perChannel,linked}`, the `Param` id enum (0..23 per `data-model.md`), and the `Effect`-contract method stubs, mirroring `core/effects/saturation/saturation-effect.h` structure.
- [x] T005 [P] [tier:fast] Create `core/labs/program-dependent-saturation/harness/program-dependent-saturation-harness.cpp` (host-only stub) and wire its build target in `CMakeLists.txt`, mirroring the existing lab harness targets.
- [x] T006 [P] [tier:fast] Register the test files in `tests/CMakeLists.txt`: `dynamics-modulator-test.cpp`, `program-dependent-saturation-orthogonality-test.cpp`, `program-dependent-saturation-test.cpp`, `program-dependent-saturation-matrix-test.cpp`, `program-dependent-saturation-topology-test.cpp`, `program-dependent-saturation-presets-test.cpp`, `program-dependent-saturation-sidechain-test.cpp`, `program-dependent-saturation-effect-test.cpp`.
- [x] T007 [P] [tier:balanced] Extend `scripts/check-portability.sh` with coverage markers for `core/primitives/dynamics/dynamics-modulator.h` (primitive: platform-free, harness-free, effect-free), `core/labs/program-dependent-saturation/*.h` (kernel headers harness-free), and `core/effects/program-dependent-saturation/**` (effect: no platform/harness headers; may include shipped primitives + `SaturationCore`), per FR-024.

---

## Phase 2: Foundational (BLOCKING — DynamicsModulator + graduation + core/effect substrate)

**Purpose**: the stateless modulation mapper, its category graduation, and the composition/wrapper substrate ALL user stories depend on

**⚠️ CRITICAL**: no user-story work begins until this phase completes

- [x] T008 [tier:powerful] Implement `DynamicsModulator` fully in `core/labs/program-dependent-saturation/dynamics-modulator.h`: `setDepth` (clamped to [−1,+1]) / `setCurve`, and `modulate(envNorm) → depth·curve(envNorm)` in normalized units for all three `ModCurve`s (each mapping [0,1]→[0,1], through (0,0)/(1,1), monotone, finite at endpoints), `depth==0 ⇒ 0` for all env, branch-only arithmetic, no runtime state, no NaN/Inf (FR-001..003, FR-021; `contracts/dynamics-modulator-api.md`).
- [x] T009 [tier:powerful] **GRADUATION (one atomic commit)**: `git mv core/labs/program-dependent-saturation/dynamics-modulator.h core/primitives/dynamics/dynamics-modulator.h` (third inhabitant of the existing `dynamics/` category); move the modulation mapper from a prospectus family to an **inhabited** member in `core/primitives/README.md`; update the lab harness + test `#include` paths to the graduated location — all in the SAME commit (FR-022, FR-023, SC-016).
- [x] T010 [tier:powerful] Implement `ProgramDependentSaturationCore::prepare/reset` + state and the per-sample `process(x, key)` chain + `newBlock(blockEnvNorm)` in `core/effects/program-dependent-saturation/program-dependent-saturation-core.h`: source select (external key / main) → sidechain HPF (`SvfPrimitive`) → topology fork (feedforward source / feedback `prevOutput`) → detect (`EnvelopeFollower`, dB domain) → normalize over the ref window → per-target `clamp(base + modulator·span, nativeRange)` for drive/bias/mix **per-sample** and tone **per-block via `newBlock()`** → push to the **unchanged** `SaturationCore` setters → `SaturationCore::process(x)` → `prevOutput = y` (final-output feedback tap); coefficients recomputed in setters, allocation-free/bounded, single shared detector; zero-depth guard skips a modulated setter when its depth is 0 (FR-004..010a, FR-019/020; `data-model.md` chain).
- [x] T011 [tier:powerful] Implement the `ProgramDependentSaturationEffect` substrate in `core/effects/program-dependent-saturation/program-dependent-saturation-effect.h`: the constexpr `ParameterDescriptor` table (~24 params per `data-model.md`, the static passthrough block matching `SaturationEffect` exactly) as the single source of truth, the `static_assert` descriptor-validity guard, `prepare/reset/process` wiring per-channel `ProgramDependentSaturationCore`s with a once-per-block `newBlock()` call, and the lock-free atomic pending-parameter handoff — mirroring `saturation-effect.h` (FR-015..018). If the file exceeds the ~300–500-line budget, split the table + denormalize into `core/effects/program-dependent-saturation/program-dependent-saturation-parameters.h` (FR-025).

**Checkpoint**: `core/primitives/dynamics/dynamics-modulator.h` exists and is inhabited/gate-ready; `ProgramDependentSaturationCore` and `ProgramDependentSaturationEffect` compile with a working (feedforward, drive-target) modulation path.

---

## Phase 3: User Story 1 — Make saturation track level with dynamic drive (Priority: P1) 🎯 MVP

**Goal**: a working envelope-driven drive modulation (feedforward, single detector) — louder input = more saturation.

**Independent test**: `tests/core/program-dependent-saturation-test.cpp` — THD rises with input level per the analytic `depth·curve(env)` drive-offset model; modulation ~63% within the attack time.

### Tests (write FIRST — must FAIL)
- [x] T012 [US1] [tier:powerful] `tests/core/program-dependent-saturation-test.cpp` — with +driveDepth, feedforward, assert measured harmonic content (THD) increases monotonically with input level per the analytic drive-offset model within tolerance, and a level step drives the modulation to ~63% within the attack time (recovers at release), reusing the measurement stimulus/response + harmonic-analysis + `svf-reference` named-tolerance patterns (SC-001, SC-005).

### Implementation
- [x] T013 [US1] [tier:balanced] Verify/complete the feedforward drive-target modulation path end-to-end in `ProgramDependentSaturationCore` (shared `EnvelopeFollower` attack/release; other target depths 0; static voicing/output defaults) so T012 passes (FR-005, FR-006, FR-009).

**Checkpoint**: US1 is a fully functional, independently testable dynamic saturator (MVP).

---

## Phase 4: User Story 2 — DynamicsModulator primitive + lab (Priority: P1)

**Goal**: the stateless modulation mapper is proven standalone and taught in the lab (graduation done in T009).

**Independent test**: `tests/core/dynamics-modulator-test.cpp` — pure `modulate` sweeps vs the analytic `depth·curve` per curve; statelessness (call-order independence); portability gate over the new paths.

### Tests (write FIRST — must FAIL)
- [x] T014 [US2] [P] [tier:balanced] `tests/core/dynamics-modulator-test.cpp` — sweep `modulate(envNorm)` across [0,1] for each `ModCurve`; assert it matches the analytic `depth·curve(envNorm)`, passes through (0,0)/(1,1), is monotone and finite at endpoints, `depth==0 ⇒ 0`, sign follows `sign(depth)`, and is call-order independent (stateless) (SC-004, SC-007).

### Implementation
- [x] T015 [US2] [P] [tier:balanced] Complete `core/labs/program-dependent-saturation/README.md` teaching content and add the graduated-primitive walkthrough; verify `core/primitives/README.md` documents the modulation mapper as inhabited and the harness/tests reference the graduated path (FR-023, SC-015).
- [x] T016 [US2] [tier:balanced] Run `scripts/check-portability.sh`; confirm PASS over `core/primitives/dynamics/dynamics-modulator.h`, `core/labs/program-dependent-saturation/**`, and `core/effects/program-dependent-saturation/**` (harness isolation, dependency direction, platform independence, file size) (SC-015).

**Checkpoint**: the modulation mapper works standalone, is gate-clean, and is taught.

---

## Phase 5: User Story 3 — Zero-depth orthogonality (Priority: P1)

**Goal**: all depths 0 ⇒ the effect equals the static `SaturationEffect` exactly (the load-bearing contract).

**Independent test**: `tests/core/program-dependent-saturation-orthogonality-test.cpp` — a stimulus battery through both effects at matching static params; outputs identical within a tight tolerance.

### Tests (write FIRST — must FAIL)
- [x] T017 [US3] [tier:powerful] `tests/core/program-dependent-saturation-orthogonality-test.cpp` — with all depths 0 and matching static params (drive/voicing/tone/mix/output/bias/quality), run tones, sweeps, noise, and transients through both `ProgramDependentSaturationEffect` and the shipped `SaturationEffect`; assert outputs match within a tight tolerance (byte-for-byte where the paths coincide); assert a single non-zero depth modulates only that target (SC-002).

### Implementation
- [x] T018 [US3] [tier:powerful] Guarantee the orthogonality identity in `ProgramDependentSaturationCore`: when a target depth is 0 the modulated setter pushes the exact static base (same denormalization as `SaturationEffect`) and the per-block tone setter is skipped (no redundant SVF recompute), so T017 passes (FR-007).

**Checkpoint**: the static saturator is preserved exactly; the dynamic layer is provably orthogonal.

---

## Phase 6: User Story 4 — Modulate the full matrix (drive/bias/tone/mix) (Priority: P2)

**Goal**: bias/tone/mix are independently modulated from the shared envelope with no cross-talk.

### Tests (write FIRST — must FAIL)
- [x] T019 [US4] [tier:powerful] `tests/core/program-dependent-saturation-matrix-test.cpp` — for each target in {bias, tone, mix} (others at depth 0), assert the corresponding `SaturationCore` parameter is offset by the analytic `depth·curve(env)` amount (bias via even-harmonic measurement; tone via spectral tilt; mix via wet/dry ratio) and the untargeted parameters stay at their static bases; independent multi-target depths show no cross-talk (SC-003).

### Implementation
- [x] T020 [US4] [tier:balanced] Verify/complete the bias/tone/mix modulation wiring in `ProgramDependentSaturationCore` (per-sample bias/mix; per-block tone via `newBlock()`), each fed by its own `DynamicsModulator` from the shared envelope, so T019 passes (FR-006, FR-010a).

**Checkpoint**: the full four-target modulation matrix works.

---

## Phase 7: User Story 5 — Signed depth + response curve (Priority: P2)

**Goal**: signed depth (up/down) and `linear`/`log`/`exp` curves per target.

### Tests (write FIRST — must FAIL)
- [x] T021 [US5] [tier:powerful] Extend `tests/core/program-dependent-saturation-test.cpp` and `tests/core/dynamics-modulator-test.cpp` — a negative drive depth decreases THD with level (mirror-image of positive); equal-magnitude opposite-sign trajectories mirror about the static base; the three curves produce distinguishable analytic offset-vs-env shapes (SC-004).

### Implementation
- [x] T022 [US5] [tier:balanced] Confirm/refine the signed-depth and `ModCurve` handling in `DynamicsModulator` and its use in `ProgramDependentSaturationCore` so the direction/curve tests pass (FR-002, FR-003).

**Checkpoint**: direction and response shaping correct for every target.

---

## Phase 8: User Story 6 — Detection topology feedforward vs feedback (Priority: P2)

**Goal**: `feedBack` reads the previous final output `y`; settles to a stable fixed point.

### Tests (write FIRST — must FAIL)
- [x] T023 [US6] [tier:powerful] `tests/core/program-dependent-saturation-topology-test.cpp` — feedback: a steady input settles to the analytic detector→modulation→saturation fixed point (± tol) and is stable (no divergence/oscillation), with a defined cold start; feedforward vs feedback trajectories differ per their analytic models (SC-006).

### Implementation
- [x] T024 [US6] [tier:powerful] Complete the `feedBack` path in `ProgramDependentSaturationCore` (detector reads `prevOutput`, the final-output tap; defined cold-start floor) so T023 passes; confirm the stability/clamp guard (FR-008, FR-010).

**Checkpoint**: feedforward and feedback both work; feedback is stable.

---

## Phase 9: User Story 7 — Detector mode + ballistics selection (Priority: P2)

**Goal**: peak/rms/peakHold detection, branching/decoupled ballistics, attack/release — delegated to `EnvelopeFollower`.

### Tests (write FIRST — must FAIL)
- [x] T025 [US7] [tier:balanced] Extend `tests/core/program-dependent-saturation-test.cpp` — peak vs rms on the same transient produce the characterized faster/sharper vs smoother/slower modulation; a configured attack/release yields the ~63%-in-τ step response on the modulation (SC-005).

### Implementation
- [x] T026 [US7] [tier:balanced] Wire `setDetectorMode`/`setBallistics`/`setAttack`/`setRelease` in `ProgramDependentSaturationCore` through to the shared `EnvelopeFollower` so T025 passes (FR-009).

**Checkpoint**: the full detector catalog is selectable and shapes the modulation.

---

## Phase 10: User Story 8 — Effect wrapper (Effect contract) (Priority: P2)

**Goal**: the host-facing wrapper satisfies the `Effect` concept with a lock-free parameter handoff.

### Tests (write FIRST — must FAIL)
- [x] T027 [US8] [tier:balanced] `tests/core/program-dependent-saturation-effect-test.cpp` — assert `ProgramDependentSaturationEffect` satisfies the `Effect` concept (`prepare`/`process`/`reset`/`parameters`/`setParameter`); a `setParameter` from a non-audio thread applies on the next `process()` with no lock/torn read; a malformed descriptor fails the build via `static_assert` (SC-012).

### Implementation
- [x] T028 [US8] [tier:balanced] Complete the `ProgramDependentSaturationEffect` parameter denormalization + per-parameter apply (static passthrough → `SaturationCore`; detector/topology → core; matrix depth/curve → the four `DynamicsModulator`s; sidechain/link → core) with the lock-free atomic handoff consumed at the top of `process()` so T027 passes (FR-015..018).

**Checkpoint**: the effect is host-drivable and thread-safe.

---

## Phase 11: User Story 9 — Named dynamic-character presets (Priority: P2)

**Goal**: `opto`/`variMu`/`tapeComp` configure the matrix to documented characters; `none` = neutral.

### Tests (write FIRST — must FAIL)
- [ ] T029 [US9] [tier:balanced] `tests/core/program-dependent-saturation-presets-test.cpp` — for each preset, assert the realized modulation-matrix configuration (per-target depths/curves, topology, detector/ballistics) equals the documented preset definition; `none` leaves the matrix at all-depths-0 defaults (SC-008).

### Implementation
- [ ] T030 [US9] [tier:balanced] Implement the `DynamicPreset` apply in `ProgramDependentSaturationEffect` as a documented, testable table of matrix configurations (no new DSP), with `none` = neutral, so T029 passes (FR-014).

**Checkpoint**: the named characters recall correctly.

---

## Phase 12: User Story 10 — Sidechain filter HPF (Priority: P3)

**Goal**: a pre-detector highpass attenuates low-frequency modulation; 0 Hz = bypass; main path unaffected.

### Tests (write FIRST — must FAIL)
- [ ] T031 [US10] [tier:balanced] `tests/core/program-dependent-saturation-sidechain-test.cpp` — scHpf 120 Hz: a 60 Hz tone yields much less modulation than a 1 kHz tone at the same level; 0 Hz = full-band; the main saturation path is otherwise unchanged (SC-009).

### Implementation
- [ ] T032 [US10] [tier:balanced] Wire the composed `SvfPrimitive` highpass into the detection path of `ProgramDependentSaturationCore` (cutoff 0 = bypass; guarded near Nyquist / ≤0) so T031 passes (FR-011).

**Checkpoint**: sidechain filtering shapes detection; main path clean.

---

## Phase 13: User Story 11 — External sidechain key (Priority: P3)

**Goal**: modulation tracks an external key when supplied; falls back to the main input otherwise.

### Tests (write FIRST — must FAIL)
- [ ] T033 [US11] [tier:balanced] Extend `tests/core/program-dependent-saturation-sidechain-test.cpp` — a loud external key + quiet main → modulation follows the key level, applied to the main saturation path; no key → detection reads the main input (SC-010).

### Implementation
- [ ] T034 [US11] [tier:balanced] Route the external key through `ProgramDependentSaturationEffect::process` into `ProgramDependentSaturationCore::process(x, key)` (keyless callers pass `x` as `key`), threading the host's sidechain buffer per the `AudioBlock` convention, so T033 passes (FR-012).

**Checkpoint**: keyed modulation (ducking the saturation) works.

---

## Phase 14: User Story 12 — Stereo/multichannel linking (Priority: P3)

**Goal**: linked detection drives a common modulation (cross-channel max); per-channel is independent.

### Tests (write FIRST — must FAIL)
- [ ] T035 [US12] [tier:balanced] Extend `tests/core/program-dependent-saturation-sidechain-test.cpp` — a stereo signal with a transient in L only: linked mode applies the same modulation to both channels (character/image stable); per-channel modulates only L; linking over one channel degenerates to per-channel (SC-011).

### Implementation
- [ ] T036 [US12] [tier:balanced] Implement `StereoLink` in `ProgramDependentSaturationEffect` (linked = drive a common modulation from the max detector value across linked channels; perChannel = independent) so T035 passes (FR-013).

**Checkpoint**: linked and per-channel detection both work; stereo character stable.

---

## Phase 15: Polish & Cross-Cutting Concerns

**Purpose**: RT-safety sentinel, numerical-safety sweep, and gate/doc closeout across all configs

- [ ] T037 [P] [tier:balanced] Extend `tests/core/no-allocation-test.cpp` to cover `ProgramDependentSaturationEffect::process()` across all targets, curves, topologies, presets, and sidechain/link configs — assert zero heap allocation after `prepare()` (SC-013).
- [ ] T038 [P] [tier:powerful] Numerical-safety sweep: assert no NaN/Inf for silence, DC, impulse, level step, feedback cold start, extreme depth, and low-sample-rate short-τ in any configuration, and that every modulated parameter stays within `SaturationCore`'s valid range (SC-014); add cases to the relevant suites.
- [ ] T039 [P] [tier:fast] Complete the host-only `core/labs/program-dependent-saturation/harness/program-dependent-saturation-harness.cpp` to emit orthogonality + THD-vs-level + step-response measurement evidence driving the graduated primitive and the effect.
- [ ] T040 [tier:balanced] Final gate + quickstart pass: run `make test` and `scripts/check-portability.sh`; confirm all suites green and the gate PASS over the three new paths; walk `quickstart.md` scenarios; confirm `core/primitives/README.md` lists the modulation mapper as inhabited.

---

## Dependencies & completion order

- **Phase 1 (Setup)** → **Phase 2 (Foundational, BLOCKING)** → user-story phases.
- **Foundational (T008–T011)** blocks every user story: the primitive, its graduation, the core chain, and the effect substrate.
- **P1 stories first**: US1 (MVP, T012–T013), US2 (primitive+lab, T014–T016), US3 (orthogonality, T017–T018).
- **P2 stories**: US4 (matrix), US5 (signed depth/curve), US6 (topology), US7 (detector), US8 (effect contract), US9 (presets).
- **P3 stories**: US10 (SC HPF), US11 (external key), US12 (linking).
- **Polish (T037–T040)** last.
- **Test-first within each story**: the test task precedes its implementation task and must fail first.

## Parallel opportunities

- Setup: T003/T004/T005/T006/T007 are `[P]` (distinct files) after T001/T002.
- US2's T014/T015 are `[P]`; the primitive test and lab-doc work touch different files.
- Polish T037/T038/T039 are `[P]` (distinct files).
- Across stories, test-authoring tasks for different suites can be drafted in parallel, but each story's implementation depends on the foundational substrate.

## Implementation strategy (MVP first)

1. **MVP = Phase 1 + Phase 2 + Phase 3 (US1)**: a working feedforward dynamic-drive saturator with the
   graduated `DynamicsModulator`. Shippable on its own.
2. **Prove the primitive + the contract (US2, US3)**: the standalone stateless mapper and the
   zero-depth orthogonality guarantee — the two P1 correctness anchors.
3. **Fill the matrix and controls (US4–US9)**: bias/tone/mix targets, signed depth/curves, feedback,
   detector catalog, the Effect wrapper, and the named presets.
4. **Routing extensions (US10–US12)**: sidechain HPF, external key, stereo linking.
5. **Polish**: RT-safety sentinel, numerical-safety sweep, harness evidence, final gate + quickstart.

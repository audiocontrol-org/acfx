---

description: "Task list for three-layer-structure implementation"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Three-Layer DSP Core Structure

**Input**: Design documents from `specs/three-layer-structure/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/{layering-rules,lab-folder}.md, quickstart.md

**Tests**: No NEW unit tests are requested — the migration is behavior-preserving, so the existing host suite (`ctest --preset test`) is the regression guard. Test tasks below are *verification* tasks (run the existing suite, the gate, the harness), not new TDD tests.

**Organization**: grouped by user story (US1 P1 → US2 P2 → US3 P3) so each is an independently testable increment.

## Format: `[ID] [P?] [Story] [tier:label] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via `.stack-control/config.yaml` `tier_map`.
- Paths are repo-relative; the include root is `core/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: establish a known-green baseline before any move.

- [x] T001 [tier:fast] Confirm the baseline builds and passes today: `cmake --preset test && cmake --build --preset test && ctest --preset test` — record that all tests are green before migration (SC-001 reference point).
- [x] T002 [tier:fast] Confirm the portability gate is green today: `./scripts/check-portability.sh` exits 0.

---

## Phase 2: Foundational (blocks US1 and US2)

**Purpose**: the taxonomy document that both migration stories write into; creating it first prevents two stories racing on its content.

- [x] T003 [tier:balanced] Create the taxonomy document `core/primitives/README.md`: enumerate the full intended taxonomy — inhabited categories `filters/`, `delays/`, `modulation/` (filled in by US1/US2) and documented-only prospectus families `nonlinear/`, `dynamics/`, `analog/`, `circuit/`, `convolution/`, `wdf/`, `physical/`; state the rule that a category folder is created only when it has an inhabitant and uninhabited categories live here, not as empty dirs (FR-008, FR-009, SC-006).

**Checkpoint**: taxonomy doc exists; baseline green.

---

## Phase 3: User Story 1 — Three-layer structure exists, proven by SVF end-to-end (P1) 🎯 MVP

**Goal**: the three layers exist and the SVF concept is migrated lab → graduated primitive → effect.

**Independent test**: `ls core/{labs,primitives,effects,dsp}` and `core/primitives/filters/svf-primitive.h` succeed; `ctest --preset test` passes unchanged; the SVF lab has README + host-only harness; `svf-effect.h` includes the new path.

- [x] T004 [US1] [tier:fast] `git mv core/primitives/svf-primitive.h core/primitives/filters/svf-primitive.h` (preserve history; body unchanged — research Decision 1).
- [x] T005 [US1] [tier:fast] Update the SVF effect include in `core/effects/svf/svf-effect.h`: `"primitives/svf-primitive.h"` → `"primitives/filters/svf-primitive.h"` (FR-012).
- [x] T006 [US1] [tier:fast] Update the SVF include in `core/effects/modulated-delay/modulated-delay-effect.h`: `"primitives/svf-primitive.h"` → `"primitives/filters/svf-primitive.h"` (FR-012).
- [x] T007 [US1] [tier:balanced] Author `core/labs/state-variable-filter/README.md` per the lab-folder contract: Theory, Walkthrough, Graduation target (`core/primitives/filters/svf-primitive.h`, graduated state), Measurements (per-mode frequency response + high-resonance stability) (FR-003, FR-013).
- [x] T008 [US1] [tier:balanced] Author the host-only harness `core/labs/state-variable-filter/harness/svf-harness.cpp` that drives the graduated `acfx::SvfPrimitive` and prints per-mode frequency-response + stability evidence, reusing the measurement intent of `tests/core/svf-test.cpp` (FR-013; research Decision 6). Include only `core/dsp/`, `core/primitives/**`, and the kernel — never an effect.
- [x] T009 [US1] [tier:balanced] Add the host-only CMake target `acfx_lab_svf_harness` (links `acfx_core`, built under the `test`/`desktop` configuration only, absent from `daisy`/`teensy`) — in `CMakeLists.txt` / the appropriate `cmake/*.cmake` (research Decision 3, FR-005).
- [x] T010 [US1] [tier:fast] Verify: `cmake --preset test && cmake --build --preset test && ctest --preset test` — all existing SVF tests pass unchanged (SC-001); build `acfx_lab_svf_harness` and run it, confirming it emits the expected evidence (SC-002).
- [x] T011 [US1] [tier:fast] Commit + push US1 as an atomic increment (Commandment 1).

**Checkpoint**: US1 is a complete, demonstrable MVP — one concept exists at every stage.

---

## Phase 4: User Story 2 — Remaining flat primitives migrated into the taxonomy (P2)

**Goal**: no loose primitive header remains in `core/primitives/`.

**Independent test**: `find core/primitives -maxdepth 1 -name '*.h'` returns nothing; `ctest --preset test` passes; modulated-delay builds with updated includes.

- [x] T012 [P] [US2] [tier:fast] `git mv core/primitives/delay-line.h core/primitives/delays/delay-line.h` (FR-011).
- [x] T013 [P] [US2] [tier:fast] `git mv core/primitives/lfo.h core/primitives/modulation/lfo.h` (FR-011).
- [x] T014 [US2] [tier:fast] Update delay-line + lfo includes in `core/effects/modulated-delay/wow-flutter.h`: `"primitives/delay-line.h"` → `"primitives/delays/delay-line.h"`, `"primitives/lfo.h"` → `"primitives/modulation/lfo.h"` (FR-012).
- [x] T015 [US2] [tier:fast] Update delay-line + lfo includes in `core/effects/modulated-delay/modulated-delay-effect.h` to the new `delays/` + `modulation/` paths (FR-012).
- [x] T016 [P] [US2] [tier:fast] Update `tests/core/delay-line-test.cpp` include: `"primitives/delay-line.h"` → `"primitives/delays/delay-line.h"` (FR-012).
- [x] T017 [P] [US2] [tier:fast] Update `tests/core/lfo-test.cpp` include: `"primitives/lfo.h"` → `"primitives/modulation/lfo.h"` (FR-012).
- [x] T018 [US2] [tier:fast] Mark `filters/`, `delays/`, `modulation/` as inhabited in `core/primitives/README.md` (FR-009).
- [x] T019 [US2] [tier:fast] Verify: `ctest --preset test` passes unchanged (SC-001); `find core/primitives -maxdepth 1 -name '*.h'` is empty (SC-003).
- [x] T020 [US2] [tier:fast] Commit + push US2 (Commandment 1).

**Checkpoint**: `core/primitives/` holds only category subdirs + README.

---

## Phase 5: User Story 3 — Portability gate enforces the layering invariants (P3)

**Goal**: `scripts/check-portability.sh` mechanically guards harness isolation + dependency direction and covers the new layout.

**Independent test**: gate exits 0 on the conformant tree; exits 1 naming the rule on each deliberate violation.

- [x] T021 [US3] [tier:balanced] Extend `scripts/check-portability.sh` with check C-1 (lab-harness isolation): fail if any portable file — `core/dsp/**`, `core/primitives/**`, `core/effects/**`, or a lab kernel (`core/labs/**` excluding `*/harness/**`) — has an `#include` matching `labs/[^/]*/harness/` (contracts/layering-rules.md C-1, FR-016).
- [x] T022 [US3] [tier:balanced] Extend `scripts/check-portability.sh` with check C-2 (dependency direction): fail if any `core/primitives/**` file `#include`s an `effects/` path (C-2, FR-015).
- [x] T023 [US3] [tier:balanced] Extend the existing platform-header scan (check 2) to exclude `core/labs/*/harness/` (host-only, legitimately non-portable) while still covering `core/labs/**` kernels; confirm the file-size scan already covers `core/labs` (contracts C-4, FR-017).
- [x] T024 [US3] [tier:balanced] Add the MCU-harness backstop (check C-3): fail if a `labs/*/harness/` path appears in the `adapters/daisy` or `adapters/teensy` build inputs (C-3, FR-005).
- [x] T025 [US3] [tier:fast] Verify the gate on the conformant tree: `./scripts/check-portability.sh` exits 0 with the new checks among its passes (SC-004).
- [x] T026 [US3] [tier:balanced] Verify the gate catches violations: temporarily inject (a) a `core/` file including a `labs/*/harness/` header and (b) a `core/primitives/` header including `effects/svf/svf-effect.h`; confirm exit 1 naming each rule; revert both probes (quickstart Scenario D, SC-004).
- [x] T027 [US3] [tier:fast] Commit + push US3 (Commandment 1).

**Checkpoint**: the invariants are mechanically locked in.

---

## Phase 6: Polish & Cross-Cutting

- [x] T028 [tier:balanced] Run the full quickstart validation (`specs/three-layer-structure/quickstart.md` Scenarios A–D; E if an ARM toolchain is present) and confirm each expected outcome.
- [x] T029 [P] [tier:fast] Confirm no file touched/created exceeds the ~500-line budget (the gate's check 1 covers this; spot-confirm the new harness + README) (FR-021, Principle VII).
- [x] T030 [P] [tier:fast] Confirm CI (`.github/workflows/ci.yml`) exercises the extended gate + host tests on the branch (no hook added — Commandment 2 / FR-018).
- [x] T031 [tier:fast] Final commit + push; branch ready for `/stack-control:execute` governance + ship.

---

## Dependencies & Execution Order

- **Phase 1 (Setup)** → **Phase 2 (Foundational, T003)** must complete before user stories.
- **US1 (P1)** is the MVP and should land first; it migrates SVF and proves the structure.
- **US2 (P2)** depends only on the taxonomy doc (T003); it is otherwise independent of US1 but sequenced after for a clean MVP-first delivery. Note T006 (US1) and T015 (US2) both edit `modulated-delay-effect.h` — keep US1 before US2 to avoid a dirty overlap.
- **US3 (P3)** should land after US1+US2 so the gate runs against the final conformant tree (it can be authored earlier but is validated last).
- **Polish (Phase 6)** last.

## Parallel Opportunities

- Within US2: T012 ‖ T013 (distinct `git mv`s); T016 ‖ T017 (distinct test files). The effect-include edits T014/T015 touch shared files — keep sequential.
- Within US3: T021/T022/T024 edit the same script — sequential; T023 likewise.
- Cross-story parallelism is intentionally avoided on `modulated-delay-effect.h` (US1 T006 vs US2 T015).

## MVP Scope

**User Story 1 alone** delivers the MVP: the three layers exist and one concept (SVF)
is migrated end-to-end as the copyable pattern. US2 completes the migration of
pre-existing code; US3 locks the invariants in mechanically.

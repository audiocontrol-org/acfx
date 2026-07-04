> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — zero hooks; gates are explicit build/test steps.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information.
> (acfx Constitution, Principles I–III.)

# Tasks: passive-tone-stacks

**Feature dir**: `specs/passive-tone-stacks/` | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

Tests are **included** — the spec mandates two validation tiers (Tier-1 primitive tests, Tier-2 AC/analytic) plus a lab harness (Constitution VIII, test the core host-side). Within each story, tests come before/with implementation.

## Format: `[ID] [P?] [Story] Description with file path`

- **[P]** = parallelizable (different files, no incomplete dependency).
- **[US1/US2/US3]** = user-story phase tasks only (Setup/Foundational/Polish carry no story label).

## Path Conventions

- Primitive (portable, C++17): `core/primitives/circuit/tone-stack/`
- Lab (host-only, C++20 ok): `core/labs/passive-tone-stacks/`
- Tests (host-side): `tests/core/`

---

## Phase 1: Setup (Shared Infrastructure)

- [x] T001 Create the primitive subfolder `core/primitives/circuit/tone-stack/` and the lab tree `core/labs/passive-tone-stacks/{solver,harness}/` (empty placeholders to be filled by later tasks).
- [x] T002 [P] Register the three new host tests (`tone-stack-taper-test.cpp`, `tone-stack-builder-test.cpp`, `tone-stack-ac-test.cpp`) in `tests/CMakeLists.txt`, and add the lab harness target `acfx_lab_passive_tone_stacks_harness` (source `core/labs/passive-tone-stacks/harness/passive-tone-stacks-harness.cpp`, C++20) in the root `CMakeLists.txt`, mirroring the `component-abstractions` lab-harness registration.
- [x] T003 [P] Write the lab boundary note `core/labs/passive-tone-stacks/README.md` (host-only, non-normative, `.ac` not MNA, isolation guarantee) — mirrors `core/labs/component-abstractions/README.md`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Blocks US1 and US2** — the pot math is the shared prerequisite both builders and the taper tests consume.

- [x] T004 Implement `core/primitives/circuit/tone-stack/taper.h`: `enum class Taper { Linear, Log }`, `struct WiperSplit { double rTop, rBottom; }`, constants `kEndResistanceOhms = 10.0` and `kLogTaperBase = 10.0`, `wiper(rTrack, pos, Taper)` (taper law → fraction `f`; `rBottom=f·rTrack`, `rTop=(1-f)·rTrack`; per-leg `max(leg, 10.0)` floor) and `rheostat(rTrack, pos, Taper)` (single floored leg). Throw `std::invalid_argument` on `pos ∉ [0,1]` or `rTrack ≤ 0`. Header-only, C++17, standard-library only, ≤ ~300 lines. Contract: `contracts/potentiometer.md`.

**Checkpoint**: `taper.h` compiles standalone; US1 and US2 can begin.

---

## Phase 3: User Story 1 - Assemble a named passive tone stack, solver-neutrally (Priority: P1) 🎯 MVP

**Goal**: `toneStackFMV` / `toneStackBaxandall` return a `prepare()`-valid `Netlist` of frozen-vocabulary components.
**Independent test**: build each stack at several control settings, `prepare()` passes, counts match the BOM, only frozen-vocabulary elements present — no solver.

### Tests for User Story 1

- [x] T005 [P] [US1] `tests/core/tone-stack-builder-test.cpp`: for FMV and Baxandall, assert `prepare()` succeeds at all-0 / all-1 / mixed / design-center controls; component and node counts equal the BOM; every held component is `Resistor`/`Capacitor`/`VoltageSource` (frozen vocabulary); and a compile-level check that `tone-stack.h`/`taper.h` include nothing under `core/labs/` (isolation, FR-016). Tests fail until T006–T009 land.

### Implementation for User Story 1

- [x] T006 [US1] In `core/primitives/circuit/tone-stack/tone-stack.h`, define the value structs `FMVValues`/`FMVControls`, `BaxandallValues`/`BaxandallControls` and the per-topology capacity constants `kFmvNodes`/`kFmvComponents`, `kBaxNodes`/`kBaxComponents` per `data-model.md`.
- [x] T007 [US1] Implement `toneStackFMV(...)` in `tone-stack.h`: wire the FMV-style topology (basic Bassman two-cap form) — input `VoltageSource`, slope `r1`, caps `c1` (treble) + `c2` (bass), treble pot as a `wiper()` divider (wiper = output), bass and mid pots as `rheostat()`s (bass in series after `c2`, mid shunting the bass/mid junction to ground), explicit `rLoad` output-to-ground — then `prepare()` and return a `ToneStack` with the input/output node ids for the AC probe. (A third/mid cap and exact vendor BOM fidelity are the later `fender-tone-stack` feature.)
- [x] T008 [US1] Implement `toneStackBaxandall(...)` in `tone-stack.h`: wire the passive James 2-band topology (bass/treble `wiper()` dividers, shelving caps, explicit `rLoad`), `prepare()`, return.
- [x] T009 [US1] Add builder input validation: any non-positive `*Values` field or any control `∉ [0,1]` → descriptive `std::invalid_argument` naming the field (FR-010).
- [x] T010 [P] [US1] Update `core/primitives/README.md` to register the `circuit/tone-stack/` subfolder and the two builders.

**Checkpoint**: US1 independently testable — MVP delivered (a solver-neutral tone-stack builder).

---

## Phase 4: User Story 2 - Drive the tone stack with pot controls via taper laws (Priority: P2)

**Goal**: the potentiometer is a faithful, independently proven control surface (taper laws, rheostat, end-resistance floor).
**Independent test**: call `wiper()` / `rheostat()` directly and check leg resistances against hand values — no circuit.
**Depends on**: Foundational T004 (`taper.h`).

### Tests for User Story 2

- [x] T011 [P] [US2] `tests/core/tone-stack-taper-test.cpp`: `pos=0.5` Linear → equal legs summing to `rTrack`; `Log` matches the reference exponential fraction at ≥2 positions; away from extremes `rTop+rBottom == rTrack`; `pos=0` and `pos=1` → each leg exactly 10 Ω, never 0; `rheostat()` returns the single floored leg; `pos ∉ [0,1]` and `rTrack ≤ 0` throw `std::invalid_argument`.

### Implementation for User Story 2

- [x] T012 [US2] Apply any `taper.h` refinements surfaced by T011 (exact `Log` fraction form, per-leg floor/sum edge behavior at extremes) while keeping `taper.h` ≤ ~300 lines and the contract in `contracts/potentiometer.md` accurate.

**Checkpoint**: the pot control surface is proven in isolation.

---

## Phase 5: User Story 3 - Read and validate the frequency response (Priority: P3)

**Goal**: `solveAC` computes exact `H(jω)`; FMV/Baxandall match the analytic (Duncan) curve within 0.1 dB.
**Independent test**: AC solver on RC/divider sanity nets to ~1e-9, then FMV/Baxandall vs analytic at ≥3 settings.
**Depends on**: US1 (builders produce the netlists) + Foundational.

### Implementation & Tests for User Story 3

- [x] T013 [US3] Implement `core/labs/passive-tone-stacks/solver/ac-solver.h`: `solveAC(netlist, ω, inNode, outNode) → std::complex<double>` — stamp admittances at `jω` (`R→1/R`, `C→jωC`, `L→1/(jωL)`), impose the ideal input source by fixed-node reduction (reuse the `LinearSolver` structure over `std::complex<double>`), complex Gaussian elimination with partial pivoting, heap-free (fixed `std::array` buffers). Singular pivot → `std::runtime_error` naming `ω` (FR-012). No MNA/Newton (FR-013). Contract: `contracts/ac-solver.md`.
- [x] T014 [P] [US3] `tests/core/tone-stack-ac-test.cpp` sanity block: RC low-pass → `−20 dB/decade`, phase → `−90°`; resistive divider → flat `R2/(R1+R2)`; matched to closed form to ~1e-9 (SC-003).
- [x] T015 [US3] **Validation reference (per the 2026-07-04 implementation-decision — see spec Clarifications).** Because `solveAC` is proven exact against RC/divider/RLC closed forms (T014, ~1e-9), the FMV/Baxandall validation reference is realized as **exact resistive-limit closed forms + monotonic musical invariants + passivity**, NOT a hand-transcribed published Duncan rational (which cannot be verified in-session; a wrong reference agreeing with a wrong builder = false confidence). The exact FMV DC limit `rLoad/(R1 + trebleBottom + rLoad)` is the closed-form anchor; the point-by-point-vs-published-rational check is captured for the exact-BOM `fender-tone-stack` feature.
- [x] T016 [US3] Extend `tone-stack-ac-test.cpp` (SC-004): FMV validated by the exact DC resistive-limit closed form (≥3 treble settings), passivity `|H(f)| ≤ 1` across the 20 Hz–20 kHz log grid, and the monotonic musical invariants — the mid scoop deepens as the mid pot lowers, HF rises with the treble pot, LF rises with the bass pot.
- [x] T017 [US3] Extend `tone-stack-ac-test.cpp` (SC-005): Baxandall validated by passivity `|H(f)| ≤ 1` across the grid and the shelf invariants — LF magnitude rises with the bass pot and HF magnitude rises with the treble pot (the bass/treble shelves move as expected).
- [x] T018 [US3] Implement `core/labs/passive-tone-stacks/harness/passive-tone-stacks-harness.cpp` mirroring T014/T016/T017 assertions with PASS/FAIL measured-vs-expected prints; exits nonzero on any failure (FR-014).

**Checkpoint**: the assembled tone stacks are validated by the proven-exact solver + exact DC limits + passivity + monotonic invariants (the operator-agreed approach).

---

## Phase 6: Polish & Cross-Cutting Concerns

- [x] T019 [P] Verify isolation (SC-006 / FR-016) at the **dependency level** (the substantive guarantee): the `tone-stack/` primitive headers and the Tier-1 test files (`tone-stack-taper-test.cpp`, `tone-stack-builder-test.cpp`) include **nothing** under `core/labs/` — grep-verified — so they are compilable independent of the lab. Only the Tier-2 `tone-stack-ac-test.cpp` (the assembled-response validation, and the lab harness) include the lab solver; those are the artifacts that "go away" when the lab is deleted. (Note per AUDIT-BARRAGE codex-02: Tier-1 and Tier-2 share one `acfx_core_tests` executable, so a literal `rm -rf` + rebuild of that target would need the Tier-2 file excluded; the isolation being asserted is the primitive's dependency-independence, not a single-target rebuild trick.)
- [x] T020 [P] No-heap audit (SC-007): confirm no `new`/`delete`/`std::vector` under `core/primitives/circuit/tone-stack/`; capacities are compile-time `Netlist` parameters.
- [x] T021 [P] Full green + hygiene: `make test` and the harness both pass; each new file ≤ ~500 lines (Constitution VII); the `component-abstractions` `circuit/` vocabulary is unmodified (FR-003).

---

## Dependencies & Story Completion Order

- **Setup (T001–T003)** → everything.
- **Foundational (T004, `taper.h`)** → blocks **US1** and **US2**.
- **US1 (T005–T010)** depends on T004. → **MVP.**
- **US2 (T011–T012)** depends on T004. Independent of US1 (pure pot math).
- **US3 (T013–T018)** depends on US1 (needs built netlists) + T004.
- **Polish (T019–T021)** depends on all prior.

Story order: **US1 (MVP) → US2 → US3.** US1 and US2 can proceed in parallel once T004 lands (different files: `tone-stack.h` vs `tone-stack-taper-test.cpp`).

## Parallel Opportunities

- Setup: T002 ∥ T003.
- After T004: US1 (T005–T009) ∥ US2 (T011). T010 ∥ US1 impl.
- Within US3: T014 ∥ T015 (sanity test vs analytic-reference encoding), then T016/T017 build on both.
- Polish: T019 ∥ T020 ∥ T021.

## Implementation Strategy

**MVP first**: Setup → Foundational → US1 yields a working, solver-neutral tone-stack builder with topology tests — deliverable and independently valuable (a solver author can already consume it). Then US2 hardens and proves the pot control surface, and US3 adds the frequency-domain validation that makes the stacks *recognizable*. Each story is a complete, independently testable increment; ship/commit at each checkpoint.

## Independent Test Criteria (per story)

- **US1**: builders return `prepare()`-valid, frozen-vocabulary netlists at all control extremes; counts match BOM; no lab include. (No solver.)
- **US2**: `wiper()`/`rheostat()` leg values match hand calculation for Linear/Log incl. the 10 Ω floor at extremes; bad input throws. (No circuit.)
- **US3**: `solveAC` matches closed form on RC/divider to ~1e-9; FMV/Baxandall match the analytic curve within 0.1 dB at ≥3 settings with correct scoop/shelf behavior.

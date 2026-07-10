---
description: "Task list — WDF-primitives"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: WDF-primitives

**Input**: Design documents in `specs/wdf-primitives/`
(plan.md, spec.md, research.md, data-model.md, contracts/wdf-one-ports.md, quickstart.md).

**Tests**: TDD is REQUESTED (acfx Principle VIII — test the core host-side; the recorded
`circuit-model-validation-approach`). Every implementation task is preceded by a failing
doctest task in the same story. Validation is **per-element** — full-circuit response and
adaptation are the sibling adaptor/network nodes (FR-023).

**Organization**: grouped by the spec's user stories. The `OnePort` port-interface concept
(`one-port.h`) is Foundational — every leaf and every generic-sweep test depends on it. `[tier:]`
hints drive model-sized dispatch in `/stack-control:execute` (fast→haiku, balanced→sonnet,
powerful→opus).

**Out of scope (do NOT implement here)**: series/parallel **adaptors** (`wdf-adaptors`); tree
assembly + reflection-free **adaptation** (`wdf-passive-networks`); **ideal (non-resistive)
source** (`b = 2E − a`) and **nonlinear root** (diode) one-ports (`wdf-complete-analog-stages`);
per-leaf variable-`dt`/`setSampleRate` (spec Open Question 2); complex/AC scalar. WDF is a
**parallel wave-domain reader** of the frozen vocabulary — it reuses the physical constants,
carries no `NodeId`, and does NOT depend on the nodal solvers (MNA/Newton/integration). No new
component types are added.

## Format: `[ID] [P?] [Story] [tier:] Description + file path`

- **[P]**: parallelizable (different file, no incomplete-task dependency).
- **[Story]**: US1..US8 from spec.md (Setup/Foundational/Polish carry no story label).
- **[tier:]** (`stack-control-model-tier-v1`): exactly one of `fast` / `balanced` / `powerful`
  on every task — mechanical/RED-test/doc-only → `fast`; standard implementation → `balanced`;
  cross-cutting/architectural/high-blast-radius → `powerful`.

---

## Phase 1: Setup (shared infrastructure)

- [ ] T001 [tier:fast] Inhabit the primitive directory `core/primitives/circuit/wdf/` (create it in this commit — "inhabit before creating"; no empty pre-creation) and add a short `core/primitives/circuit/wdf/README.md` stating: production primitive family, namespace `acfx::wdf`, wave-domain leaf one-ports under the voltage-wave convention, a parallel reader of the frozen circuit vocabulary (reuses physical constants, no `NodeId`, no nodal-solver dependency), duck-typed `OnePort` port interface, bilinear reactive discretization, no-fallback, and that adaptors / tree adaptation / nonlinear-or-ideal roots are the sibling WDF nodes (out of scope).
- [ ] T002 [P] [tier:fast] Register the host test sources (`wdf-port-interface-test.cpp`, `wdf-resistor-test.cpp`, `wdf-reactive-test.cpp`, `wdf-sources-test.cpp`, `wdf-terminations-test.cpp`, `wdf-rt-safety-test.cpp`, `wdf-passivity-test.cpp`) in `tests/CMakeLists.txt`, mirroring the existing `mna-*` / `newton-*` / `integration-*` registrations. Files may be empty stubs at this point.

---

## Phase 2: Foundational — the `OnePort` port-interface concept — BLOCKS all stories

**Goal**: the duck-typed `OnePort` port interface (`portResistance()` / `reflected()` /
`incident()` + the `isAdaptable` trait) and the voltage-wave relations, in `one-port.h`.
Contract: `contracts/wdf-one-ports.md` (I1–I3, W1).

- [ ] T003 [tier:balanced] Write FAILING doctest `tests/core/wdf-port-interface-test.cpp`: a `static_assert`-style compile check that a trivial stub type exposing `portResistance()/reflected()/incident()` (all `noexcept`) plus `isAdaptable` satisfies the `OnePort` concept/trait, and that a type missing a member does NOT; plus a runtime check of the voltage-wave inverse helpers (`v=(a+b)/2`, `i=(a−b)/(2·Rp)`). Fails until T004.
- [ ] T004 [tier:powerful] Implement `core/primitives/circuit/wdf/one-port.h` (`acfx::wdf`): the C++17 duck-typed `OnePort` concept-emulation trait (SFINAE/`std::void_t` detection of the three `noexcept` members + the `isAdaptable` static bool), the adaptable/reflective call-ordering documentation (I2), and the voltage-wave inverse helper free functions. No inheritance, no vtable. Header-only, C++17, no platform headers, ≤ ~300 lines. Make T003 pass.

---

## Phase 3: User Story 1 — Resistor one-port (Priority: P1) 🎯 MVP

**Goal**: the minimal memoryless wave-domain leaf — `Rp = R`, adapted `b = 0`.
**Independent test**: `portResistance() == R`; adapted `reflected() == 0` for any incident
history; general-reflection oracle cross-check; `R <= 0` throws.

- [ ] T005 [US1] [tier:balanced] Write FAILING doctest `tests/core/wdf-resistor-test.cpp`: `Resistor(R)` → `portResistance() == R`; `reflected() == 0` for any prior `incident(a)`; the general unadapted reflection `b = a·(R−Rp)/(R+Rp)` is reproduced by a **test-local oracle helper** (not a public method) and equals `0` at `Rp = R`; `R <= 0` throws `std::invalid_argument`; `incident(a)` is a `noexcept` no-op. Fails until T006.
- [ ] T006 [US1] [tier:balanced] Implement `Resistor` in `core/primitives/circuit/wdf/wave-elements.h` (`acfx::wdf`): `explicit Resistor(double R)` validating `R > 0` (throw); `portResistance()==R`; `reflected()==0`; `incident(double) noexcept {}`; `static constexpr bool isAdaptable = true`. Satisfies `OnePort`. Make T005 pass. Contract R1–R3.

---

## Phase 4: User Story 2 — Reactive one-ports become unit delays (Priority: P1)

**Goal**: capacitor/inductor as bilinear unit delays — the WDF-defining behavior.
**Independent test**: `Rp = T/(2C)` / `2L/T`; `b[n] = a[n−1]` / `−a[n−1]`; duality;
bilinear-impedance agreement; `C/L/dt <= 0` throws.

- [ ] T007 [US2] [tier:powerful] Write FAILING doctest `tests/core/wdf-reactive-test.cpp`: `Capacitor(C, dt)` → `portResistance() == T/(2C)`, and driving an incident sequence gives `reflected()` this sample equal to the previous `incident` (`b[n] = a[n−1]`, `b[0] = 0`); `Inductor(L, dt)` → `portResistance() == 2L/T`, `b[n] = −a[n−1]`; **duality** (Rp + reflected-sign swapped under one `dt`); **bilinear port-impedance agreement** vs. the bilinear-discretized analog impedance at test frequencies; `C <= 0` / `L <= 0` / `dt <= 0` throws `std::invalid_argument`. Fails until T008.
- [ ] T008 [US2] [tier:powerful] Implement `Capacitor` and `Inductor` in `core/primitives/circuit/wdf/wave-elements.h`: `Capacitor(double C, double dt)` computes `Rp = T/(2C)` once in the ctor (validate `C>0, dt>0`, throw), `state_` init `0`, `reflected()==state_`, `incident(a){state_=a}`; `Inductor(double L, double dt)` computes `Rp = 2L/T`, `reflected()==-state_`, `incident(a){state_=a}`; both `isAdaptable = true`, reactive. Make T007 pass. Contract C1–C2, L1–L2, G4.

---

## Phase 5: User Story 3 — Resistive source one-ports (Priority: P1)

**Goal**: the signal-input injection — resistive voltage source (`b = E`) and current source
(`b = R·I`), drive value updatable per sample.
**Independent test**: `Rp = R`; `reflected() == E` / `R·I`; `setVoltage`/`setCurrent` track;
`R <= 0` throws.

- [ ] T009 [US3] [tier:balanced] Write FAILING doctest `tests/core/wdf-sources-test.cpp`: `ResistiveVoltageSource(R, E)` → `portResistance()==R`, `reflected()==E`, and after `setVoltage(E')` → `reflected()==E'`; `ResistiveCurrentSource(R, I)` → `portResistance()==R`, `reflected()==R*I`, `setCurrent` tracks; `R <= 0` throws `std::invalid_argument`; `incident(a)` is a `noexcept` no-op. Fails until T010.
- [ ] T010 [US3] [tier:balanced] Implement `ResistiveVoltageSource` and `ResistiveCurrentSource` in `core/primitives/circuit/wdf/wave-sources.h` (`acfx::wdf`): ctors validate `R>0` (throw); `portResistance()==R`; `reflected()` returns the current drive (`E` / `R·I`); `setVoltage(E) noexcept` / `setCurrent(I) noexcept`; `incident(double) noexcept {}`; `isAdaptable = true`. Make T009 pass. Contract VS1–VS2, CS1.

---

## Phase 6: User Story 4 — Terminations: resistive / short / open (Priority: P1)

**Goal**: matched resistive load (`b = 0`) plus the two **reflective** (non-adaptable)
terminations — short (`b = −a`) and open (`b = +a`).
**Independent test**: resistive `b = 0`; short `b = −a`; open `b = +a`; short/open report
non-adaptable; `Rp/R <= 0` throws.

- [ ] T011 [US4] [tier:balanced] Write FAILING doctest `tests/core/wdf-terminations-test.cpp`: `ResistiveTermination(R)` → `reflected()==0` (matched), `isAdaptable==true`; `ShortCircuit(Rp)` → after `incident(a)`, `reflected()==-a`, `isAdaptable==false`; `OpenCircuit(Rp)` → `reflected()==+a`, `isAdaptable==false`; the short/open reflection is Rp-independent (same `∓a` for two different `Rp`); `R/Rp <= 0` throws `std::invalid_argument`. Fails until T012.
- [ ] T012 [US4] [tier:balanced] Implement `ResistiveTermination`, `ShortCircuit`, `OpenCircuit` in `core/primitives/circuit/wdf/wave-terminations.h` (`acfx::wdf`): `ResistiveTermination(double R)` (matched load, `reflected()==0`, `isAdaptable=true`, may reuse `Resistor`); `ShortCircuit(double Rp)` / `OpenCircuit(double Rp)` store the last `incident` and return `∓a` / `±a` (`isAdaptable=false`, Rp carried only for junction conversion, R6); all validate their parameter `> 0` (throw). Make T011 pass. Contract RT1, SH1–SH2, OP1.

---

## Phase 7: User Story 5 — The one-port port interface generic sweep (Priority: P1)

**Goal**: prove every leaf satisfies the uniform `OnePort` interface, driven generically with
no virtual dispatch — the seam `wdf-adaptors` will consume.
**Independent test**: a templated up/down-sweep over a heterogeneous leaf set invokes the
interface on each; `isAdaptable` sequences `reflected()` vs. `incident()` correctly.

- [ ] T013 [US5] [tier:balanced] Extend `tests/core/wdf-port-interface-test.cpp`: a single **templated** up-sweep/down-sweep driver over a heterogeneous tuple of leaves (`Resistor`, `Capacitor`, `Inductor`, both sources, all three terminations) that calls `portResistance()`/`reflected()`/`incident()` on each with **no** virtual dispatch (assert via `static_assert`/type traits that no vtable is involved); assert the driver honors the `isAdaptable` timing — adaptable leaves are read (`reflected`) before being written (`incident`); reflective leaves (`ShortCircuit`/`OpenCircuit`) are written first, then read. Depends on T004, T006, T008, T010, T012. Validation only — no new production code. Contract I1–I3.

---

## Phase 8: User Story 6 — Construction lifecycle + real-time-safe wave path (Priority: P1)

**Goal**: `(param, dt)` construction computes `Rp` once; the wave path is zero-heap, lock-free.
**Independent test**: `AllocationSentinel` zero heap across many samples; no per-leaf re-prepare
API exists.

- [ ] T014 [US6] [tier:balanced] Write doctest `tests/core/wdf-rt-safety-test.cpp`: wrap a many-sample `reflected()`/`incident()` loop over each leaf in `AllocationSentinel::reset()` and assert `allocations()==0 && deallocations()==0`; assert a reactive leaf's `portResistance()` is readable immediately after construction (computed once, not lazily); assert (by API surface / compile check) there is **no** per-leaf `prepare()`/`setSampleRate` method in v1 (research R4). Depends on T006, T008, T010, T012. Contract G1.

---

## Phase 9: User Story 7 — Passivity & physical invariants (Priority: P2)

**Goal**: validate passivity with the **two correct criteria** — memoryless instantaneous vs.
reactive wave-power balance.
**Independent test**: memoryless `|b| ≤ |a|` + `Rp > 0`; reactive `Σ(a²−b²) ≥ 0` (NOT
same-sample `|b| ≤ |a|`).

- [ ] T015 [US7] [tier:powerful] Write doctest `tests/core/wdf-passivity-test.cpp`: for the **memoryless** passive leaves (adapted `Resistor`, `ResistiveTermination`), assert `|b| ≤ |a|` over a range of incident waves and `portResistance() > 0`; for the **reactive** leaves (`Capacitor`, `Inductor`), drive an incident-wave sequence and assert the accumulated wave-power balance `Σ(a[k]² − b[k]²) ≥ 0` at every prefix, equal to the stored `a[N]²` for the lossless capacitor; **explicitly assert same-sample `|b| ≤ |a|` is NOT required** for a reactive leaf (drive `a[n−1]=1, a[n]=0` → `b[n]=1 > |a[n]|=0` for a correct capacitor — the case that would break a naive criterion). Depends on T006, T008, T012. Contract G3 (corrected per spec review).

---

## Phase 10: User Story 8 — No-fallback parameter validation (Priority: P2)

**Goal**: non-physical parameters surfaced, never clamped; reflections never clamped to fake
passivity.
**Independent test**: bad parameter → descriptive throw at construction; no reflection clamp.

- [ ] T016 [US8] [tier:balanced] Extend `tests/core/wdf-rt-safety-test.cpp` (a no-fallback section): every leaf constructed with a non-physical parameter (`R`/`C`/`L`/`dt`/`Rp ≤ 0`) throws a descriptive `std::invalid_argument` at construction (off the hot path), NOT a substituted/clamped value; assert no leaf clamps its reflection to force `|b| ≤ |a|` when a parameter is out of range (drive an intentionally large incident and confirm the reflection is the exact scattering value, unclamped). Depends on T006, T008, T010, T012. Contract G2.

---

## Phase 11: Polish & cross-cutting

- [ ] T017 [P] [tier:fast] Run `scripts/check-portability.sh` and confirm: C++17, header-only, no platform headers, and every `core/primitives/circuit/wdf/` file within the ~300–500 line budget (SC-007). Split a header (elements / sources / terminations) if any exceeds budget.
- [ ] T018 [tier:balanced] Cross-check `quickstart.md` scenarios against the implemented `wdf*` suites; confirm each spec Success Criterion (SC-001…008) maps to a passing assertion; update `core/primitives/circuit/wdf/README.md` if the header split changed. Confirm all `wdf*` doctest suites pass.

---

## Dependencies & completion order

- **Setup (T001–T002)** → **Foundational (T003–T004: `one-port.h`)** blocks everything.
- **Leaves** (US1 T005–T006 → the MVP; US2 T007–T008; US3 T009–T010; US4 T011–T012) each depend
  only on Foundational and are otherwise **independent** (different files/types) — parallelizable
  across stories once `one-port.h` exists.
- **Cross-cutting validations** depend on the leaves they exercise: US5 (T013) on all leaves;
  US6 (T014), US8 (T016) on all leaves; US7 (T015) on resistor + reactive + terminations.
- **Polish (T017–T018)** last.

## Parallel opportunities

- T002 ∥ T001 (different files).
- Once `one-port.h` (T004) lands, the four leaf stories run in parallel: the RED tests
  (T005/T007/T009/T011) are all `[P]`-eligible (distinct test files), and the implementations
  (T006/T008/T010/T012) touch distinct headers (`wave-elements.h` for US1+US2 is shared, so
  T006 and T008 serialize on that file; US3/US4 are separate files and parallel to it).
- The cross-cutting validation tests (T013/T014/T015/T016) can be authored in parallel after the
  leaves exist (distinct test files / sections).

## Implementation strategy

- **MVP = User Story 1** (T001–T006): the `OnePort` concept + the `Resistor` leaf — a single
  wave-domain one-port producing correct scattering against an exact closed form. Delivers the
  wave convention + the port interface, the foundation every other leaf and the adaptor node
  build on.
- **Incremental**: add the reactive leaves (US2 — the WDF-defining unit delay), then sources
  (US3 — signal input), then terminations (US4), then the cross-cutting validations (US5–US8).
  Each story is an independently testable increment.
- **Per-element only**: no adaptors/tree/roots — those are the sibling nodes. Full-circuit
  response is validated there, not here.

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: WDF-adaptors

**Input**: Design documents in `specs/wdf-adaptors/`
(plan.md, spec.md, research.md, data-model.md, contracts/wdf-adaptors.md, quickstart.md;
design record `docs/superpowers/specs/2026-07-10-wdf-adaptors-design.md`).

**Tests**: TDD is REQUESTED (acfx Principle VIII — test the core host-side; the recorded
`circuit-model-validation-approach`). Every implementation task is preceded by a failing
doctest task in the same story. Validation is **root-driver-free** — single-sample scattering
and invariants only; full network transfer-function response is the sibling
`wdf-passive-networks` node (design D11).

**Organization**: grouped by the spec's user stories. The shared `adaptor-detail.h` machinery
(variadic sweep + conformance `static_assert`s + construction validation) is Foundational —
both adaptors depend on it. `[tier:]` hints drive model-sized dispatch in
`/stack-control:execute` (fast→haiku, balanced→sonnet, powerful→opus).

**Out of scope (do NOT implement here)**: the single-sample **root driver**, whole-tree
**topology / root-port selection**, **R-type / rigid** adaptors, named **passive networks**
(`wdf-passive-networks`); **ideal-source** (`b = 2E − a`) and **nonlinear** (`b = f(a)`)
**roots** (`wdf-complete-analog-stages`); **time-varying `Rp`** re-adaptation propagation
(captured with the leaf `setSampleRate` question); full network **transfer-function** tests
(root-driver owner). WDF is a parallel path — no `NodeId`, no nodal-solver dependency; the
shipped `OnePort` concept and wave convention are consumed unchanged.

## Format: `[ID] [P?] [Story] [tier:] Description + file path`

- **[P]**: parallelizable (different file, no incomplete-task dependency).
- **[Story]**: US1..US7 from spec.md (Setup/Foundational/Polish carry no story label).
- **[tier:]** (`stack-control-model-tier-v1`): exactly one of `fast` / `balanced` / `powerful`
  on every task — mechanical/RED-test/doc-only → `fast`; standard implementation/validation →
  `balanced`; cross-cutting/architectural/scattering-correctness → `powerful`.

---

## Phase 1: Setup (shared infrastructure)

- [x] T001 [P] [tier:fast] Register the new host test sources (`wdf-series-adaptor-test.cpp`, `wdf-parallel-adaptor-test.cpp`, `wdf-adaptor-tree-test.cpp`, `wdf-adaptor-reflection-free-test.cpp`, `wdf-adaptor-passivity-test.cpp`, `wdf-adaptor-rt-safety-test.cpp`, `wdf-adaptor-child-access-test.cpp`) in `tests/CMakeLists.txt`, mirroring the existing `wdf-*` / `mna-*` / `newton-*` registrations. Files may be empty stubs at this point. (The `core/primitives/circuit/wdf/` directory already exists — shipped by `wdf-primitives` — so no directory-inhabitation task; new headers are added into it.)

---

## Phase 2: Foundational — shared adaptor machinery (`adaptor-detail.h`) — BLOCKS all stories

**Goal**: the variadic sweep machinery, conformance guards, and construction validation both
adaptors compose.
**Note**: `adaptor-detail.h` is internal machinery with no behavior independent of an adaptor,
so it carries no standalone RED test; its correctness is exercised transitively by the US1/US2
scattering tests and directly by the RT-safety / no-fallback test (T012). Its compile-time
guards are demonstrated by T007 (US3).

- [x] T002 [tier:powerful] Implement `core/primitives/circuit/wdf/adaptor-detail.h` (`acfx::wdf`): (a) the three conformance `static_assert` helpers used by both adaptors — arity `sizeof...(Child) >= 1`, `(is_one_port_v<Child> && ...)`, `(Child::isAdaptable && ...)` (the delay-free-loop guard); (b) `std::index_sequence` fold helpers to gather each child's `portResistance()`, gather each child's `reflected()` into an in-object cache, and fan `incident()` back to the children; (c) construction-time validation that throws `std::invalid_argument` (naming child index + value) when a child `portResistance()` is non-finite or `<= 0` — never clamps. Header-only, C++17, no platform headers, no heap/vtable, ≤ ~300 lines. Consumes the shipped `one-port.h` unchanged. Contract C2, C3, C6.

---

## Phase 3: User Story 1 — Series adaptor as a composable one-port (Priority: P1) 🎯 MVP

**Goal**: `SeriesAdaptor<Child...>` — an N-port series junction that is itself a `OnePort`,
`R_up = Σ R_child`, exact series scattering.
**Independent test**: `portResistance() == Σ R_k`; down-sweep child incidents equal
`b_k = a_k − (2R_k/R)Σa_i`; recovered voltages match the Ohm's-law series divider to `≤ 1e-12`.

- [x] T003 [US1] [tier:balanced] Write FAILING doctest `tests/core/wdf-series-adaptor-test.cpp`: `SeriesAdaptor(Resistor(Ra), Resistor(Rb))` → `is_one_port_v` true, `portResistance() == Ra + Rb`; for a chosen upward incident `a_u`, run `reflected()` then `incident(a_u)` and assert each child's delivered incident equals `b_k = a_k − (2R_k/R)Σ_i a_i`; recover child voltages via `waveToVoltage`/`waveToCurrent` and assert they match the exact series voltage divider to `≤ 1e-12` relative. Fails until T004.
- [x] T004 [US1] [tier:powerful] Implement `SeriesAdaptor<Child...>` in `core/primitives/circuit/wdf/series-adaptor.h` (`acfx::wdf`): variadic children held by value in a `std::tuple`; ctor reads/validates child `portResistance()` via `adaptor-detail.h`, precomputes `R_up = Σ R_k` and the multiply-folded coefficients `2R_k/R`; `portResistance()==R_up`; `reflected()` caches child reflected waves and returns `b_u = −Σ_child a_child`; `incident(a_u)` delivers `b_k = a_k − (2R_k/R)·(a_u + Σ_child a_child)` to each child; `static constexpr bool isAdaptable = true`; typed `child<I>()` (+`const`). Satisfies `OnePort`; per-sample path `noexcept`, heap-free, O(N). Make T003 pass. Contract C1, C4 (series), C5.

---

## Phase 4: User Story 2 — Parallel adaptor as a composable one-port (Priority: P1)

**Goal**: `ParallelAdaptor<Child...>` — the dual: N-port parallel junction, `G_up = Σ G_child`,
exact parallel scattering.
**Independent test**: `1/portResistance() == Σ G_k`; child incidents equal
`b_k = 2(Σ G_i a_i)/G − a_k`; recovered quantities match the current divider to `≤ 1e-12`.

- [x] T005 [P] [US2] [tier:balanced] Write FAILING doctest `tests/core/wdf-parallel-adaptor-test.cpp`: `ParallelAdaptor(Resistor(Ra), Resistor(Rb))` → `is_one_port_v` true, `1/portResistance() == 1/Ra + 1/Rb`; assert child incidents equal `b_k = 2(Σ_i G_i a_i)/G − a_k` and recovered quantities match the exact current divider to `≤ 1e-12` relative. Fails until T006.
- [x] T006 [US2] [tier:powerful] Implement `ParallelAdaptor<Child...>` in `core/primitives/circuit/wdf/parallel-adaptor.h` (`acfx::wdf`): variadic by-value children; ctor computes `G_k = 1/R_k`, `G_up = Σ G_k` (validate via `adaptor-detail.h`), precomputes the coefficients; `portResistance()==1/G_up`; `reflected()` caches child waves and returns `b_u = (Σ_child G_child·a_child)/G_up`; `incident(a_u)` delivers the parallel scattering `b_k = 2(Σ_i G_i a_i)/G − a_k` to each child; `isAdaptable = true`; typed `child<I>()`. Satisfies `OnePort`; per-sample path `noexcept`, heap-free, O(N). Make T005 pass. Contract C1, C4 (parallel), C5.

---

## Phase 5: User Story 3 — Nest adaptors into a filter tree (Priority: P1)

**Goal**: adaptors nest recursively (adaptor-as-child) via the `OnePort` seam; N-port; single-
child pass-through; non-adaptable child rejected at compile time.
**Independent test**: `SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>>` is a
`OnePort`, `portResistance()` equals the recursive combination, and one up/down sweep matches
the by-hand nested scattering to `≤ 1e-12`. Validation only — no new production code.

- [x] T007 [US3] [tier:balanced] Write doctest `tests/core/wdf-adaptor-tree-test.cpp`: build `SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>>`; `static_assert(is_one_port_v<...>)`; assert `portResistance()` equals the recursive series/parallel combination of the subtree; run one up-sweep + down-sweep and assert every node's produced waves equal the by-hand nested scattering at a sampled instant to `≤ 1e-12`. Assert single-child pass-through: `SeriesAdaptor(Resistor(R))` and `ParallelAdaptor(Resistor(R))` → `portResistance() == R` and transparent wave behavior. Include a **commented** negative-compilation example (`SeriesAdaptor<Resistor, ShortCircuit>`) with a note that uncommenting it must fail to compile (the `adaptor-detail.h` `isAdaptable` `static_assert`, SC-007). Depends on T004, T006. Contract C1, C2, C4.

---

## Phase 6: User Story 4 — The adapted upward port is reflection-free (Priority: P2)

**Goal**: prove `reflected()` (`b_u`) is independent of this sample's upward incident `a_u`.
**Independent test**: with child state fixed, `reflected()` is invariant under `a_u` to
`< 1e-15`; the closed forms `b_u = −Σ a_child` (series) / `(Σ G_child a_child)/G_up` (parallel).

- [x] T008 [P] [US4] [tier:balanced] Write doctest `tests/core/wdf-adaptor-reflection-free-test.cpp`: for a series and a parallel adaptor with fixed child state, read `reflected()` across a range of upward incident values (delivered on separate samples) and assert the up-sweep `reflected()` is invariant to `< 1e-15` absolute; assert the adapted-port closed forms `b_u = −Σ_child a_child` (series) and `b_u = (Σ_child G_child a_child)/G_up` (parallel). Depends on T004, T006. Validation only. Contract C4 (adapted upward port).

---

## Phase 7: User Story 5 — Real-time safety & construction-time validation (Priority: P2)

**Goal**: zero heap on the sweep; non-physical inputs throw at construction; nothing clamped.
**Independent test**: `AllocationSentinel` zero heap across many samples; bad child `Rp` and
empty child set rejected; no clamped coefficients.

- [x] T009 [P] [US5] [tier:balanced] Write doctest `tests/core/wdf-adaptor-rt-safety-test.cpp`: wrap a many-sample `reflected()`/`incident()` loop over a nested adaptor tree in `AllocationSentinel::reset()` and assert `allocations()==0 && deallocations()==0` and that no exception escapes the wave path; assert constructing an adaptor with a non-positive or non-finite child `portResistance()` throws a descriptive `std::invalid_argument` at construction naming the child, with **no** clamped/fabricated coefficient; note the empty-child-set case is a compile-time `static_assert` (arity ≥ 1, research R7), demonstrated by a commented negative-compilation line. Depends on T002, T004, T006. Reuses `tests/support/allocation-sentinel.h`. Validation only. Contract C3, C6.

---

## Phase 8: User Story 6 — Reach nested elements via a typed accessor (Priority: P2)

**Goal**: `child<I>()` reaches owned children; mutating a nested source is observed by a sweep.
**Independent test**: via `child<I>()`, read a nested source's port resistance and mutate its
drive; a subsequent sweep reflects the change.

- [x] T010 [US6] [tier:balanced] Write FAILING doctest `tests/core/wdf-adaptor-child-access-test.cpp`: build a tree containing a nested `ResistiveVoltageSource`; via `child<I>()` (compile-time index) obtain a reference of the child's exact static type, read its `portResistance()`, then change its drive (`setVoltage`) and assert a subsequent up-sweep/down-sweep reflects the changed value. Fails until T011.
- [x] T011 [US6] [tier:balanced] Ensure the compile-time typed accessor `template <std::size_t I> auto& child() noexcept` (+ `const` overload) returning `std::get<I>(children_)` is present on `SeriesAdaptor` and `ParallelAdaptor` (in `series-adaptor.h` / `parallel-adaptor.h`, or factored into `adaptor-detail.h`). No runtime indirection. Make T010 pass. Contract C1 (`child<I>`).

---

## Phase 9: User Story 7 — Junctions are lossless (validated passivity) (Priority: P3)

**Goal**: validate the **conductance-weighted** pseudo-power balance (not the unweighted one).
**Independent test**: `|Σ G_k a_k² − Σ G_k b_k²| / Σ G_k a_k² < 1e-12` over randomized inputs.

- [x] T012 [P] [US7] [tier:powerful] Write doctest `tests/core/wdf-adaptor-passivity-test.cpp`: for randomized admissible child resistances and incident-wave vectors, compute all port waves for a series and a parallel adaptor and assert the conductance-weighted balance `|Σ_k G_k a_k² − Σ_k G_k b_k²| / (Σ_k G_k a_k²) < 1e-12` (`G_k = 1/R_k`, over all ports incl. upward); **explicitly assert the unweighted `Σ a_k² = Σ b_k²` is NOT relied on** (construct an unequal-resistance case where it fails while the weighted balance holds). Depends on T004, T006. Validation only. Contract C7.

---

## Phase 10: Polish & cross-cutting

- [x] T013 [P] [tier:fast] Run `scripts/check-portability.sh` and confirm: C++17, header-only, no platform headers, and every `core/primitives/circuit/wdf/` adaptor file (`adaptor-detail.h`, `series-adaptor.h`, `parallel-adaptor.h`) within the ~300–500 line budget (spec FR-015). Split a header if any exceeds budget.
- [x] T014 [tier:balanced] Update `core/primitives/circuit/wdf/README.md` to note the adaptors (series/parallel scattering junctions as recursive adapted one-ports; adaptable-children-only with the reflective port at the root; root driver / adaptation / R-type / nonlinear roots are sibling nodes). Cross-check `quickstart.md` scenarios against the implemented `wdf*adaptor*` suites; confirm each Success Criterion SC-001…SC-007 maps to a passing assertion; confirm all `wdf*adaptor*` doctest suites pass.

---

## Dependencies & completion order

- **Foundational (T002, `adaptor-detail.h`) BLOCKS everything** — both adaptors compose it.
- **Series (US1: T003→T004)** is the 🎯 MVP: the first composable adaptor one-port. **Parallel
  (US2: T005→T006)** is independent of series (`[P]` at the RED stage) but shares
  `adaptor-detail.h`.
- **US3 (tree, T007)**, **US4 (reflection-free, T008)**, **US7 (passivity, T012)** are
  validation-only and depend on both adaptors (T004, T006).
- **US5 (RT-safety, T009)** depends on T002/T004/T006. **US6 (child access, T010→T011)** depends
  on the adaptors existing.
- **Polish (T013, T014)** last.

## Parallel opportunities

- T001 (`[P]`) runs alongside early authoring.
- After T002, the two adaptor RED tests are independent files: T003 and T005 (`[P]`) can be
  written in parallel; their implementations T004 and T006 touch different headers and can
  proceed in parallel once their RED tests exist.
- The validation-only tests T008, T009, T012 (`[P]`) are independent files and can be authored
  in parallel once T004+T006 land.

## Implementation strategy

- **MVP = Foundational + User Story 1** (T001, T002, T003, T004): `adaptor-detail.h` + the
  `SeriesAdaptor` — the first proof that an adaptor is a composable, reflection-free `OnePort`
  with exact scattering. Parallel adaptor (US2) is the immediate second slice; the remaining
  stories are validation of composability, the adapted-port property, RT-safety, child access,
  and passivity. No task here crosses into the deferred sibling scope (root driver / whole-tree
  adaptation / R-type / nonlinear roots).

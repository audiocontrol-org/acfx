---

description: "Task list for acfx Program Scaffolding"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III.)

# Tasks: acfx Program Scaffolding

**Input**: Design documents from `specs/program-scaffolding/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: This is a **governance-only** feature (no source code). There are no unit/TDD
tests; each user story ends with a **verification** task that inspects the governed artifact
(file/grep/`stackctl roadmap` output) per quickstart.md. All mutations dry-run before apply
(idempotence, FR-012).

**Organization**: Grouped by the three user stories (all P1, but ordered: prospectus →
constitution → roadmap, since the constitution and roadmap reference the prospectus).

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (Setup/Polish carry no story label)

## Path Conventions

Governed artifacts only: `docs/superpowers/specs/` (prospectus), `.specify/memory/constitution.md`
+ `.specify/templates/` (constitution), the stack-control `ROADMAP.md` (roadmap). No `core/`,
`adapters/`, or `tests/` changes.

---

## Phase 1: Setup

**Purpose**: Confirm the working branch and the front-door node for this spec.

- [X] T001 Confirm work is on the `program-scaffolding` branch and that this spec has a roadmap node (capture-fusion / FR-014); if absent, create `design:feature/program-scaffolding --spec specs/program-scaffolding` (dry-run → `--apply`).

---

## Phase 2: Foundational

**Purpose**: None blocking — the three stories are independent artifacts. (No foundational
code; intentionally empty so stories can proceed.)

---

## Phase 3: User Story 1 — Prospectus persisted & discoverable (Priority: P1) 🎯 MVP

**Goal**: The program vision exists as a durable, referenced document.

**Independent Test**: The prospectus file exists at its canonical path with the content
intact, and is referenced from the constitution and the roadmap program node.

- [X] T002 [US1] Create `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` containing the project prospectus verbatim (the "Progressive Audio DSP & Analog Modeling Platform" prospectus text).
- [X] T003 [US1] Verify (quickstart US1): the file exists and holds the prospectus content; commit + push the prospectus.

(Its inbound references from the constitution and roadmap are added in US2 T006 and US3 T013.)

**Checkpoint**: Prospectus persisted — the program has a north-star doc.

---

## Phase 4: User Story 2 — Constitution Principles IX–XI (Priority: P1)

**Goal**: The program's operating principles are encoded as governed constitution principles.

**Independent Test**: `.specify/memory/constitution.md` carries Principles IX, X, XI with the
agreed content, version `1.3.0`, and dependent templates in sync.

- [ ] T004 [US2] Drive the `speckit-constitution` mechanism to add Principle IX (Progressive Layered Architecture), X (Measurable Engineering), XI (One Concept at a Time) in the guidance tier after VIII, with the content from data-model.md, in `.specify/memory/constitution.md`.
- [ ] T005 [US2] Bump the constitution version `1.2.0 → 1.3.0` (MINOR) and propagate to the dependent templates in `.specify/templates/` per the constitution's governance policy (no dangling references).
- [ ] T006 [US2] Add a reference to the persisted prospectus in the constitution governance section (north-star pointer, supports FR-002).
- [ ] T007 [US2] Verify (quickstart US2): Principles IX–XI present with agreed content; version `1.3.0`; templates consistent. Commit + push the constitution change.

**Checkpoint**: Constitution governs the new principles.

---

## Phase 5: User Story 3 — Roadmap DAG for the nine phases (Priority: P1)

**Goal**: The whole program is a navigable, sequenced roadmap graph.

**Independent Test**: `stackctl roadmap graph` shows the program node, 9 phase clusters with
headline deliverables and `part-of`/`depends-on` edges, shipped work under Phase 1, and the
three-layer-structure item; `stackctl roadmap next` surfaces measurement-infrastructure.

All roadmap mutations: **dry-run first, then `--apply`** (FR-012). Use the ids in data-model.md
(`contracts/roadmap-nodes.md`).

- [ ] T008 [US3] Add the program node `multi:feature/progressive-dsp-platform` with a ref to the prospectus (dry-run → apply); record the prospectus pointer (FR-005, supports FR-002).
- [ ] T009 [US3] Add the nine phase cluster nodes (`multi:feature/phase-*` per data-model.md) and an edge `part-of` the program for each (FR-006).
- [ ] T010 [US3] Add `depends-on` edges sequencing phase N+1 → phase N for phases 1→9 (FR-008).
- [ ] T011 [P] [US3] Add each phase's headline-deliverable items (`design:<kind>/<slug>` per data-model.md) `part-of` their phase (FR-007); include the Phase-1 `measurement-infrastructure` deliverable.
- [ ] T012 [US3] Add the cross-cutting `three-layer-structure` item and sequence it before Phase-2 implementation work (FR-010).
- [ ] T013 [US3] Map shipped/closed work under Phase 1: edge `impl:feature/modulated-delay`, `design:feature/svf-vertical-slice`, `design:feature/workbench-audio-config` `part-of` the Phase-1 cluster, preserving their existing status (FR-009).
- [ ] T014 [US3] Verify (quickstart US3): `stackctl roadmap graph` shows program + 9 phases + deliverables + edges + shipped-work-under-Phase-1 + three-layer item; `stackctl roadmap next` returns measurement-infrastructure (FR-011). Commit + push the roadmap change.

**Checkpoint**: The program is a governed, queryable DAG with a clear next-ready item.

---

## Phase 6: Polish & Cross-Cutting

- [ ] T015 [P] Idempotence check (FR-012): re-run a representative `roadmap add`/`add-edge` and confirm no duplicate nodes/edges and no `ROADMAP.md` corruption.
- [ ] T016 [P] Governance-only scope check (FR-013, SC-007): `git diff --name-only origin/main...HEAD` touches only `docs/`, `.specify/`, `ROADMAP.md`, and `specs/program-scaffolding/` — no `core/`/`adapters/`/`tests/`, no `labs/`, no primitive reorg.
- [ ] T017 [P] Walk quickstart.md end to end; confirm every Success Criterion (SC-001..007) is satisfied by a governed artifact.

---

## Dependencies & Execution Order

- **Setup (T001)** → first.
- **US1 (T002–T003)** → first story; the prospectus is referenced by US2/US3, so persist it first.
- **US2 (T004–T007)** → after US1 (T006 references the prospectus). Independent of US3.
- **US3 (T008–T014)** → after US1 (T008 references the prospectus). Independent of US2.
  Internal order: program node → phases → edges → deliverables (T011 `[P]`) → three-layer →
  shipped-mapping → verify.
- **Polish (T015–T017)** → after the stories; all `[P]`.

US2 and US3 are independent of each other (different governed files) and may be done in either
order after US1.

## Parallel Execution Examples

- T011 (deliverables) is `[P]` across phases (distinct items).
- Polish T015/T016/T017 run in parallel.
- US2 (constitution) and US3 (roadmap) can proceed concurrently once US1 is committed.

## Implementation Strategy

- **MVP = User Story 1** (the prospectus persisted) — the smallest standalone increment.
- **Incremental**: US1 → US2 → US3 → Polish; commit + push at each story boundary
  (Commandment 1). Every roadmap/constitution mutation dry-runs before applying.
- **Governance, not code**: each story's "test" is artifact verification (quickstart), not
  `make test`.

## Task Summary

- **Total**: 17 tasks
- **Setup**: 1 (T001) · **US1**: 2 (T002–T003) · **US2**: 4 (T004–T007) · **US3**: 7 (T008–T014) · **Polish**: 3 (T015–T017)
- **Parallel**: T011 (deliverables), T015–T017 (polish), and US2 ∥ US3 after US1.
- **MVP scope**: Phase 1 + US1.

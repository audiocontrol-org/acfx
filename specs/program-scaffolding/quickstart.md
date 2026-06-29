# Quickstart & Validation: acfx Program Scaffolding

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

A verification guide for the governance scaffolding (no audio/DSP build). Each block maps to
a User Story / Success Criterion in [spec.md](./spec.md).

## US1 — Prospectus persisted & discoverable (SC-001)
```sh
test -f docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md && echo "prospectus present"
grep -l "progressive-dsp" .specify/memory/constitution.md       # constitution references it
```
Expected: the prospectus file exists with the prospectus content; the constitution governance
section and the roadmap program node both reference it.

## US2 — Constitution carries Principles IX–XI (SC-002)
```sh
grep -nE "^### (IX|X|XI)\." .specify/memory/constitution.md      # the three new principles
grep -n "Version.*1.3.0" .specify/memory/constitution.md         # bumped version
```
Expected: Principles IX (Progressive Layered Architecture), X (Measurable Engineering), XI
(One Concept at a Time) present with the agreed content; version `1.3.0`; dependent templates
in `.specify/templates/` consistent (no dangling references).

## US3 — Roadmap DAG (SC-003/004/005)
```sh
stackctl roadmap graph        # program node + 9 phase clusters + headline deliverables + edges
stackctl roadmap next         # next-ready frontier
```
Expected:
- A program node (`multi:feature/progressive-dsp-platform`) referencing the prospectus.
- Nine phase clusters (Digital Fundamentals → Reference Hardware Models), each `part-of` the
  program; phase N+1 `depends-on` phase N.
- Each phase's headline deliverables `part-of` it.
- The shipped SVF filter / modulated-delay / parameter work appears under Phase 1 at
  shipped/closed status.
- A `three-layer-structure` item present, sequenced before Phase-2 implementation.
- `roadmap next` surfaces the **Phase-1 measurement-infrastructure** deliverable.

## Idempotence (SC-006)
```sh
stackctl roadmap add multi:feature/phase-nonlinear-dsp   # dry-run: "would add" or already-exists, no dup
```
Expected: re-running a build step does not create duplicates or corrupt `ROADMAP.md`.

## Governance-only (SC-007)
```sh
git diff --name-only origin/main...HEAD     # only docs/, .specify/, ROADMAP.md, specs/program-scaffolding/
```
Expected: no `core/`, `adapters/`, or `tests/` changes; no `labs/` dir; no primitive reorg.

## Notes
This feature is verified by inspecting governed artifacts, not by `make test` (no code
changes). The next session specs the first sub-project (`measurement-infrastructure`) via
`/stack-control:define`, which resolves it from `roadmap next`.

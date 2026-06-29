# Quickstart & Validation: acfx Program Scaffolding

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

A verification guide for the governance scaffolding (no audio/DSP build). Each block maps to
a User Story / Success Criterion in [spec.md](./spec.md). Concrete node ids below are the
**actual built ids** (reconciled against the committed `ROADMAP.md`).

## US1 — Prospectus persisted & discoverable (SC-001)
```sh
test -f docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md && echo "prospectus present"
grep -q "progressive-dsp" .specify/memory/constitution.md && echo "constitution references it"
# the roadmap program node must also reference the prospectus (the second half of SC-001):
grep -q "progressive-dsp-prospectus" ROADMAP.md && echo "roadmap references it"
```
Expected: the prospectus file exists with the prospectus content; AND both the constitution
governance section and the roadmap program node reference it (all three echoes print).

## US2 — Constitution carries Principles IX–XI (SC-002)
```sh
grep -nE "^### (IX|X|XI)\." .specify/memory/constitution.md      # the three new principles
grep -n "Version.*1.3.0" .specify/memory/constitution.md         # bumped version
# template sync: the templates carry no hardcoded constitution version and do not
# enumerate principles IV+ (they use the [CONSTITUTION_VERSION] placeholder), so there is
# nothing to drift. Confirm no template pins an old version:
! grep -rqE "Version.*1\.[0-2]\.[0-9]" .specify/templates/ && echo "no stale version pinned in templates"
```
Expected: Principles IX, X, XI present with the agreed content; version `1.3.0`; templates
consistent — they reference the version via a placeholder and do not enumerate these
principles, so no template edit is needed (sync is satisfied vacuously).

## US3 — Roadmap DAG (SC-003/004/005)
```sh
stackctl roadmap next         # next-ready frontier
stackctl roadmap blocked      # confirms Phases 2-9 are gated
stackctl roadmap graph        # full structure (program + phases + deliverables + edges)
```
Expected:
- A program node `multi:feature/progressive-dsp-platform` referencing the prospectus.
- Nine phase clusters (`multi:feature/phase-digital-fundamentals` … `…/phase-reference-hardware`),
  each `part-of` the program; phase N+1 `depends-on` phase N.
- Each phase's headline deliverables `part-of` it; Phases 2-9 deliverables also `depends-on`
  the prior phase, so they are blocked (visible in `roadmap blocked`).
- The already-shipped work appears under Phase 1 at shipped/closed status:
  `design:feature/svf-vertical-slice` (SVF filter + parameter system),
  `impl:feature/modulated-delay` (delay + modulation),
  `design:feature/workbench-audio-config` (workbench audio config).
- `design:gap/three-layer-structure` present, and Phase 2 (`phase-nonlinear-dsp`)
  `depends-on` BOTH it and `design:feature/measurement-infrastructure`.
- `roadmap next` lists exactly the Phase-1 frontier — including
  `design:feature/measurement-infrastructure` (the recommended first sub-project) AND
  `design:gap/three-layer-structure` (both are legitimately unblocked); no Phase-2…9
  deliverable appears.

## Idempotence (SC-006)
```sh
before=$(grep -c '^- id:' ROADMAP.md 2>/dev/null || stackctl roadmap graph | grep -c '\["')
stackctl roadmap add multi:feature/phase-nonlinear-dsp --apply 2>&1   # must REFUSE (identifier not unique)
after=$(grep -c '^- id:' ROADMAP.md 2>/dev/null || stackctl roadmap graph | grep -c '\["')
[ "$before" = "$after" ] && echo "idempotent: node count unchanged ($before)"
```
Expected: re-adding an already-built node **fails loud** with an identifier-uniqueness
violation (it does NOT silently duplicate, and it does NOT report "would add" — "would add"
would mean the node was never built, a build failure). Node count is unchanged; `ROADMAP.md`
is uncorrupted.

## Governance-only (SC-007)
```sh
git diff --name-only origin/main...HEAD     # expect only governance paths
```
Expected: changed paths fall **only** under `docs/`, `.specify/`, `ROADMAP.md`, `CLAUDE.md`
(the SPECKIT agent-context marker), and `specs/program-scaffolding/`. No `core/`,
`adapters/`, `tests/`, or `labs/` changes; no primitive reorg.

## Notes
This feature is verified by inspecting governed artifacts, not by `make test` (no code
changes). The next session specs the first sub-project (`design:feature/measurement-infrastructure`)
via `/stack-control:define`, which resolves it from `stackctl roadmap next`.

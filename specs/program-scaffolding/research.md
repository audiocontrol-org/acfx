# Phase 0 Research: acfx Program Scaffolding

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

This is a governance-setup feature; the decisions were settled with the operator during
brainstorming (see the approved design). Recorded here for traceability.

## Decision 1 — Scope: governance only
**Decision**: This session persists the prospectus, adds constitution Principles IX–XI, and
builds the roadmap DAG. It does NOT create the physical `labs/` layer, reorganize
`core/primitives/`, or implement any phase's code.
**Rationale**: A spine before building; the physical reorg touches includes/build and is
safer as its own reviewed sub-project (captured as a roadmap item).
**Alternatives**: full reorg now (rejected — churn, unrelated to giving the program a spine).

## Decision 2 — Principles are guidance tier
**Decision**: IX–XI join the guidance tier (like IV–VIII), not the NON-NEGOTIABLE tier (I–III).
**Rationale**: Operator decision; they shape how phases are built but should not block already
shipped work or carry the commandment weight of I–III.
**Alternatives**: NON-NEGOTIABLE tier (not chosen).

## Decision 3 — Constitution mechanism + version bump
**Decision**: Apply the change via the `speckit-constitution` mechanism; bump the version
**1.2.0 → 1.3.0** (MINOR: new principles added) and propagate to dependent templates per the
constitution's governance policy.
**Rationale**: The governed, sync-keeping path; matches the constitution's own amendment rule.
**Alternatives**: hand-edit the file (rejected — risks template drift, skips the versioning policy).

## Decision 4 — Prospectus location
**Decision**: `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` (verbatim),
referenced from the constitution governance section and the roadmap program node.
**Rationale**: Matches the existing dated design-doc convention; discoverable from governance.
**Alternatives**: `docs/PROSPECTUS.md` (offered; operator accepted the specs/ location).

## Decision 5 — Roadmap modeling within the id grammar
**Decision**: Model the program and its nine phases within the roadmap's
`<phase>:<kind>/<slug>` identifier grammar (phase ∈ {design,plan,impl,multi}; kind ∈
{feature,primitive,fix,gap}). A program/phase cluster is a `multi:feature/<slug>` node; an
as-yet-unspecced deliverable is `design:<kind>/<slug>` with `planned` status; shipped work
keeps its existing node/status. Phases connect to the program with `part-of`; deliverables
connect to their phase with `part-of`; phase N+1 `depends-on` phase N. Exact ids are chosen
at build time via `stackctl roadmap add/cluster/add-edge` with **dry-run before apply**.
**Rationale**: The id `phase` field is a lifecycle stage, not a prospectus phase, so prospectus
phases must be nodes/edges. `multi:` fits a cluster that spans many lifecycle stages.
**Alternatives**: encode phases only as a doc list (rejected — not queryable/sequenced); a flat
list with no edges (rejected — loses the progressive dependency the curriculum requires).

## Decision 6 — Map shipped work under Phase 1
**Decision**: Record the existing SVF filter, modulated-delay, and parameter system under
Phase 1 with their real shipped/closed status (do not reset to planned). The existing
`impl:feature/modulated-delay (shipped)` and the two closed design nodes are linked
`part-of` Phase 1.
**Rationale**: The roadmap should show real progress, not a blank slate; avoids re-planning done work.
**Alternatives**: leave existing nodes unlinked (rejected — Phase 1 would look empty/unstarted).

## Decision 7 — Idempotent application
**Decision**: Every roadmap/constitution mutation dry-runs first, then applies; re-running must
not duplicate nodes or corrupt governed files.
**Rationale**: FR-012; the roadmap `add` is dry-run by default, matching this.

## Summary

| # | Topic | Decision |
|---|-------|----------|
| 1 | Scope | governance only; physical reorg deferred to a roadmap item |
| 2 | Principle tier | guidance (IX–XI), like IV–VIII |
| 3 | Constitution | via speckit-constitution; 1.2.0 → 1.3.0 (MINOR); templates synced |
| 4 | Prospectus | docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md |
| 5 | Roadmap modeling | nodes/edges within `<phase>:<kind>/<slug>`; cluster=`multi:feature`, unspecced=`design:*`; dry-run→apply |
| 6 | Shipped work | mapped under Phase 1 at real status |
| 7 | Application | idempotent dry-run→apply |

No `NEEDS CLARIFICATION` remain. Ready for Phase 1.

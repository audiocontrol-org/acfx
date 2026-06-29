> ‼ **acfx COMMANDMENTS** ‼ Commit & push often · No git hooks · Descriptive names.
> (Constitution I–III — `.specify/memory/constitution.md`.)

# Governance-Completeness Checklist: acfx Program Scaffolding

**Purpose**: Unit-test the *requirements* (spec.md) for the governance scaffolding — are the
three deliverables (prospectus, constitution IX–XI, roadmap DAG) specified completely,
clearly, consistently, and verifiably? Interrogates the requirements, not the implementation.
**Created**: 2026-06-29
**Feature**: [spec.md](../spec.md)

## Prospectus Persistence

- [x] CHK001 Is the prospectus's canonical path specified exactly (not "somewhere in docs")? [Clarity, Spec §FR-001]
- [x] CHK002 Is "verbatim" specified as the persistence fidelity (vs paraphrased/summarized)? [Clarity, Spec §FR-001]
- [x] CHK003 Are the inbound references required of BOTH the constitution and the roadmap program node? [Completeness, Spec §FR-002]
- [x] CHK004 Is it specified that the prospectus is a vision doc, NOT run through the Spec Kit chain? [Consistency, Spec Assumptions]

## Constitution Principles IX–XI

- [x] CHK005 Is the content of each new principle (IX, X, XI) specified, not just its title? [Completeness, Spec §FR-003]
- [x] CHK006 Is the tier of the new principles (guidance vs NON-NEGOTIABLE) unambiguous? [Clarity, Spec §FR-003, Assumptions]
- [x] CHK007 Is the exact version transition specified (1.2.0 → 1.3.0) with the bump rationale (MINOR)? [Measurability, Plan/Research]
- [x] CHK008 Is "templates kept in sync" specified as a concrete, checkable obligation (no dangling references)? [Clarity, Spec §FR-004]
- [x] CHK009 Are the three principles' measurable acceptance criteria distinct and non-overlapping? [Consistency, Spec §SC-002]

## Roadmap DAG

- [x] CHK010 Is the program node's identity and its pointer to the prospectus specified? [Completeness, Spec §FR-005]
- [x] CHK011 Are all nine phases enumerated by name (not "the phases")? [Completeness, Spec §FR-006]
- [x] CHK012 Is the `part-of` containment (deliverable→phase, phase→program) specified for every level? [Coverage, Spec §FR-006/007]
- [x] CHK013 Is the `depends-on` sequencing rule (phase N+1 → phase N) stated unambiguously? [Clarity, Spec §FR-008]
- [x] CHK014 Is "headline deliverables only" specified as the granularity (vs full task breakdown)? [Clarity, Spec Assumptions]
- [x] CHK015 Is the required status of mapped shipped work specified (shipped/closed, not reset to planned)? [Consistency, Spec §FR-009]
- [x] CHK016 Is the three-layer-structure item's sequencing (before Phase-2 code) specified, not just its existence? [Completeness, Spec §FR-010]
- [x] CHK017 Is the next-ready expectation pinned to a specific item (measurement-infrastructure)? [Measurability, Spec §FR-011]
- [x] CHK018 Is conformance to the `<phase>:<kind>/<slug>` identifier grammar stated as a constraint? [Clarity, Spec Edge Cases]

## Idempotence & Scope

- [x] CHK019 Is idempotence specified as dry-run→apply with a no-duplicate / no-corruption guarantee? [Measurability, Spec §FR-012]
- [x] CHK020 Is the governance-only boundary specified as a checkable negative (no code, no labs/, no reorg, no shipped-effect edits)? [Clarity, Spec §FR-013]
- [x] CHK021 Is the no-orphan-spec requirement (this spec has a roadmap node) stated? [Completeness, Spec §FR-014]

## Acceptance & Verifiability

- [x] CHK022 Can every Success Criterion (SC-001..007) be verified from a governed artifact (file/grep/graph), not opinion? [Measurability, Spec §SC]
- [x] CHK023 Do the User Story "Independent Test" steps each map to a concrete inspectable artifact? [Coverage, Spec US1–US3]
- [x] CHK024 Are there any requirements whose acceptance is stated only qualitatively ("discoverable", "navigable") without a concrete check? [Ambiguity, Spec US1/US3]

## Notes

- A failing item means the *requirement text* needs work, not the code.
- Companion to `requirements.md` (general spec quality). This file is governance-specific.
- ≥80% of items carry a `[Spec §…]` or quality-marker traceability reference.

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: acfx Program Scaffolding — governed spine for the Progressive Audio DSP program

**Feature Branch**: `program-scaffolding`

**Created**: 2026-06-29

**Status**: Draft

**Input**: Approved design `docs/superpowers/specs/2026-06-29-acfx-program-scaffolding-design.md` — governance setup for the multi-phase "Progressive Audio DSP & Analog Modeling Platform" program. Governance only; no phase code, no physical `labs/` layer or primitive reorg this session.

## User Scenarios & Testing *(mandatory)*

The "user" is an **acfx contributor/maintainer**. This feature gives the multi-phase
program a governed spine — a persisted vision, encoded principles, and a navigable phased
roadmap — so that subsequent sub-projects can be specced and built in order without
re-deriving the plan each time. It deliberately ships **no** phase implementation.

### User Story 1 - The program vision is persisted and discoverable (Priority: P1)

A contributor opening the repo can find the program's north-star vision: what acfx is
becoming (a progressive DSP curriculum + reusable primitives + production effects), its
guiding principles, the three-layer architecture, and the nine-phase roadmap — captured in
one durable document, linked from the governance surfaces.

**Why this priority**: Without a persisted vision the program lives only in conversation;
every later decision would re-litigate intent. This is the smallest standalone increment of
value.

**Independent Test**: Open the repo; locate the prospectus document at its canonical path;
confirm it captures the prospectus verbatim and is referenced from the constitution and the
roadmap.

**Acceptance Scenarios**:

1. **Given** the repo, **When** a contributor looks for the program vision, **Then** the prospectus exists at `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` with the prospectus content intact.
2. **Given** the constitution and the roadmap, **When** a contributor reads them, **Then** each references the prospectus as the program's north star.

---

### User Story 2 - The program's principles are encoded in the constitution (Priority: P1)

A contributor (or an agent) reading the constitution sees the program's new operating
principles as first-class governance: the layered Theory→Lab→Primitive→Production
progression, measurable engineering, and one-concept-at-a-time — alongside the existing
principles, with the document's dependent templates kept consistent.

**Why this priority**: The principles bind how every later phase is built; encoding them in
the governed constitution (not just a doc) is what makes them enforceable guidance.

**Independent Test**: Read `.specify/memory/constitution.md`; confirm Principles IX, X, XI
are present with the agreed content, the version is bumped, and the dependent templates
referenced by the constitution remain in sync.

**Acceptance Scenarios**:

1. **Given** the constitution, **When** a contributor reads it, **Then** Principle IX (Progressive Layered Architecture), Principle X (Measurable Engineering), and Principle XI (One Concept at a Time) appear with the agreed wording, in the guidance tier alongside IV–VIII.
2. **Given** the constitution change, **When** its version and sync are checked, **Then** the version is bumped and any dependent templates the constitution governs are consistent (no dangling references).

---

### User Story 3 - The nine phases are a navigable, governed roadmap (Priority: P1)

A contributor (or an agent running the next session) can ask the roadmap "what's next" and
see the whole program as a dependency graph: a program node, the nine phases as clusters
with their headline deliverables, sequencing edges, the already-shipped work recorded under
Phase 1, the deferred three-layer-structure item, and a clear next-ready first sub-project.

**Why this priority**: This is what lets the program be executed incrementally and governs
ordering; it turns the prospectus's prose phases into queryable, sequenced work.

**Independent Test**: Render the roadmap graph; confirm the program node, nine phase
clusters with headline deliverables and `part-of`/`depends-on` edges, the shipped work under
Phase 1, and the three-layer-structure item are all present; query next-ready and confirm it
surfaces the Phase-1 measurement-infrastructure sub-project.

**Acceptance Scenarios**:

1. **Given** the roadmap, **When** rendered as a graph, **Then** it shows a program node pointing at the prospectus and nine phase clusters (Digital Fundamentals → Reference Hardware Models) each `part-of` the program.
2. **Given** the phases, **When** their edges are inspected, **Then** each phase's headline deliverables are `part-of` that phase and each phase N+1 `depends-on` phase N.
3. **Given** the already-shipped SVF filter, modulated-delay, and parameter system, **When** the roadmap is inspected, **Then** they are recorded under Phase 1 with a done/shipped status.
4. **Given** the deferred physical reorg, **When** the roadmap is inspected, **Then** a cross-cutting "establish the three-layer structure + primitive taxonomy" item exists, sequenced before Phase-2 code.
5. **Given** the roadmap frontier, **When** next-ready is queried, **Then** the Phase-1 measurement-infrastructure sub-project is surfaced as the recommended next item.

---

### Edge Cases

- **Idempotence**: Re-running any scaffolding step (re-adding a roadmap node, re-persisting the prospectus) must not create duplicates or corrupt the governed files — mutating steps dry-run first, then apply.
- **No orphan spec**: This scaffolding spec itself must have a roadmap node (the front-door capture-fusion backstop), so governance verbs can resolve it.
- **Already-shipped work**: Mapping existing effects under Phase 1 must reflect their real (shipped/closed) status, not reset them to planned.
- **Grammar limits**: Roadmap identifiers must satisfy the `<phase>:<kind>/<slug>` grammar; phase/deliverable nodes are chosen to fit it (the exact ids are an implementation detail).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST persist the project prospectus verbatim as a program-vision document at `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`.
- **FR-002**: The constitution and the roadmap program node MUST reference the persisted prospectus so it is discoverable as the program's north star.
- **FR-003**: The constitution MUST gain Principle IX (Progressive Layered Architecture), Principle X (Measurable Engineering), and Principle XI (One Concept at a Time) with the agreed content, in the guidance tier alongside Principles IV–VIII.
- **FR-004**: The constitution change MUST bump the constitution version and keep the dependent templates the constitution governs in sync (no dangling/contradictory references).
- **FR-005**: The roadmap MUST contain a program node representing the Progressive Audio DSP program, pointing at the prospectus.
- **FR-006**: The roadmap MUST contain one cluster node per prospectus phase (1 Digital Fundamentals, 2 Nonlinear DSP, 3 Dynamic Systems, 4 Circuit Modeling, 5 Numerical Circuit Solvers, 6 Wave Digital Filters, 7 Physical Modeling, 8 Convolution, 9 Reference Hardware Models), each `part-of` the program node.
- **FR-007**: Each phase's headline deliverables (from the prospectus) MUST be recorded as items `part-of` their phase.
- **FR-008**: The roadmap MUST sequence the phases with `depends-on` edges such that phase N+1 depends on phase N.
- **FR-009**: The already-shipped SVF filter, modulated-delay, and parameter system MUST be recorded under Phase 1 with a status reflecting that they are done.
- **FR-010**: The roadmap MUST contain a cross-cutting item "establish the three-layer structure + primitive taxonomy", sequenced before Phase-2 implementation work.
- **FR-011**: The roadmap's next-ready frontier MUST surface the Phase-1 measurement-infrastructure sub-project as the recommended next item.
- **FR-012**: All mutating governance steps MUST be idempotent / non-duplicating (dry-run before apply); re-running them MUST NOT create duplicate nodes or corrupt governed files.
- **FR-013**: This feature MUST NOT implement any phase's effect, primitive, or laboratory code, MUST NOT create the physical `labs/` layer or reorganize `core/primitives/`, and MUST NOT modify already-shipped effect code.
- **FR-014**: This scaffolding spec MUST have its own roadmap node (no orphan spec), per the front-door backstop.

### Key Entities *(include if feature involves data)*

- **Prospectus document**: The persisted program-vision text; the north star referenced by governance surfaces.
- **Constitution principles IX–XI**: The new governed operating principles for the program.
- **Roadmap program node**: The top-level node for the Progressive Audio DSP program.
- **Phase cluster (×9)**: One governed node per prospectus phase, holding that phase's headline deliverables.
- **Headline deliverable**: A governed item within a phase (e.g., "oversampling" under Phase 2).
- **Three-layer-structure item**: The deferred cross-cutting reorg work, recorded for early sequencing.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A contributor can locate the program vision in one place — the prospectus exists at its canonical path and is linked from both the constitution and the roadmap.
- **SC-002**: The constitution contains exactly three new principles (IX, X, XI) with the agreed content, a bumped version, and in-sync dependent templates.
- **SC-003**: The roadmap graph renders a program node plus all nine phase clusters, every phase carrying its headline deliverables and connected by `part-of` (to the program) and `depends-on` (phase to prior phase) edges.
- **SC-004**: The three already-shipped pieces of work appear under Phase 1 with a done/shipped status (program progress is visible, not a blank slate).
- **SC-005**: Querying the roadmap's next-ready frontier returns the Phase-1 measurement-infrastructure sub-project.
- **SC-006**: Re-running the scaffolding steps produces no duplicate nodes and no governed-file corruption (idempotence).
- **SC-007**: No phase code, no `labs/` layer, and no primitive reorg are introduced by this feature (governance-only is verifiable from the diff).

## Assumptions

- **Governance vehicles**: The constitution is updated via the project's constitution mechanism (keeps templates in sync); the roadmap is built via the project's roadmap tooling (dry-run → apply). These are the sanctioned governed surfaces.
- **Roadmap identifier grammar**: Phase and deliverable nodes are named to satisfy the roadmap's `<phase>:<kind>/<slug>` identifier grammar; the specific identifiers are an implementation detail of the plan.
- **Guidance tier**: Principles IX–XI are guidance (like IV–VIII), not the NON-NEGOTIABLE tier (I–III), per the operator's decision.
- **Prospectus is a vision doc**: It is persisted as documentation, not run through the Spec Kit authoring chain.
- **Existing work status**: The SVF filter, modulated-delay, and parameter system are already shipped; the roadmap reflects that rather than re-planning them.
- **Phases 2–9 are placeholders for now**: Each phase cluster records headline deliverables only; finer task breakdowns are authored when each sub-project is specced.

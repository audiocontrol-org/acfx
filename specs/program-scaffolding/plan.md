> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: acfx Program Scaffolding

**Branch**: `program-scaffolding` | **Date**: 2026-06-29 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/program-scaffolding/spec.md` (derived from the
approved design `docs/superpowers/specs/2026-06-29-acfx-program-scaffolding-design.md`).

## Summary

Establish the governed spine for the multi-phase Progressive Audio DSP program: persist the
prospectus as the program-vision document, add constitution Principles IX–XI (guidance
tier) via the project's constitution mechanism, and build the stack-control roadmap into a
DAG of the nine phases (program node → phase clusters → headline deliverables, with
`part-of`/`depends-on` edges), recording shipped work under Phase 1 and a deferred
three-layer-structure item, leaving the Phase-1 measurement-infrastructure sub-project as the
next-ready frontier. **Governance only** — no phase code, no `labs/` layer, no primitive reorg.

## Technical Context

**Language/Version**: N/A — this feature changes governed documents and the roadmap, not
source code. (The DSP core is untouched.)

**Primary Dependencies**: the project's existing governance tooling — `stackctl roadmap`
(node/edge/cluster mutation, graph render, next-ready), the `speckit-constitution` mechanism
(constitution edit + template sync + version bump), and git for committing docs.

**Storage**: governed working files in the repo — `.specify/memory/constitution.md`,
the stack-control `ROADMAP.md`, and `docs/superpowers/specs/` for the prospectus.

**Testing**: verification of governed artifacts (not unit tests): prospectus present +
referenced; constitution carries IX–XI + bumped version + in-sync templates; `stackctl
roadmap graph` shows the program/phase/deliverable nodes + edges; `roadmap` next-ready
surfaces the measurement-infrastructure item. See quickstart.md.

**Target Platform**: the repository's governance surfaces (constitution, roadmap, docs).

**Project Type**: governance / program-setup change (single repo).

**Performance Goals**: N/A.

**Constraints**: idempotent mutations (dry-run → apply; no duplicate nodes); roadmap
identifiers must satisfy the `<phase>:<kind>/<slug>` grammar; no phase code / no `labs/` /
no primitive reorg / no changes to shipped effects (FR-013); small atomic commits, pushed.

**Scale/Scope**: 1 prospectus doc, 3 constitution principles, ~1 program node + 9 phase
clusters + their headline deliverables + 1 three-layer-structure item + edges.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I. Commit & push early/often | Each artifact (prospectus, constitution, roadmap) committed + pushed in small increments | ✅ planned |
| II. No git hooks | None added; governance steps are explicit commands | ✅ by design |
| III. Descriptive names | Spec dir `program-scaffolding`; roadmap slugs descriptive (no numeric prefixes; datestamp only on the prospectus filename) | ✅ |
| IV. Platform-independent core | No core/ change at all | ✅ N/A |
| V. No fallbacks/mock outside tests | Idempotent dry-run→apply; mutations fail loud, no silent fallback | ✅ |
| VI. Real-time safety | No audio-path code touched | ✅ N/A |
| VII. Strict typing & small modules | No source code; docs only | ✅ N/A |
| VIII. Test the core host-side | No core change; verification is of governed artifacts (quickstart) | ✅ N/A |
| IX–XI (being ADDED) | This feature introduces them; it must encode them faithfully and is the first thing they govern | ✅ self-consistent |

**Result**: PASS — governance-only change; no violations. Complexity Tracking empty.

## Project Structure

### Documentation (this feature)

```text
specs/program-scaffolding/
├── plan.md              # This file
├── research.md          # Phase 0 — decisions (mostly settled in brainstorming)
├── data-model.md        # Phase 1 — the governed entities (nodes/principles) as data
├── quickstart.md        # Phase 1 — how to verify the scaffolding
├── contracts/
│   └── roadmap-nodes.md # Phase 1 — the roadmap identifier/edge shapes future sub-projects consume
├── checklists/
│   └── requirements.md  # spec-quality checklist (passing)
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Repository artifacts touched (no source code)

```text
docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md   # NEW (prospectus)
.specify/memory/constitution.md                                        # +Principles IX–XI, version bump
ROADMAP.md (stack-control governed)                                    # program node + 9 phases + deliverables + edges
```

**Structure Decision**: no code structure changes. The only repository mutations are the
prospectus doc, the constitution, and the governed roadmap — exactly the governance spine.

## Complexity Tracking

> No Constitution Check violations — intentionally empty.

## Phases (artifacts generated by this command)

- **Phase 0 — Research** (`research.md`): record the already-settled decisions (governance-only
  scope; guidance-tier principles; headline-deliverable roadmap granularity; prospectus
  location; roadmap-id grammar approach; mapping shipped work).
- **Phase 1 — Design & Contracts** (`data-model.md`, `contracts/roadmap-nodes.md`,
  `quickstart.md`): the governed entities as data, the roadmap node/edge contract future
  sub-projects consume, and the verification guide.
- **Phase 2 — Tasks** (`tasks.md`): produced by `/speckit-tasks` — the ordered governance steps
  (persist prospectus → constitution IX–XI → roadmap DAG → verify), each dry-run→apply.

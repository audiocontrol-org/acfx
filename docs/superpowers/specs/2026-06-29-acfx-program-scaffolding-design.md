# acfx Program Scaffolding — Design

**Date:** 2026-06-29
**Status:** Approved (brainstorming) — ready for writing-plans
**Input:** "Project Prospectus: acfx — A Progressive Audio DSP & Analog Modeling Platform"

## Context

acfx today is a portable DSP core (one source, many targets) with two shipped effects
(SVF filter; modulated delay = delay + modulation + tape wow & flutter), a parameter
system, real-time-safety + host-test infrastructure, desktop/Daisy/Teensy adapters, a
reusable multi-effect target factory, and stack-control governance (a roadmap DAG, a
constitution with Principles I–VIII).

The prospectus expands acfx into a **progressive curriculum + reusable library + production
effects** spanning 9 phases and 3 architectural layers (Labs → Primitives → Effects). That
is a **program**, not a single feature. This document designs the **governance scaffolding**
that gives the program a spine; it does NOT build any phase's code.

Scope decisions confirmed during brainstorming:
- **Governance only** this session (no physical `labs/` layer or primitive-taxonomy reorg
  yet — those become explicit early roadmap items).
- Adopt **three** new constitution principles (below).
- Identify (not build) the first concrete sub-project.

## Decomposition

The program decomposes into sub-projects, each running the normal spec → plan → implement
cycle, sequenced on the roadmap. This scaffolding produces the program structure; subsequent
sessions spec/build sub-projects one at a time, phase by phase.

## Deliverable 1 — Persist the prospectus

Save the prospectus verbatim as the program-vision document at:

`docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`

(matches the existing dated design-doc convention). Reference it from the constitution
(governance section) and from the roadmap program node so it is discoverable as the north
star. It is a vision document, not a feature spec — it is not run through Spec Kit.

## Deliverable 2 — Constitution Principles IX, X, XI

Add three principles in the same style/tier as existing IV–VIII (guidance, not the
NON-NEGOTIABLE I–III tier). Apply via the `speckit-constitution` skill so dependent
templates stay in sync; bump the constitution version (a MINOR bump — additive principles).

- **IX. Progressive Layered Architecture.** `core/` is organized as `labs/ → primitives/ →
  effects/`. Every concept graduates **Theory → Laboratory → Reusable Primitive → Production
  Effect**. Educational code is not disposable: laboratory implementations evolve into
  tested, reusable primitives; effects are composed from primitives and document which
  primitives they use.
- **X. Measurable Engineering.** Every effect is validated by objective measurements —
  frequency response, impulse response, phase response, harmonic distortion (THD), latency,
  CPU usage, memory allocation, and numerical stability. Listening tests complement but never
  replace measurement.
- **XI. One Concept at a Time.** Each phase (and each laboratory) introduces a single major
  new idea and applies it to a complete effect before moving on. No advanced algorithm is
  presented as an unexplained black box; every step is explainable and reproducible.

These principles inform — but do not retroactively block — already-shipped work.

## Deliverable 3 — Roadmap DAG for the 9 phases

Encode the program into the stack-control roadmap as a governed DAG.

- **Program node** — a top-level node representing the progressive-DSP program, pointing at
  the persisted prospectus.
- **Phase clusters** — one node per prospectus phase (1–9), each `part-of` the program node:
  1. Digital Fundamentals · 2. Nonlinear DSP · 3. Dynamic Systems · 4. Circuit Modeling ·
  5. Numerical Circuit Solvers · 6. Wave Digital Filters · 7. Physical Modeling ·
  8. Convolution · 9. Reference Hardware Models.
- **Headline deliverables** — each phase's prospectus deliverables as child items `part-of`
  their phase (e.g. Phase 2: waveshapers, saturation, oversampling, harmonic analysis).
  Granularity: headline deliverables only — finer tasks are authored when each sub-project is
  specced.
- **Dependency edges** — Phase N+1 `depends-on` Phase N (the curriculum is progressive).
  Cross-phase primitive dependencies can be added as discovered.
- **Existing work mapped under Phase 1** — the shipped SVF filter, modulated-delay, and
  parameter system are recorded under Phase 1 "Digital Fundamentals" (status reflecting that
  they are done), so the roadmap shows real progress, not a blank slate.
- **Cross-cutting early item** — "Establish the three-layer structure + primitive taxonomy"
  (the deferred physical reorg: create `labs/`, reorganize `core/primitives/` into
  `filters/ nonlinear/ dynamics/ analog/ circuit/ convolution/ wdf/ physical/`, migrate
  existing primitives, fix includes/build), sequenced before Phase-2 code lands.

Roadmap id grammar is `<lifecycle-phase>:<kind>/<slug>` (lifecycle-phase ∈
{design,plan,impl,multi}; kind ∈ {feature,primitive,fix,gap}). Prospectus phases are work
clusters, modeled as nodes with `part-of`/`depends-on` edges and `planned` status; unspecced
future deliverables use the `design:` lifecycle prefix until specced. Exact ids/edges are an
implementation detail of the plan, built via `stackctl roadmap add/cluster/add-edge` with
dry-runs first.

## Deliverable 4 — Recommended first sub-project (identified only)

**Measurement infrastructure** (Phase 1's open deliverable). Rationale: it is the concrete
enabler of Principle X — every later phase depends on objective measurement to validate
effects — and it is self-contained and host-testable (frequency/impulse/phase response, THD,
latency, CPU, allocation, stability harnesses building on the existing allocation sentinel +
reference-vector pattern). The "three-layer structure + primitive taxonomy" reorg is the
natural second sub-project. Neither is built in this scaffolding session.

## Out of scope (this session)

- Any phase's effect/primitive/lab implementation.
- The physical `labs/` layer and primitive-taxonomy reorg (captured as a roadmap item).
- Changes to already-shipped effects.

## Acceptance

- Prospectus persisted and referenced.
- Constitution carries Principles IX–XI (templates in sync; version bumped).
- Roadmap shows the program node, 9 phase clusters with headline deliverables and
  `depends-on`/`part-of` edges, existing shipped work mapped under Phase 1, and the
  three-layer-structure item — verifiable via `stackctl roadmap graph`.
- The recommended first sub-project is recorded on the roadmap as the next-ready item.

## Implementation approach

Hand to `writing-plans` to produce a task-ordered plan covering: (1) persist the prospectus;
(2) constitution update via `speckit-constitution`; (3) roadmap construction via `stackctl
roadmap` (dry-run → apply); (4) verification (`stackctl roadmap graph`, next-ready check).
Each mutating step dry-runs first; doc/governance changes are committed in small increments
and pushed.

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Implicit-integration primitive

**Branch**: `implicit-integration` | **Date**: 2026-07-08 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/implicit-integration/spec.md`
(design record: `docs/superpowers/specs/2026-07-08-implicit-integration-design.md`)

## Summary

Build the implicit-integration engine as a **production primitive** under
`core/primitives/circuit/integration/`, namespace `acfx::integration`, in one
header-only, template-sized type `ReactiveIntegrator<Rule, MaxNodes, MaxComponents,
MaxBranches>`. It is the trio's **one stateful sibling**: it owns per-reactive-element
history `{vPrev, iPrev}`, the timestep `dt`, and the warm-start node voltages. Each
sample it discretizes every `Capacitor`/`Inductor` into a Norton `Companion{Geq, Ieq}`
per a **template-selected integration rule** — backward-Euler (default, L-stable) or
trapezoidal (2nd-order, A-stable) — exposes those fixed companions as a
`CompanionSupply`, drives the timestep by **composing** the shipped siblings
(`NewtonSolver` for a nonlinear netlist, `MnaAssembler`/`MnaSystem` directly for a
linear one), and advances history **once** on convergence via the rule-agnostic
contract `vPrev := v^n`, `iPrev := Geq·v^n − Ieq` (using that step's stamped companion).
Assembly is two-phase: a throw-permitted `plan()` (delegating topology to
`MnaAssembler::plan`, recording reactive indices, sizing history) run once per netlist,
and a throw-free, alloc-free `step()` on the hot path — which computes companions once
per sample (dissolving TASK-13 by construction). Validation is by exact closed-forms
(RC/RL step under both rules), a convergence-order distinguishing test (≈1 BE, ≈2 trap),
DC-steady-state / passivity invariants, a lab-equivalence oracle (rule = BE), and a
zero-heap `AllocationSentinel` assertion.

## Technical Context

**Language/Version**: C++17 (matches the DSP core and the MNA/Newton siblings; no C++20
constructs, unlike the host-only labs).

**Primary Dependencies**: the shipped MNA primitive (`core/primitives/circuit/mna/`:
`MnaSystem`, `MnaAssembler`) and the shipped Newton primitive
(`core/primitives/circuit/newton/`: `NewtonSolver`, `NewtonStatus`, its
`ComposedCompanionSupply` `base` slot), plus the frozen vocabulary in
`core/primitives/circuit/` (`Netlist`, `node.h`, `components.h` `isReactive`,
`models/capacitor.h`/`inductor.h` for the BE `companion()` physics, `models/companion.h`
for `Companion`). No platform/hardware headers (Principle IV).

**Storage**: N/A (in-memory fixed-size `std::array`; no persistence).

**Testing**: doctest under `tests/core/`, with `tests/support/allocation-sentinel.h` for
the no-heap invariant. Exact closed forms via in-test analytic discrete responses
(RC/RL step, LC); convergence order via timestep-refinement regression; equivalence
oracle against the existing lab backward-Euler solvers (`LinearSolver` /
`TransientClipper` / `OpAmpClipperSolver`).

**Target Platform**: platform-independent core — DAW plugins, desktop, Daisy, Teensy.
Header-only so it compiles into any target.

**Project Type**: reusable DSP primitive (library headers), not an application.

**Performance Goals**: per `step()` computes O(reactive-count) companions then drives one
composed solve (a linear MNA solve, or up to `maxIterations` MNA solves under Newton),
each an O(Dim³) dense elimination with zero heap allocation (Dim fixed at compile time).
Sized for analog guitar/audio stages (tens of nodes, a handful of reactive elements),
called per sample.

**Constraints**: no heap allocation or locks or unbounded work on the `step()` path
(Principle VI); no fallbacks / no mock data outside tests (Principle V — non-convergence
surfaced by value via the propagated `NewtonStatus`; the rule is never silently switched);
files within ~300–500 lines (Principle VII); strict typing, no `any`/unchecked casts,
composition over inheritance (Principle VII).

**Scale/Scope**: one primitive header (plus a small internal reactive-companion-supply
helper if it keeps files within budget) + a test suite. No new component types. Lab
migration (TASK-14) is out of scope for this feature.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Commit & push early/often | ✅ | Atomic commits per artifact; pushed promptly. No AI attribution. |
| II. No git hooks | ✅ | None added or depended on. |
| III. Descriptive names | ✅ | `specs/implicit-integration/`, `reactive-integrator.h`, `acfx::integration` — no numeric prefixes. |
| IV. Platform-independent core | ✅ | Pure core over MNA + Newton + vocabulary; dependencies point inward only; no platform headers. |
| V. No fallbacks / no mock data | ✅ | Non-convergence → propagated `NewtonStatus.converged == false` (by value, no throw on hot path); the integration rule is NEVER silently switched/adapted to rescue stiffness (no hidden fallback); mocks confined to tests. |
| VI. RT-safety in the audio path | ✅ | Two-phase: throw-permitted `plan()` off the hot path; throw-free, alloc-free `step()`; companions computed once per step (not per Newton iteration). `AllocationSentinel` asserts zero heap (SC-004). |
| VII. Strict typing & small modules | ✅ | Templates + composition (no inheritance); header(s) within budget; no `any`/casts. |
| VIII. Test the core host-side | ✅ | doctest closed-forms, convergence order, invariants, lab-equivalence, no-alloc — all host-side, no hardware. |
| IX. Progressive layered architecture | ✅ (justified) | This is the **primitive-stage graduation** of the reactive-companion + history + time-stepping logic prototyped and validated in the `component-abstractions`, `diode-clippers`, and `opamp-stages` labs (the 4× hand-rolled `advanceHistory`). Principle IX: "laboratory implementations evolve into production primitives rather than being thrown away" — this is that step. The capability gain (selectable higher-order rule) is the graduation, not a new black box. |
| X. Measurable engineering | ✅ | Acceptance is objective: exact-discrete-response match ≤ tolerance, convergence-order slopes (≈1 BE / ≈2 trap), lab-equivalence ≤ tolerance, zero heap ops, DC-steady-state / passivity invariants, determinism. |
| XI. One concept at a time | ✅ | Single concept: implicit time-stepping + reactive companions + history. The linear solve (MNA) and the nonlinear loop (Newton) are separate shipped siblings, composed, not folded in. |

**No violations.** Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/implicit-integration/
├── plan.md              # This file
├── research.md          # Phase 0 output — implementation-shaping decisions
├── data-model.md        # Phase 1 output — entities & relationships
├── quickstart.md        # Phase 1 output — build/validate guide
├── contracts/           # Phase 1 output — public header API contract
│   └── reactive-integrator.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already present)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/primitives/circuit/
├── node.h, components.h, netlist.h        # existing frozen vocabulary (consumed as-is; isReactive routing)
├── models/                                # capacitor.h / inductor.h (BE companion physics), companion.h (consumed)
├── mna/                                   # shipped sibling (MnaSystem, MnaAssembler) — driven, not modified
├── newton/                                # shipped sibling (NewtonSolver) — composed, not modified
└── integration/                           # NEW — this feature (inhabited in the same commit)
    └── reactive-integrator.h              # ReactiveIntegrator + reactive CompanionSupply + rule policy + history advance

tests/core/
├── integration-integrator-test.cpp        # unit: plan/step lifecycle, companion computation, history advance, construction validation
├── integration-closed-form-test.cpp        # exact discrete responses: RC/RL step (both rules), LC
├── integration-convergence-order-test.cpp   # timestep refinement: ≈1 BE / ≈2 trap
├── integration-invariants-test.cpp          # DC steady state (cap→open, inductor→short), passivity
├── integration-composition-test.cpp         # supply as MNA refresh (linear) + Newton base (nonlinear transient)
└── integration-equivalence-test.cpp         # oracle vs LinearSolver / TransientClipper / OpAmpClipperSolver (rule=BE)
tests/support/
└── allocation-sentinel.h                   # existing — reused for the no-heap assertion
```

**Structure Decision**: A new `core/primitives/circuit/integration/` directory
(inhabited, not pre-created) holds the primitive header adjacent to the MNA + Newton
cores and the vocabulary it consumes. A single `ReactiveIntegrator` type is the whole
primitive; the integration-rule policy (backward-Euler / trapezoidal) is a template
parameter, and the reactive companion supply is a small internal helper in the same
header (split into a second header only if the line budget requires it). Tests live
beside the existing solver tests under `tests/core/`.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

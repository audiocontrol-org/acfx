> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Modified Nodal Analysis (MNA) primitive

**Branch**: `modified-nodal-analysis` | **Date**: 2026-07-07 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/modified-nodal-analysis/spec.md`
(design record: `docs/superpowers/specs/2026-07-07-modified-nodal-analysis-design.md`)

## Summary

Build a general, RT-safe Modified Nodal Analysis engine as a **production primitive**
under `core/primitives/circuit/mna/`, namespace `acfx::mna`, in two header-only,
template-sized layers: `MnaSystem` (an abstract bordered linear engine that stamps
and solves `A·x = z` with partial pivoting) and `MnaAssembler` (the single
authoritative site that maps a `Netlist` onto engine stamps). MNA owns the linear
solve and element stamping only; the sibling primitives `newton-iteration` and
`implicit-integration` supply Norton companions and own the nonlinear loop / the
integration rule. Assembly is two-phase — a throw-permitted plan phase (fix branch
topology, validate) run once per netlist, and a throw-free, alloc-free per-solve
phase on the hot path. Validation is by exact closed-forms, physical invariants, a
lab-equivalence oracle, and a zero-heap `AllocationSentinel` assertion.

## Technical Context

**Language/Version**: C++17 (matches the DSP core; no C++20 constructs, unlike the
host-only labs).

**Primary Dependencies**: none outside the DSP core. Consumes the frozen vocabulary
in `core/primitives/circuit/` (`Netlist`, `node.h`, `models/*`, `Companion`) and
`core/dsp/span.h`. No platform/hardware headers (Principle IV).

**Storage**: N/A (in-memory fixed-size `std::array`; no persistence).

**Testing**: doctest under `tests/core/`, with `tests/support/allocation-sentinel.h`
for the no-heap invariant. Equivalence oracle runs against the existing lab solvers.

**Target Platform**: platform-independent core — DAW plugins, desktop, Daisy,
Teensy. Header-only so it compiles into any target.

**Project Type**: reusable DSP primitive (library headers), not an application.

**Performance Goals**: per-solve phase is O(Dim³) dense elimination with zero heap
allocation; Dim = MaxNodes + MaxBranches fixed at compile time. Suitable for the
node/branch counts of analog guitar/audio stages (tens of nodes), called per-sample
and per-Newton-iteration.

**Constraints**: no heap allocation or locks or unbounded work on the per-solve
path (Principle VI); no fallbacks / no mock data outside tests (Principle V); files
within ~300–500 lines (Principle VII); strict typing, no `any`/unchecked casts,
composition over inheritance (Principle VII).

**Scale/Scope**: two headers + a test suite. No new component types. Lab migration
(TASK-14) is out of scope for this feature.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Commit & push early/often | ✅ | Atomic commits per artifact; pushed promptly. No AI attribution. |
| II. No git hooks | ✅ | None added or depended on. |
| III. Descriptive names | ✅ | `specs/modified-nodal-analysis/`, `mna-system.h`, `mna-assembler.h`, `acfx::mna` — no numeric prefixes. |
| IV. Platform-independent core | ✅ | Pure core; dependencies point inward only; no platform headers. |
| V. No fallbacks / no mock data | ✅ | No silent gmin fallback; `solve()` returns a boolean not-solved signal; plan-phase throws descriptively; mocks confined to test harness. |
| VI. RT-safety in the audio path | ✅ | Two-phase design: throw-permitted plan phase off the hot path; throw-free, alloc-free per-solve phase. `AllocationSentinel` asserts zero heap (SC-003). |
| VII. Strict typing & small modules | ✅ | Templates + composition (no inheritance); two headers each within budget; no `any`/casts. |
| VIII. Test the core host-side | ✅ | doctest closed-forms, invariants, no-alloc invariant — all host-side, no hardware. |
| IX. Progressive layered architecture | ✅ (justified) | MNA is the **primitive-stage graduation** of concepts already introduced and validated across four labs (`component-abstractions`, `opamp-stages`, `diode-clippers`, `passive-tone-stacks`). Principle IX explicitly says "laboratory implementations evolve into production primitives rather than being thrown away" — this feature *is* that step (the linear-algebra core TASK-14 identifies). Not a new black-box concept; the lab stage is complete. |
| X. Measurable engineering | ✅ | Acceptance is objective: exact-closed-form relative error ≤1e-12, lab-equivalence ≤1e-12, zero heap ops, ill-posed→not-solved. |
| XI. One concept at a time | ✅ | Single concept: the linear MNA solve + element stamping. The nonlinear loop and integration rule are the separate sibling primitives, not folded in here. |

**No violations.** Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/modified-nodal-analysis/
├── plan.md              # This file
├── research.md          # Phase 0 output — implementation-shaping decisions
├── data-model.md        # Phase 1 output — entities & relationships
├── quickstart.md        # Phase 1 output — build/validate guide
├── contracts/           # Phase 1 output — public header API contracts
│   ├── mna-system.md
│   └── mna-assembler.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already present)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/primitives/circuit/
├── node.h, components.h, netlist.h        # existing frozen vocabulary (consumed as-is)
├── models/                                # existing element structs + Companion (consumed)
└── mna/                                   # NEW — this feature (inhabited in the same commit)
    ├── mna-system.h                        # Layer 1: abstract bordered linear engine
    └── mna-assembler.h                     # Layer 2: Netlist → MnaSystem stamps

tests/core/
├── mna-system-test.cpp                     # Layer 1 unit tests (stamps + pivoted solve)
├── mna-assembler-test.cpp                  # Layer 2 element-mapping + closed-forms
├── mna-invariants-test.cpp                 # passivity / reciprocity / well-posedness
└── mna-equivalence-test.cpp                # oracle vs LinearSolver + NullorSolver
tests/support/
└── allocation-sentinel.h                   # existing — reused for the no-heap assertion
```

**Structure Decision**: A new `core/primitives/circuit/mna/` directory (inhabited,
not pre-created) holds the two headers adjacent to the vocabulary they consume. The
engine/assembler split keeps each file within the line budget and isolates the
reusable linear-algebra core (Layer 1) from element knowledge (Layer 2). Tests live
beside the existing solver tests under `tests/core/`.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

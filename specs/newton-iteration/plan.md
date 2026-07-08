> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Newton–Raphson iteration primitive

**Branch**: `newton-iteration` | **Date**: 2026-07-07 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/newton-iteration/spec.md`
(design record: `docs/superpowers/specs/2026-07-07-newton-iteration-design.md`)

## Summary

Build the Newton–Raphson nonlinear outer-loop engine as a **production primitive**
under `core/primitives/circuit/newton/`, namespace `acfx::newton`, in one header-only,
template-sized type `NewtonSolver<MaxNodes, MaxComponents, MaxBranches>`. It drives the
shipped MNA core: each iteration it linearizes every diode about the current node-voltage
iterate into a Norton `Companion{Geq: g, Ieq: I − g·vAK}` (from `Diode::evaluate`),
composes those with a caller-supplied base companion supply (the fixed reactive companions
`implicit-integration` will own), refreshes and solves the MNA system, `pnjlim`-damps each
junction (`Diode::limitJunctionVoltage`), and tests the voltage residual — to a bounded
iteration count, with **no fabricated fallback** on non-convergence. Newton is **stateless
per solve** (caller owns the initial node-voltage guess) and **general multi-diode** (it
lifts the labs' single-nonlinearity refusal). Assembly is two-phase: a throw-permitted
`plan()` (delegating to `MnaAssembler::plan`) run once per netlist, and a throw-free,
alloc-free `solve()` on the hot path. Validation is by exact closed-forms, physical
invariants, a lab-equivalence oracle, and a zero-heap `AllocationSentinel` assertion.

## Technical Context

**Language/Version**: C++17 (matches the DSP core and the MNA sibling; no C++20 constructs,
unlike the host-only labs).

**Primary Dependencies**: the shipped MNA primitive (`core/primitives/circuit/mna/`:
`MnaSystem`, `MnaAssembler`) and the frozen vocabulary in `core/primitives/circuit/`
(`Netlist`, `node.h`, `models/diode.h` for `evaluate`/`limitJunctionVoltage`,
`models/companion.h` for `Companion`). No platform/hardware headers (Principle IV).

**Storage**: N/A (in-memory fixed-size `std::array`; no persistence).

**Testing**: doctest under `tests/core/`, with `tests/support/allocation-sentinel.h` for the
no-heap invariant. Equivalence oracle runs against the existing lab solvers
(`OpAmpClipperSolver` / `TransientClipper`); exact closed forms via an independently
iterated / Lambert-W reference computed in-test.

**Target Platform**: platform-independent core — DAW plugins, desktop, Daisy, Teensy.
Header-only so it compiles into any target.

**Project Type**: reusable DSP primitive (library headers), not an application.

**Performance Goals**: per `solve()` runs up to `maxIterations` MNA solves, each an
O(Dim³) dense elimination with zero heap allocation (Dim fixed at compile time). Sized for
analog guitar/audio stages (tens of nodes, a handful of diodes), called per sample.

**Constraints**: no heap allocation or locks or unbounded work on the `solve()` path
(Principle VI — the iteration bound makes the work bounded); no fallbacks / no mock data
outside tests (Principle V — non-convergence and singularity are surfaced by value); files
within ~300–500 lines (Principle VII); strict typing, no `any`/unchecked casts, composition
over inheritance (Principle VII).

**Scale/Scope**: one primitive header (plus a small internal composed-supply helper if it
keeps the file within budget) + a test suite. No new component types. Lab migration
(TASK-14) is out of scope for this feature.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Commit & push early/often | ✅ | Atomic commits per artifact; pushed promptly. No AI attribution. |
| II. No git hooks | ✅ | None added or depended on. |
| III. Descriptive names | ✅ | `specs/newton-iteration/`, `newton-solver.h`, `acfx::newton` — no numeric prefixes. |
| IV. Platform-independent core | ✅ | Pure core over MNA + vocabulary; dependencies point inward only; no platform headers. |
| V. No fallbacks / no mock data | ✅ | Non-convergence → `NewtonStatus.converged == false` + last iterate; singularity via `MnaSystem::solve() == false`; NO gmin/source-step/substitution; mocks confined to test harness. |
| VI. RT-safety in the audio path | ✅ | Two-phase: throw-permitted `plan()` off the hot path; throw-free, alloc-free, iteration-bounded `solve()`. `AllocationSentinel` asserts zero heap (SC-003). |
| VII. Strict typing & small modules | ✅ | Templates + composition (no inheritance); header(s) within budget; no `any`/casts. |
| VIII. Test the core host-side | ✅ | doctest closed-forms, invariants, lab-equivalence, no-alloc — all host-side, no hardware. |
| IX. Progressive layered architecture | ✅ (justified) | Newton is the **primitive-stage graduation** of the nonlinear loop already introduced and validated in the `diode-clippers` and `opamp-stages` labs. Principle IX: "laboratory implementations evolve into production primitives rather than being thrown away" — this is that step. The lab stage is complete; the capability gain (general multi-diode) is the graduation, not a new black box. |
| X. Measurable engineering | ✅ | Acceptance is objective: exact-closed-form match ≤ solve tolerance, lab-equivalence ≤ tolerance, zero heap ops, non-converge→surfaced, symmetry/monotonicity invariants. |
| XI. One concept at a time | ✅ | Single concept: the nonlinear Newton loop over MNA. The linear solve (MNA) and the integration rule / reactive history (`implicit-integration`) are separate siblings, not folded in. |

**No violations.** Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/newton-iteration/
├── plan.md              # This file
├── research.md          # Phase 0 output — implementation-shaping decisions
├── data-model.md        # Phase 1 output — entities & relationships
├── quickstart.md        # Phase 1 output — build/validate guide
├── contracts/           # Phase 1 output — public header API contracts
│   └── newton-solver.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already present)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/primitives/circuit/
├── node.h, components.h, netlist.h        # existing frozen vocabulary (consumed as-is)
├── models/                                # diode.h (evaluate/limitJunctionVoltage), companion.h (consumed)
├── mna/                                   # shipped sibling (MnaSystem, MnaAssembler) — driven, not modified
└── newton/                                # NEW — this feature (inhabited in the same commit)
    └── newton-solver.h                     # NewtonSolver + NewtonStatus + composed companion supply

tests/core/
├── newton-solver-test.cpp                  # unit: loop, linearization, damping, convergence gate, construction validation
├── newton-closed-form-test.cpp             # exact operating points (single diode + resistor; antiparallel pair)
├── newton-invariants-test.cpp              # monotonicity / antiparallel symmetry / I(0)=0 / passivity
├── newton-equivalence-test.cpp             # oracle vs OpAmpClipperSolver / TransientClipper
└── newton-nofallback-test.cpp              # non-convergence + singular → surfaced, stateless under failure
tests/support/
└── allocation-sentinel.h                   # existing — reused for the no-heap assertion
```

**Structure Decision**: A new `core/primitives/circuit/newton/` directory (inhabited, not
pre-created) holds the primitive header adjacent to the MNA core and vocabulary it consumes.
A single `NewtonSolver` type is the whole primitive; the composed companion supply is a small
internal helper in the same header (split into a second header only if the line budget
requires it). Tests live beside the existing solver tests under `tests/core/`.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: component-abstractions

**Branch**: `component-abstractions` | **Date**: 2026-07-03 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/component-abstractions/spec.md`; design record `docs/superpowers/specs/2026-07-03-component-abstractions-design.md` (approved).

## Summary

Define the **solver-neutral typed vocabulary of electronic circuit components** — the first deliverable of Phase 4 (Circuit Modeling). Components are header-only value structs that carry **their own physics** (`admittance()` for linear elements; `evaluate(v) → {current, conductance}` for the nonlinear diode; a continuous relation + backward-Euler **companion hook** for reactive C/L), bound to integer node handles (ground ≡ 0). A templated **`Netlist<MaxNodes, MaxComponents>`** owns topology heap-free and validates it at `prepare()`. A deliberately-naive **reference solver lives in the lab** (`core/labs/component-abstractions/`), never the primitive — fixed-size Gaussian elimination for linear circuits plus a bounded fixed-iteration Newton for a single-diode/antiparallel clipper, refusing anything with ≥2 interacting nonlinearities (that is Phase 5 MNA). Physics and the solve compute in `double`; the audio boundary stays `float` (FR-022). The seam — *components own physics, solvers own assembly* — is what lets Phase 5 MNA and Phase 6 WDF adapt this vocabulary without a rewrite.

## Technical Context

**Language/Version**: C++17 for the primitive headers (Teensy target lacks C++20 concepts); the lab harness may use C++20 (as existing labs do, per root `CMakeLists.txt`).

**Primary Dependencies**: none beyond the standard library. `std::variant` / `std::visit` for the heap-free tagged container (C++17). No JUCE / libDaisy / Teensy dependency (Constitution IV).

**Storage**: N/A (a compute/library feature; no persistence).

**Testing**: host-side unit tests under `tests/core/` (registered in `tests/CMakeLists.txt`, the existing GoogleTest-style suite) for component physics, netlist validation, and no-allocation invariants; a lab harness executable under `core/labs/component-abstractions/harness/` (registered in root `CMakeLists.txt`) for the assembled-circuit analytic validations.

**Target Platform**: platform-independent core; must compile host-side (desktop CI) and be C++17-clean for the Teensy target. No hardware in the loop for validation.

**Project Type**: single-project DSP library (the acfx three-layer core: `labs/ → primitives/ → effects/`).

**Performance Goals**: the primitive's per-sample solve path is **zero-allocation** (Constitution VI); the reference solver is a lab/validation artifact, so its performance target is "correct within analytic tolerance," not a throughput number. No RT-audio latency claim for the lab solver.

**Constraints**: no heap/locks in the solve path; files ~300–500 lines (Constitution VII); no fallbacks/mock outside tests (Constitution V); `double` physics/solve, `float` audio boundary (FR-022); inhabit-before-creating for the `circuit/` category folder (FR-018).

**Scale/Scope**: six v1 component inhabitants (R, C, L, V, I, diode); four validation circuits (divider, RC, RLC, diode clipper); templated capacities sized for those circuits (small — order 10s of nodes/components).

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1 design. All gates pass; no Complexity-Tracking entries required.*

| Principle | Status | How this feature satisfies it |
|---|---|---|
| I. Commit & push early/often | ✅ | Atomic commits per artifact/story; pushed promptly (already doing so through define). |
| II. No git hooks | ✅ | No hooks added or depended on; gates are the explicit `/speckit-*` + build steps. |
| III. Descriptive names | ✅ | Spec dir `component-abstractions`, category `circuit/`, per-component headers named for what they are — no numeric prefixes. |
| IV. Platform-independent core, thin adapters | ✅ | Headers depend only on the standard library; **solvers are the adapters** (MNA/WDF later), and even the reference solver is isolated in the lab, not the primitive. |
| V. No fallbacks / mock outside tests | ✅ | Topology errors, Newton non-convergence, and out-of-scope circuits all raise descriptive errors (FR-010/015/016/019); no fallback path. |
| VI. Real-time safety | ✅ | `Netlist` is fixed-capacity/heap-free; the per-sample solve allocates nothing (FR-011); asserted by test (SC-006). The reference solver is a lab artifact, but still holds the no-alloc invariant in its solve loop. |
| VII. Strict typing & small modules | ✅ | No inheritance/virtual dispatch (FR-002/008); `std::variant` composition; per-file ≤~500 lines (FR-020). |
| VIII. Test the core host-side | ✅ | `tests/core/` unit tests for physics + validation + no-alloc; analytic references (divider/RC/RLC/diode). |
| IX. Progressive layered architecture | ✅ | Lab (reference solver + harness) → primitive (vocabulary); the lab code is not disposable — it is the honest naive solver that Phase 5 MNA supersedes, not deletes. |
| X. Measurable engineering | ✅ | Validation is objective: exact divider ratio, analytic RC/RLC magnitude/phase, analytic diode transfer, asserted zero-alloc (SC-001..006). |
| XI. One concept at a time | ✅ | The single new idea is **component modeling as a solver-neutral vocabulary**; the general solver (MNA) is deliberately deferred to its own phase. |

**Result: PASS (pre-Phase-0 and post-Phase-1).** No violations; Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/component-abstractions/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output — entities: NodeId, Component types, Netlist, Companion
├── quickstart.md        # Phase 1 output — how to build + run the validations
├── contracts/           # Phase 1 output — the primitive's public contracts
│   ├── component-physics.md    # admittance() / evaluate() / companion hook contract
│   ├── netlist.md              # Netlist<MaxNodes,MaxComponents> + prepare() validation contract
│   └── reference-solver.md     # lab solver contract (linear + single-nonlinearity + refusal)
├── checklists/
│   └── requirements.md  # spec quality checklist (all green)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── primitives/
│   ├── README.md                     # UPDATE: register circuit/ category + 6 inhabitants (FR-021)
│   └── circuit/                       # NEW category (created with its first inhabitant — FR-018)
│       ├── node.h                    # NodeId, ground=0, node-count helpers
│       ├── components.h              # the std::variant<...> component value + terminal-to-node binding
│       ├── models/
│       │   ├── resistor.h            # admittance()  (linear)
│       │   ├── capacitor.h           # continuous relation + backward-Euler companion hook (reactive)
│       │   ├── inductor.h            # dual of capacitor (reactive)
│       │   ├── sources.h             # independent voltage + current sources
│       │   └── diode.h               # Shockley evaluate(v) -> {current,conductance} (nonlinear)
│       └── netlist.h                 # Netlist<MaxNodes,MaxComponents> + prepare()-time validation
└── labs/
    └── component-abstractions/       # NEW lab (reference solver + harness) — NOT the primitive
        ├── README.md                 # what the lab teaches; "this solver is naive, Phase 5 supersedes it"
        ├── solver/
        │   ├── linear-solver.h       # fixed-size Gaussian elimination (no heap in solve)
        │   └── newton-clipper.h      # bounded fixed-iteration Newton for the single/antiparallel diode
        └── harness/
            └── component-abstractions-harness.cpp   # assembled-circuit analytic validations

tests/
└── core/
    ├── circuit-components-test.cpp    # per-component physics (US1)
    ├── circuit-netlist-test.cpp       # netlist assembly + prepare() validation + no-alloc (US2)
    └── circuit-solver-test.cpp        # divider/RC/RLC + diode clipper vs analytic; refusal (US3/US4)

CMakeLists.txt                         # UPDATE: register the lab harness executable (mirror acfx_lab_* pattern)
tests/CMakeLists.txt                   # UPDATE: register the three new tests/core/*.cpp suites
```

**Structure Decision**: Single-project acfx three-layer core. The feature lands as (1) a new **primitive category** `core/primitives/circuit/` holding the pure vocabulary, (2) a new **lab** `core/labs/component-abstractions/` holding the deliberately-naive reference solver + harness, and (3) three **host-side test suites** under `tests/core/`. The split enforces the load-bearing seam: the primitive compiles and its physics/netlist tests pass with the lab absent (SC-007). Build integration mirrors the existing `acfx_lab_*` harness and `tests/core/*-test.cpp` conventions already in the two `CMakeLists.txt`.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — Research** (`research.md`): resolve the physics/formulation decisions the spec left to implementation — Shockley formulation + Newton voltage-limiting, backward-Euler companion algebra for C and L, MNA-style handling of ideal voltage sources in the nodal system (the one subtlety a pure nodal `G` matrix can't express), and the `std::variant`-vs-tagged-union / `double`-instantiation code-size question (OQ5). No spec-level NEEDS CLARIFICATION remain (the numeric-precision one was resolved in `/speckit-clarify`).
- **Phase 1 — Design & contracts** (`data-model.md`, `contracts/`, `quickstart.md`): pin the entity fields and the three public contracts, and the runnable validation guide.
- **Phase 2 — Tasks** (`/speckit-tasks`, not this command): dependency-ordered `tasks.md`.

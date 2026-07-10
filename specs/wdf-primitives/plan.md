> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: WDF-primitives

**Branch**: `wdf-primitives` | **Date**: 2026-07-09 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/wdf-primitives/spec.md`
(design record: `docs/superpowers/specs/2026-07-09-wdf-primitives-design.md`)

## Summary

Establish the **wave-domain leaf one-port vocabulary** for the WDF phase as a header-only
primitive family under `core/primitives/circuit/wdf/`, namespace `acfx::wdf`, sibling to
`acfx::mna` / `acfx::newton` / `acfx::integration`. Each circuit element becomes a
**one-port** under the **voltage-wave** convention (`a = v + Rp·i`, `b = v − Rp·i`) with a
**port resistance** `Rp` and a **scattering relation** `b = f(a)`. The family is a set of
small value/stateful types — resistor, capacitor, inductor, resistive voltage/current
sources, and resistive/short/open terminations — each satisfying one **duck-typed one-port
port interface** (`portResistance()` / `reflected()` / `incident(a)`, all `noexcept`, plus
an adaptable/reflective classifier), the seam the sibling `wdf-adaptors` node will consume.
Reactive elements are **bilinear**-discretized so a capacitor becomes port resistance
`T/(2C)` with reflected wave `b[n] = a[n−1]` (a **unit delay**) and an inductor its dual
(`2L/T`, `b[n] = −a[n−1]`). Reactive leaves are **constructed with `(parameter, dt)`**,
computing `Rp` once in the constructor; the per-sample wave path is zero-heap, lock-free.
Validation is **per-element**: exact scattering closed forms, the unit-delay relation, a
**reactive wave-power balance** `Σ(a² − b²) ≥ 0` (NOT same-sample `|b| ≤ |a|`, which is
invalid for a reactive leaf), memoryless-passivity, cap/inductor duality, bilinear-impedance
agreement, and a zero-heap `AllocationSentinel` assertion. Adaptors, tree assembly /
adaptation, and ideal/nonlinear roots are the sibling WDF nodes and are out of scope.

## Technical Context

**Language/Version**: C++17 (matches the DSP core and the MNA/Newton/integration siblings;
no C++20 constructs, unlike the host-only labs).

**Primary Dependencies**: the **physical constants** of the frozen circuit vocabulary
(`core/primitives/circuit/models/`: `Resistor` `R`, `Capacitor` `C`, `Inductor` `L`, source
values) — reused as parameters, NOT as nodal containers. No dependency on the nodal solvers
(MNA/Newton/integration): WDF is a **parallel reader** of the vocabulary, not a consumer of
the matrix engines. No `NodeId` (WDF topology is the connection tree). No platform/hardware
headers (Principle IV).

**Storage**: N/A (each reactive leaf holds one `double` of wave state in-object; no
persistence, no heap).

**Testing**: doctest under `tests/core/`, with `tests/support/allocation-sentinel.h` for the
zero-heap invariant. Exact scattering closed forms and the unit-delay relation are analytic
in-test; the reactive wave-power balance and bilinear-impedance agreement are computed from
first principles (no transcribed published values).

**Target Platform**: platform-independent core — DAW plugins, desktop, Daisy, Teensy.
Header-only so it compiles into any target.

**Project Type**: reusable DSP primitive (library headers), not an application.

**Performance Goals**: each leaf's wave path (`portResistance` / `reflected` / `incident`) is
O(1), branch-light, and heap-free — called per sample within a tree's up/down sweep (the
sweep itself is a downstream adaptor/network concern). Sized for analog audio stages (a
handful of one-ports per network).

**Constraints**: no heap allocation or locks on the wave path (Principle VI); no fallbacks —
non-physical parameters are surfaced at construction, never clamped, and reflections are
never limited to fake passivity (Principle V); files within ~300–500 lines (Principle VII);
strict typing, no `any`/unchecked casts, **composition over inheritance** — the port
interface is a duck-typed concept, not a virtual base class (Principle VII).

**Scale/Scope**: a small set of one-port headers (one per element family, or grouped so each
file stays within budget) + a shared port-interface concept header + a test suite. No new
component types added to the frozen vocabulary. Adaptors / tree assembly / adaptation /
nonlinear-or-ideal roots are the sibling nodes — out of scope.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Commit & push early/often | ✅ | Atomic commits per artifact; pushed promptly. No AI attribution. |
| II. No git hooks | ✅ | None added or depended on. |
| III. Descriptive names | ✅ | `specs/wdf-primitives/`, `core/primitives/circuit/wdf/`, element headers named for what they are — no numeric prefixes. |
| IV. Platform-independent core | ✅ | Pure core reusing the frozen physical vocabulary; no platform headers; dependencies point inward only (WDF depends only on the physical constants, not even on the nodal engines). |
| V. No fallbacks / no mock data | ✅ | Non-physical parameters (`R/C/L/dt ≤ 0`) surfaced at construction (throw), never clamped; reflections never clamped to force apparent passivity; passivity is a **validated** property, not an enforced one. Mocks confined to tests. |
| VI. RT-safety in the audio path | ✅ | `Rp` computed once at construction (off the hot path); the wave path (`portResistance`/`reflected`/`incident`) is `noexcept`, zero-heap, lock-free, O(1). `AllocationSentinel` asserts zero heap (SC-004). |
| VII. Strict typing & small modules | ✅ | Duck-typed **concept** port interface (no inheritance, no vtable on the wave path); small headers within budget; no `any`/casts. |
| VIII. Test the core host-side | ✅ | doctest exact scattering, unit-delay, wave-power balance, duality, bilinear-impedance, zero-heap — all host-side, no hardware. |
| IX. Progressive layered architecture | ✅ (justified) | This is a **new primitive family**, not a lab graduation (WDF has no Phase-4 lab predecessor). Principle IX permits a primitive that introduces a new concept; the concept (wave-domain one-ports) is introduced minimally and explainably (Principle XI), one paradigm foundation at a time, deferring adaptors/networks/roots to sibling nodes. |
| X. Measurable engineering | ✅ | Acceptance is objective: exact scattering match ≤ tolerance, unit-delay `b[n] = a[n−1]` exact, wave-power balance `Σ(a²−b²) ≥ 0`, `Rp` positivity, duality, zero heap ops, file-size budget. |
| XI. One concept at a time | ✅ | Single concept: the wave-domain leaf one-port + the port interface. Adaptors (junctions), tree adaptation, and nonlinear/ideal roots are separate sibling nodes, not folded in. |

**No violations.** Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/wdf-primitives/
├── plan.md              # This file
├── research.md          # Phase 0 output — implementation-shaping decisions
├── data-model.md        # Phase 1 output — one-port types, port interface, state
├── quickstart.md        # Phase 1 output — build/validate guide
├── contracts/           # Phase 1 output — public header API contract
│   └── wdf-one-ports.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already present)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/primitives/circuit/
├── node.h, components.h, netlist.h        # existing frozen vocabulary (physical constants reused; NOT NodeId topology)
├── models/                                # resistor.h/capacitor.h/inductor.h/sources.h — physical R/C/L/E/I reused as params
├── mna/, newton/, integration/            # shipped nodal siblings — NOT a dependency (WDF is a parallel reader)
└── wdf/                                    # NEW — this feature (inhabited in the same commit)
    ├── one-port.h                          # the duck-typed OnePort port-interface concept + adaptable/reflective trait
    ├── wave-elements.h                     # Resistor / Capacitor / Inductor one-ports (voltage waves, bilinear)
    ├── wave-sources.h                      # resistive voltage source / resistive current source one-ports
    └── wave-terminations.h                 # resistive / short / open terminations
    # (exact file split may adjust to keep each header within the ~300–500 line budget)

tests/core/
├── wdf-resistor-test.cpp                   # resistor: Rp = R, adapted b = 0, general-reflection oracle
├── wdf-reactive-test.cpp                   # capacitor/inductor: Rp bilinear, unit-delay b[n]=±a[n-1], duality, bilinear-impedance
├── wdf-sources-test.cpp                    # resistive V/I sources: Rp = R, b = E / R·I, per-sample drive update
├── wdf-terminations-test.cpp               # resistive/short/open: matched b=0, reflective b=∓a, non-adaptable classifier
├── wdf-port-interface-test.cpp             # generic up/down-sweep over a heterogeneous leaf set, no virtual dispatch
├── wdf-passivity-test.cpp                  # memoryless |b|≤|a| + reactive wave-power balance Σ(a²−b²)≥0
└── wdf-rt-safety-test.cpp                  # AllocationSentinel zero-heap on the wave path; non-physical-param throw
tests/support/
└── allocation-sentinel.h                   # existing — reused for the no-heap assertion
```

**Structure Decision**: A new `core/primitives/circuit/wdf/` directory (inhabited, not
pre-created) holds the one-port family co-located with the circuit physics it reuses,
matching the recent `circuit/<name>/` sibling pattern (the prospectus's top-level
`primitives/wdf/` is the noted alternative — research R10 / spec Open Question 5). The port
interface is a **concept** in `one-port.h`; the concrete leaves are grouped into a few small
headers (elements / sources / terminations) so each stays within the line budget. Tests live
beside the existing solver tests under `tests/core/`.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: WDF-adaptors

**Branch**: `wdf-adaptors` | **Date**: 2026-07-11 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/wdf-adaptors/spec.md`
(design record: `docs/superpowers/specs/2026-07-10-wdf-adaptors-design.md`)

## Summary

Add the **series and parallel scattering adaptors** for the WDF phase as header-only
templates under `core/primitives/circuit/wdf/`, namespace `acfx::wdf`, consuming the shipped
`OnePort` concept (`one-port.h`) established by `wdf-primitives`. An adaptor is an **N-port
scattering junction** that is **itself a `OnePort`**: it makes its **upward port** the
adapted (reflection-free) port with `R_up = Σ R_child` (series) / `1/R_up = Σ 1/R_child`
(parallel), so it nests recursively as a child of another adaptor and connects the shipped
leaves through the same seam. Adaptors are **variadic templates on their child one-port
types** (general N-port; the 3-port case is the two-child instantiation), holding children
**by value** (composition); a whole filter is one statically-composed, heap-free tree
evaluated by an **up-sweep** (`reflected()`, from cached child waves) then a **down-sweep**
(`incident(a)`, scattering back down). Under the shipped voltage-wave convention
(`a = v + Rp·i`, `b = v − Rp·i`): series `b_k = a_k − (2·R_k/R)·Σ_i a_i`; parallel
`b_k = 2·(Σ_i G_i·a_i)/G − a_k`. Coefficients are precomputed at construction from each
child's `portResistance()`; the per-sample path is `noexcept`, zero-heap, lock-free, O(N).
Adaptors accept **only adaptable children** (compile-time `static_assert`) so every adaptor
is itself adaptable — the single reflective port is the tree **root**, owned by the sibling
nodes. A compile-time typed `child<I>()` accessor reaches owned children. Validation is
**root-driver-free**: exact scattering on series/parallel resistive dividers, the
reflection-free property of the adapted port, the **conductance-weighted pseudo-power
balance** `Σ_k G_k·a_k² = Σ_k G_k·b_k²`, an `AllocationSentinel` zero-heap assertion, and a
static-assertion/compile-fail demonstration that a non-adaptable child is rejected. Whole-tree
topology / root-port selection / the root driver / R-type adaptors / named networks
(→ `wdf-passive-networks`) and ideal-source / nonlinear roots (→ `wdf-complete-analog-stages`)
are captured but out of scope.

## Technical Context

**Language/Version**: C++17 (matches the DSP core and the `wdf-primitives` /
MNA / Newton / integration siblings; no C++20 constructs).

**Primary Dependencies**: the shipped **`OnePort` concept** and voltage-wave inverse helpers
of `wdf-primitives` (`core/primitives/circuit/wdf/one-port.h`: `is_one_port_v`,
`portResistance()` / `reflected()` / `incident(a)` / `isAdaptable`, `waveToVoltage` /
`waveToCurrent`). **No dependency on the nodal solvers** (MNA / Newton / integration): WDF is
a parallel path; **no `NodeId`** (topology is the connection tree). No platform/hardware
headers (Principle IV). The shipped leaves are used as test children but the adaptors are
generic over any adaptable `OnePort`.

**Storage**: N/A. Each adaptor holds its children by value plus a small fixed set of
in-object `double` coefficients and one cached wave per child; no persistence, no heap.

**Testing**: doctest under `tests/core/`, reusing `tests/support/allocation-sentinel.h` for
the zero-heap invariant. Exact scattering closed forms, the reflection-free property, and the
conductance-weighted passivity balance are computed analytically in-test (no transcribed
published values). Full network transfer-function tests are **deferred** to the root-driver
owner (`wdf-passive-networks`).

**Target Platform**: platform-independent core — DAW plugins, desktop, Daisy, Teensy.
Header-only; compiles into any target.

**Project Type**: reusable DSP primitive (library headers), not an application.

**Performance Goals**: the per-sample up-sweep/down-sweep is O(N) in the adaptor's port count,
branch-light, and heap-free; all scattering coefficients are precomputed once at construction.
Sized for analog audio stages (a handful of one-ports per network); deep static trees inline.

**Constraints**: no heap allocation or locks on the wave path (Principle VI); no fallbacks —
non-physical child resistances and empty child sets are surfaced at construction (throw),
never clamped; reflections are never limited to fake passivity (Principle V); files within
~300–500 lines (Principle VII); strict typing, no `any` / unchecked casts, **composition over
inheritance** — adaptors consume and satisfy the duck-typed `OnePort` concept, never a virtual
base (Principle VII).

**Scale/Scope**: two adaptor family headers (`series-adaptor.h`, `parallel-adaptor.h`) plus a
shared `adaptor-detail.h` for the variadic sweep machinery and conformance `static_assert`s,
each within budget, plus a test suite. No new leaf types; no change to the shipped `OnePort`
concept or wave convention. Root driver / whole-tree adaptation / R-type adaptors /
nonlinear-or-ideal roots are sibling nodes — out of scope.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Commit & push early/often | ✅ | Atomic commits per artifact; pushed promptly. No AI attribution. |
| II. No git hooks | ✅ | None added or depended on. |
| III. Descriptive names | ✅ | `specs/wdf-adaptors/`, `core/primitives/circuit/wdf/{series,parallel}-adaptor.h`, `adaptor-detail.h` — named for what they are, no numeric prefixes. |
| IV. Platform-independent core | ✅ | Pure core consuming only the shipped `OnePort` concept + physical `double`s; no platform headers; dependencies point inward (no nodal-engine dependency, no `NodeId`). |
| V. No fallbacks / no mock data | ✅ | Non-positive / non-finite child `Rp` and empty child sets throw at construction; reflections never clamped to force apparent passivity; passivity is **validated**, not enforced. Mocks confined to tests. |
| VI. RT-safety in the audio path | ✅ | All coefficients precomputed at construction; the wave path (`reflected` / `incident`) is `noexcept`, zero-heap, lock-free, O(N). `AllocationSentinel` asserts zero heap (SC-005). |
| VII. Strict typing & small modules | ✅ | Variadic templates over the duck-typed concept (no inheritance, no vtable, no RTTI on the wave path); `static_assert` conformance; headers within budget; no `any` / casts. |
| VIII. Test the core host-side | ✅ | doctest exact scattering, reflection-free, weighted passivity, zero-heap, compile-time child rejection — all host-side, no hardware. |
| IX. Progressive layered architecture | ✅ | A direct layer on the shipped `wdf-primitives` seam: adaptors consume the `OnePort` ABI and add the connection primitive. One paradigm step; adaptation/networks/roots deferred to siblings. |
| X. Measurable engineering | ✅ | Acceptance is objective: exact scattering ≤ tolerance (SC-001/003), reflection-free to machine ε (SC-002), weighted pseudo-power residual ≤ tolerance (SC-004), zero heap ops (SC-005), construction-time throws (SC-006), compile-fail on non-adaptable child (SC-007). |
| XI. One concept at a time | ✅ | Single concept: the series/parallel scattering adaptor as a recursive adapted one-port. Root selection, whole-tree adaptation, R-type adaptors, and nonlinear/ideal roots are separate sibling nodes, not folded in. |

**No violations.** Complexity Tracking table below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/wdf-adaptors/
├── plan.md              # This file
├── research.md          # Phase 0 output — implementation-shaping decisions
├── data-model.md        # Phase 1 output — adaptor types, scattering, cached state, child access
├── quickstart.md        # Phase 1 output — build/validate guide
├── contracts/           # Phase 1 output — public header API contract
│   └── wdf-adaptors.md
├── checklists/
│   └── requirements.md  # spec quality checklist (already present)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/primitives/circuit/wdf/
├── one-port.h                # SHIPPED — the OnePort concept + inverse helpers (consumed, unchanged)
├── wave-elements.h           # SHIPPED — Resistor / Capacitor / Inductor leaves (test children)
├── wave-sources.h            # SHIPPED — resistive V/I source leaves
├── wave-terminations.h       # SHIPPED — resistive / short / open leaves
├── adaptor-detail.h          # NEW — shared variadic sweep machinery: child-tuple fold helpers,
│                             #       cached-wave storage, R/G accumulation, OnePort + adaptable
│                             #       static_assert helpers, construction-time validation
├── series-adaptor.h          # NEW — SeriesAdaptor<Child...>: b_k = a_k − (2R_k/R)Σa; R_up = ΣR_child
└── parallel-adaptor.h        # NEW — ParallelAdaptor<Child...>: b_k = 2(ΣG_i a_i)/G − a_k; G_up = ΣG_child
    # (exact split may adjust to keep each header within the ~300–500 line budget)

tests/core/
├── wdf-series-adaptor-test.cpp     # Rp = ΣR, series divider closed form, down-sweep scattering
├── wdf-parallel-adaptor-test.cpp   # 1/Rp = ΣG, parallel/current divider closed form
├── wdf-adaptor-tree-test.cpp       # nested series(R, parallel(L,C)); composite is_one_port_v; recursive Rp
├── wdf-adaptor-reflection-free-test.cpp  # b_u invariant under a_u (adapted-port property)
├── wdf-adaptor-passivity-test.cpp  # conductance-weighted balance ΣG_k a_k² = ΣG_k b_k²
├── wdf-adaptor-rt-safety-test.cpp  # AllocationSentinel zero-heap sweep; construction-time throws
└── wdf-adaptor-child-access-test.cpp  # child<I>() typed access; nested source mutation observed
tests/support/
└── allocation-sentinel.h           # existing — reused for the no-heap assertion
# A compile-fail / static_assert demonstration that a non-adaptable child is rejected
# (SC-007) is realized as a documented static_assert + a negative-compilation note in the
# tree test (mechanism finalized in tasks).
```

**Structure Decision**: Adaptors live beside the shipped leaves in
`core/primitives/circuit/wdf/`, consuming `one-port.h` unchanged. The shared variadic sweep
machinery and conformance `static_assert`s go in `adaptor-detail.h`; `SeriesAdaptor` and
`ParallelAdaptor` get one header each (they share the tuple iteration and differ only in the
coefficient formulas), keeping every file within the line budget (design D10). Tests live
beside the existing WDF tests under `tests/core/`, mirroring the shipped `wdf-*-test.cpp`
naming.

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |

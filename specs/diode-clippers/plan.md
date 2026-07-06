> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: diode-clippers

**Branch**: `diode-clippers` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/diode-clippers/spec.md`; design record `docs/superpowers/specs/2026-07-04-diode-clippers-design.md` (approved, design-to-spec exit gate 7/7).

## Summary

Compose the frozen `component-abstractions` vocabulary into **solver-neutral diode-clipper builders** — the third deliverable of Phase 4 and the first that assembles a **nonlinear circuit with reactive memory**. Three header-only builders in the portable primitive (`symmetricShuntClipper`, `asymmetricShuntClipper`, `seriesClipper`) take a bill of materials + diode parameters and return a `Netlist<MaxNodes, MaxComponents>` of ordinary `Resistor`/`Capacitor`/`Diode`/`VoltageSource` values — **topology only, no solve, no audio path**. The reactive nonlinear behavior is solved and validated in a **host-only lab** (`core/labs/diode-clippers/`) by a new **bounded transient nonlinear solver** — `TransientClipper<MaxNodes, MaxComponents, MaxDiodes>` (default `MaxDiodes=4`) that discretizes each reactive element as a backward-Euler companion (reusing the frozen `capacitor.h`/`inductor.h` `companion()` hooks) and resolves the diode nonlinearity by a bounded Newton iteration. The load-bearing structural decision — the fix for the reactive+nonlinear case `component-abstractions`' static solver deliberately refused — is **separating the timestep loop from the Newton loop**: reactive companions are computed once per timestep and the reactive history is advanced **exactly once after Newton converges**, with the inner Newton iterations holding those companions fixed. Validation mirrors `passive-tone-stacks`: **prove the solver exact first** (analytic backward-Euler RC step response ~1e-9; each clipper's DC steady-state limit cross-checked against an independent bisection root-find oracle ~1e-6 and the existing static `NewtonClipper` curve), then **assembled-clipper invariants** (odd-symmetry for the symmetric clipper; DC-offset/even-harmonic for the asymmetric; forward saturation; passivity/energy bound; and the pinned reactive signature — larger `Cf` ⇒ less post-clip HF for a fixed 1 kHz drive). Non-convergence is a first-class **test-surfaced** contract. The `component-abstractions` seam holds: **the builder produces topology; the solver reads each component's own physics** (`Diode::evaluate`, `capacitor.h::companion`) and never re-derives a law — so Phase-5 MNA / Phase-6 WDF adapt the same builders unchanged.

## Technical Context

**Language/Version**: C++17 for the primitive headers (Teensy target lacks C++20); the lab transient solver + harness may use C++20, as existing labs do (root `CMakeLists.txt`).

**Primary Dependencies**: none beyond the standard library — the frozen `component-abstractions` primitive (`core/primitives/circuit/`: `components.h`, `netlist.h`, `node.h`, `models/` incl. `diode.h`, `capacitor.h`, `inductor.h`, `companion.h`) and `dsp/span.h`. The lab reuses the `component-abstractions` lab solver structure (`core/labs/component-abstractions/solver/` — `LinearSolver`, the `NewtonClipper` companion-linearization pattern). No JUCE / libDaisy / Teensy dependency (Constitution IV).

**Storage**: N/A (a compute/library feature; no persistence).

**Testing**: host-side unit tests under `tests/core/` (doctest, registered in `tests/CMakeLists.txt`) — Tier-1 `diode-clipper-builder-test.cpp` (builder topology / prepare()-valid / counts, no solver), Tier-2 `diode-clipper-transient-test.cpp` (transient-solver sanity + assembled-clipper invariants + the explicit non-convergence test); a lab harness executable under `core/labs/diode-clippers/harness/` (registered in root `CMakeLists.txt`) mirroring the Tier-2 assertions with PASS/FAIL prints.

**Target Platform**: platform-independent core; the `diode-clipper/` primitive must compile host-side and be C++17-clean for the Teensy target. The lab is host-only. No hardware in the loop.

**Project Type**: single-project DSP library (the acfx three-layer core: `labs/ → primitives/ → effects/`).

**Performance Goals**: the builder is **zero-allocation** (fixed compile-time `Netlist` capacities; no `new`/`delete`/`std::vector` — SC-008). The lab transient solver is a validation artifact and heap-free on the solve path (fixed `std::array`-backed augmented netlist + nested `LinearSolver`); its target is "correct within the stated tolerances," not a throughput number. No RT-audio path exists in this deliverable.

**Constraints**: no heap in the builder or solve path (Constitution VI); files ~300–500 lines (Constitution VII); no fallbacks/mock outside tests — descriptive throws on bad BOM input, `dt ≤ 0`, a singular system, and an out-of-scope (≥2 interacting nonlinearity) netlist; **non-convergence reported, never faked** (Constitution V, FR-007/FR-011/FR-012/FR-013); `double` throughout the lab, no `float` audio boundary (FR-014); the frozen `component-abstractions` vocabulary is **not** modified (FR-004); the lab solver stays **bounded single-port, single-nonlinearity-location — must not grow into MNA** (FR-012).

**Scale/Scope**: three v1 topologies (symmetric shunt, asymmetric shunt, series); one transient solver templated on `<MaxNodes, MaxComponents, MaxDiodes>` (default `MaxDiodes=4`, augmented capacity `MaxComponents + 2·MaxDiodes`); two solver sanity checks (linear RC step response, DC-limit bisection oracle) + per-clipper invariants; templated capacities sized per topology (small — order 10s of nodes/components).

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1 design. All gates pass; no Complexity-Tracking entries required.*

| Principle | Status | How this feature satisfies it |
|---|---|---|
| I. Commit & push early/often | ✅ | Atomic commits per artifact/story; pushed promptly (already so through design → spec → clarify → plan). |
| II. No git hooks | ✅ | No hooks added or depended on; gates are the explicit `/speckit-*` + build steps. |
| III. Descriptive names | ✅ | Spec dir `diode-clippers`, subfolder `circuit/diode-clipper/`, headers `diode-clipper.h` / `clipper-config.h` / `transient-clipper.h` — named for what they are, no numeric prefixes. |
| IV. Platform-independent core, thin adapters | ✅ | The builder headers depend only on the standard library + the frozen vocabulary; **solvers are the adapters** — the transient solver is isolated in the lab, and the primitive has no lab include (FR-019). |
| V. No fallbacks / mock outside tests | ✅ | Bad BOM → `std::invalid_argument`; `dt ≤ 0` / singular / ≥2-interacting-nonlinearity → descriptive throw; Newton non-convergence → a reported status, never a fabricated output (FR-007/FR-011/FR-012/FR-013). |
| VI. Real-time safety | ✅ | The builder is fixed-capacity/heap-free (SC-008); the solve path is `std::array`-backed, no heap. There is no audio-callback path here; the solver is a host-only validation artifact. |
| VII. Strict typing & small modules | ✅ | No inheritance/virtual dispatch; plain value structs + templated free/solver types over `std::variant`; per-file ≤~500 lines (the solver is split if it approaches the ceiling). No `any`/unchecked casts. |
| VIII. Test the core host-side | ✅ | `tests/core/` Tier-1 (builder topology, no solver) + Tier-2 (solver sanity + invariants + non-convergence); the lab harness mirrors Tier-2. |
| IX. Progressive layered architecture | ✅ | Lab (transient solver + harness) → primitive (builders); the solver is retained reference, not disposable; isolation is a verified outcome (SC-007). |
| X. Measurable engineering | ✅ | Objective validation: exact RC step to ~1e-9; DC-limit vs independent bisection oracle to ~1e-6; symmetry / saturation / passivity / monotonic reactive-signature invariants; a verified non-convergence report (SC-001..008). |
| XI. One concept at a time | ✅ | The single new idea is **the diode clipping stage** (builders + a bounded reactive-nonlinear transient solve); MNA/WDF/realtime/op-amp/named-products stay deferred to their own phases/features. |

**Result: PASS (pre-Phase-0 and post-Phase-1).** No violations; Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/diode-clippers/
├── plan.md              # This file
├── research.md          # Phase 0 output — topology/BOM, MaxDiodes sizing, timestep/Newton-loop-separation, oracle decisions
├── data-model.md        # Phase 1 output — clipper BOM/config structs, Netlist sizing, NewtonStatus, oracle
├── quickstart.md        # Phase 1 output — how to build + run the validations
├── contracts/           # Phase 1 output — the primitive + lab contracts
│   ├── diode-clipper-builder.md   # the three builders + frozen-vocabulary + isolation contract
│   └── transient-clipper.md       # lab TransientClipper solver contract: loop separation, MaxDiodes, non-convergence, no-MNA boundary
├── checklists/
│   └── requirements.md  # spec quality checklist (all green)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── primitives/
│   ├── README.md                          # UPDATE: register circuit/diode-clipper/ + the three builders
│   └── circuit/                           # EXISTING category (component-abstractions)
│       ├── components.h  netlist.h  node.h  models/   # FROZEN — consumed, not modified (FR-004)
│       └── diode-clipper/                 # NEW — the builder primitive
│           ├── clipper-config.h           # per-topology BOM/value structs + diode-population config (plain aggregates)
│           └── diode-clipper.h            # symmetricShuntClipper(), asymmetricShuntClipper(), seriesClipper() -> Netlist + port nodes
└── labs/
    └── diode-clippers/                    # NEW — host-only, non-normative
        ├── README.md                      # the load-bearing-boundary note (mirrors component-abstractions / passive-tone-stacks)
        ├── solver/
        │   └── transient-clipper.h        # TransientClipper<MaxNodes,MaxComponents,MaxDiodes>: BE companions + separated timestep/Newton loops
        └── harness/
            └── diode-clippers-harness.cpp # PASS/FAIL: RC step + DC-limit oracle + symmetry/saturation/passivity/reactive-signature + non-convergence

tests/
└── core/
    ├── diode-clipper-builder-test.cpp     # Tier-1: builder topology / prepare()-valid / counts / frozen-vocabulary / no lab include (no solver)
    └── diode-clipper-transient-test.cpp   # Tier-2: transient sanity + DC-limit oracle + assembled invariants + explicit non-convergence (shares harness assertions)
```

**Structure Decision**: single-project DSP library. The **builders are portable primitive code** under a new `core/primitives/circuit/diode-clipper/` subfolder (they depend only on the frozen `circuit/` vocabulary + `Netlist`, so Phase-5/6 solvers adapt them), and the **transient nonlinear solver + harness are host-only lab code** under a new `core/labs/diode-clippers/`. This mirrors the `component-abstractions` / `passive-tone-stacks` primitive↔lab split exactly, including the isolation guarantee (delete the lab → the primitive + Tier-1 tests still build; FR-019 / SC-007). The frozen `circuit/` vocabulary is consumed unchanged (FR-004).

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — research.md**: resolve the per-topology node/BOM layout (symmetric shunt: series R → antiparallel pair to ground + `Cf` across the diodes; asymmetric shunt: 2-up/1-down population; series: input coupling `Cc` → inline diodes → R-to-ground), the `MaxDiodes=4` sizing and augmented-capacity `MaxComponents + 2·MaxDiodes`, the **timestep/Newton loop-separation** mechanism (why calling the history-advancing `LinearSolver::solve()` inside Newton corrupts reactive history, and how the separated structure avoids it), the DC-steady-state driving method (run-to-settle vs cap-open) and the independent bisection oracle per topology, and the 1 kHz / 100 kHz / >5 kHz reactive-signature measurement.
- **Phase 1 — data-model.md, contracts/, quickstart.md**: the value types (per-topology BOM/config structs, `NewtonStatus`), the builder + transient-solver contracts, and the build/run guide for the two test tiers + harness.
- **Phase 2 — tasks.md**: produced by `/speckit-tasks` (NOT here), ordered by user story (US1 builders → US2 transient solver → US3 assembled invariants) with the isolation check and the non-convergence test as explicit tasks.

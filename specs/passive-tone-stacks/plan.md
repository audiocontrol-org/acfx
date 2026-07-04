> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: passive-tone-stacks

**Branch**: `passive-tone-stacks` | **Date**: 2026-07-04 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/passive-tone-stacks/spec.md`; design record `docs/superpowers/specs/2026-07-04-passive-tone-stacks-design.md` (approved, design-to-spec exit gate 7/7).

## Summary

Compose the frozen `component-abstractions` vocabulary into a **solver-neutral passive tone-stack builder** — the second deliverable of Phase 4 and the first that yields a musically recognizable circuit. Two header-only builders in the portable primitive (`toneStackFMV`, `toneStackBaxandall`) take a bill of materials + pot positions and return a `Netlist<MaxNodes, MaxComponents>` of ordinary `Resistor`/`Capacitor`/`VoltageSource` values — **topology only, no solve, no audio path**. The **potentiometer is build-time math** (`taper.h`): `wiper(rTrack, pos, taper)` maps position through a taper law (Linear / Log) to two `Resistor` legs, each floored at a fixed **10 Ω** modelled contact/end resistance; the following-stage load is an **explicit `rLoad`** to ground. Frequency response is validated in a **host-only lab** (`core/labs/passive-tone-stacks/`) by a new **complex `.ac` solver** — `solveAC(netlist, ω, in, out)` stamps admittances at `jω` (`R→1/R`, `C→jωC`, `L→1/(jωL)`) and Gaussian-eliminates over `std::complex<double>`, yielding the exact continuous-time `H(jω)`, cross-checked against the independently-derived analytic (Duncan) transfer function to **0.1 dB** on a ~10-points/decade log grid over 20 Hz–20 kHz. The seam from `component-abstractions` holds: **the builder produces topology, the solver reads each component's admittance and never re-derives its law** — so Phase-5 MNA / Phase-6 WDF adapt the same builders unchanged.

## Technical Context

**Language/Version**: C++17 for the primitive headers (Teensy target lacks C++20); the lab AC solver + harness may use C++20, as existing labs do (root `CMakeLists.txt`).

**Primary Dependencies**: none beyond the standard library — the frozen `component-abstractions` primitive (`core/primitives/circuit/`: `components.h`, `netlist.h`, `node.h`, `models/`) and `dsp/span.h`. The lab adds `<complex>`. No JUCE / libDaisy / Teensy dependency (Constitution IV).

**Storage**: N/A (a compute/library feature; no persistence).

**Testing**: host-side unit tests under `tests/core/` (GoogleTest-style, registered in `tests/CMakeLists.txt`) — Tier-1 wiper/taper + builder-topology tests (no solver), Tier-2 `tone-stack-ac-test.cpp` (AC solver sanity + FMV/Baxandall analytic match); a lab harness executable under `core/labs/passive-tone-stacks/harness/` (registered in root `CMakeLists.txt`) mirroring the Tier-2 assertions with PASS/FAIL prints.

**Target Platform**: platform-independent core; the `tone-stack/` primitive must compile host-side and be C++17-clean for the Teensy target. The lab is host-only. No hardware in the loop.

**Project Type**: single-project DSP library (the acfx three-layer core: `labs/ → primitives/ → effects/`).

**Performance Goals**: the builder is **zero-allocation** (fixed compile-time `Netlist` capacities; no `new`/`delete`/`std::vector` — SC-007). The lab AC solver is a validation artifact: its target is "correct within the 0.1 dB analytic tolerance," not a throughput number. No RT-audio path exists in this deliverable.

**Constraints**: no heap in the builder path (Constitution VI); files ~300–500 lines (Constitution VII); no fallbacks/mock outside tests — descriptive throws on bad input and on a singular AC system (Constitution V, FR-010/FR-012); `double`/`std::complex<double>` in the lab, no `float` audio boundary (FR-015); the frozen `component-abstractions` vocabulary is **not** modified (FR-003); the lab **must not** grow into MNA/Newton (FR-013).

**Scale/Scope**: two v1 topologies (FMV 3-band, Baxandall/James 2-band); two taper laws (Linear, Log); two sanity networks (RC low-pass, resistive divider) + FMV + Baxandall validated over the audio band at ≥3 control settings each; templated capacities sized per topology (small — order 10s of nodes/components).

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1 design. All gates pass; no Complexity-Tracking entries required.*

| Principle | Status | How this feature satisfies it |
|---|---|---|
| I. Commit & push early/often | ✅ | Atomic commits per artifact/story; pushed promptly (already so through design → spec → clarify → plan). |
| II. No git hooks | ✅ | No hooks added or depended on; gates are the explicit `/speckit-*` + build steps. |
| III. Descriptive names | ✅ | Spec dir `passive-tone-stacks`, subfolder `circuit/tone-stack/`, headers `taper.h` / `tone-stack.h` / `ac-solver.h` — named for what they are, no numeric prefixes. |
| IV. Platform-independent core, thin adapters | ✅ | The builder headers depend only on the standard library + the frozen vocabulary; **solvers are the adapters** — the AC solver is isolated in the lab, and the primitive has no lab include (FR-016). |
| V. No fallbacks / mock outside tests | ✅ | Bad control/BOM input → `std::invalid_argument`; a singular AC system → `std::runtime_error` naming `ω`; no silent clamp, no fabricated `H` (FR-010/FR-012). The 10 Ω floor is a modelled physical value, not a fallback (FR-008). |
| VI. Real-time safety | ✅ | The builder is fixed-capacity/heap-free (SC-007). There is no audio-callback path here; the lab solver is a host-only validation artifact. |
| VII. Strict typing & small modules | ✅ | No inheritance/virtual dispatch; plain value structs + free functions; per-file ≤~500 lines. No `any`/unchecked casts. |
| VIII. Test the core host-side | ✅ | `tests/core/` Tier-1 (wiper/topology, no solver) + Tier-2 (AC sanity + analytic match); the lab harness mirrors Tier-2. |
| IX. Progressive layered architecture | ✅ | Lab (AC solver + harness) → primitive (builders); the AC solver is retained reference, not disposable; isolation is a verified outcome (SC-006). |
| X. Measurable engineering | ✅ | Objective validation: exact RC/divider closed-form to ~1e-9; FMV/Baxandall to 0.1 dB vs analytic on a defined grid; monotonic scoop/treble invariants (SC-001..007). |
| XI. One concept at a time | ✅ | The single new idea is **the pot-driven passive frequency-shaping sub-circuit** (build-time pots + AC reading); MNA/WDF/realtime lowering stay deferred to their own phases. |

**Result: PASS (pre-Phase-0 and post-Phase-1).** No violations; Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/passive-tone-stacks/
├── plan.md              # This file
├── research.md          # Phase 0 output — topology/taper/AC-solve decisions
├── data-model.md        # Phase 1 output — Taper, WiperSplit, FMV/Baxandall Values+Controls, Netlist sizing, H(jω)
├── quickstart.md        # Phase 1 output — how to build + run the validations
├── contracts/           # Phase 1 output — the primitive + lab contracts
│   ├── tone-stack-builder.md   # toneStackFMV / toneStackBaxandall + frozen-vocabulary + isolation contract
│   ├── potentiometer.md        # wiper() / rheostat() / taper-law / end-resistance-floor contract
│   └── ac-solver.md            # lab solveAC(netlist,ω,in,out) contract + singular-system refusal + no-MNA boundary
├── checklists/
│   └── requirements.md  # spec quality checklist (all green)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── primitives/
│   ├── README.md                          # UPDATE: register circuit/tone-stack/ + the two builders
│   └── circuit/                           # EXISTING category (component-abstractions)
│       ├── components.h  netlist.h  node.h  models/   # FROZEN — consumed, not modified (FR-003)
│       └── tone-stack/                    # NEW — the builder primitive
│           ├── taper.h                    # Taper{Linear,Log}, WiperSplit, wiper(), rheostat(), 10Ω floor
│           └── tone-stack.h              # FMVValues/Controls, BaxandallValues/Controls, toneStackFMV(), toneStackBaxandall()
└── labs/
    └── passive-tone-stacks/               # NEW — host-only, non-normative
        ├── README.md                      # the load-bearing-boundary note (mirrors component-abstractions)
        ├── solver/
        │   └── ac-solver.h                # solveAC(netlist,ω,in,out) -> std::complex<double>; complex Gaussian elim
        └── harness/
            └── passive-tone-stacks-harness.cpp   # PASS/FAIL |H(f)|,∠H(f): RC/divider sanity + FMV + Baxandall vs analytic

tests/
└── core/
    ├── tone-stack-taper-test.cpp          # Tier-1: wiper/taper math (no solver)
    ├── tone-stack-builder-test.cpp        # Tier-1: builder topology / prepare()-valid / counts (no solver)
    └── tone-stack-ac-test.cpp             # Tier-2: solveAC sanity + FMV/Baxandall analytic match (shares harness assertions)
```

**Structure Decision**: single-project DSP library. The **builder is portable primitive code** under a new `core/primitives/circuit/tone-stack/` subfolder (it depends only on the frozen `circuit/` vocabulary + `Netlist`, so Phase-5/6 solvers adapt it), and the **complex `.ac` solver + harness are host-only lab code** under a new `core/labs/passive-tone-stacks/`. This mirrors the `component-abstractions` primitive↔lab split exactly, including the isolation guarantee (delete the lab → the primitive + Tier-1 tests still build; FR-016 / SC-006). The frozen `circuit/` vocabulary is consumed unchanged (FR-003).

## Complexity Tracking

> No Constitution Check violations — table intentionally empty.

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — research.md**: resolve the topology/BOM decisions (FMV per-pot wiring: bass/treble as 3-terminal dividers, mid as a rheostat-to-ground; the passive Baxandall/James topology), the Log taper-law form, the exact per-topology `Netlist` capacities, and the complex-Gaussian-elimination approach reusing the `LinearSolver` structure over `std::complex<double>`.
- **Phase 1 — data-model.md, contracts/, quickstart.md**: the value types (Taper, WiperSplit, the two Values/Controls structs), the builder + potentiometer + AC-solver contracts, and the build/run guide for the two test tiers + harness.
- **Phase 2 — tasks.md**: produced by `/speckit-tasks` (NOT here), ordered by user story (US1 builder → US2 pot → US3 AC validation) with the isolation check as an explicit task.

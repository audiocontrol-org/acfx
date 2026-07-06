> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: opamp-stages

**Branch**: `opamp-stages` | **Date**: 2026-07-06 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/opamp-stages/spec.md`; design record `docs/superpowers/specs/2026-07-05-opamp-stages-design.md` (approved, design-to-spec exit gate 7/7).

## Summary

Extend the circuit vocabulary with **one** new element — the ideal op-amp (`OpAmp`, a nullor) — and compose it into **solver-neutral op-amp-stage builders**, the active-element deliverable of Phase 4 and the first to *extend* the `component-abstractions` vocabulary rather than only compose it. Four header-only builders in the portable primitive (`nonInvertingGain`, `invertingGain`, `activeFirstOrder`, `opAmpDiodeClipper`) take a bill of materials and return a `Netlist<MaxNodes, MaxComponents>` of ordinary `Resistor`/`Capacitor`/`Diode`/`VoltageSource` values **plus the new `OpAmp`** — topology only, no solve, no audio path. The active behavior is solved and validated in a **host-only lab** (`core/labs/opamp-stages/`) by a **new nullor-augmented solver** that realizes each ideal op-amp honestly: **one extra unknown** (the norator output branch current) and **one extra constraint row** (`V(inPlus) − V(inMinus) = 0`, the virtual short) appended per op-amp to the reduced nodal system — the first, bounded taste of Modified Nodal Analysis. The solver is a **validation ladder**: (a) the resistive stages match their analytic closed-form gains `1 + Rf/Rg` / `−Rf/Rin` to ~1e-9 (the nullor stamp is exact); (b) the active first-order low-pass (feedback `Cf ∥ Rf`) matches its analytic backward-Euler response to ~1e-9 (nullor+reactive exact **before** any nonlinearity — reusing the frozen `capacitor.h` `companion()` hook); (c) the op-amp feedback-diode clipper (the TS808 core) reuses `diode-clippers`' **separated timestep/Newton structure** wrapped around the augmented solve, and its DC limit is cross-checked against an **independent bisection oracle** of the KCL equation at the virtual-short node to ~1e-6. Then the assembled-clipper invariants: forward saturation near the feedback-diode drop, symmetry/asymmetry per population, passivity of the passive sub-network, and the pinned reactive signature — larger `Cf` ⇒ less post-clip HF for a fixed 1 kHz drive at `dt = 1e-5 s`. Non-convergence is a first-class **test-surfaced** contract. The boundary is enforced by three checkable tripwires — the augmentation is **`OpAmp`-specific**, the **single-nonlinearity-location refusal** carries forward from `diode-clippers`, and the augmentation is **one row/column per op-amp sized at instantiation** — so the lab solver gains the nullor stamp without becoming general Phase-5 MNA and **never modifies** the existing `component-abstractions` `LinearSolver`. The op-amp is **never** approximated as a large-but-finite-gain source (the gmin fallback `sources.h` forbids). The `component-abstractions` seam holds: the builder produces topology; the solver reads each component's own physics and never re-derives a law — so Phase-5 MNA / Phase-6 WDF adapt the same builders (and the same `OpAmp`) unchanged.

## Technical Context

**Language/Version**: C++17 for the primitive headers (Teensy target lacks C++20) — including the `OpAmp` element added to the frozen `circuit/` vocabulary; the lab nullor-augmented solver + harness may use C++20, as existing labs do (root `CMakeLists.txt`).

**Primary Dependencies**: none beyond the standard library — the `component-abstractions` primitive (`core/primitives/circuit/`: `components.h`, `netlist.h`, `node.h`, `models/` incl. `diode.h`, `capacitor.h`, `companion.h`, `sources.h`) and `dsp/span.h`. The lab reuses the `component-abstractions` lab solver structure (`core/labs/component-abstractions/solver/` — `LinearSolver`'s reduced-nodal Gaussian elimination + the `NewtonClipper` companion-linearization pattern) and `diode-clippers`' transient timestep/Newton coupling as references. No JUCE / libDaisy / Teensy dependency (Constitution IV).

**Storage**: N/A (a compute/library feature; no persistence).

**Testing**: host-side unit tests under `tests/core/` (doctest, registered in `tests/CMakeLists.txt`) — Tier-1 `opamp-stage-builder-test.cpp` (builder topology / prepare()-valid / counts / vocabulary incl. `OpAmp` / no solver), Tier-2 `opamp-stage-solve-test.cpp` (nullor-augmented solver ladder + assembled-clipper invariants + the explicit non-convergence test; split if it approaches the ~500-line ceiling); a lab harness executable under `core/labs/opamp-stages/harness/` (registered in root `CMakeLists.txt`) mirroring the Tier-2 assertions with PASS/FAIL prints.

**Target Platform**: platform-independent core; the `opamp-stage/` primitive **and the `OpAmp` element in `circuit/`** must compile host-side and be C++17-clean for the Teensy target. The lab is host-only. No hardware in the loop.

**Project Type**: single-project DSP library (the acfx three-layer core: `labs/ → primitives/ → effects/`).

**Performance Goals**: the builder is **zero-allocation** (fixed compile-time `Netlist` capacities; no `new`/`delete`/`std::vector` — SC-009). The lab nullor-augmented solver is a validation artifact and heap-free on the solve path (fixed `std::array`-backed augmented system); its target is "correct within the stated tolerances," not a throughput number. No RT-audio path exists in this deliverable.

**Constraints**: no heap in the builder or solve path (Constitution VI); files ~300–500 lines (Constitution VII); no fallbacks/mock outside tests — descriptive throws on bad BOM input, floating op-amp input / missing feedback path, `dt ≤ 0`, a **singular augmented system** (the authoritative well-posedness gate), and an out-of-scope (≥2 interacting nonlinearity) netlist; **non-convergence reported, never faked** (Constitution V, FR-010/FR-014/FR-015/FR-016); the op-amp **never** a large-but-finite-gain / large-conductance approximation (FR-017); `double` throughout the lab, no `float` audio boundary (FR-018); the lab solver stays **bounded — nullor augmentation only — and must not grow into general MNA or modify `LinearSolver`** (FR-015).

**Scale/Scope**: **one** new vocabulary element (`OpAmp`); four v1 builders (non-inverting gain, inverting gain, active first-order low-pass, op-amp+diode+cap clipper); one nullor-augmented solver templated per topology (augmented size `nodes + non-ground reductions + numOpAmps`, and for the clipper `+ 2·MaxDiodes`); a three-rung validation ladder (analytic gains, analytic first-order response, DC-limit bisection oracle) + per-clipper invariants; templated capacities sized per topology (small — order 10s of nodes/components).

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1 design. All gates pass; no Complexity-Tracking entries required.*

| Principle | Status | How this feature satisfies it |
|---|---|---|
| I. Commit & push early/often | ✅ | Atomic commits per artifact/story; pushed promptly (already so through design → spec → clarify → plan). |
| II. No git hooks | ✅ | No hooks added or depended on; gates are the explicit `/speckit-*` + build steps. |
| III. Descriptive names | ✅ | Spec dir `opamp-stages`, subfolder `circuit/opamp-stage/`, headers `opamp-stage.h` / `opamp-config.h` / `opamp-stage-solver.h`, element `OpAmp` — named for what they are, no numeric prefixes. |
| IV. Platform-independent core, thin adapters | ✅ (with a deliberate, bounded vocabulary extension) | The `OpAmp` element and the four builders depend only on the standard library + the existing vocabulary; **solvers are the adapters** — the nullor-augmented solver is isolated in the lab, and the primitive has no lab include (FR-024). **This is the first feature to extend the `circuit/` vocabulary**: it adds exactly **one** element (`OpAmp`), a pure ideal-nullor value struct that is a constraint (no `admittance()`/`companion()`), C++17-clean and platform-independent like every other `Component` inhabitant. The "frozen vocabulary" discipline the composing features (`passive-tone-stacks`, `diode-clippers`) held was frozen *for them*; `opamp-stages` **is** the sanctioned extension deliverable (design D2), and the extension is minimal by construction — see Complexity Tracking. |
| V. No fallbacks / mock outside tests | ✅ | Bad BOM / floating input / missing feedback path → `std::invalid_argument`; `dt ≤ 0` / singular augmented system / ≥2-interacting-nonlinearity → descriptive throw; Newton non-convergence → a reported status, never a fabricated output; the op-amp is never a gmin/large-gain fallback (FR-010/FR-014/FR-015/FR-016/FR-017). |
| VI. Real-time safety | ✅ | The builder is fixed-capacity/heap-free (SC-009); the solve path is `std::array`-backed, no heap. There is no audio-callback path here; the solver is a host-only validation artifact. |
| VII. Strict typing & small modules | ✅ | No inheritance/virtual dispatch; plain value structs (`OpAmp` joins the `std::variant` `Component`) + templated free/solver types; per-file ≤~500 lines (the Tier-2 test / solver split if they approach the ceiling). No `any`/unchecked casts. |
| VIII. Test the core host-side | ✅ | `tests/core/` Tier-1 (builder topology incl. `OpAmp`, no solver) + Tier-2 (solver ladder + invariants + non-convergence); the lab harness mirrors Tier-2. |
| IX. Progressive layered architecture | ✅ | Lab (nullor-augmented solver + harness) → primitive (`OpAmp` element + builders); the solver is retained reference, not disposable; isolation is a verified outcome (SC-008). |
| X. Measurable engineering | ✅ | Objective validation: exact analytic gains + first-order response to ~1e-9; DC-limit vs independent bisection oracle to ~1e-6; saturation / symmetry / passivity / monotonic reactive-signature invariants; a verified non-convergence report; verified bounded charter (SC-001..009). |
| XI. One concept at a time | ✅ | The single new idea is **the op-amp gain stage** (the `OpAmp` nullor element + builders + a bounded nullor-augmented solve); general MNA / WDF / realtime / op-amp non-ideality / named-products stay deferred to their own phases/features (FR-025). |

**Result: PASS (pre-Phase-0 and post-Phase-1).** No violations. The one notable move — extending the `circuit/` vocabulary — is the deliberate charter of this deliverable (design D2), is minimal (one element), and is recorded in Complexity Tracking for visibility rather than as a violation.

## Project Structure

### Documentation (this feature)

```text
specs/opamp-stages/
├── plan.md              # This file
├── research.md          # Phase 0 output — OpAmp element shape, nullor MNA augmentation math, per-topology node/BOM layout, DC-limit oracle, well-posedness gate
├── data-model.md        # Phase 1 output — OpAmp struct, per-topology BOM/config structs, Netlist sizing, augmented-system layout, NewtonStatus, oracle
├── quickstart.md        # Phase 1 output — how to build + run the validations
├── contracts/           # Phase 1 output — the primitive + lab contracts
│   ├── opamp-element.md          # the OpAmp vocabulary element: nullor semantics, classifiers, terminalsOf/contributesConductivePath extension
│   ├── opamp-stage-builder.md    # the four builders + vocabulary + isolation contract
│   └── opamp-stage-solver.md     # lab nullor-augmented solver contract: augmentation, loop reuse, well-posedness authority, non-convergence, no-MNA tripwires
├── checklists/
│   └── requirements.md  # spec quality checklist (all green)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── primitives/
│   ├── README.md                          # UPDATE: register circuit/opamp-stage/ + the four builders + the OpAmp element
│   └── circuit/                           # EXISTING category (component-abstractions) — EXTENDED here (one element)
│       ├── components.h                    # UPDATE: add OpAmp to the Component variant + classifiers (isLinear/isNonlinear/isReactive)
│       ├── netlist.h                       # UPDATE: terminalsOf + contributesConductivePath recognize OpAmp (output excluded)
│       ├── node.h  models/                 # consumed unchanged
│       ├── models/
│       │   └── opamp.h                      # NEW — the OpAmp element value struct (ideal nullor; constraint, no admittance/companion)
│       └── opamp-stage/                    # NEW — the builder primitive
│           ├── opamp-config.h              # per-topology BOM/value structs (plain aggregates)
│           └── opamp-stage.h               # nonInvertingGain(), invertingGain(), activeFirstOrder(), opAmpDiodeClipper() -> Netlist + port nodes
└── labs/
    └── opamp-stages/                       # NEW — host-only, non-normative
        ├── README.md                       # the load-bearing-boundary note (mirrors component-abstractions / diode-clippers)
        ├── solver/
        │   └── opamp-stage-solver.h        # nullor-augmented solver: per-op-amp row/col augmentation + BE companions + (for the clipper) separated timestep/Newton loops
        └── harness/
            └── opamp-stages-harness.cpp    # PASS/FAIL: analytic gains + first-order response + DC-limit oracle + saturation/symmetry/passivity/reactive-signature + non-convergence
tests/
└── core/
    ├── opamp-stage-builder-test.cpp        # Tier-1: builder topology / prepare()-valid / counts / vocabulary incl OpAmp / no lab include (no solver)
    └── opamp-stage-solve-test.cpp          # Tier-2: solver ladder (gains, first-order, DC-limit oracle) + assembled invariants + explicit non-convergence (may split under the line budget)
```

**Structure Decision**: single-project DSP library. The **`OpAmp` element extends the frozen `circuit/` vocabulary** (a new `models/opamp.h` + the `components.h`/`netlist.h` classifier/terminal updates — the sanctioned one-element extension, design D2), the **four builders are portable primitive code** under a new `core/primitives/circuit/opamp-stage/` subfolder (depending only on the `circuit/` vocabulary + `Netlist`, so Phase-5/6 solvers adapt them), and the **nullor-augmented solver + harness are host-only lab code** under a new `core/labs/opamp-stages/`. This mirrors the `component-abstractions` / `diode-clippers` primitive↔lab split exactly, including the isolation guarantee (delete the lab → the primitive + Tier-1 tests still build; FR-024 / SC-008). The existing `component-abstractions` `LinearSolver` is **not** modified (FR-015).

## Complexity Tracking

> One deliberate, bounded deviation from the composing-feature norm — recorded for visibility, not a Constitution violation.

| Move | Why needed | Simpler alternative rejected because |
|------|-----------|--------------------------------------|
| Extend the frozen `circuit/` vocabulary with one new element (`OpAmp`) | The op-amp is an active element with no passive-network equivalent; every downstream op-amp feature (`tube-screamer`, `rat-distortion`, `neve-preamp`) needs it, and `diode-clippers` explicitly deferred it to this deliverable. It cannot be composed from R/C/L/sources/diode. | Faking the op-amp from existing elements (a large-gain VCVS / large-conductance divider) is the gmin fallback `sources.h` forbids — numerically fragile and constitution-violating (Principle V). Extending by exactly one minimal ideal-nullor value struct is the honest, minimal move. |
| Add a bounded nullor MNA augmentation to a **new** lab solver | An ideal op-amp is a constraint + unknown branch current, not a conductance; solving it honestly requires one augmented row/column. | Growing the existing `LinearSolver` into MNA violates its documented "must never grow into MNA" charter; a general MNA engine is Phase-5 scope. The augmentation is held to nullor-only by three checkable tripwires (FR-015). |

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — research.md**: resolve the `OpAmp` element shape (three node handles, constraint semantics, classifier bucket, `contributesConductivePath` exclusion of the output); the **nullor MNA augmentation** math (the bordered `[[G, B],[C, 0]]` system: `B` places the norator output current, `C` imposes `V+ − V− = 0`) and how it slots into the lab's existing reduced-nodal Gaussian elimination; the per-topology node/BOM layout for the four builders (non-inverting `Rf`/`Rg`; inverting `Rin`/`Rf`; active first-order `Rin`/`Rf ∥ Cf`; clipper `Rf` + antiparallel diode pair + `Cf` across the feedback); the DC-steady-state driving method and the independent bisection oracle at the virtual-short node; the well-posedness gate (augmented-system singularity authoritative, connectivity pre-filter conservative — verified sound for the four exemplars); and the 1 kHz / 100 kHz / >5 kHz reactive-signature measurement.
- **Phase 1 — data-model.md, contracts/, quickstart.md**: the value types (`OpAmp`, per-topology BOM/config structs, `NewtonStatus`), the element + builder + solver contracts, and the build/run guide for the two test tiers + harness.
- **Phase 2 — tasks.md**: produced by `/speckit-tasks` (NOT here), ordered by user story (US1 element + builders → US2 nullor-augmented solver → US3 assembled invariants) with the isolation check, the bounded-charter tripwire tests, and the non-convergence test as explicit tasks.

# Phase 1 — Data Model: opamp-stages

The value types and their validation/relationships. All are plain aggregates or templated value types — no inheritance, no virtual dispatch, no heap. `double` for physical quantities; `NodeId` (int) for node handles.

---

## Vocabulary element (primitive — `core/primitives/circuit/models/opamp.h`)

### `OpAmp` — the ideal op-amp (nullor)

| Field | Type | Meaning |
|-------|------|---------|
| `inPlus` | `NodeId` | non-inverting input node |
| `inMinus` | `NodeId` | inverting input node |
| `out` | `NodeId` | output node (norator-driven) |

- **Semantics:** virtual short `V(inPlus) = V(inMinus)`; zero input current; output current is a solve unknown. **Linear**; no rails/GBW/slew/finite-gain/offset (design D1).
- **No physics hooks:** no `admittance()`, no `companion()` — it is a *constraint*, not a conductance (mirrors `VoltageSource`).
- **Classifiers** (`components.h`): `isLinear = true`, `isNonlinear = false`, `isReactive = false`.
- **Validation:** node handles must be valid for the netlist; `inPlus`, `inMinus`, `out` should be distinct (a degenerate op-amp with tied input/output is an ill-posed build → `std::invalid_argument`). No parameter fields to validate (there are none).
- **Netlist integration** (`netlist.h`): `terminalsOf` → `{inPlus, inMinus}`; `contributesConductivePath` → `false` (output excluded from reachability; R1/R5).

> This is the **only** new `Component` variant this feature introduces (design D2, FR-001/FR-007).

---

## Builder BOM/config structs (primitive — `core/primitives/circuit/opamp-stage/opamp-config.h`)

Plain aggregates; every field a positive `double` unless noted. Non-positive values (or a diode parameter ≤ 0, or a diode `count` of 0) → `std::invalid_argument` on the build thread.

### `NonInvertingGainBom`
| Field | Type | Meaning |
|-------|------|---------|
| `Rf` | `double` | feedback resistor (out → inMinus), Ω |
| `Rg` | `double` | ground-leg resistor (inMinus → gnd), Ω |
| `vin` | `double` | drive source amplitude at `inPlus`, V (test excitation) |

Closed-loop gain `1 + Rf/Rg`.

### `InvertingGainBom`
| Field | Type | Meaning |
|-------|------|---------|
| `Rin` | `double` | input resistor (vin → inMinus), Ω |
| `Rf` | `double` | feedback resistor (out → inMinus), Ω |
| `vin` | `double` | drive source amplitude, V |

Gain `−Rf/Rin`.

### `ActiveFirstOrderBom`  *(inverting first-order low-pass — OQ4)*
| Field | Type | Meaning |
|-------|------|---------|
| `Rin` | `double` | input resistor, Ω |
| `Rf` | `double` | feedback resistor (out → inMinus), Ω |
| `Cf` | `double` | feedback capacitor in parallel with `Rf`, F |
| `vin` | `double` | drive amplitude, V |

DC gain `−Rf/Rin`; corner `1/(2π·Rf·Cf)`.

### `OpAmpDiodeClipperBom`  *(TS808 core)*
| Field | Type | Meaning |
|-------|------|---------|
| `Rin` | `double` | input resistor (vin → inMinus), Ω |
| `Rf` | `double` | feedback resistor setting the clean-gain floor, Ω |
| `Cf` | `double` | feedback capacitor across the diode network, F |
| `diode` | `DiodeParams` | Shockley `Is`, `n`, `Vt` (from `component-abstractions`) |
| `nUp` | `int` | forward diode count in the antiparallel string |
| `nDown` | `int` | reverse diode count (symmetric ⇔ `nUp == nDown`) |
| `vin` | `double` | drive amplitude, V |

Exactly one nonlinearity location (the feedback antiparallel diode string). `nUp`/`nDown` ≥ 1; asymmetric ⇒ DC offset / even harmonics.

---

## Netlist sizing (primitive)

Each builder returns a `Netlist<MaxNodes, MaxComponents>` sized per topology, plus `inNode` / `outNode` handles. Small (order 10s). Compile-time capacities — heap-free (SC-009). Per-builder aliases (e.g. `using NonInvertingGainNet = Netlist<...>;`) sized to the exact element count so the returned netlist is minimal.

---

## Augmented-system layout (lab — `core/labs/opamp-stages/solver/opamp-stage-solver.h`)

The bordered system `[[G, B],[C, 0]]·[v; j] = [i; 0]` (R2), backed by `std::array` sized at instantiation:

| Quantity | Size |
|----------|------|
| reduced node unknowns `v` | `nodes − 1 − (fixed-node reductions)` |
| op-amp branch unknowns `j` | `numOpAmps` (one per `OpAmp`) |
| **linear augmented dimension** | `reducedNodes + numOpAmps` |
| **clipper augmented dimension** | `reducedNodes + numOpAmps + 2·MaxDiodes` (per-iteration diode companion stamps: each diode → `Resistor` + `CurrentSource`) |

No dynamic growth; the dimension is a compile-time function of the template parameters (R6 tripwire iii).

### `NewtonStatus`  *(clipper solve only)*
| Field | Type | Meaning |
|-------|------|---------|
| `converged` | `bool` | did the inner Newton meet tolerance within the budget |
| `iterations` | `int` | iterations taken this timestep |
| `voltageResidual` | `double` | final `|Δv|` |
| `currentResidual` | `double` | final KCL current residual |

Non-convergence is reported here, never swallowed (FR-014). Defaults: `maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12`.

---

## Oracles & measurement types (lab, validation)

- **Analytic-gain oracle** — the scalars `1 + Rf/Rg`, `−Rf/Rin` (rungs a).
- **First-order response oracle** — the closed-form backward-Euler recurrence of `−Rf/Rin` with corner `1/(Rf·Cf)` (rung b).
- **DC-limit bisection oracle** — an independent bisection root-find of the clipper's KCL at the virtual-short node: the input current `vin/Rin` balanced by the antiparallel Shockley feedback current at the solved feedback voltage, op-amp holding `inMinus = 0` (rung c), to ~1e-6.
- **Reactive signature** — post-clip output spectral energy above a 5 kHz cutoff as a function of `Cf`, for a fixed 1 kHz drive at `dt = 1e-5 s`; the pinned strictly-monotonic-decreasing invariant (FR-022 / SC-005).

---

## Relationships & flow

```
BOM struct ──(builder, pure fn)──▶ Netlist{components incl OpAmp} + {inNode,outNode}
                                        │
                                        ▼
                        nullor-augmented solver (lab)
                          linear stages: single bordered solve
                          active first-order: + BE companion, history advanced once/step
                          clipper: separated timestep/Newton around the bordered solve → NewtonStatus
                                        │
                                        ▼
             validation: analytic gains ~1e-9 · first-order ~1e-9 · DC oracle ~1e-6
                        · saturation/symmetry/passivity · reactive signature · non-convergence
```

The seam: the builder emits topology; the solver reads each component's own physics (`Diode::evaluate`, `capacitor.h::companion`, and the `OpAmp` constraint) and never re-derives a law — so Phase-5 MNA / Phase-6 WDF adapt the same builders and the same `OpAmp` unchanged.

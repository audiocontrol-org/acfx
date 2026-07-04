# Phase 1 Data Model — passive-tone-stacks

The value types the builders and the lab AC solver exchange. Everything is a plain value struct or a free function result — no inheritance, no heap, no hidden state (Constitution VII). All resistances/capacitances are `double` (SI units: ohms, farads); positions are dimensionless `double` in `[0, 1]`.

## Constants

| Name | Value | Meaning |
|---|---|---|
| `kEndResistanceOhms` | `10.0` | Fixed per-leg wiper contact/end-resistance floor (R4 / FR-008). |
| `kLogTaperBase` | `10.0` | Base of the exponential "audio" taper law (R3). |

## Taper (enum)

```
enum class Taper { Linear, Log };
```

- `Linear` — electrical fraction `f = pos`.
- `Log` — `f = (pow(kLogTaperBase, pos) - 1) / (kLogTaperBase - 1)` (audio taper; `f(0)=0`, `f(1)=1`, concave).
- **No `Antilog`** (D10) — absent, not a stub. Adding a case is a future change, not a dead branch.

## WiperSplit

```
struct WiperSplit { double rTop; double rBottom; };
```

The result of a 3-terminal divider wiper: `rTop` is the leg from the track's high end to the wiper, `rBottom` from the wiper to the low end. Invariants:

- Before flooring: `rTop + rBottom == rTrack`, `rBottom = f·rTrack`, `rTop = (1-f)·rTrack`.
- After flooring: each leg `= max(leg, kEndResistanceOhms)`; so both legs are `≥ 10 Ω` and the sum may exceed `rTrack` by up to one floor near an extreme (physically correct series end resistance).

## FMVValues / FMVControls

```
struct FMVValues {
    double r1;       // slope resistor (Ω)
    double c1, c2;   // treble / bass capacitors (F)  — basic Bassman form (no mid cap)
    double rTreble;  // treble pot track (Ω) — 3-terminal divider, wiper = output
    double rBass;    // bass pot track (Ω)   — rheostat (bass branch to ground)
    double rMid;     // mid pot track (Ω)     — rheostat (shunt to ground)
    double rLoad;    // following-stage input impedance to ground (Ω) — explicit (FR-009)
};
struct FMVControls { double bass; double mid; double treble; };  // each ∈ [0,1]
```

Validation (build thread, FR-010): every field `> 0`; every control `∈ [0,1]`; otherwise `std::invalid_argument` naming the offending field/control. No silent clamp.

**Node roles (FMV):** input (driven by the ideal `VoltageSource`), the treble-cap node (treble pot top), the slope-resistor junction (treble pot bottom), the output (treble wiper, into `rLoad`), the bass-cap node, and the bass/mid junction (mid rheostat to ground — the mid-scoop mechanism). The treble pot is a 3-terminal divider (`wiper()`); the bass and mid pots are rheostats (`rheostat()`). The node/component **counts** are fixed by the capacities below. (The mid cap of some variants is omitted in this basic Bassman form; exact vendor-BOM fidelity is the later `fender-tone-stack` feature.)

## BaxandallValues / BaxandallControls

```
struct BaxandallValues {
    double rBass;       // bass pot track (Ω) — 3-terminal divider
    double cBass;       // bass cap (F) — bypasses the bass pot top at HF (low shelf)
    double rBassOut;    // bass wiper -> output mixing resistor (Ω)
    double cTreble;     // treble cap (F) — couples HF into the treble pot (high shelf)
    double rTreble;     // treble pot track (Ω) — 3-terminal divider
    double rTrebleOut;  // treble wiper -> output mixing resistor (Ω)
    double rLoad;       // following-stage input impedance to ground (Ω) — explicit (FR-009)
};
struct BaxandallControls { double bass; double treble; };  // each ∈ [0,1] — linear taper
```

Validation identical in spirit to FMV (all `> 0`, controls `∈ [0,1]`, else `std::invalid_argument`). Baxandall/James pots are conventionally **linear** taper (R2).

## Netlist sizing (compile-time capacities)

Per-topology constants sized exactly to each bill of materials with small headroom; the builder emits a fixed component set so over-capacity cannot occur at runtime (and `Netlist::add`/`addNode` still throw `std::out_of_range` if a future edit exceeds them):

| Topology | Nodes used (incl. ground) | Components used | Capacities (with headroom) |
|---|---|---|---|
| FMV | 7 | 9: `VoltageSource` + `r1` + `c1`/`c2` + treble divider (2 legs) + bass rheostat + mid rheostat + `rLoad` | `kFmvNodes = 8`, `kFmvComponents = 12` |
| Baxandall | 6 | 10: `VoltageSource` + bass divider (2 legs) + `cBass` + `rBassOut` + `cTreble` + treble divider (2 legs) + `rTrebleOut` + `rLoad` | `kBaxNodes = 7`, `kBaxComponents = 12` |

Exact constants are set in `tone-stack.h` against the wired schematic and asserted by the Tier-1 builder test (component/node counts match the BOM, SC-001).

## Tone-stack Netlist (builder output)

A `Netlist<MaxNodes, MaxComponents>` holding only frozen-vocabulary `Resistor` / `Capacitor` / `VoltageSource` values (FR-003), already `prepare()`d by the builder so it is ready for any solver. Ground ≡ node 0 (implicit, `component-abstractions` convention). The builder also reports the **input and output node ids** the AC solver reads `H = V_out/V_in` between (returned alongside the netlist, or as named constants on the builder result — finalized in the builder contract).

## AC transfer function H(jω) — lab only

`solveAC(netlist, ω, inNode, outNode)` returns `std::complex<double>` = `V(outNode)/V(inNode)` at angular frequency `ω`. Derived quantities for validation: magnitude `20·log10(|H|)` (dB) and phase `arg(H)` (rad/deg). Never a `float`; there is no audio boundary in this deliverable (FR-015).

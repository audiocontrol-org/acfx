# Contract — Diode-clipper builders (portable primitive)

**Location:** `core/primitives/circuit/diode-clipper/{clipper-config.h, diode-clipper.h}`
**Layer:** primitive — portable, C++17, header-only, RT-safe, no lab include.
**Consumes (frozen):** `core/primitives/circuit/` — `Resistor`, `Capacitor`, `Diode`,
`VoltageSource`, `Netlist`, `NodeId`.

## Surface

```cpp
namespace acfx {

SymmetricShuntClipper  symmetricShuntClipper(const SymmetricShuntValues& v);
AsymmetricShuntClipper asymmetricShuntClipper(const AsymmetricShuntValues& v);
SeriesClipper          seriesClipper(const SeriesValues& v);

}
```

Each returns a `Clipper<MaxNodes, MaxComponents>` value = `{ netlist, inNode, outNode, portP, portN }`
(see data-model.md).

## Guarantees

1. **Topology only.** The return value is a `prepare()`-valid `Netlist` + node handles — **no**
   solved response, **no** `process()` / audio-path realization (FR-001..003).
2. **Frozen vocabulary.** Every component is a `Resistor`, `Capacitor`, `Diode`, or
   `VoltageSource`; the builders introduce **no** new element type and modify none (FR-004). The
   translation unit includes nothing under `core/labs/` (FR-019).
3. **Prepare-valid across representative BOMs.** The returned `Netlist` `prepare()`s cleanly
   (ground referenced, no floating node) for each topology's representative bills of materials
   (FR-006). The port nodes `(portP, portN)` are the diode-string node pair.
4. **Pure & heap-free.** No retained state; a BOM change is a fresh call. Fixed compile-time
   `Netlist` capacities; no `new`/`delete`/`std::vector` (FR-005 / SC-008).
5. **Fail loud.** Non-positive R/C, non-positive diode parameter, or an out-of-range population
   (total diodes `> MaxDiodes`, or `upCount == downCount` for the asymmetric builder) →
   descriptive `std::invalid_argument` on the build thread. No silent clamp, no fallback (FR-007).

## Topology summaries

- **symmetricShuntClipper:** `Vin`→`R`→`n1`; `Diode{n1,gnd}` ∥ `Diode{gnd,n1}`; `Cf` `n1`→gnd;
  `outNode = n1`, port `(n1, gnd)`. Odd-symmetric transfer.
- **asymmetricShuntClipper:** as above with `upCount`/`downCount` diodes across `(n1, gnd)`;
  non-odd transfer (DC offset). Requires `upCount + downCount ≤ MaxDiodes`.
- **seriesClipper:** `Vin`→`Cc`(series)→`n1`; `seriesCount` inline `Diode`s `n1`→`n2`; `R` `n2`→gnd;
  `outNode = n2`, port `(n1, n2)`. Coupling cap blocks DC (output → 0 at DC).

## Non-goals (carry the design's deferrals; FR-020)

No op-amp-feedback (Tube Screamer) clipper; no named-product BOMs (Rat/DS-1/Big Muff/TS); no
realtime / oversampling; no new circuit element.

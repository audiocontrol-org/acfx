# Contract — op-amp-stage builders (primitive)

**Files:** `core/primitives/circuit/opamp-stage/opamp-stage.h`, `opamp-config.h`
**Scope:** the four solver-neutral builders (design D3/D4, FR-003..FR-010).

## Signatures (shape)

Each builder is a **pure function** `Bom → { Netlist, NodeId inNode, NodeId outNode }`:

```cpp
nonInvertingGain(const NonInvertingGainBom&)   -> NonInvertingGainResult;
invertingGain(const InvertingGainBom&)         -> InvertingGainResult;
activeFirstOrder(const ActiveFirstOrderBom&)   -> ActiveFirstOrderResult;   // inverting first-order low-pass
opAmpDiodeClipper(const OpAmpDiodeClipperBom&) -> OpAmpDiodeClipperResult;  // TS808 core
```

Each `*Result` bundles a per-topology-sized `Netlist<MaxNodes, MaxComponents>` plus the `inNode`/`outNode` handles.

## Topologies (R3)

| Builder | Feedback | Gain / behavior |
|---------|----------|-----------------|
| `nonInvertingGain` | `Rf` (out→inMinus), `Rg` (inMinus→gnd), input→inPlus | `1 + Rf/Rg` |
| `invertingGain` | `Rin`→inMinus, `Rf` (out→inMinus), inPlus→gnd | `−Rf/Rin` |
| `activeFirstOrder` | `Rin`→inMinus, `Cf ∥ Rf` (out→inMinus), inPlus→gnd | DC gain `−Rf/Rin`, corner `1/(2π·Rf·Cf)` |
| `opAmpDiodeClipper` | `Rin`→inMinus, `Rf ∥ Cf ∥ antiparallel-diode-string` (out→inMinus), inPlus→gnd | soft clip near diode drop; one nonlinearity location |

## Guarantees

1. **Topology only** — no solved response, no `process()`, no audio path.
2. **Vocabulary** — only the `component-abstractions` inhabitants **plus `OpAmp`**; no element beyond `OpAmp`; no existing element modified.
3. **Pure / heap-free** — no retained state; fixed compile-time `Netlist` capacities; no `new`/`delete`/`std::vector`. A value change ⇒ rebuild (control-rate).
4. **`prepare()`-valid** across representative BOMs (ground-referenced; no floating node under the conservative pre-filter).
5. **Isolation** — the translation unit includes nothing under `core/labs/`.
6. Header-only, C++17, platform-independent.

## Errors (never fallbacks — FR-010)

- Non-positive R/C, non-positive diode parameter, diode `count == 0`, a **floating op-amp input**, or a **missing feedback path** → descriptive `std::invalid_argument` on the build thread. No silent clamp, no fabricated topology.

## Consumers

The later `tube-screamer` / `rat-distortion` / `neve-preamp` features compose these builders; Phase-5 MNA / Phase-6 WDF adapt them unchanged.

# Contract — Tone-stack builders (`tone-stack.h`)

Solver-neutral topology builders. Portable, header-only, C++17, standard-library + frozen `circuit/` vocabulary only. **Topology out; no solve, no audio path** (FR-001/FR-002).

## Shape

```
struct FMVValues { double r1, c1, c2, rTreble, rBass, rMid, rLoad; };
struct FMVControls { double bass, mid, treble; };            // each ∈ [0,1]

struct BaxandallValues { double rBass, cBass, rBassOut, cTreble, rTreble, rTrebleOut, rLoad; };
struct BaxandallControls { double bass, treble; };           // each ∈ [0,1]

// Each builder returns a ToneStack<N,M> { Netlist netlist; NodeId inNode, outNode; }
ToneStack<kFmvNodes, kFmvComponents>
  toneStackFMV(const FMVValues&, const FMVControls&, Taper);

ToneStack<kBaxNodes, kBaxComponents>
  toneStackBaxandall(const BaxandallValues&, const BaxandallControls&, Taper);
```

Each builder returns a `ToneStack` carrying the prepared netlist plus the **input and output node ids** so the lab reads `H = V(outNode)/V(inNode)`.

## Builder contract

- **Pure function, no state** (FR-004). A control change = call the builder again. No `new`/`delete`/`std::vector` — the returned `Netlist` is heap-free (SC-007).
- Splits each pot via `wiper()` / `rheostat()` (FMV mid), adds the frozen-vocabulary `Resistor`/`Capacitor`/`VoltageSource` components at the topology's nodes, `prepare()`s the netlist, and returns it.
- The returned netlist holds **only** `Resistor`, `Capacitor`, `VoltageSource` — no new element type, no modified element (FR-003).
- **`prepare()` succeeds across the full control range** — all-0, all-1, mixed, design-center (FR-005 / SC-001). The 10 Ω floor guarantees no 0-Ω short at extremes.
- **Explicit load:** `rLoad` is wired output-node-to-ground (FR-009) — never a hidden constant.

## Errors (FR-010) — no fallback

- Any non-positive value in `*Values`, or any control `∉ [0,1]` → `std::invalid_argument` naming the field, on the build thread.

## Isolation guarantee (FR-016 / SC-006)

- `tone-stack.h` / `taper.h` include **nothing** under `core/labs/`. Deleting `core/labs/passive-tone-stacks/` leaves the builders and their Tier-1 tests compiling and passing; only the assembled-response validations disappear.

## What the builders do NOT do

- No `solve()`, no `H(f)`, no `process()` / audio-path realization (deferred — Phase-6 WDF or a later lowering; FR-017).
- No named-product BOM/voicing/UI (later `design:feature/*` items).
- No value-variant stacks (Marshall/Vox) or other topologies (Big Muff, Neve) — captured, not shipped (FR-017).

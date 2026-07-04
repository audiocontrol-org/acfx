# Contract — Tone-stack builders (`tone-stack.h`)

Solver-neutral topology builders. Portable, header-only, C++17, standard-library + frozen `circuit/` vocabulary only. **Topology out; no solve, no audio path** (FR-001/FR-002).

## Shape

```
struct FMVValues { double r1, c1, c2, c3, rTreble, rBass, rMid, rLoad; };
struct FMVControls { double bass, mid, treble; };            // each ∈ [0,1]

struct BaxandallValues { double rIn, cBass, cTreble, rBass, rTreble, rLoad; };
struct BaxandallControls { double bass, treble; };           // each ∈ [0,1]

Netlist<kFmvNodes, kFmvComponents>
  toneStackFMV(const FMVValues&, const FMVControls&, Taper);

Netlist<kBaxNodes, kBaxComponents>
  toneStackBaxandall(const BaxandallValues&, const BaxandallControls&, Taper);
```

Each builder also exposes the **input and output node ids** for the AC probe (as named constants or a small returned struct; finalized in implementation) so the lab reads `H = V_out/V_in`.

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

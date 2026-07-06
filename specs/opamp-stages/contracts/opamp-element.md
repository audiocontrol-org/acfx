# Contract — `OpAmp` vocabulary element (primitive)

**File:** `core/primitives/circuit/models/opamp.h` (+ edits to `components.h`, `netlist.h`)
**Scope:** the one new circuit-vocabulary element this feature adds (design D2, FR-001/FR-002).

## Type

```cpp
namespace acfx {
struct OpAmp {
    NodeId inPlus;   // non-inverting input
    NodeId inMinus;  // inverting input
    NodeId out;      // output (norator-driven)
};
}
```

- Ideal nullor: `V(inPlus) = V(inMinus)`, zero input current, output current a solve unknown, infinite **linear** gain.
- **No** `admittance()`, **no** `companion()`, **no** parameter fields (no `Vsat`/`GBW`/`slewRate`/finite-gain in v1).

## Guarantees

1. `OpAmp` is added to the `Component` `std::variant` in `components.h` as the **only** new inhabitant.
2. Classifiers: `isNonlinear(OpAmp) == false`, `isReactive(OpAmp) == false`, `isLinear(OpAmp) == true`.
3. `terminalsOf(OpAmp) == {inPlus, inMinus}` (the constraint span; the output is reported by the solver's augmentation, not as a passive edge).
4. `contributesConductivePath(OpAmp) == false` — the op-amp output is excluded from the nodal reachability pre-filter (mirrors `CurrentSource`/`Diode`).
5. Header-only, C++17, standard-library only, platform-independent (Teensy-clean), zero-overhead, no heap, no I/O. Includes nothing under `core/labs/`.

## Errors (never fallbacks)

- A build that places an `OpAmp` with a degenerate terminal set (e.g. `out == inMinus`, or an out-of-range `NodeId`) → `std::invalid_argument` on the build thread.

## Non-goals (captured, deferred — FR-025)

- No non-ideality fields or behavior (finite gain, rails, GBW, slew, offset). The struct is deliberately shaped so those fields can be added later with ideal defaults, non-breaking, by a separate nonideal deliverable / Phase-5 solver.

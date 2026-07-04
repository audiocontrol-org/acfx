# Contract — Netlist<MaxNodes, MaxComponents>

The fixed-capacity, heap-free container that owns circuit topology and validates it at
`prepare()`. Templated capacities are compile-time (FR-009, OQ3).

## Shape

```
template <int MaxNodes, int MaxComponents>
class Netlist {
  // build phase
  NodeId addNode();                         // returns next NodeId; node 0 is ground
  void   add(const Component& c);           // append; over-capacity → descriptive throw
  // finalize
  void   prepare();                         // validate topology; throw on ill-posed
  // read (for a solver)
  int    nodeCount() const;
  int    componentCount() const;
  span<const Component> components() const; // immutable view; solver reads physics from here
};
```

## `add(...)` contract

- Appends a component. If `componentCount() == MaxComponents` (or a referenced node index
  `>= MaxNodes`), raises a **descriptive over-capacity error** naming the limit (FR-010).
- **No heap growth, no truncation** — the capacity is a hard, compile-time bound.

## `prepare()` contract (topology validation — FR-010)

Runs all checks; on the first failure raises a **distinct, descriptive error** (no fallback):

| Check | Failure error |
|---|---|
| Ground referenced | node `0` referenced by ≥1 component, else `"missing ground reference"` |
| No floating node | every non-ground node has a conductive path to ground, else `"floating node N"` (names the node) |
| Within capacity | counts ≤ template capacities, else `"netlist over capacity"` |

- On success the netlist is **solve-ready**: any solver that accepts it may assume a well-posed
  topology (SC-005 asserts each failure path is exercised and distinct).

## Post-`prepare()` invariant (FR-011)

- The per-sample **solve path does not throw and does not allocate**. All validation that could
  throw happens in `prepare()`; the solve consumes an already-valid netlist.
- **Test (SC-006):** a no-allocation assertion wraps a representative solve loop.

## What the netlist does NOT do

- It does not solve (that is a solver's job) and does not know about matrices or waves.
- It does not enforce the ≥2-nonlinearity limit — that is the **reference solver's** scope rule
  (FR-016), because a different solver (Phase 5 MNA) will accept more.

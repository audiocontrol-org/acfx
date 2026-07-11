# Contract — WDF adaptors (public header API)

The behavioral contract the `wdf-adaptors` headers expose. Consumers: `wdf-passive-networks`,
`wdf-complete-analog-stages`, and developers composing WDF trees. Everything below is under
`namespace acfx::wdf`, header-only, C++17. The contract is pinned to the shipped voltage-wave
convention W1 (`a = v + Rp·i`, `b = v − Rp·i`, current `i` into the port).

## C1 — Types

```cpp
template <class... Child> class SeriesAdaptor;    // shared current, KVL (Σv = 0)
template <class... Child> class ParallelAdaptor;  // shared voltage, KCL (Σi = 0)
```

Each is a **`OnePort`**: `is_one_port_v<SeriesAdaptor<...>>` and
`is_one_port_v<ParallelAdaptor<...>>` are `true`. Each exposes exactly the shipped OnePort
surface plus a typed child accessor:

```cpp
double portResistance() const noexcept;    // R_up (adapted upward port resistance)
double reflected()      const noexcept;     // b_u  (up-sweep output)
void   incident(double a) noexcept;         // a_u  (down-sweep input)
static constexpr bool isAdaptable = true;   // always adaptable (adaptable children only)

template <std::size_t I> auto&       child() noexcept;        // reference to the I-th child
template <std::size_t I> const auto& child() const noexcept;
```

## C2 — Compile-time preconditions (static_assert)

For `SeriesAdaptor<Child...>` / `ParallelAdaptor<Child...>`:
1. `sizeof...(Child) >= 1` — at least one child.
2. `(is_one_port_v<Child> && ...)` — every child is a `OnePort`.
3. `(Child::isAdaptable && ...)` — every child is adaptable. **A non-adaptable child
   (`ShortCircuit`, `OpenCircuit`, any `isAdaptable == false`) is a compile error** (the
   delay-free-loop guard). The single reflective port belongs at the tree root, owned by the
   sibling nodes.

## C3 — Construction

`SeriesAdaptor(Child... children)` / `ParallelAdaptor(Child... children)` take the children by
value (moved into the owned tuple) and, off the hot path:
- read each `child.portResistance()`,
- **validate**: each MUST be finite and `> 0`, else throw `std::invalid_argument` naming the
  offending child index and value — never clamp or substitute (no fallbacks),
- compute `R_up = Σ R_k` (series) / `G_up = Σ G_k` (parallel) and precompute all scattering
  coefficients as reciprocal-folded multiplies.

Post-condition: `portResistance()` equals the series (`Σ R_child`) or parallel
(`1 / Σ (1/R_child)`) combination of the children.

## C4 — Scattering relations (the testable behavior)

Let ports be the `N` children plus the upward port `u`. Series total `R = Σ_i R_i`; parallel
`G = Σ_i G_i` with `G_i = 1/R_i`.

- **Series**: `b_k = a_k − (2·R_k / R) · Σ_i a_i` for every port `k`. Enforces `Σ_k v_k = 0`
  (KVL, `Σ b = −Σ a`) and equal port currents `i_k = (Σ_i a_i)/R`.
- **Parallel**: `b_k = 2·(Σ_i G_i·a_i)/G − a_k` for every port `k`. Enforces common voltage
  `v = (Σ_i G_i a_i)/G` and `Σ_k i_k = 0` (KCL).

**Adapted upward port** (`R_up = Σ_child R_child` series / `G_up = Σ_child G_child` parallel):
`b_u` is **independent of `a_u`**:
- Series: `b_u = −Σ_child a_child`.
- Parallel: `b_u = (Σ_child G_child · a_child) / G_up`.

## C5 — Call ordering (inherited from OnePort I2, `isAdaptable == true`)

The adaptor is adaptable, so `reflected()` is valid **before** this sample's `incident()`:
- **Up-sweep**: call `reflected()` — it reads each child's `reflected()` (recursively, the
  subtree up-sweep), caches those child waves, and returns `b_u`.
- **Down-sweep**: call `incident(a_u)` — it computes each child's incident wave (C4) from
  `a_u` and the cached child waves and calls `child.incident(...)` (recursively, the subtree
  down-sweep).

`reflected()` MUST NOT depend on the not-yet-delivered `a_u`. Calling `incident()` before
`reflected()` in a sample is a contract violation by the caller (the sweep order is
up-then-down).

## C6 — Real-time safety

`portResistance()`, `reflected()`, `incident()`, and `child<I>()` are `noexcept`, perform no
heap allocation and take no lock, and are O(N) in the adaptor's port count. All
resistance-derived coefficients are fixed at construction. (Validated by an `AllocationSentinel`
zero-heap test.)

## C7 — Losslessness (validated invariant)

For all admissible inputs, the **conductance-weighted pseudo-power balance** holds:

```
Σ_k G_k · a_k²  =  Σ_k G_k · b_k²      (G_k = 1/R_k, over all ports incl. upward)
```

This is the correct passivity invariant; the unweighted `Σ a_k² = Σ b_k²` holds only when all
branch resistances are equal and is NOT the contract. Passivity is validated, never enforced by
clamping.

## C8 — Out of scope (captured, sibling-owned)

Not provided by these headers: the single-sample **root driver**, whole-tree **topology /
root-port selection**, **R-type / rigid** adaptors, named **passive networks**
(→ `wdf-passive-networks`); the **ideal-source** (`b = 2E − a`) and **nonlinear** (`b = f(a)`)
**roots** (→ `wdf-complete-analog-stages`); **time-varying `Rp`** re-adaptation propagation
(captured with the leaf `setSampleRate` question). Full network **transfer-function** tests
belong to the root-driver owner, not here.

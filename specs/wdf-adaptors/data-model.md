# Phase 1 Data Model — WDF-adaptors

Types and state for the series/parallel scattering adaptors. All types live in
`namespace acfx::wdf`, header-only, C++17. No new leaf types and no change to the shipped
`OnePort` concept or wave convention.

## Entity: `SeriesAdaptor<Child...>`

An N-port series scattering junction (shared port current, `Σ v = 0`) that is itself a
`OnePort`.

**Template parameters**: `Child...` — one or more child one-port types.

**Compile-time constraints** (in `adaptor-detail.h`, shared with `ParallelAdaptor`):
- `static_assert(sizeof...(Child) >= 1)` — at least one child (empty pack rejected).
- `static_assert((is_one_port_v<Child> && ...))` — every child satisfies the `OnePort` concept.
- `static_assert((Child::isAdaptable && ...))` — every child is adaptable (no reflective child;
  the one reflective port is the root, owned by siblings).

**State (members)**:
| Member | Type | Role |
|--------|------|------|
| `children_` | `std::tuple<Child...>` | the owned child one-ports (by value, composition) |
| `Rk_` | `std::array<double, N>` | each child's port resistance `R_k` (from `child.portResistance()`) |
| `Rup_` | `double` | adapted upward port resistance `= Σ_k R_k` |
| `coeff_` | `std::array<double, N>` | precomputed `2·R_k / R`, where `R = Σ_k R_k` |
| `cachedChildWave_` | `std::array<double, N>` | child reflected waves captured during the up-sweep, reused in the down-sweep |

`N = sizeof...(Child)`.

**Behavior (the `OnePort` surface)**:
- `static constexpr bool isAdaptable = true;`
- `double portResistance() const noexcept` → `Rup_`.
- `double reflected() const noexcept` (up-sweep) → caches `a_k = std::get<k>(children_).reflected()`
  for each `k`, returns `b_u = −Σ_k a_k` (the adapted-port series relation).
- `void incident(double a_u) noexcept` (down-sweep) → with `S = a_u + Σ_k cachedChildWave_[k]`,
  delivers to each child `child_k.incident( cachedChildWave_[k] − coeff_[k]·S )` (the series
  scattering `b_k = a_k − (2·R_k/R)·Σ_i a_i`, `i` over all ports incl. upward).

> The `const`-ness of `reflected()` and the mutation of `cachedChildWave_` are reconciled in
> the contract (the cache is logically part of the same sample's evaluation; realized via a
> `mutable` cache or by computing the cache in `reflected()` and reading it in `incident()`;
> the precise `const`/`mutable` shape is a task-level detail that must preserve the shipped
> `reflected() const noexcept` signature the concept requires).

**Accessor**: `template <std::size_t I> Child_I& child() noexcept` and a `const` overload →
`std::get<I>(children_)`.

## Entity: `ParallelAdaptor<Child...>`

The dual — an N-port parallel scattering junction (shared port voltage, `Σ i = 0`), itself a
`OnePort`. Same compile-time constraints, same tuple-of-children ownership, same `child<I>()`.

**State differences**:
| Member | Type | Role |
|--------|------|------|
| `Gk_` | `std::array<double, N>` | each child's conductance `G_k = 1 / R_k` |
| `Gup_` | `double` | adapted upward conductance `= Σ_k G_k` (so `portResistance() = 1/Gup_`) |
| `coeff_` | `std::array<double, N>` | precomputed `2·G_k / G`, where `G = Σ_k G_k` |
| `cachedChildWave_` | `std::array<double, N>` | as above |

**Behavior**:
- `static constexpr bool isAdaptable = true;`
- `double portResistance() const noexcept` → `1.0 / Gup_`.
- `double reflected() const noexcept` (up-sweep) → caches `a_k = child_k.reflected()`, returns
  `b_u = (Σ_k G_k·a_k) / Gup_` (the adapted-port parallel relation).
- `void incident(double a_u) noexcept` (down-sweep) → with common voltage-wave term
  `v2 = 2·(G_up·a_u + Σ_k G_k·cachedChildWave_[k]) / G` (`G = 2·G_up`), delivers to each child
  `child_k.incident( v2 − cachedChildWave_[k] )` (the parallel scattering
  `b_k = 2·(Σ_i G_i·a_i)/G − a_k`).

## Shared: `adaptor-detail.h`

Holds the machinery both adaptors share, so `series-adaptor.h` / `parallel-adaptor.h` stay
small (design D10):
- The three conformance `static_assert` helpers (arity, `is_one_port_v`, `isAdaptable`).
- Index-sequence fold helpers to (a) gather child `portResistance()` into `Rk_`/`Gk_`,
  (b) gather child `reflected()` into the cache, (c) fan `incident()` back to children.
- Construction-time validation: for each child, require `portResistance()` finite and `> 0`,
  else throw `std::invalid_argument` naming the child index and value (R7). No clamping.

## Validation rules (from spec FR-004/007/010/011/014)

- Every child's `portResistance()` MUST be finite and `> 0` at construction, else throw.
- Arity MUST be `≥ 1` (compile-time).
- Every child MUST be an adaptable `OnePort` (compile-time).
- `portResistance()` MUST equal the series/parallel combination of the children.
- `reflected()` MUST be independent of the current `incident()` argument (reflection-free
  adapted port).
- Per-sample path MUST be `noexcept`, heap-free, lock-free, O(N).
- Conductance-weighted pseudo-power MUST balance: `Σ_k G_k·a_k² = Σ_k G_k·b_k²` across all
  ports (validated, not enforced).

## Relationships

- **Consumes**: the shipped `acfx::wdf::OnePort` concept and `waveToVoltage`/`waveToCurrent`
  from `one-port.h` (unchanged).
- **Is-a**: each adaptor satisfies `is_one_port_v`, so adaptors nest as children of adaptors
  (`SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>>`).
- **Terminated by** (out of scope, sibling-owned): a single reflective root at the top of a
  whole tree — `wdf-passive-networks` (driver/topology) and `wdf-complete-analog-stages`
  (nonlinear/ideal roots).

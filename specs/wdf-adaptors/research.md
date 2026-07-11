# Phase 0 Research — WDF-adaptors

Implementation-shaping decisions. The architectural decisions (D1–D12) are fixed by the
operator-approved, third-party-reviewed design record
(`docs/superpowers/specs/2026-07-10-wdf-adaptors-design.md`); this file consolidates them in
Decision / Rationale / Alternatives form and resolves the remaining *implementation* unknowns
(how to realize the variadic sweep, caching, validation, and the compile-time guards). There
are no open `NEEDS CLARIFICATION` items — the spec has none.

## R1 — N-port representation: variadic templates over a by-value child tuple

- **Decision**: `SeriesAdaptor<Child...>` / `ParallelAdaptor<Child...>` are variadic on their
  child `OnePort` types and hold the children by value in a `std::tuple<Child...>`. General
  N-port; the 3-port (two-child) junction is just the two-argument instantiation. Iteration
  over ports uses `std::index_sequence` fold expressions.
- **Rationale**: matches the shipped primitives' idiom (heterogeneous leaf sets driven via
  `std::tuple` + `if constexpr`, no vtable); a whole tree becomes one statically-composed,
  inlinable, heap-free type. By-value ownership removes pointer indirection and lifetime
  hazards on the wave path.
- **Alternatives**: 3-port-only (rejected — forces artificial binary nesting for flat
  junctions); reference/aggregation-held children (rejected — pointers + lifetime hazards on
  the hot path); runtime `std::variant` node (rejected — reintroduces the vtable/branch cost
  and `NodeId` baggage the primitives rejected).

## R2 — Adapted port = the upward port; local adaptation

- **Decision**: each adaptor makes its **upward-facing port** reflection-free by setting
  `R_up = Σ_child R_child` (series) / `G_up = Σ_child G_child`, `G = 1/R` (parallel).
  `portResistance()` returns `R_up`.
- **Rationale**: the canonical Fettweis adaptation; cheap (a sum, no search), and it is exactly
  what makes the adaptor a valid adapted `OnePort` that can nest. Reflection-freedom of the up
  port (`γ_up = 1` series; conductance-weighted parallel) follows algebraically.
- **Alternatives**: adapting an arbitrary (non-upward) port — needed only when a reflective
  root sits at a leaf position — is the whole-tree adaptation deferred to
  `wdf-passive-networks`.

## R3 — Adaptable children only; the reflective port is the root

- **Decision**: each adaptor `static_assert`s that **every** child satisfies
  `Child::isAdaptable == true` (in addition to `is_one_port_v<Child>`), and therefore reports
  `static constexpr bool isAdaptable = true`.
- **Rationale**: a WDF tree admits **at most one reflective port, at the root** — a reflective
  (non-adaptable) child would close an instantaneous delay-free loop the sweep cannot solve.
  Drawing this as a compile-time boundary makes the illegal tree fail to compile rather than
  misbehave at runtime, and keeps root handling in the siblings.
- **Alternatives**: allow one reflective child and invert the evaluation protocol (the
  reflective port propagates up to the root) — that *is* the general tree-topology/root-choice
  adaptation, deferred to `wdf-passive-networks`.

## R4 — Cache child reflected waves between up-sweep and down-sweep

- **Decision**: `reflected()` reads each `child.reflected()` once, stores the values in an
  in-object `std::array<double, N>`, and returns `b_u`. `incident(a_u)` reuses the cached
  values (with `a_u`) to compute and deliver each child's incident wave.
- **Rationale**: the down-sweep's scattering needs the same child waves the up-sweep read;
  caching guarantees a single evaluation per child per sample and keeps both sweep halves
  consistent. The cache is `N` `double`s — in-object, zero-heap, matching the leaf state
  discipline. (Adaptable children's `reflected()` is `const noexcept` and side-effect-free, so
  correctness does not depend on caching, but single-evaluation and clarity do.)

## R5 — Precompute coefficients as multiplies (no hot-path division)

- **Decision**: at construction, store per-child `R_k` (series) or `G_k = 1/R_k` (parallel),
  the totals `R`/`G`, and the reused ratios (e.g. series `2·R_k/R`; parallel `2·G_k/G` and
  `G_k`). Store reciprocals so the per-sample path is multiply/add only.
- **Rationale**: Principle VI — no division or allocation on the audio path; all
  resistance-derived constants are fixed for the tree's lifetime (Rp is fixed for the shipped
  leaves), so they are computed once off the hot path.

## R6 — `child<I>()` via `std::get<I>` on the child tuple

- **Decision**: `template <std::size_t I> auto& child() noexcept { return std::get<I>(children_); }`
  plus a `const` overload — a compile-time-typed reference to the `I`-th owned child.
- **Rationale**: zero-cost, resolved at compile time, returns the child's exact static type;
  gives tests and `wdf-passive-networks` a defined way to reach nested sources/probes without a
  second connection mechanism or runtime lookup (design D12).

## R7 — Validation split: arity is compile-time, resistances are runtime

- **Decision**: an **empty child set** is a **compile-time** rejection
  (`static_assert(sizeof...(Child) >= 1)`) — arity is statically known, so this is stronger
  than a runtime throw. A **non-positive or non-finite child `portResistance()`** is a
  **runtime** `std::invalid_argument` at construction (the value is only known once children
  exist), with a message naming the offending child/value. Nothing is clamped or defaulted.
- **Rationale**: fail as early as the information allows (Principle V, no fallbacks). This
  refines spec FR-011 / SC-006: "empty child set is a construction-time error" is satisfied —
  indeed strengthened — by a compile-time `static_assert`.
- **Note**: a single-child adaptor is admissible (`R_up = R_child`) and must behave as a
  wave-domain pass-through — covered by a dedicated test assertion.

## R8 — Realizing the "non-adaptable child rejected" guarantee (SC-007)

- **Decision**: the guarantee is the `static_assert(Child::isAdaptable && ...)` in R3. Because
  doctest cannot assert a *failed* compilation inline, SC-007 is realized as (a) the
  documented `static_assert` in `adaptor-detail.h`, and (b) a commented negative-compilation
  example in the tree test (a line that, if uncommented, must fail to compile), optionally
  backed by a build-level negative-compilation check. The exact mechanism is finalized in
  `tasks.md`.
- **Rationale**: keeps the guarantee visible and testable without a bespoke compile-fail
  harness; the `static_assert` itself is the enforcement.

## R9 — Numerical conditioning for wide/ill-scaled junctions

- **Decision**: default to straightforward `Σ R` / `Σ G` accumulation and the ratio
  coefficients. If a conditioning problem is demonstrated for very large `N` or extreme
  resistance ratios (design Open Question 8), switch to ordered summation and/or add a
  conditioning assertion — but only on evidence, and always off the hot path.
- **Rationale**: no evidence it bites at audio-typical junction sizes; premature reordering
  adds cost without proven benefit. Captured, not pre-solved.

## R10 — Deferred cross-cuts (captured, routed to siblings — not cut)

The single-sample **root driver**, whole-tree **topology / root-port selection**, **R-type /
rigid adaptors**, and named **passive networks** are `wdf-passive-networks`; the
**ideal-source root** (`b = 2E − a`) and **nonlinear root** (`b = f(a)`, iterative) are
`wdf-complete-analog-stages`; **time-varying port resistances** (re-adaptation propagation)
are captured with the leaf-level `setSampleRate` question (design Open Question 7). This node
provides the adaptor `OnePort`s those siblings assemble and terminate.

**Output**: all implementation unknowns resolved; no `NEEDS CLARIFICATION` remains.

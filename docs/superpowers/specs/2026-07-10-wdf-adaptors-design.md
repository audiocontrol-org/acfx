---
title: wdf-adaptors — design record
date: 2026-07-10
roadmap-node: design:primitive/wdf-adaptors
part-of: multi:feature/phase-wave-digital-filters
depends-on: multi:feature/phase-numerical-solvers
builds-on: design:primitive/wdf-primitives (shipped)
status: designing (awaiting operator design-approved marker)
backend: superpowers:brainstorming (driven under stack-control design house rules v1)
---

# wdf-adaptors — design record

Wave Digital Filter **adaptors**: the series and parallel scattering junctions
that connect the shipped one-port WDF leaves into computable filter trees. This
record fixes the scope boundary against the sibling nodes, the wave and sign
conventions inherited from the shipped primitives, the exact adapted-port
scattering relations, the static-composition representation, and the validation
approach.

---

## Problem domain

### What exists below this node

`design:primitive/wdf-primitives` is **shipped and closed** (PR #23). It delivered
eight leaf one-ports in `namespace acfx::wdf` under
`core/primitives/circuit/wdf/`, each satisfying a single **duck-typed `OnePort`
concept** (`one-port.h`, contract I1–I3):

```cpp
double portResistance() const noexcept;   // Rp — the port reference resistance
double reflected()      const noexcept;    // b  — the reflected wave (up-sweep output)
void   incident(double a) noexcept;        // a  — the incident wave (down-sweep input)
static constexpr bool isAdaptable;         // is b independent of THIS sample's a?
```

The concept is a compile-time trait (`is_one_port_v<T>`, SFINAE / `std::void_t`),
**not a virtual base class** — no vtable on the wave path (I3 static dispatch).
Voltage-wave convention (W1), current `i` referenced **into** the port:

```
forward:  a = v + Rp·i,   b = v − Rp·i
inverse:  v = (a + b)/2,  i = (a − b)/(2·Rp)      (waveToVoltage / waveToCurrent)
```

The leaves delivered: `Resistor`, `Capacitor`, `Inductor` (bilinear reactances,
one stored wave sample), `ResistiveVoltageSource`, `ResistiveCurrentSource`,
`ResistiveTermination` (all `isAdaptable == true`), and `ShortCircuit`,
`OpenCircuit` (`isAdaptable == false`, reflective). The **general reflection**
`b = a·(R − Rp)/(R + Rp)` for an arbitrary reference resistance was *deliberately
withheld* from the leaves and annotated as "the adaptor layer's concern" — that
layer is this node.

### What is missing (what this node must introduce)

There is **no connection mechanism today**. A search of `core/` and `tests/` for
`adaptor|junction|scatter|series|parallel|Rp(parent)` finds only prose describing
these as out-of-scope siblings. Specifically missing:

- Any notion of a junction's **own upward port resistance** derived from its
  children.
- The **series** (shared current, KVL) and **parallel** (shared voltage, KCL)
  **scattering relations**.
- The **up-sweep / down-sweep** evaluation over a composed tree.
- The application of the withheld general reflection at arbitrary reference
  resistances.

### Where WDF sits relative to the shipped solvers

WDF is an **independent, parallel solver path**. `plan.md` for wdf-primitives is
explicit: *"No dependency on the nodal solvers (MNA/Newton/integration): WDF is a
parallel reader of the vocabulary, not a consumer of the matrix engines. No
`NodeId`."* The `depends-on: phase-numerical-solvers` roadmap edge is
**phase-ordering, not a code dependency**. WDF does no matrix solve: it transforms
each element into a wave-domain one-port, connects them via adaptors into a tree,
and evaluates by up-sweep + down-sweep — O(N) per sample, unconditionally stable
for passive networks. This node introduces the connective tissue of that path.

### The constraint that shapes the whole design: no delay-free loops

WDF evaluability rests on one structural rule (Fettweis): a tree may contain **at
most one reflective (non-adaptable) port, and it must be the root.** Every other
port must be *adapted* — reflection-free — so the up-sweep can compute a node's
reflected wave purely from state, before any incident wave arrives. A reflective
element placed anywhere but the root closes an instantaneous signal path through
the root and back — a delay-free loop the simple sweep cannot solve. This rule is
why the primitives forced `isAdaptable` to be a compile-time constant ("sibling
adaptor code branches on it via `if constexpr`") and is the load-bearing
constraint behind the decisions below.

---

## Solution space

The design question is **how much of the WDF connection machinery lives in this
node** versus its siblings (`wdf-passive-networks`, `wdf-complete-analog-stages`),
and **how a junction is represented** in code. Alternatives considered:

### Chosen — self-adapting adaptors as recursive one-ports (static composition)

An adaptor **is itself a `OnePort`**. Its **upward-facing port is the adapted
(reflection-free) port**, with resistance derived locally from its children:
`Rp_up = Σ Rp_child` (series) or `1/Rp_up = Σ 1/Rp_child` (parallel). Because it
satisfies `OnePort`, an adaptor nests directly as a child of another adaptor and
connects the shipped leaves through the *same* seam. Adaptors are **variadic
templates** on their child types — `SeriesAdaptor<Child…>`,
`ParallelAdaptor<Child…>` — so an entire filter is one statically-composed,
fully-inlined, heap-free type, e.g.:

```cpp
SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>>
```

Children are **held by value** (the adaptor owns its subtree). Scattering
coefficients are precomputed at construction from each child's `portResistance()`
(off the hot path); the hot path is O(N) per node, `noexcept`, allocation-free.
Adaptors **require all children to be adaptable** (`static_assert`), so every
adaptor is itself adaptable (`isAdaptable == true`); the single permitted
reflective element is the **root**, terminated above the top adaptor by the tree
driver / sibling nodes — never as an adaptor child (see Decision 6).

*Chosen because:* it is the canonical Fettweis formulation and the *only* option
that composes with the shipped `OnePort` concept without inventing a second
connection mechanism; it inherits the shipped no-vtable / RT-safe / static-dispatch
pattern verbatim; local upward-port adaptation (`Σ Rp` / `Σ G`) is cheap and needs
no search, leaving the genuinely hard adaptation (whole-tree topology, root-port
choice, R-type adaptors) cleanly to the siblings.

### Rejected — raw scattering junctions only (adaptation deferred entirely)

This node ships only an N-port scattering primitive — given the port resistances
and which port is adapted, apply the scattering matrix — and the recursive
adaptation (propagating child `Rp` upward, choosing the unadapted port, tree
assembly) lives wholly in `wdf-passive-networks`.

*Rejected because:* a junction that is not itself a `OnePort` **strands the
shipped leaves** — they would have no way to be a child of a nesting junction —
and forces a *second*, parallel connection mechanism alongside the established
`OnePort` seam, violating the interface-first / one-seam principle. It also splits
an atom: local upward-port adaptation (`Σ Rp`) is intrinsic to a junction being a
usable one-port and cannot be meaningfully separated from the scattering itself.

### Rejected — 3-port (binary) adaptors only

Ship only the canonical two-children-plus-parent junction; represent wide
junctions by nesting binary adaptors.

*Rejected because:* it forces artificial binary nesting for a physically flat
junction (e.g. a five-element series loop), distorting the tree and multiplying
node count and cached state. Variadic N-port via C++17 parameter packs is
idiomatic in this codebase (the primitives already drive heterogeneous sets via
`std::tuple` + `if constexpr`) and is strictly more general, with the 3-port case
falling out as the two-child instantiation.

### Rejected — reference / aggregation-held children

The adaptor holds pointers or references to externally-owned leaves and acts as a
view over them.

*Rejected because:* it introduces pointer indirection and lifetime/aliasing
hazards on the audio-callback path. By-value composition keeps the whole tree a
single owned value with no indirection — maximally RT-safe and matching the
shipped leaves' in-object-`double` state discipline.

### Rejected — runtime tree / `std::variant` node

A heterogeneous node type (tagged union or polymorphic base) assembled into a
runtime graph.

*Rejected because:* it reintroduces the vtable / branch-per-sample cost the
primitives explicitly rejected (their Approach D/F), and a variant node would
carry the nodal-side `NodeId` baggage that WDF deliberately omits. The static
template tree is inlinable and heap-free by construction.

---

## Decisions

**D1 — Scope boundary.** This node delivers **series and parallel adaptors that
are themselves `OnePort`s**, with local upward-port adaptation. Deferred to
siblings: whole-tree topology and root-port selection, R-type / rigid adaptors for
non-series/parallel connections, and named passive networks →
`wdf-passive-networks`; the nonlinear root (`b = f(a)` iterative) and ideal-source
root → `wdf-complete-analog-stages`.

**D2 — Inherited conventions (verbatim).** Voltage waves, `a = v + Rp·i`,
`b = v − Rp·i`, `i` into the port; `waveToVoltage` / `waveToCurrent` reused. No new
wave convention is introduced. All scattering below is pinned to these signs.

**D3 — Series adaptor scattering (N ports, port resistances `R_k`,
`R = Σ R_k`).**

```
b_k = a_k − (2·R_k / R) · Σ_i a_i
```

This enforces the series constraints under W1: KVL `Σ v_k = 0` (since
`Σ b_k = −Σ a_k`) and equal port currents `i_k = (Σ_i a_i)/R` for all `k`.

**D4 — Parallel adaptor scattering (conductances `G_k = 1/R_k`,
`G = Σ G_k`).**

```
b_k = 2·(Σ_i G_i·a_i)/G − a_k
```

This enforces the parallel constraints: common voltage `v = (Σ_i G_i a_i)/G` (so
`v_k = (a_k + b_k)/2 = v` for all `k`) and KCL `Σ i_k = 0`.

**D5 — Adapted upward port (local adaptation).** Designate the upward
(parent-facing) port `u`. Choosing its reference resistance reflection-free:

- Series: `R_u = Σ_{child} R_child` (so `2R_u/R = 1`), giving
  `reflected() = b_u = −Σ_{child} a_child` — independent of `a_u`.
- Parallel: `G_u = Σ_{child} G_child` (so `1/R_u = Σ 1/R_child`), giving
  `reflected() = b_u = (Σ_{child} G_child·a_child)/G_u` — independent of `a_u`.

During the up-sweep, each child presents `a_child = child.reflected()` into the
junction; `portResistance()` returns `R_u`. This is what makes the adaptor a valid
adapted one-port.

**D6 — Adaptable children only; the reflective port is the root.** Every adaptor
`static_assert`s that all its children satisfy `isAdaptable == true`, so every
adaptor is itself `isAdaptable == true`. A reflective element (`ShortCircuit`,
`OpenCircuit`, or a future ideal-source / nonlinear root) may appear **only as the
tree root**, terminating the top adaptor's adapted upward port from above; placing
one as a non-root child would create a delay-free loop (see Problem domain). This
draws the "one reflective port, at the root" rule as a compile-time boundary and
keeps root/topology handling in the siblings.

**D7 — Up-sweep / down-sweep with cached child waves.** `reflected()` (up-sweep)
reads each `child.reflected()`, caches those values in-object, and returns `b_u`
per D5. `incident(a_u)` (down-sweep) uses `a_u` and the cached child waves to
compute each child's incident `b_k` (D3/D4 with the precomputed coefficients) and
calls `child.incident(b_k)`. Cached child waves are `double`s stored in the
adaptor — the same in-object-state discipline the leaves use. All coefficients
(`R_k/R_u`, `G_k/G` etc.) are precomputed in the constructor; the hot path does no
division and no allocation.

**D8 — Static composition, by value, variadic.** `SeriesAdaptor<Child…>` and
`ParallelAdaptor<Child…>` hold their children by value (own the subtree) and are
parametrized on the child `OnePort` types via parameter packs. Each adaptor
`static_assert`s `is_one_port_v<Child>` for every child and, per D6,
`Child::isAdaptable`. Each adaptor is itself compile-time-checked
`is_one_port_v`-conformant.

**D9 — Non-physical inputs throw at construction, never clamp.** Following the
project no-fallbacks stance, a non-positive or non-finite child port resistance
(or an empty child set) raises `std::invalid_argument` at construction (off the
hot path). Reflections are never clamped; passivity is a *validated* property, not
an enforced one.

**D10 — File layout under `core/primitives/circuit/wdf/`.** Header-only, to honor
the ~300–500-line budget and keep each unit single-purpose:

- `adaptor-detail.h` — shared sweep machinery: the tuple/parameter-pack iteration,
  fold helpers, cached-wave storage, and the common `OnePort`-conformance
  `static_assert`s. (Series and parallel share the iteration; they differ only in
  the coefficient formulas.)
- `series-adaptor.h` — `SeriesAdaptor` (D3, D5 series form).
- `parallel-adaptor.h` — `ParallelAdaptor` (D4, D5 parallel form).

Includes use project-root-relative quoted paths (e.g.
`#include "primitives/circuit/wdf/one-port.h"`), matching the C++ core (the `@/`
alias is TS/host tooling only).

**D11 — Validation approach.** Per the project's circuit-model validation stance
([[circuit-model-validation-approach]]): prove the scattering **exact on closed
forms** (two-resistor series and parallel dividers against Ohm's law; an
`R‖(series L,C)` shape against its analytic transfer function), verify the
**reflection-free property** of the adapted port numerically (`b_u` invariant under
`a_u`), verify **losslessness / pseudo-passivity** of each junction (instantaneous
pseudo-power conserved across ports), and an **allocation-sentinel** RT-safety test
mirroring the shipped `wdf-rt-safety-test`. Exact closed forms first, then
monotonic/passivity invariants — no transcribed published rationals.

---

## Open questions

Captured, not cut (per capture-over-YAGNI). Each is either routed to a named
sibling node or flagged for a later, explicit scoping pass — none is silently
dropped from scope.

1. **Root driver helper.** A minimal single-sample tree driver — given a top
   adaptor and a root `OnePort` with matching `Rp`, run one up-sweep → root
   evaluation → down-sweep — would make a full tree runnable end-to-end. The
   adaptor's scattering is directly testable *without* it (feed `a_u`, read child
   incidents; feed child reflected, read `b_u`), so it is not required for this
   node's validation. **Candidate inclusion as a thin helper vs. defer to
   `wdf-passive-networks`** (which owns root-port choice). Leaning defer; decide at
   spec time.
2. **Root-port choice / non-upward adapted port.** Here the adapted port is always
   the upward one (D5). General adaptation that adapts an arbitrary port (needed
   when the reflective root sits at a leaf position) → `wdf-passive-networks`.
3. **R-type / rigid adaptors** for topologies that are neither pure series nor
   pure parallel (bridges, lattices) → `wdf-passive-networks`.
4. **Nonlinear root** (`b = f(a)` iterative, e.g. diode) and **ideal (non-resistive)
   source** root (`b = 2E − a`) → `wdf-complete-analog-stages`. The
   adaptable-children-only rule (D6) already reserves the root slot for these.
5. **Named passive networks** (ladders, tone stacks assembled as WDF trees) →
   `wdf-passive-networks` / `wdf-complete-analog-stages`.
6. **Parameter / output access into an owned subtree.** With children held by
   value, reading a leaf output or setting a source value `E` requires accessors
   into the tree (e.g. indexed child access, or the tree exposing typed child
   references). Ergonomics question; resolve at spec time (does not change the
   scattering).
7. **Time-varying port resistances.** If a child's `Rp` changes at runtime
   (variable component / sample-rate), the adaptor's precomputed coefficients must
   be recomputed and re-propagated up the tree. Re-adaptation propagation is a
   cross-cut shared with the leaf-level `setSampleRate` open question deferred by
   the primitives; capture here, scope with that.
8. **Wide-junction numeric conditioning.** For very large `N` or extreme
   resistance ratios, `Σ R` / `Σ G` accumulation and the coefficient ratios may
   want ordered summation or a conditioning check. Flag for the validation pass; no
   evidence it bites at audio-typical sizes yet.
9. **Zero / infinite child resistance** (a `ShortCircuit`/`OpenCircuit`-like
   resistance reaching an adaptor as `R → 0` or `G → 0`). D6 already bars the
   reflective short/open leaves as children; but a *resistive* near-zero/near-inf
   `Rp` is still admissible and interacts with the `Σ` and the ratios. Define the
   admissible range and the throw boundary at spec time (D9 covers non-finite /
   non-positive; the *degenerate-but-finite* case needs an explicit call).

---

## Provenance

- **Roadmap node.** `ROADMAP.md:280` — `## design:primitive/wdf-adaptors`,
  `status: planned`, `depends-on: multi:feature/phase-numerical-solvers`,
  `part-of: multi:feature/phase-wave-digital-filters`. Design pointer set via
  `stackctl workflow link-design` to this file; compass verdict `on-course`
  (`planned → designing`).
- **Builds on (shipped).** `docs/superpowers/specs/2026-07-09-wdf-primitives-design.md`;
  `specs/wdf-primitives/spec.md`, `.../plan.md`,
  `.../contracts/wdf-one-ports.md`. Interface quoted from
  `core/primitives/circuit/wdf/one-port.h` (I1–I3, W1). The withheld general
  reflection annotated at `wave-elements.h:31-34`; junction wave↔KCL note at
  `wave-terminations.h:24-27,86-88`.
- **Codebase map.** Read-only Explore pass over the shipped WDF tree
  (`core/primitives/circuit/wdf/`, 5 headers / 637 lines) and its tests
  (`tests/core/wdf-*-test.cpp`, `tests/support/allocation-sentinel.h`); confirmed
  no existing connection/adaptor code.
- **WDF theory.** Fettweis voltage-wave wave digital filters; series/parallel
  adaptor scattering and the adapted-port ("reflection-free") formulation; the
  at-most-one-reflective-port-at-the-root evaluability rule. Formulas re-derived
  in D3–D5 and pinned to the shipped W1 sign convention rather than transcribed.
- **Repo standards.** `CLAUDE.md` / constitution — real-time safety (no heap/locks
  on `process()`), strict typing (no `any`/casts), files ~300–500 lines,
  composition-over-inheritance / interface-first, no fallbacks. Solver-independence
  of WDF from `wdf-primitives/plan.md` and `DEVELOPMENT-NOTES.md:232`.
- **Validation stance.** [[circuit-model-validation-approach]] — exact closed
  forms + exact limits + monotonic/passivity invariants, not published rationals.
- **Backend & house rules.** Designed via `superpowers:brainstorming` driven under
  stack-control design house rules `stack-control-design-v1`
  (capture-over-YAGNI re-asserted at the scope-check; handoff routed to
  `/stack-control:define`). Scope boundary (D1) decided by the operator's explicit
  delegation to project goals / guidelines / existing work.

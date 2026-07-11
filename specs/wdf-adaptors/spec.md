> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: WDF-adaptors

**Feature Branch**: `wdf-adaptors`

**Created**: 2026-07-11

**Status**: Draft

**Input**: Design record `docs/superpowers/specs/2026-07-10-wdf-adaptors-design.md`
(operator-approved 2026-07-11, third-party-reviewed); roadmap item
`design:primitive/wdf-adaptors`, part of `multi:feature/phase-wave-digital-filters`,
depends-on `multi:feature/phase-numerical-solvers`. Builds directly on the shipped
`design:primitive/wdf-primitives`.

## Context

Phase 6 (Wave Digital Filters) opened with the shipped **`wdf-primitives`** node: eight
leaf one-ports (`Resistor`, `Capacitor`, `Inductor`, resistive/ideal sources,
short/open) exposing a single **duck-typed `OnePort` concept** — `portResistance()`,
`reflected()`, `incident(a)`, and a compile-time `isAdaptable` flag — under the
voltage-wave convention `a = v + Rp·i`, `b = v − Rp·i` (current `i` referenced into the
port). Those leaves are **not connected to anything**: `wdf-primitives` deliberately
withheld the general reflection `b = a·(R − Rp)/(R + Rp)` from the leaves, annotating it
"the adaptor layer's concern." **This node is that layer.**

**What an adaptor is.** A Wave Digital Filter **adaptor** is an N-port **scattering
junction** that connects one-ports. A **series** adaptor enforces shared port current and
Kirchhoff's voltage law (`Σ v = 0`); a **parallel** adaptor enforces shared port voltage
and Kirchhoff's current law (`Σ i = 0`). Each port `k` carries a reference resistance
`R_k`; the junction maps the ports' incident waves to their reflected waves through a
scattering relation fixed by those resistances.

**Adaptors are themselves one-ports (the load-bearing decision).** Each adaptor makes its
**upward-facing port** *reflection-free* (adapted) by choosing that port's resistance from
its children — `R_up = Σ R_child` (series) or `1/R_up = Σ 1/R_child` (parallel). Because
it satisfies the same `OnePort` concept the leaves do, an adaptor **nests recursively** as
a child of another adaptor, and connects the shipped leaves through the *same* seam. A
whole filter is therefore one statically-composed tree of adaptors and leaves, evaluated
by a cheap **up-sweep** (each node reports its reflected wave from stored state) then a
**down-sweep** (incident waves distribute back down) — **no matrix solve**, O(N) per
sample.

**The evaluability rule that shapes the whole design.** A WDF tree may contain **at most
one reflective (non-adaptable) port, and it must be the root.** Every other port must be
adapted so the up-sweep can compute a node's reflected wave before any incident wave
arrives. A reflective element anywhere but the root closes an instantaneous (delay-free)
loop the sweep cannot solve. This node draws that rule as a **compile-time boundary**:
adaptors accept only adaptable children, so every adaptor is itself adaptable; the single
permitted reflective element (short/open, or a future ideal-source / nonlinear root) is
terminated at the tree root by the sibling nodes, never as an adaptor child.

**Scope split (drawn by the roadmap's four WDF nodes).** This node owns the **series and
parallel adaptors as recursive self-adapting one-ports** and their **local upward-port
adaptation**. Per the **capture-over-YAGNI** rule every cross-cut is *captured* (see
Assumptions and the design record's Open Questions) but the following are *implemented by
siblings, not here*:

- **Whole-tree topology, root-port selection, R-type / rigid adaptors, named passive
  networks, and the single-sample root driver** → `wdf-passive-networks`.
- **Ideal (non-resistive) source root (`b = 2E − a`) and the nonlinear root
  (`b = f(a)`, iterative)** → `wdf-complete-analog-stages`.

**Sibling boundary.** `wdf-adaptors` consumes the `OnePort` port ABI fixed by
`wdf-primitives` and produces adaptors that are themselves `OnePort`s;
`wdf-passive-networks` assembles adaptors + leaves into whole trees, chooses the root
port, and adds the root driver; `wdf-complete-analog-stages` adds the reflective
source/nonlinear roots those trees terminate on.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Connect one-ports in series as a composable one-port (Priority: P1)

A caller has two or more one-ports (e.g. a resistor and a capacitor) sharing a single
current path and wants their **series** combination as a *single* wave-domain one-port
that can itself be connected further. The series adaptor exposes a port resistance, a
reflected wave, and an incident-wave setter — the full `OnePort` surface — computed from
its children's port resistances and waves.

**Why this priority**: The series junction is one of the two irreducible WDF connection
primitives and the minimal proof that an adaptor can be a composable one-port. Without it
the shipped leaves cannot be assembled at all.

**Independent Test**: Build a series adaptor over two resistors `R_a`, `R_b`; assert its
port resistance equals `R_a + R_b`, and that a known incident wave produces child
incident waves whose recovered port voltages satisfy the exact series voltage-divider
identity (via `waveToVoltage`/`waveToCurrent`) — a single-sample algebraic check needing
no source, root, or time loop.

**Acceptance Scenarios**:

1. **Given** a series adaptor over children with resistances `R_k`, **When** its port
   resistance is queried, **Then** it equals `Σ R_k`.
2. **Given** a two-resistor series adaptor and an incident upward wave `a_u`, **When** the
   down-sweep runs, **Then** each child's incident wave equals the series scattering
   result `b_k = a_k − (2·R_k/R)·Σ_i a_i` and the recovered voltages match the Ohm's-law
   series divider to within a tight numerical tolerance.

---

### User Story 2 - Connect one-ports in parallel as a composable one-port (Priority: P1)

A caller has two or more one-ports sharing a single voltage and wants their **parallel**
combination as a single composable one-port — the dual of the series case.

**Why this priority**: The parallel junction is the second irreducible connection
primitive; together with series it spans every ladder/passive topology WDF targets.

**Independent Test**: Build a parallel adaptor over two resistors; assert its port
resistance satisfies `1/R_up = 1/R_a + 1/R_b` (conductance sum), and that a known incident
wave produces child incident waves whose recovered voltages/currents satisfy the exact
current-divider identity — single-sample, no root.

**Acceptance Scenarios**:

1. **Given** a parallel adaptor over children with conductances `G_k = 1/R_k`, **When**
   its port resistance is queried, **Then** `1/R_up = Σ G_k`.
2. **Given** a two-resistor parallel adaptor and an incident upward wave `a_u`, **When**
   the down-sweep runs, **Then** each child's incident wave equals the parallel scattering
   result `b_k = 2·(Σ_i G_i·a_i)/G − a_k` and the recovered quantities match the Ohm's-law
   current divider to within a tight numerical tolerance.

---

### User Story 3 - Nest adaptors into a filter tree (Priority: P1)

A caller composes adaptors and leaves into a **tree** — an adaptor whose children are
themselves adaptors and/or leaves (e.g. a resistor in series with a parallel `L`‖`C`).
Because an adaptor is a `OnePort`, nesting requires no new mechanism: the same
up-sweep/down-sweep protocol recurses to arbitrary depth and width (**N-port**: N children
plus one upward port).

**Why this priority**: Recursive composability *is* the point of making adaptors
one-ports; it is what lets whole filters be built from the two junction primitives without
a second connection API. It also validates the N-port generalization over the 3-port case.

**Independent Test**: Build `SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>>`;
assert the composite satisfies the `OnePort` concept, that its port resistance equals the
series/parallel combination of the subtree, and that one up-sweep + down-sweep produces
child waves consistent with the nested scattering (checked against the composed
closed-form at a single sample).

**Acceptance Scenarios**:

1. **Given** an adaptor with an adaptor child, **When** the composite's port resistance is
   read, **Then** it equals the recursive series/parallel combination of the whole subtree.
2. **Given** a nested tree, **When** an up-sweep then down-sweep is run for one sample,
   **Then** every node's produced waves equal the values computed by evaluating the tree's
   scattering relations by hand at that sample.

---

### User Story 4 - The adapted upward port is reflection-free (Priority: P2)

A caller relies on the WDF adaptation property: an adaptor's upward port reflects **no**
wave that depends on this sample's upward incident wave — its `reflected()` is a function
of its children's state only. This is what makes the adaptor connectable above (to a
parent adaptor or a root) without a delay-free loop.

**Why this priority**: Reflection-freedom of the adapted port is the invariant the whole
tree's computability rests on; it must be validated directly, independent of any specific
filter.

**Independent Test**: For a series and a parallel adaptor, hold the children's state fixed,
vary the upward incident wave `a_u` over a range, and assert `reflected()` (`b_u`) is
invariant to within machine precision.

**Acceptance Scenarios**:

1. **Given** any adaptor with fixed child state, **When** `reflected()` is read for two
   different upward incident waves, **Then** the two values are identical (to machine
   precision).
2. **Given** a series adaptor, **When** its adapted-port relation is examined, **Then**
   `b_u = −Σ_child a_child`; **Given** a parallel adaptor, **Then**
   `b_u = (Σ_child G_child·a_child)/G_up`.

---

### User Story 5 - Real-time safety and construction-time validation (Priority: P2)

A caller composes an adaptor tree once (off the audio path) and then evaluates it every
sample inside the audio callback. Construction validates all inputs and precomputes every
coefficient; the per-sample sweep does no heap allocation, takes no lock, and throws
nothing. Non-physical inputs are rejected at construction with a descriptive error — never
clamped, never silently corrected (no fallbacks).

**Why this priority**: Real-time safety and the no-fallbacks stance are project
constitution principles; a primitive that allocates or clamps on the hot path is unusable
downstream regardless of correctness.

**Independent Test**: Run an up-sweep/down-sweep for many samples under an allocation
sentinel and assert zero heap allocations; construct adaptors with a non-positive /
non-finite child resistance and with an empty child set and assert each throws a
descriptive `std::invalid_argument` at construction.

**Acceptance Scenarios**:

1. **Given** a constructed adaptor tree, **When** the sweep runs for N samples under an
   allocation sentinel, **Then** zero heap allocations occur and no exception is thrown.
2. **Given** a child port resistance that is non-positive or non-finite, **When** the
   adaptor is constructed, **Then** construction throws `std::invalid_argument` with a
   message naming the offending input.
3. **Given** an attempt to construct an adaptor with no children, **When** construction
   runs, **Then** it throws `std::invalid_argument` (an adaptor requires ≥1 child).

---

### User Story 6 - Reach nested elements through a typed accessor (Priority: P2)

Because an adaptor owns its children by value, a caller (a downstream sibling, or a test)
that needs to read a nested probe or set a nested source value must reach into the owned
subtree. The adaptor provides a compile-time-typed indexed accessor `child<I>()` returning
a reference to the `I`-th child, resolved at compile time with no indirection.

**Why this priority**: Owned-by-value composition is only usable downstream if nested
access is defined; leaving it unspecified would block `wdf-passive-networks` and the tests
that drive nested sources/probes. It is a P2 (not P1) because the core scattering does not
depend on it.

**Independent Test**: Build a tree with a nested `ResistiveVoltageSource`; via `child<I>()`
read the child's port resistance and confirm it is the same object the adaptor scatters
against (mutating the child's drive value is observed by a subsequent sweep).

**Acceptance Scenarios**:

1. **Given** an adaptor with heterogeneous children, **When** `child<I>()` is called with
   a compile-time index, **Then** it returns a reference to the `I`-th child of its exact
   static type.
2. **Given** access to a nested source via `child<I>()`, **When** its drive value is
   changed and a new sweep is run, **Then** the produced waves reflect the changed value.

---

### User Story 7 - Junctions are lossless (validated passivity) (Priority: P3)

A caller depends on adaptors being **lossless** scattering junctions — they neither create
nor dissipate energy, which is what preserves the whole tree's guaranteed passivity and
unconditional stability. This is a *validated* property, never an enforced clamp.

**Why this priority**: Passivity is the headline guarantee of the WDF paradigm, but at the
adaptor level it is a validation obligation rather than a distinct capability, so it ranks
below the scattering and safety stories.

**Independent Test**: For random admissible child resistances and random incident waves,
assert the **conductance-weighted pseudo-power balance** `Σ_k G_k·a_k² = Σ_k G_k·b_k²`
(with `G_k = 1/R_k`) holds across all ports to within a tight tolerance. Confirm the
*unweighted* `Σ a_k² = Σ b_k²` is NOT asserted (it holds only for equal branch
resistances).

**Acceptance Scenarios**:

1. **Given** an adaptor and any admissible incident-wave vector, **When** the reflected
   waves are computed, **Then** `Σ_k G_k·a_k² = Σ_k G_k·b_k²` to within tolerance.

---

### Edge Cases

- **Reflective child rejected at compile time.** Attempting to build an adaptor whose
  child is non-adaptable (`ShortCircuit`, `OpenCircuit`, or any `isAdaptable == false`
  type) is a **compile-time error** — it would create a delay-free loop. The one reflective
  element belongs at the root, handled by the sibling nodes.
- **Single-child adaptor.** An adaptor over exactly one child is admissible (`R_up = R_child`
  for series; `R_up = R_child` for parallel) and must behave as a transparent pass-through
  in the wave domain.
- **Empty child set.** Constructing an adaptor with zero children is rejected at
  construction (`std::invalid_argument`).
- **Degenerate-but-finite resistances.** Very small or very large (but positive, finite)
  child resistances are admissible; the admissible range and any conditioning safeguard for
  the `Σ R` / `Σ G` accumulation and the coefficient ratios are defined during planning
  (design Open Question 8/9). Non-finite or non-positive values are rejected (US5).
- **Wide junctions (large N).** N-port adaptors with many children are supported; ordered
  summation for `Σ R` / `Σ G` is considered during planning if conditioning warrants.
- **Mismatched root resistance.** When the tree root terminates the top adaptor, the root
  element's port resistance must equal the top adaptor's `R_up`; enforcing/observing that
  match is the root-driver sibling's concern, captured here as an interface expectation.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a **series adaptor** and a **parallel adaptor**, each
  connecting **N ≥ 1** child one-ports plus one upward port, and each itself satisfying the
  shipped `OnePort` concept (`portResistance()`, `reflected()`, `incident(a)`,
  `isAdaptable`).
- **FR-002**: The series adaptor MUST implement the scattering relation
  `b_k = a_k − (2·R_k/R)·Σ_i a_i` (`R = Σ_i R_i`), enforcing shared port current and
  `Σ v_k = 0`.
- **FR-003**: The parallel adaptor MUST implement the scattering relation
  `b_k = 2·(Σ_i G_i·a_i)/G − a_k` (`G = Σ_i G_i`, `G_k = 1/R_k`), enforcing shared port
  voltage and `Σ i_k = 0`.
- **FR-004**: Each adaptor MUST make its **upward port** the adapted (reflection-free) port
  with `R_up = Σ_child R_child` (series) / `1/R_up = Σ_child 1/R_child` (parallel), so that
  `reflected()` depends only on child state, not on this sample's upward incident wave.
- **FR-005**: `reflected()` (up-sweep) MUST compute the adapted upward reflected wave from
  the children's reflected waves (`b_u = −Σ_child a_child` series;
  `b_u = (Σ_child G_child·a_child)/G_up` parallel), caching the child waves for the
  down-sweep.
- **FR-006**: `incident(a_u)` (down-sweep) MUST compute each child's incident wave from
  `a_u` and the cached child waves via the FR-002/FR-003 scattering and deliver it to that
  child's `incident()`.
- **FR-007**: Adaptors MUST accept **only adaptable children** — a non-adaptable
  (`isAdaptable == false`) child MUST be a **compile-time** rejection — and each adaptor
  MUST itself report `isAdaptable == true`.
- **FR-008**: Adaptors MUST **own their children by value** (composition), forming a single
  statically-composed tree with no pointer indirection on the evaluation path.
- **FR-009**: Adaptors MUST be **variadic** on their child `OnePort` types (general N-port),
  with the two-child (3-port) case as an ordinary instantiation; each MUST `static_assert`
  that every child satisfies the `OnePort` concept.
- **FR-010**: All scattering coefficients MUST be **precomputed at construction** from the
  children's `portResistance()`; the per-sample `reflected()`/`incident()` path MUST be
  `noexcept`, allocation-free, lock-free, and O(N).
- **FR-011**: Construction MUST **validate** inputs and throw `std::invalid_argument` (off
  the hot path) for a non-positive or non-finite child port resistance and for an empty
  child set; the system MUST NOT clamp, substitute, or otherwise fall back.
- **FR-012**: Each adaptor MUST expose a compile-time-typed accessor `child<I>()` (and a
  `const` overload) returning a reference to the `I`-th owned child of its exact static
  type.
- **FR-013**: The adaptors MUST reuse the shipped voltage-wave convention verbatim
  (`a = v + Rp·i`, `b = v − Rp·i`, `waveToVoltage`/`waveToCurrent`); they MUST NOT introduce
  a new wave convention.
- **FR-014**: The junctions MUST be **lossless**: for all admissible inputs the
  conductance-weighted pseudo-power balance `Σ_k G_k·a_k² = Σ_k G_k·b_k²` MUST hold (to
  numerical tolerance). Passivity MUST be validated, never enforced by clamping.
- **FR-015**: The implementation MUST live under `core/primitives/circuit/wdf/`, header-only,
  each file within the ~300–500-line budget (design D10: a shared `adaptor-detail.h` for the
  common sweep machinery, plus `series-adaptor.h` and `parallel-adaptor.h`), using
  project-root-relative include paths.
- **FR-016**: The validation suite MUST cover, **without a root driver**: exact scattering
  on series and parallel resistive dividers (closed form), the reflection-free property of
  the adapted port, the conductance-weighted passivity balance, and an allocation-sentinel
  RT-safety test. Full network **transfer-function** / frequency-response tests, which
  require a source, a root, and a runnable tree over time, are **deferred to the
  root-driver-owning sibling** (`wdf-passive-networks`) and are out of scope here.

### Key Entities

- **Series adaptor**: an N-port scattering junction enforcing shared current + KVL; a
  `OnePort` whose adapted upward port has resistance `Σ R_child`. Holds its children by
  value and caches their per-sample reflected waves.
- **Parallel adaptor**: the dual — shared voltage + KCL; a `OnePort` whose adapted upward
  port has conductance `Σ G_child`.
- **Adapted upward port**: the single reflection-free port each adaptor presents to its
  parent; the mechanism by which adaptors nest without delay-free loops.
- **Child (owned one-port)**: any adaptable `OnePort` (leaf or nested adaptor) owned by an
  adaptor by value and reachable via `child<I>()`.
- **Tree root (interface expectation)**: the single reflective port that terminates a whole
  tree, owned and driven by the sibling nodes — referenced here only as the boundary the
  adapted upward port connects to.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: For two-resistor series and parallel adaptors, the computed child incident
  waves and recovered port voltages/currents match the exact Ohm's-law divider identities
  to within `1e-12` relative error.
- **SC-002**: An adaptor's `reflected()` is invariant under changes to the upward incident
  wave (fixed child state) to within machine epsilon (`< 1e-15` absolute), confirming the
  adapted port is reflection-free.
- **SC-003**: A composed nested tree (`series(R, parallel(L, C))`) satisfies the `OnePort`
  concept and reproduces the by-hand nested scattering at a sampled instant to within
  `1e-12`.
- **SC-004**: The conductance-weighted pseudo-power residual `|Σ G_k a_k² − Σ G_k b_k²|`,
  normalized by `Σ G_k a_k²`, is below `1e-12` across randomized admissible inputs for both
  adaptor kinds.
- **SC-005**: The per-sample up-sweep/down-sweep performs **zero** heap allocations over an
  arbitrarily long run (allocation sentinel), and no evaluation-path method is capable of
  throwing.
- **SC-006**: Every non-physical construction input (non-positive resistance, non-finite
  resistance, empty child set) produces a descriptive construction-time error and **no**
  silently clamped or fabricated value.
- **SC-007**: Building an adaptor with a non-adaptable child fails to compile (the
  delay-free-loop guard), demonstrated by a compile-fail test or documented static
  assertion.

## Assumptions

- **A1** — The shipped `wdf-primitives` `OnePort` concept and voltage-wave convention
  (`one-port.h`) are fixed and consumed as-is; this node adds no leaf and changes no
  convention.
- **A2** — The single reflective root that terminates a whole tree, the root driver, whole-
  tree topology/root-port selection, and R-type / rigid adaptors are owned by
  `wdf-passive-networks`; ideal-source and nonlinear roots by
  `wdf-complete-analog-stages`. These are **captured** here (Context, Edge Cases,
  FR-016) but implemented by those siblings — not scope-cut, relocated by the roadmap.
- **A3** — Adaptor coefficients are fixed for the tree's lifetime; **time-varying port
  resistances** (variable component / sample-rate re-adaptation propagation) are a captured
  cross-cut deferred with the leaf-level `setSampleRate` question (design Open Question 7),
  not implemented here.
- **A4** — WDF is a parallel solver path; this node has **no** code dependency on the nodal
  solvers (MNA/Newton/implicit-integration). The `depends-on phase-numerical-solvers`
  roadmap edge is phase-ordering only.
- **A5** — "Users" of this primitive are downstream code (`wdf-passive-networks`,
  `wdf-complete-analog-stages`) and developers composing WDF trees; there is no end-user UI.
- **A6** — Numerical tolerances in the Success Criteria are working targets to be confirmed
  during planning against the shipped primitives' test conventions; the *form* of each
  invariant (exact closed form, reflection-free, weighted passivity) is fixed by the design
  record.

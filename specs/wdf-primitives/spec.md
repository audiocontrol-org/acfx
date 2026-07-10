> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: WDF-primitives

**Feature Branch**: `wdf-primitives`

**Created**: 2026-07-09

**Status**: Draft

**Input**: Design record `docs/superpowers/specs/2026-07-09-wdf-primitives-design.md`
(operator-approved 2026-07-09, third-party-reviewed); roadmap item
`design:primitive/wdf-primitives`, part of `multi:feature/phase-wave-digital-filters`,
base of the phase whose sibling nodes are `wdf-adaptors`, `wdf-passive-networks`,
and `wdf-complete-analog-stages`.

## Context

Phase 6 opens a **second, complementary circuit-modeling paradigm** alongside the
shipped nodal solver trio (`modified-nodal-analysis`, `newton-iteration`,
`implicit-integration`, all closed). This is the **base of that phase**: three sibling
nodes build on the vocabulary it establishes.

**How WDF differs from the nodal trio.** The nodal solvers stamp every element's
contribution into an augmented system `A·x = z` and **solve the matrix** for node
voltages. Wave Digital Filters instead transform each element into a **one-port in the
wave domain**: a port carries an **incident wave** `a` and a **reflected wave** `b`,
related to the Kirchhoff voltage `v` and current `i` through a **port resistance** `Rp`.
Each element imposes a **scattering (reflection) relation** `b = f(a)` fixed by its
constitutive law. Elements connect via **adaptors** (scattering junctions) into a
**tree**, evaluated by a cheap **up-sweep** (children report reflected waves) then a
**down-sweep** (the root distributes incident waves) — **no matrix solve**.

**The capability (there is no WDF lab predecessor to gain over — this *establishes* the
paradigm):**

- **Guaranteed passivity and unconditional stability** for passive networks — energy is
  provably non-increasing sample-to-sample, by construction of the wave variables and
  passive scattering, independent of sample rate.
- **O(N) per-sample modularity** — each one-port and adaptor is a small local
  computation; there is no global matrix to factor. Ladder/passive topologies are the
  WDF sweet spot.
- **Reactive elements become unit delays** — the WDF "magic": a capacitor's reflected
  wave this sample is simply its incident wave from *last* sample.

This is a **complementary** path, not a replacement: WDF is cheap and unconditionally
stable for passive/ladder circuits but structurally constrained for circuits with
multiple nonlinearities or arbitrary (non-tree) topologies — exactly where MNA + Newton
excel.

**WDF is another reader of the frozen vocabulary.** The circuit-modeling seam was built
solver-neutral on purpose (`DEVELOPMENT-NOTES.md:232`: *"the reference solver, MNA
(Phase 5), and WDF (Phase 6) are all just alternative readers of one immutable
vocabulary — the primitive never learns what a matrix or a wave is."*). The frozen
element value types (`core/primitives/circuit/models/`) hold the **physical parameters**
(`R`, `C`, `L`, `E`, `I`) and expose *nodal* stamps. WDF is a **parallel wave-domain
family** that reuses those physical constants but (a) carries **no `NodeId`** — a WDF
circuit's topology is the **connection tree**, not the node set — and (b) is **stateful
for reactive one-ports** (each holds one sample of wave state), unlike the deliberately
stateless nodal value types.

**Scope split (drawn by the roadmap's four WDF nodes).** This node owns only the **leaf
one-ports** and the **two foundations they force** — the wave convention and the
one-port interface. Adaptors, tree assembly / reflection-free adaptation, and
nonlinear/ideal-source roots are separate planned nodes. Per the **capture-over-YAGNI**
rule, every cross-cut (the adaptor ABI, the adaptation algorithm, the root-element seam,
time-varying elements, variable `dt`) is *captured* here without being *implemented*
beyond the leaf one-ports — mirroring how MNA defined the linear engine plus the
`CompanionSupply` seam and deferred the rest to its siblings.

**Sibling boundary:** `wdf-adaptors` consumes the port ABI defined here;
`wdf-passive-networks` assembles leaves + adaptors into trees and owns adaptation;
`wdf-complete-analog-stages` adds ideal-source and nonlinear roots. This node ships first
and standalone so the ABI is fixed before adaptors are designed against it.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Model a resistor as a wave-domain one-port (Priority: P1)

A caller has a resistor of resistance `R` and wants its wave-domain behavior: a port
resistance and a reflected-wave relation under the voltage-wave convention. Adapted
(port resistance chosen equal to `R`), the resistor is **reflection-free** — it reflects
no wave back.

**Why this priority**: This is the minimal wave-domain one-port and the concrete
realization of the voltage-wave convention (`a = v + Rp·i`, `b = v − Rp·i`). Without it
the family does nothing; with it, the simplest memoryless leaf produces correct
scattering against an exact closed form.

**Independent Test**: Construct a resistor one-port with resistance `R`; assert its port
resistance equals `R` and its adapted reflected wave is exactly `0` for any incident
wave. (The general unadapted reflection `b = a·(R − Rp)/(R + Rp)` is an **analytical
oracle used only by the test suite** to cross-check the adapted `b = 0` limit — it is
NOT a public leaf-API capability; arbitrary reference-resistance behavior is the adaptor
layer's concern.)

**Acceptance Scenarios**:

1. **Given** a resistor one-port of resistance `R`, **When** its port resistance is
   queried, **Then** it equals `R`.
2. **Given** an adapted resistor one-port and any incident wave `a`, **When** its
   reflected wave is read, **Then** it is exactly `0` (reflection-free).

---

### User Story 2 - Reactive one-ports become unit delays (Priority: P1)

A caller has a capacitor `C` (or inductor `L`) and a sample period `T = dt`. The
wave-domain reactive one-port has a port resistance derived from the **bilinear**
discretization and a reflected wave that is the stored incident wave from the previous
sample — a **unit delay** (with a sign inversion for the inductor).

**Why this priority**: The reactive-element-as-unit-delay is the defining property of
WDF and the reason the paradigm is cheap and unconditionally stable. It is the capability
that distinguishes WDF from the nodal companion model.

**Independent Test**: Construct a capacitor one-port `(C, dt)`; assert its port
resistance equals `T/(2C)`; drive a sequence of incident waves and assert each sample's
reflected wave equals the previous sample's incident wave (`b[n] = a[n−1]`). Repeat for
the inductor `(L, dt)` with `Rp = 2L/T` and `b[n] = −a[n−1]`.

**Acceptance Scenarios**:

1. **Given** a capacitor one-port `(C, dt)`, **When** its port resistance is queried,
   **Then** it equals `T/(2C)`; **and When** driven by an incident-wave sequence,
   **Then** `b[n] = a[n−1]` at every sample (zero state initially → `b[0] = 0`).
2. **Given** an inductor one-port `(L, dt)`, **When** its port resistance is queried,
   **Then** it equals `2L/T`; **and When** driven, **Then** `b[n] = −a[n−1]`.

---

### User Story 3 - Inject a signal through a resistive source one-port (Priority: P1)

A caller drives the network from a **resistive voltage source** (a real/Thévenin source:
value `E` in series with resistance `R`) — the normal audio-input injection point — or a
**resistive current source** (Norton: `I` in parallel with `R`). Adapted (port resistance
`R`), the reflected wave carries the source's drive value.

**Why this priority**: A network with no source produces no signal. The resistive voltage
source is how the input signal enters a WDF, and both resistive sources are adaptable
leaves needed for any driven passive network.

**Independent Test**: Construct a resistive voltage source `(E, R)`; assert its port
resistance equals `R` and its adapted reflected wave equals `E`; update the drive value
per sample and assert the reflected wave tracks it. Construct a resistive current source
`(I, R)`; assert port resistance `R` and adapted reflected wave `R·I`.

**Acceptance Scenarios**:

1. **Given** a resistive voltage source `(E, R)`, **When** the port resistance and adapted
   reflected wave are read, **Then** they equal `R` and `E`; **and When** the drive value
   is updated to `E'`, **Then** the reflected wave becomes `E'`.
2. **Given** a resistive current source `(I, R)`, **When** the port resistance and adapted
   reflected wave are read, **Then** they equal `R` and `R·I`.

---

### User Story 4 - Terminate a port: resistive, short, open (Priority: P1)

A caller terminates a port with a matched **resistive** load (reflection-free), a **short
circuit** (`v = 0`), or an **open circuit** (`i = 0`). The short and open are **reflective**
(non-adaptable) one-ports whose reflection is `−a` and `+a` respectively, independent of
any port resistance.

**Why this priority**: Terminations close off ports in a network; the roadmap names
resistive/short/open explicitly. They also exercise the **reflective (non-adaptable)**
classification the port interface must report — the first elements that cannot be made
reflection-free.

**Independent Test**: Construct a resistive termination `R`; assert adapted reflected wave
`0`. Construct a short; assert `b = −a` for any `a`. Construct an open; assert `b = +a`
for any `a`. Assert short/open report themselves **non-adaptable** (no free port
resistance).

**Acceptance Scenarios**:

1. **Given** a resistive termination of resistance `R`, **When** its adapted reflected
   wave is read, **Then** it is `0` (matched load).
2. **Given** a short-circuit termination, **When** driven by incident wave `a`, **Then**
   `b = −a`; **and Given** an open-circuit termination, **Then** `b = +a`.
3. **Given** a short or open termination, **When** its adaptability is queried, **Then**
   it reports **non-adaptable** (reflective).

---

### User Story 5 - Satisfy the one-port port interface the adaptors will consume (Priority: P1)

Every leaf one-port satisfies a single **duck-typed port interface** (a concept, not a
base class): report the port resistance, return the reflected wave (up-sweep), accept an
incident wave (down-sweep), and report whether it is adaptable or reflective. This is the
seam the sibling `wdf-adaptors` node connects to.

**Why this priority**: Defining the port interface here — not letting the adaptor node
retro-define it — is what makes this node the foundation. Every downstream WDF node
depends on this contract being fixed and uniform across all leaves.

**Independent Test**: For each leaf type, statically confirm it exposes
`portResistance()`, `reflected()`, and `incident(a)` (all `noexcept`) and an
adaptable/reflective classifier; drive a generic (templated) up-sweep/down-sweep over a
heterogeneous set of leaves and confirm each is invoked through the same interface with no
virtual dispatch.

**Acceptance Scenarios**:

1. **Given** any leaf one-port, **When** the interface is exercised, **Then**
   `portResistance()`, `reflected()`, and `incident(a)` are callable and `noexcept`, and
   the leaf reports whether it is adaptable or reflective.
2. **Given** a heterogeneous collection of leaves driven through the interface generically,
   **When** an up-sweep reads `reflected()` and a down-sweep calls `incident(a)`, **Then**
   each leaf responds per its type with no runtime (virtual) dispatch.

---

### User Story 6 - Construct with parameter + dt; run a real-time-safe wave path (Priority: P1)

A reactive leaf is **constructed with its physical parameter and `dt`** (e.g.
`Capacitor(C, dt)`), computing its port resistance once in the constructor; memoryless
leaves are constructed with their fixed parameter. Thereafter the per-sample wave path
(`portResistance` / `reflected` / `incident`) performs **zero** heap allocation and takes
no locks.

**Why this priority**: Real-time safety (Constitution Principle VI) is non-negotiable on
the audio path. Pinning the construction/prepare lifecycle at the leaf (rather than a
netlist-scale two-phase `prepare()`) removes ambiguity and keeps port-resistance
computation off the hot path.

**Independent Test**: Construct each leaf with its parameter (and `dt` for reactive
leaves); run many `reflected()`/`incident()` cycles under an `AllocationSentinel`
asserting zero heap allocations; confirm no leaf exposes a per-leaf re-prepare/sample-rate
API in v1.

**Acceptance Scenarios**:

1. **Given** a reactive leaf constructed as `(param, dt)`, **When** it is created, **Then**
   its port resistance is computed once in the constructor and readable immediately.
2. **Given** any constructed leaf, **When** its wave path runs across many samples,
   **Then** it performs zero heap allocations and takes no locks.

---

### User Story 7 - Validate passivity and physical invariants (Priority: P2)

Each passive leaf's scattering is validated against physical invariants, using **two
distinct criteria** because instantaneous and stateful passivity are different properties:

- **Memoryless passive leaves** (adapted resistor, resistive termination) are validated by
  the **instantaneous** scattering bound `|b| ≤ |a|` (energy non-increasing same-sample)
  and `Rp > 0`.
- **Reactive leaves** (capacitor, inductor) are validated by their **unit-delay scattering
  relation**, **capacitor/inductor duality**, and a **wave-power balance across state
  transitions** — NOT same-sample magnitude. (Same-sample `|b| ≤ |a|` is invalid for a
  reactive leaf: `b[n] = a[n−1]` returns energy stored from a previous sample, so e.g.
  `a[n−1]=1, a[n]=0` gives `b[n]=1 > |a[n]|=0` for a perfectly correct capacitor.)

**Why this priority**: Passivity is the property that gives WDF its unconditional
stability; asserting it per element (before adaptors combine them) is how this node earns
trust. P2 because it validates rather than delivers new capability.

**Independent Test**: For the **memoryless** passive leaves, drive a range of incident
waves and assert `|b| ≤ |a|` and `Rp > 0`. For the **reactive** leaves, assert the exact
unit-delay relation (`b[n] = a[n−1]` / `−a[n−1]`), assert cap/inductor duality, and assert
the accumulated wave-power balance `Σ(a[k]² − b[k]²) ≥ 0` over a driven sequence (for the
lossless capacitor it telescopes to the currently-stored `a[N]²`, demonstrating energy is
**stored and returned**, never created or dissipated).

**Acceptance Scenarios**:

1. **Given** a memoryless passive leaf, **When** driven by incident waves, **Then**
   `|b| ≤ |a|` (no energy created same-sample) and its port resistance is positive.
2. **Given** a reactive leaf driven by an incident-wave sequence, **When** the wave-power
   balance is accumulated, **Then** `Σ(a[k]² − b[k]²) ≥ 0` at every prefix and equals the
   stored wave energy (`a[N]²` for the capacitor) — passive and lossless, without requiring
   same-sample `|b| ≤ |a|`.
3. **Given** a capacitor and an inductor under the same `dt`, **When** their port
   resistances and reflected-wave signs are compared, **Then** they exhibit the dual
   relationship (`Rp = T/2C` / `b = +state` vs. `Rp = 2L/T` / `b = −state`).

---

### User Story 8 - No fallback: surface non-physical parameters (Priority: P2)

A non-physical parameter (`R ≤ 0`, `C ≤ 0`, `L ≤ 0`, `dt ≤ 0`) is surfaced as a
configuration error, never silently clamped to a "safe" value, and reflections are never
limited to fake passivity.

**Why this priority**: No-fallback honesty (Constitution Principle V) is a hard project
rule, consistent with MNA (no silent gmin) and the nodal elements' precondition
discipline. A clamped port resistance or a masked reflection is a bug factory.

**Independent Test**: Construct leaves with non-physical parameters and assert the error is
surfaced (precondition violation), not a substituted value; assert no code path clamps a
reflection to force `|b| ≤ |a|` when a parameter is out of range.

**Acceptance Scenarios**:

1. **Given** a leaf constructed with a non-physical parameter, **When** it is created,
   **Then** the error is surfaced deterministically, not papered over with a fallback
   value.
2. **Given** any leaf, **When** its reflection is computed, **Then** the value is never
   clamped to force apparent passivity — passivity is validated, not enforced.

---

### Edge Cases

- **Reference resistance ≠ natural resistance (unadapted port).** A resistor's general
  reflection `b = a·(R − Rp)/(R + Rp)` is nonzero when the reference resistance differs
  from `R`. The leaf API is **adapted-only** (`Rp = R` → `b = 0`); the general form is a
  test-suite oracle, and arbitrary reference-resistance behavior (choosing `Rp` across a
  tree) is the downstream adaptor/adaptation concern, not a leaf capability.
- **Zero initial state.** A reactive leaf's stored wave defaults to zero, so its first
  reflected wave is `0` (`b[0] = 0` for the capacitor, `−0` for the inductor). The first
  `incident(a)` establishes the state the next sample reflects.
- **Short / open are non-adaptable.** Their reflection (`∓a` / `±a`) is independent of any
  port resistance; there is no reflection-free choice. They report reflective and, in a
  tree, can only sit where a reflective port is valid (a root/leaf position the adaptor
  node decides) — this node only reports the fact.
- **Ideal (non-resistive) source / nonlinear element.** An ideal voltage source
  (`b = 2E − a`) and a nonlinear one-port (diode) are the other non-adaptable, root-facing
  one-ports; they are **out of scope for v1** (`wdf-complete-analog-stages`). The port
  interface is designed so such a root satisfies the same `reflected()`/`incident()`
  protocol.
- **Sample-rate change / time-varying parameter.** A `dt` change or a swept `R`/`C`/`L`
  changes a reactive leaf's port resistance; v1 handles this by **reconstruction** (off the
  hot path) and downstream tree re-adaptation, never an in-place mutation — there is no
  per-leaf re-prepare API in v1.
- **Source drive update.** A resistive source's drive value (`E` / `I`) is the per-sample
  audio input and is updated via a setter between samples; the port resistance stays fixed.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The family MUST adopt the **voltage-wave convention** with an explicit port
  reference resistance `Rp`: incident `a = v + Rp·i`, reflected `b = v − Rp·i`, inverse
  `v = (a + b)/2` and `i = (a − b)/(2·Rp)`, current referenced into the port.
- **FR-002**: A **resistor** one-port MUST report port resistance `Rp = R` and, adapted,
  reflected wave `b = 0`. The public leaf API is **adapted-only** (`portResistance` /
  `reflected` / `incident` describe an adapted leaf); the general unadapted reflection
  `b = a·(R − Rp)/(R + Rp)` is an analytical identity for the **test suite only** (a
  validation oracle), NOT a public leaf capability — arbitrary reference-resistance
  behavior belongs to the adaptor layer.
- **FR-003**: A **capacitor** one-port MUST discretize via the **bilinear** rule to port
  resistance `Rp = T/(2C)` (`T = dt`) with reflected wave `b[n] = a[n−1]` — a unit delay of
  the incident wave.
- **FR-004**: An **inductor** one-port MUST discretize via the bilinear rule to port
  resistance `Rp = 2L/T` with reflected wave `b[n] = −a[n−1]` — the dual of the capacitor
  (unit delay with sign inversion).
- **FR-005**: The reactive discretization MUST be **bilinear** and **non-selectable at the
  leaf** — no per-leaf integration-rule knob (bilinear is what yields the unit-delay
  structure and preserves passivity).
- **FR-006**: A **resistive voltage source** one-port (`E` in series with `R`) MUST report
  port resistance `Rp = R` and, adapted, reflected wave `b = E`, with `E` updatable per
  sample (the audio-input injection).
- **FR-007**: A **resistive current source** one-port (`I` in parallel with `R`) MUST
  report port resistance `Rp = R` and, adapted, reflected wave `b = R·I`.
- **FR-008**: A **resistive termination** MUST behave as a resistor load: port resistance
  `Rp = R`, adapted reflected wave `b = 0` (matched).
- **FR-009**: A **short-circuit** termination (`v = 0`) MUST report reflected wave `b = −a`
  and classify **non-adaptable**; an **open-circuit** termination (`i = 0`) MUST report
  `b = +a` and classify **non-adaptable**.
- **FR-010**: Every leaf one-port MUST satisfy a single **duck-typed port interface** (a
  concept, NOT a virtual base class): `double portResistance() const noexcept`,
  `double reflected() const noexcept`, `void incident(double a) noexcept`, plus an
  adaptable/reflective classifier. Dispatch MUST be static (no vtable, no RTTI).
- **FR-011**: The port interface MUST support the WDF **up-sweep / down-sweep** protocol:
  `reflected()` returns the reflected wave from the leaf's current stored state (available
  before this sample's incident wave); `incident(a)` consumes the down-sweep incident wave
  (reactive leaves store it as next sample's state; memoryless leaves ignore it).
- **FR-012**: Each reactive leaf MUST hold exactly **one** stored wave sample of state (the
  previous incident wave); `reflected()` reads it and `incident(a)` writes it. Memoryless
  leaves MUST hold no state.
- **FR-013**: Reactive leaves MUST be **constructed with their physical parameter and `dt`**
  (e.g. `Capacitor(C, dt)`, `Inductor(L, dt)`), computing the port resistance **once in the
  constructor**; memoryless leaves MUST be constructed with their fixed parameter. There
  MUST be **no per-leaf `prepare()`/re-prepare/sample-rate API in v1**.
- **FR-014**: The per-sample wave path (`portResistance` / `reflected` / `incident`) MUST
  perform **zero** heap allocation and take no locks (Constitution Principle VI). Parameter
  and `dt` validation is throw-permitted at construction, off the hot path.
- **FR-015**: The family MUST reuse the **physical constants** (`R`/`C`/`L`/`E`/`I`) of the
  frozen circuit vocabulary but MUST NOT carry `NodeId` (WDF topology is the connection
  tree) and MUST NOT be a literal reader of the nodal `Component{a, b, …}` container.
- **FR-016**: A non-physical parameter (`R ≤ 0`, `C ≤ 0`, `L ≤ 0`, `dt ≤ 0`) MUST be
  surfaced as a configuration error (precondition discipline), never clamped or substituted
  with a fallback value; reflections MUST never be clamped to force apparent passivity.
- **FR-017**: Passivity MUST be a **validated property**, not an enforced clamp, with
  criteria distinguished by element memory: **memoryless** passive leaves (adapted
  resistor, resistive termination) MUST satisfy the instantaneous bound `|b| ≤ |a|` with
  `Rp > 0`; **reactive** leaves (capacitor, inductor) MUST instead satisfy a **wave-power
  balance across state transitions** — the accumulated absorbed wave energy
  `Σ(a[k]² − b[k]²)` MUST stay `≥ 0` (equalling the stored wave energy for a lossless
  leaf). Same-sample `|b| ≤ |a|` MUST NOT be required of a reactive leaf (its `b[n]`
  returns energy stored from a previous sample).
- **FR-018**: The family MUST be placed under `core/primitives/circuit/wdf/`, namespace
  `acfx::wdf`, sibling to `acfx::mna` / `acfx::newton` / `acfx::integration`; the folder
  MUST be created in the same commit as the primitive.
- **FR-019**: The family MUST be **header-only**, C++17, with no platform headers, and each
  source file MUST stay within the ~300–500 line budget.
- **FR-020**: v1 MUST be validated by **exact scattering closed forms** per element (adapted
  resistor `b = 0`; resistive voltage source `b = E`; resistive current source `b = R·I`;
  capacitor unit delay `b[n] = a[n−1]`; inductor `b[n] = −a[n−1]`; short `b = −a`; open
  `b = +a`), NOT transcribed published values.
- **FR-021**: v1 MUST be validated against **physical invariants**: memoryless-passivity
  (`|b| ≤ |a|`, `Rp > 0`) for memoryless leaves; the reactive **wave-power balance**
  (`Σ(a² − b²) ≥ 0`, energy stored-and-returned) for reactive leaves (FR-017);
  capacitor/inductor **duality**; and agreement of a reactive leaf's discrete port
  impedance with the **bilinear-discretized analog impedance**.
- **FR-022**: A test MUST assert (via `AllocationSentinel`) that the per-sample wave path
  performs zero heap allocations across many samples.
- **FR-023**: Validation MUST be **per-element** (individual one-port scattering, unit
  delay, passivity). Full-circuit frequency response and adaptation correctness are
  explicitly deferred to the adaptor/network nodes and are NOT validated here.
- **FR-024**: The port interface MUST be designed so that a future **root** one-port (ideal
  non-resistive source `b = 2E − a`; nonlinear diode) can satisfy the same
  `reflected()`/`incident()` protocol without reshaping the interface — such roots are out
  of scope for v1 but MUST NOT be precluded.
- **FR-025**: v1 MUST NOT implement adaptors, tree assembly, the reflection-free adaptation
  algorithm, or ideal/nonlinear root elements — these are the sibling nodes
  (`wdf-adaptors`, `wdf-passive-networks`, `wdf-complete-analog-stages`). Each leaf MUST
  only **report** its port resistance and adaptable/reflective status; the adaptation
  decision is downstream.

### Key Entities *(include if feature involves data)*

- **One-port port interface (concept)**: the duck-typed contract every leaf satisfies —
  `portResistance()`, `reflected()`, `incident(a)` (all `noexcept`), plus an
  adaptable/reflective classifier. Statically dispatched (no inheritance), the seam
  `wdf-adaptors` consumes.
- **Resistor one-port**: memoryless; `Rp = R`, adapted `b = 0`. Adaptable.
- **Capacitor one-port**: reactive; `Rp = T/(2C)`, `b[n] = a[n−1]`; one wave sample of
  state; constructed `(C, dt)`. Adaptable.
- **Inductor one-port**: reactive; `Rp = 2L/T`, `b[n] = −a[n−1]`; one wave sample of state;
  constructed `(L, dt)`. Adaptable (dual of capacitor).
- **Resistive voltage source one-port**: `Rp = R`, adapted `b = E` (drive value updatable
  per sample; the signal-input injection). Adaptable.
- **Resistive current source one-port**: `Rp = R`, adapted `b = R·I`. Adaptable.
- **Resistive termination**: `Rp = R`, adapted `b = 0` (matched load). Adaptable.
- **Short-circuit / open-circuit terminations**: reflective; `b = −a` / `b = +a`,
  independent of `Rp`. Non-adaptable.
- **Port resistance `Rp`**: the per-port reference resistance relating wave and Kirchhoff
  variables; fixed at construction for reactive leaves (derived from parameter + `dt`).
- **Wave state**: the single stored incident-wave sample a reactive leaf reflects next
  sample.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every leaf's scattering matches its exact closed form to within tolerance:
  adapted resistor `b = 0`; resistive voltage source `b = E`; resistive current source
  `b = R·I`; capacitor `b[n] = a[n−1]`; inductor `b[n] = −a[n−1]`; short `b = −a`; open
  `b = +a`.
- **SC-002**: Each reactive leaf's port resistance equals its bilinear value exactly
  (`Rp = T/(2C)` capacitor, `Rp = 2L/T` inductor), and driving an incident-wave sequence
  reproduces the unit-delay relation at every sample.
- **SC-003**: For every **memoryless** passive leaf and a range of incident waves,
  `|b| ≤ |a|` holds and the port resistance is positive (the adapted resistor fully
  absorbs, `b = 0`). For every **reactive** leaf, the accumulated wave-power balance
  `Σ(a[k]² − b[k]²) ≥ 0` holds at every prefix and equals the stored wave energy — energy
  stored and returned, validated **without** requiring same-sample `|b| ≤ |a|`.
- **SC-004**: The per-sample wave path performs exactly **zero** heap allocations and takes
  no locks across a large batch of samples (asserted by `AllocationSentinel`).
- **SC-005**: Every leaf is exercised through the single port interface generically (one
  up-sweep/down-sweep driver over a heterogeneous leaf set) with **no** virtual dispatch,
  confirming the uniform ABI the adaptor node will consume.
- **SC-006**: Constructing any leaf with a non-physical parameter surfaces the error (no
  clamped/substituted value), and no reflection is clamped to force apparent passivity —
  zero silent fallbacks.
- **SC-007**: Each shipped source file remains within the ~300–500 line budget, is
  header-only C++17, and lives under `core/primitives/circuit/wdf/` (`acfx::wdf`).
- **SC-008**: The capacitor and inductor demonstrably form a dual pair (port resistance and
  reflected-wave sign swapped under the same `dt`).

## Assumptions

- The frozen circuit vocabulary (`core/primitives/circuit/models/`) supplies the physical
  constants; WDF leaves reuse those constants but are their own wave-domain types (no
  `NodeId`; reactive leaves stateful). No new component types are added to the frozen
  vocabulary.
- The wave convention is **voltage waves**; the reactive discretization is **bilinear**.
  Both are fixed for this family (not runtime-selectable).
- v1 is real-valued (`double`) and transient — leaves stay real even if a later WDF
  analysis wants frequency-domain treatment.
- A reactive leaf's `dt` is fixed at construction; a sample-rate change is handled by
  reconstruction + downstream re-adaptation, not an in-place mutation.
- Topology is expressed by the connection **tree** (adaptors), not by the nodal node set;
  this node defines only the leaf half of the port protocol.
- Adaptors, tree assembly / reflection-free adaptation, and ideal-source / nonlinear roots
  are the sibling WDF nodes and are out of scope for v1 (captured cross-cuts, not YAGNI
  cuts).

## Open Questions

1. **Signal I/O convention (design Open Q1).** The resistive voltage source's `E` is the
   natural audio **input**, and a port's `v = (a + b)/2` is the natural **output**. Whether
   a thin input/output helper lives at the leaf layer or is a network/assembly concern is
   captured, not v1 (leaning: assembly concern, since a probe needs a port within a tree).
2. **Variable `dt` / sample-rate change (design Open Q2).** v1 fixes `dt` at construction;
   a supported in-place re-prepare/`setSampleRate` path is deferred to the
   network/adaptation layer rather than added per leaf.
3. **Time-varying element parameters (design Open Q3).** A swept `R`/`C`/`L` forces tree
   re-adaptation (adaptor coefficients depend on children `Rp`); the propagation is a tree
   concern. Captured, not v1.
4. **Root-element interface fidelity (design Open Q4).** v1 defers ideal-source/nonlinear
   roots but commits to not designing the leaf ABI in a way that precludes them; whether a
   root needs an extra affordance (e.g. an iterative-solve hook) is a
   `wdf-complete-analog-stages` question.
5. **Placement — `circuit/wdf/` vs top-level `wdf/` (design Open Q5).** v1 co-locates under
   `core/primitives/circuit/wdf/`; the prospectus lists a top-level `wdf/`. Revisitable at
   spec/plan time if the later phase nodes argue for paradigm separation.
6. **Complex/AC (design Open Q6).** WDF leaves are real-valued transient one-ports; an
   AC/frequency-domain WDF analysis is not a v1 concern.

# WDF-primitives — design

- Roadmap item: `design:primitive/wdf-primitives`
- Part of: `multi:feature/phase-wave-digital-filters`
- Depends-on: `multi:feature/phase-numerical-solvers`
- Siblings (planned, this phase): `design:primitive/wdf-adaptors`, `design:primitive/wdf-passive-networks`, `design:feature/wdf-complete-analog-stages`
- Date: 2026-07-09

## stack-control design house rules (stack-control-design-v1)

- **capture-over-yagni** (mechanical): capture everything known or knowably-implied; scoping is a separate, operator-driven pass AFTER capture.
- **solution-space-alternatives** (mechanical): the solution space enumerates ≥2 alternatives, including the rejected ones with reasons.
- **required-sections** (mechanical): problem-domain, solution-space, decisions, open-questions, provenance.
- **operator-approval** (operator): not done until the operator records the `design-approved:` marker on the roadmap node.
- **handoff-to-spec-kit** (soft): terminal handoff routes to `/stack-control:define`, never `writing-plans`.
- **installation-anchored-record** (mechanical): this record lives under `docs/superpowers/specs/`.

## Problem domain

Phase 6 opens a **second, complementary circuit-modeling paradigm** alongside the
just-shipped nodal solver trio (`design:primitive/modified-nodal-analysis`,
`design:primitive/newton-iteration`, `design:primitive/implicit-integration`, all
closed). This primitive is the **base of that phase**: three sibling nodes
(`wdf-adaptors`, `wdf-passive-networks`, `wdf-complete-analog-stages`) build on the
vocabulary it establishes.

**How WDF differs from the nodal trio.** The nodal solvers stamp every element's
contribution as a conductance / branch constraint / `Companion{Geq, Ieq}` into an
augmented system `A·x = z` and **solve the matrix** for node voltages. Wave Digital
Filters instead transform each element into a **one-port in the wave domain**: a
port carries an **incident wave** `a` and a **reflected wave** `b`, related to the
Kirchhoff voltage `v` and current `i` through a **port resistance** `Rp`. Each
element imposes a **scattering (reflection) relation** `b = f(a)` fixed by its
constitutive law. Elements are connected by **adaptors** (series/parallel scattering
junctions) into a **binary tree**, and the whole network is evaluated by a cheap
**up-sweep** (children report reflected waves) followed by a **down-sweep** (the root
distributes incident waves) — **no matrix solve**.

**Why the paradigm is worth its own phase (the "capability").** Unlike the three
nodal siblings — each of which earned its surface by a *capability gain over lab
scaffolding it replaced* (MNA lifted grounded-source-only; Newton lifted
single-nonlinearity; implicit-integration lifted hardcoded first-order backward-Euler
to a selectable rule) — **WDF has no lab predecessor**. Its justification is not a
gain over prior scaffolding but the **intrinsic properties of the paradigm**, which
the nodal solvers do not offer:

- **Guaranteed passivity and unconditional stability** for passive networks — energy
  is provably non-increasing sample-to-sample, by construction of the wave variables
  and the passive scattering, independent of sample rate.
- **O(N) per-sample modularity** — each one-port and each adaptor is a small local
  computation; there is no global matrix to factor. Ladder/passive topologies (tone
  stacks, passive filters, reactive networks) are the WDF sweet spot.
- **Reactive elements become unit delays** — the WDF "magic": a capacitor's reflected
  wave this sample is simply its incident wave from *last* sample. No companion model,
  no matrix, no per-timestep re-stamp.

This is a **complementary** modeling path, not a replacement for the nodal trio: WDF
is cheap and unconditionally stable for passive/ladder circuits but structurally
constrained for circuits with multiple nonlinearities or arbitrary (non-tree)
topologies, exactly where MNA + Newton excel. Establishing the wave-domain vocabulary
is what this primitive does.

**WDF is another reader of the frozen vocabulary.** The circuit-modeling seam was
built solver-neutral on purpose. `DEVELOPMENT-NOTES.md:232` records the intent:
*"the reference solver, MNA (Phase 5), and WDF (Phase 6) are all just alternative
readers of one immutable vocabulary — the primitive never learns what a matrix or a
wave is."* The frozen element value types
(`core/primitives/circuit/models/{resistor,capacitor,inductor,sources}.h`) hold the
**physical parameters** (`R`, `C`, `L`, `V`, `I`) and expose *nodal* stamps
(`admittance()`, `companion(dt, ·)`). WDF is a **parallel wave-domain family** that
reuses those physical constants but:

- carries **no `NodeId`** — a WDF circuit's topology is expressed by the **connection
  tree** (which adaptor connects to which child), not by the node set `0..nodeCount-1`
  that the nodal netlist uses (`node.h`); and
- is **stateful for reactive one-ports** — a WDF capacitor/inductor stores one sample
  of wave state, unlike the deliberately stateless nodal `Capacitor{a,b,C}` whose
  `vPrev` is supplied by the solver.

So "reader of the vocabulary" here means the **same physical circuit description** can
be modeled either nodally or as a WDF tree; it does **not** mean the WDF leaf type
literally embeds `Resistor{a, b, R}` (see Decision 9 and Rejected D).

**The scope split is already drawn by the roadmap (4 nodes).** This node deliberately
owns only the **leaf one-ports** and the **two foundations they force** (the wave
convention and the one-port interface). Adaptors, tree assembly / adaptation, and
nonlinear roots are separate planned nodes. The boundary is held per the
**capture-over-yagni** rule by *capturing* every cross-cut (the adaptor ABI, the
adaptation algorithm, the root-element seam, time-varying elements, variable `dt`)
without *implementing* beyond the leaf one-ports — mirroring how MNA defined the
linear engine plus the `CompanionSupply` seam and deferred reactive history and the
nonlinear loop to its siblings.

RT-safety and portability bar (Constitution Principle VI, mirrored from the nodal
siblings): header-only, **no heap allocation or locks in the per-sample wave path**
(`portResistance` / `reflected` / `incident`), C++17, no platform headers, files
within ~300–500 lines.

## Solution space

Common to every alternative: each circuit element becomes a **one-port** with a
**port resistance** `Rp` and a **scattering relation** `b = f(a)`; reactive elements
are discretized so their reflection is a stored-state operation (the WDF unit delay);
the per-sample wave path is RT-safe (zero heap, no locks); non-physical states are
surfaced, never masked (no clamped reflection, no fabricated port resistance). The
alternatives differ in the **wave convention**, the **discretization**, **how the
port interface is expressed**, and **how much of the WDF stack this node owns**.

### Chosen — Stateful wave-domain one-port family, voltage waves + bilinear, duck-typed port ABI (Approach A)

A header-only primitive family under `core/primitives/circuit/wdf/`, namespace
`acfx::wdf` (sibling to `acfx::mna` / `acfx::newton` / `acfx::integration`):

- **Voltage-wave convention.** At a port with reference (port) resistance `Rp`,
  incident and reflected waves are `a = v + Rp·i`, `b = v − Rp·i`, with inverse
  `v = (a + b)/2`, `i = (a − b)/(2·Rp)` (current `i` referenced *into* the port). This
  is the modern voltage-wave (Fettweis) convention used by contemporary
  virtual-analog WDF work, and it keeps the arithmetic in the repo's plain `double`
  physics with no normalization bookkeeping.

- **The one-port interface (the "wave ABI").** A **duck-typed concept**, not a base
  class (composition over inheritance, Constitution Principle II; the same shape as
  the nodal `CompanionSupply` concept). A WDF one-port exposes:
  - `double portResistance() const noexcept` — `Rp`, consumed by a parent adaptor to
    compute its scattering coefficients (down-scope: the adaptor node consumes this).
  - `double reflected() const noexcept` — `b`, the up-sweep output computed from the
    element's *current stored state* (so it is available before this sample's incident
    wave arrives).
  - `void incident(double a) noexcept` — the down-sweep input; reactive elements store
    it as next sample's state, memoryless elements ignore it.
  All three are `noexcept` and heap-free.

- **Bilinear (trapezoidal) discretization for reactive elements.** The bilinear
  transform `s → (2/T)·(1 − z⁻¹)/(1 + z⁻¹)` is intrinsic to WDF: it is exactly what
  makes a reactance a **frequency-independent port resistance plus a unit delay**, and
  what preserves passivity. Its known cost is bilinear frequency warping. It is **not
  a selectable knob at the leaf** (that would break the unit-delay structure the whole
  paradigm and its adaptors depend on — see Rejected C).

- **The leaf element vocabulary** (voltage waves, bilinear; `T = dt` = sample period):

  | One-port | Port resistance `Rp` | Reflected wave `b` | State | Adaptable? |
  |---|---|---|---|---|
  | Resistor (R) | `R` | `0` (adapted) | none | yes |
  | Capacitor (C) | `T/(2C)` | `state` | 1 wave sample | yes (unit delay) |
  | Inductor (L) | `2L/T` | `−state` | 1 wave sample | yes (delay + invert) |
  | Resistive voltage source (E, series R) | `R` | `E` (adapted) | none | yes |
  | Resistive current source (I, parallel R) | `R` | `R·I` (adapted) | none | yes |
  | Resistive termination (R load) | `R` | `0` (matched) | none | yes |
  | Short circuit (v = 0) | — | `−a` | none | no (reflective) |
  | Open circuit (i = 0) | — | `+a` | none | no (reflective) |

  Derivation basis (voltage waves; current `i` referenced INTO the port): a linear
  one-port with element law `v = R·i` gives `b = a·(R − Rp)/(R + Rp)`, reflection-free
  at `Rp = R`; a Thévenin source `v = E + R·i` (the sign consistent with current-into-port
  — `v = E − R·i` would be singular at `Rp = R`) gives `b = E` at `Rp = R`; the bilinear
  capacitor gives
  `Rp = T/(2C)` with `b[n] = a[n−1]` (store the incident wave, reflect it next
  sample); the inductor is its dual (`Rp = 2L/T`, `b[n] = −a[n−1]`); a short (`v = 0`)
  gives `b = −a`, an open (`i = 0`) gives `b = +a` — both independent of `Rp`, hence
  non-adaptable.

- **Per-sample protocol.** A tree evaluates by an **up-sweep** that reads each leaf's
  `reflected()` (computed from stored state), then a **down-sweep** that calls each
  leaf's `incident(a)` (reactive leaves do `state := a`). This node defines the leaf
  half of that protocol; the sweep orchestration is a tree/adaptor concern
  (down-scope). **Leaf lifecycle:** a reactive leaf is **constructed with its physical
  parameter and `dt`** (e.g. `Capacitor(C, dt)`, `Inductor(L, dt)`), computing its port
  resistance `Rp` **once in the constructor** — there is no separate leaf-level
  `prepare()` phase (that two-phase shape belongs to the *tree*, which sizes/scans a
  whole netlist; a single leaf has nothing to size). The per-sample wave path
  (`portResistance` / `reflected` / `incident`) is `noexcept` and heap-free. A
  sample-rate change or a time-varying parameter is handled by reconstruction and tree
  re-adaptation at the network/adaptation layer, **not** a per-leaf re-prepare API in v1
  (Open questions 2–3).

- **The adaptable/reflective split, reported not decided.** Each leaf reports whether
  it has a free port resistance (adaptable: R, C, L, resistive sources/terminations)
  or a fixed `±a` reflection (reflective: short, open — and, downstream, ideal sources
  and nonlinearities). *Which* port a tree leaves unadapted (the root) is an adaptation
  decision that belongs to the tree/adaptor nodes, not the leaf. The leaf only exposes
  the fact.

- **Honest, no-fallback contract.** A non-physical parameter (`R ≤ 0`, `C ≤ 0`,
  `L ≤ 0`, `T ≤ 0`) is a configuration error surfaced by the same
  precondition-not-fallback discipline the nodal elements already use — `Rp` is never
  clamped to a "safe" value and reflections are never limited to fake passivity
  (Constitution Principle V). Passivity is a **validated property**, not an enforced
  clamp.

Chosen because it (a) establishes the wave-domain foundation the three sibling WDF
nodes require — the convention and the port ABI are exactly what adaptors plug into;
(b) preserves the roadmap's four-way split by owning only the leaf one-ports and the
conventions they force, capturing (not implementing) adaptors/tree/roots; (c) honors
`DEVELOPMENT-NOTES.md:232` by reusing the frozen physical constants as a parallel
wave-domain reader without dragging nodal topology or statelessness into the wave
family; and (d) inherits the nodal siblings' proven shape (concept-based seams,
compute-`Rp`-once-at-construction / hot-path-`noexcept`, template-light structs,
RT-safe, no-fallback).

### Rejected — Power / normalized waves (Approach B)

Use the classical power-wave (or normalized-wave) formulation `a = (v + Rp·i)/(2√Rp)`
instead of voltage waves. Rejected because it adds `√Rp` normalization bookkeeping to
every port and every adaptor coefficient for no benefit at this layer, and it is less
aligned with the contemporary virtual-analog WDF references and with the repo's plain
`double`-physics simplicity. Voltage waves are the modern default and keep the leaf
arithmetic trivial. (Power waves have niche numerical-conditioning arguments for
specific adaptor forms; if one ever arises it is an adaptor-node concern, revisitable
there without reshaping the leaves.)

### Rejected — Selectable discretization rule at the leaf (Approach C)

Mirror `implicit-integration`'s selectable-rule capability (backward-Euler *and*
trapezoidal, etc.) on the WDF reactive leaves. Rejected because it is a category error
for WDF: the **bilinear transform is what makes a reactance a port resistance plus a
unit delay**. Backward/forward Euler in the wave domain yield a frequency-dependent or
non-passive port relation that breaks the clean `b[n] = ±a[n−1]` delay structure the
adaptors and the whole up/down-sweep depend on. In the nodal world a selectable rule
was a real capability gain; in WDF, bilinear is canonical and non-optional at the leaf.
(Oversampling remains the honest, paradigm-preserving answer to bilinear warping — an
existing primitive, not a per-leaf rule switch.)

### Rejected — One-ports as literal readers of the nodal `Component` variant (Approach D)

Make the WDF leaves read directly from the frozen `Component`
(`Resistor{a, b, R}`, `Capacitor{a, b, C}`, …) so there is one shared element type.
Rejected because the nodal value types carry **`NodeId a, b`** topology that is
meaningless in a WDF connection tree, and are deliberately **stateless**, which is
wrong for reactive WDF leaves that must hold a wave sample. Forcing WDF onto those
types would either smuggle nodal topology into the wave family or bolt external state
onto stateless value types. The correct reading of `DEVELOPMENT-NOTES.md:232` is that
the *same physical circuit* can be modeled nodally or as a WDF tree — not that the two
paradigms share one leaf struct. WDF leaves reuse the **physical constants**
(`R`/`C`/`L`/`E`), not the nodal container. (A convenience adapter that constructs WDF
leaves *from* a `Component` description is plausible, but it is a circuit-assembly
concern for the passive-networks node, not a leaf concern here — captured, not v1.)

### Rejected — Fold adaptors into this node (Approach E)

Design the leaf one-ports and the series/parallel adaptors together as one wave-domain
core. Rejected because it contradicts the roadmap's deliberate split
(`design:primitive/wdf-adaptors` is its own planned node) and oversizes a single spec.
The nodal phase set the precedent: MNA, Newton, and implicit-integration each shipped
as a focused primitive with named seams between them; the WDF phase follows the same
decomposition (leaves → adaptors → networks → stages).

### Rejected — Inheritance-based `OnePort` abstract base class (Approach F)

Express the port interface as a virtual `OnePort` base class the concrete elements
derive from. Rejected on Constitution Principle II (avoid class inheritance; use
composition with interfaces/concepts) and Principle VI (a virtual call per port per
sample is a hot-path vtable indirection). The duck-typed **concept** — the same
mechanism the nodal `CompanionSupply` seam uses — gives the adaptors a static,
inlinable, heap-free contract without inheritance.

### Rejected (numerics) — clamp reflections / substitute a port resistance to force passivity

Silently clamp `|b| ≤ |a|` or substitute a "safe" `Rp` when a parameter is
out-of-range so a network "always looks passive/stable." Rejected on the repo's
**no-fallback** standard (Constitution Principle V), consistent with MNA (no silent
gmin) and Newton (no fabricated output). Passivity is a property to **validate**, and
a non-physical parameter is a configuration error to **surface**, never a case the
leaf papers over.

## Decisions

1. **Scope.** This primitive owns the **leaf one-port elements** and the **two
   foundations they force**: the wave-variable convention and the one-port port
   interface (the "wave ABI"). It does **not** own adaptors (`wdf-adaptors`), tree
   assembly / the reflection-free adaptation algorithm (`wdf-passive-networks`), or
   nonlinear/ideal-source root elements (`wdf-complete-analog-stages`). Those are
   *captured as cross-cuts* (Open questions + Decision 11), not implemented here.
2. **Wave convention — voltage waves** (domain call, operator-delegated). `a = v +
   Rp·i`, `b = v − Rp·i`; inverse `v = (a + b)/2`, `i = (a − b)/(2·Rp)`; current into
   the port. Modern VA-modeling standard; keeps `double`-physics arithmetic trivial.
3. **Discretization — bilinear/trapezoidal, non-selectable at the leaf** (domain call,
   operator-delegated). Bilinear is intrinsic to the WDF unit-delay structure and
   passivity; not a per-leaf rule knob (Rejected C). Warping is addressed by
   oversampling (an existing primitive), not a rule switch.
4. **Leaf vocabulary (v1).** Resistor, Capacitor, Inductor; resistive voltage source
   and resistive current source (the real/Thévenin & Norton sources — the resistive
   voltage source is the normal **signal input** injection point); and the three
   **terminations** named by the roadmap: resistive (matched load), short (`b = −a`),
   open (`b = +a`). No ideal (non-resistive) source and no nonlinear element at the
   leaf in v1 (Decision 11).
5. **Port resistance formulas.** Resistor `Rp = R`; Capacitor `Rp = T/(2C)`; Inductor
   `Rp = 2L/T`; resistive sources/terminations `Rp = R`. Short/open have no free `Rp`
   (reflection is `±a` independent of `Rp`) and are reported non-adaptable. `T = dt`.
6. **Reflection (scattering) relations.** As tabulated in Approach A. Adapted
   (reflection-free) forms: R → `0`, resistive V-source → `E`, resistive I-source →
   `R·I`, resistive termination → `0`; reactive: capacitor `b = state`, inductor
   `b = −state`; reflective: short `b = −a`, open `b = +a`.
7. **State shape.** Exactly **one stored wave sample** per reactive leaf
   (`state`), holding the previous incident wave. `reflected()` reads it; `incident(a)`
   writes `state := a`. Memoryless leaves hold no state. Reactive WDF leaves are
   therefore **stateful** — the divergence from the stateless nodal value types.
8. **Port ABI — a duck-typed concept, not a base class.** `portResistance() const
   noexcept`, `reflected() const noexcept`, `incident(double) noexcept`. The same
   composition-over-inheritance, statically-dispatched seam shape as the nodal
   `CompanionSupply`. Defined here; consumed by `wdf-adaptors`.
9. **Reuse physics, not the nodal container** (per `DEVELOPMENT-NOTES.md:232`). WDF
   leaves reuse the **physical constants** (`R`/`C`/`L`/`E`/`I`) but are their **own
   wave-domain types** carrying no `NodeId` and (for reactive) holding state. Not a
   literal reader of `Component{a, b, …}` (Rejected D). A `Component`→WDF-leaf
   convenience adapter, if ever wanted, is a passive-networks/assembly concern.
10. **Leaf lifecycle — explicit construction with `(param, dt)`, no per-leaf prepare**
    (pinned per third-party review, 2026-07-09). A reactive leaf is **constructed with
    its physical parameter and `dt`** — `Capacitor(C, dt)`, `Inductor(L, dt)` — and
    computes its port resistance `Rp` **once in the constructor**. There is **no
    separate leaf-level `prepare()`/re-prepare API in v1**: the two-phase
    prepare-once/scan pattern of the nodal siblings sizes a *whole netlist* and does not
    apply to a single one-port. Memoryless leaves are likewise constructed with their
    fixed parameter (Resistor `Resistor(R)`; resistive source/termination with `R`).
    Construction (parameter/`dt` validation) is throw-permitted; the per-sample wave path
    (`portResistance` / `reflected` / `incident`) is `noexcept` and heap-free. **Fixed vs.
    per-sample:** fixed at construction = physical parameter, `dt`, and the derived `Rp`;
    mutable per sample = a reactive leaf's stored wave `state` (via `incident`) and a
    source's drive value `E`/`I` (the audio-input path — a per-sample setter; see Open
    question 1). A sample-rate change or a swept parameter is handled by **reconstruction
    + tree re-adaptation** at the network/adaptation layer (Open questions 2–3), never a
    silent in-place `dt` mutation.
11. **Root / non-adaptable seam — captured, not v1.** Short and open are the two
    reflective leaves in v1 (named terminations). The **ideal (non-resistive) voltage
    source** (`b = 2E − a`) and **nonlinear root** elements (diode, `b = f(a)`
    iteratively) are the other non-adaptable one-ports; they belong at a tree's
    unadapted **root** and are deferred to `wdf-complete-analog-stages`. The leaf
    interface is designed so a root element satisfies the same
    `reflected()`/`incident()` protocol (it simply reports non-adaptable and its
    reflection depends on the current incident wave).
12. **Adaptation is reported, not performed.** A leaf exposes its port resistance and
    its adaptable/reflective status; the reflection-free **choice** across a whole tree
    (which port stays unadapted; how an adaptor derives its coefficients from children's
    `Rp`) is a tree/adaptor algorithm → `wdf-passive-networks` / `wdf-adaptors`.
13. **No fallback.** Non-physical parameters (`R/C/L ≤ 0`, `T ≤ 0`) are surfaced as
    configuration errors (precondition discipline), never clamped; reflections are never
    limited to fake passivity (Principle V). Passivity is a validated property.
14. **Placement.** A production primitive family under
    `core/primitives/circuit/wdf/`, namespace `acfx::wdf`, sibling to `acfx::mna` /
    `acfx::newton` / `acfx::integration`. Chosen over the prospectus's top-level
    `core/primitives/wdf/` (noted as the alternative, Open question 5) because WDF
    fundamentally models circuits and reuses the circuit element physics, and the three
    most recent sibling primitives all established the `circuit/<name>/` co-location.
    Header-only, C++17, no platform headers, zero heap / no locks in the wave path,
    ≤~300–500 lines/file. Folder created in the same commit as the primitive ("inhabit
    before creating").
15. **Validation** (per `circuit-model-validation-approach`): **exact scattering closed
    forms** per element (R adapted → `b = 0`; resistive V-source adapted → `b = E`; the
    reactive **unit-delay property** `b[n] = a[n−1]` / `−a[n−1]` exactly; short/open →
    `∓a`/`±a`); **physical invariants** — **passivity** (`|b| ≤ |a|` energy
    non-increasing for the passive leaves; `Rp > 0`) and cap/inductor **duality**; the
    reactive leaf's discrete port impedance matches the **bilinear-discretized analog
    impedance**; and an `AllocationSentinel` asserting zero heap in the wave path.
    **Per-element** correctness only — full-circuit frequency response and adaptation
    correctness wait on the adaptor/network nodes. **Not** transcribed published values.
16. **Sibling sequencing.** `wdf-adaptors` consumes the port ABI defined here;
    `wdf-passive-networks` assembles leaves + adaptors into trees and owns adaptation;
    `wdf-complete-analog-stages` adds ideal-source and nonlinear roots. This node
    deliberately ships first and standalone so the ABI is fixed before adaptors are
    designed against it.

## Open questions

1. **Signal I/O convention.** The resistive voltage source's value `E` is the natural
   audio-**input** injection, and a port's `v = (a + b)/2` at a chosen probe is the
   natural **output**. Whether a thin input/output helper lives at the leaf layer or is
   a network/assembly concern is captured, not v1 (leaning: assembly concern, since a
   probe needs a port within a tree).
2. **Variable `dt` / sample-rate change.** v1 fixes `T = dt` at **construction**
   (Decision 10); a sample-rate change is handled by **reconstructing** the affected
   leaves (recomputing reactive `Rp`) and re-adapting the tree, off the hot path. A
   supported in-place re-prepare/`setSampleRate` path is deliberately deferred to the
   network/adaptation layer rather than added per leaf (audio rate is effectively fixed
   within a render block), consistent with implicit-integration's variable-`dt` open
   question. The *need* is captured; the API is not v1.
3. **Time-varying element parameters.** A potentiometer sweeping `R`, or a modulated
   `C`, changes a leaf's port resistance and therefore forces **re-adaptation** of the
   enclosing tree (adaptor coefficients depend on children `Rp`). The leaf can update its
   own `Rp` off the hot path; the re-adaptation propagation is a tree concern
   (`wdf-passive-networks`). Captured, not v1.
4. **Root-element interface fidelity.** Decision 11 defers ideal-source/nonlinear roots
   but asserts they satisfy the same `reflected()`/`incident()` protocol. Whether the
   root needs an *extra* affordance beyond the leaf ABI (e.g. a converged-solve hook for
   an iterative nonlinear root) is a `wdf-complete-analog-stages` question; this node
   commits only to not designing the leaf ABI in a way that precludes it.
5. **Placement — `circuit/wdf/` vs top-level `wdf/`.** Decision 14 co-locates under
   `core/primitives/circuit/wdf/`; the prospectus's primitive diagram lists a top-level
   `wdf/` peer to `circuit/`. The co-location call can be revisited at spec time if the
   phase's later nodes argue for paradigm separation; noted, not blocking.
6. **Complex/AC.** WDF leaves are real-valued (`double`) transient one-ports, consistent
   with the real-valued nodal siblings. An AC/frequency-domain WDF analysis is not a v1
   concern. Noted, not v1.

## Provenance

- Design conducted in-session via `superpowers:brainstorming`, driven by the
  `/stack-control:design` frontend (house rules `stack-control-design-v1` injected
  above).
- Compass verdict `on-course` for intent `design` from phase `planned`; `design:`
  pointer set on the roadmap node
  (`docs/superpowers/specs/2026-07-09-wdf-primitives-design.md`) before authoring.
- Operator (non-domain-expert) explicitly delegated the scope call and the two domain
  calls (wave convention, discretization), directing the decision to project goals,
  guidelines, and existing work + documentation — the same delegation recorded for the
  `implicit-integration` sibling. The scope boundary (leaf one-ports + the wave
  convention + the port ABI; adaptors/tree/roots captured, not implemented) follows the
  roadmap's deliberate four-way WDF split and the nodal phase's precedent of shipping
  focused primitives with named seams between them. Operator approved the scope and the
  chosen shape before this record was written.
- Third-party review (2026-07-09) approved the direction — the phase boundary (WDF as a
  parallel circuit-modeling family, not a replacement for MNA/Newton/integration; this
  node owning only leaf one-ports + the wave convention + the ABI, with adaptors, trees,
  adaptation, and nonlinear roots deferred) and every domain call (voltage waves,
  non-selectable bilinear, duck-typed ABI, no inheritance, no per-leaf adaptation, no
  silent reflection/`Rp` clamping) — and requested one spec-time clarification, accepted:
  **pin the per-leaf construction/prepare lifecycle.** The design had over-applied the
  nodal siblings' two-phase `prepare()` shape (which sizes a whole netlist) to a single
  leaf. Resolution (Decision 10): reactive leaves are **explicitly constructed with
  `(physical parameter, dt)`** — `Capacitor(C, dt)`, `Inductor(L, dt)` — computing `Rp`
  once in the constructor, with **no per-leaf re-prepare/sample-rate API in v1**;
  re-prepare and time-varying-parameter handling are deferred to the network/adaptation
  layer (Open questions 2–3).
- Architectural anchor: `DEVELOPMENT-NOTES.md:232` — *"the reference solver, MNA
  (Phase 5), and WDF (Phase 6) are all just alternative readers of one immutable
  vocabulary — the primitive never learns what a matrix or a wave is."* — establishing
  WDF as a wave-domain reader of the frozen physical vocabulary, and
  `DEVELOPMENT-NOTES.md:213,232` (component-abstractions built the solver-neutral seam
  expressly so MNA and WDF could both adapt it).
- Prospectus: `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
  Phase 6 (WDF primitives → adaptors → passive networks → complete analog stages) and
  the `wdf/` primitive family.
- Existing-code references (from a code-map exploration of the trunk):
  `core/primitives/circuit/models/resistor.h` (`v = R·i`, `admittance()`),
  `core/primitives/circuit/models/capacitor.h` and `.../inductor.h` (physical `C`/`L`,
  the nodal companion + the "a better integrator would change companion()" anchor —
  the WDF analogue is bilinear at the leaf),
  `core/primitives/circuit/models/sources.h` (ideal `VoltageSource` = the future
  non-adaptable root; `CurrentSource`),
  `core/primitives/circuit/components.h:42-66` (the frozen `Component` variant + the
  `isReactive`/`isLinear`/`isNonlinear` classifiers WDF re-reads by physics),
  `core/primitives/circuit/node.h` (the nodal `NodeId` topology WDF does **not** use),
  and the `CompanionSupply` concept seam
  (`core/primitives/circuit/mna/mna-assembler.h`) whose composition-over-inheritance
  shape the WDF port ABI mirrors.
- Sibling design records (nodal trio, all closed):
  `docs/superpowers/specs/2026-07-07-modified-nodal-analysis-design.md`,
  `docs/superpowers/specs/2026-07-07-newton-iteration-design.md`,
  `docs/superpowers/specs/2026-07-08-implicit-integration-design.md`.
- Recorded memory: `circuit-model-validation-approach` (prove exact on closed forms;
  validate assembled circuits via exact limits + monotonic/passivity invariants; not
  transcribed published values) — applied here at the per-element scattering layer, with
  full-circuit validation explicitly deferred to the adaptor/network nodes.

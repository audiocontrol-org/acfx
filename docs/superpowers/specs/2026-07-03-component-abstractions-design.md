> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# component-abstractions — Design Record

**Roadmap item:** `design:primitive/component-abstractions`
**Phase:** 4 — Circuit Modeling (first deliverable)
**Part-of:** `multi:feature/phase-circuit-modeling`
**Depends-on:** `multi:feature/phase-dynamic-systems` (shipped)
**Layer:** primitive (`core/primitives/`), platform-independent, RT-safe, header-only

---

## Problem domain

Phase 4 introduces circuit modeling. Every deliverable in this phase — passive
tone stacks, diode clippers, op-amp stages — and every solver family in the phases
that follow (Phase 5 Modified Nodal Analysis / Newton / implicit integration;
Phase 6 Wave Digital Filters) needs a **shared, typed vocabulary for electronic
circuit elements**. `component-abstractions` is that vocabulary and nothing more.

A circuit element has two separable aspects:

1. **Physics** — its constitutive relation (a resistor obeys `i = v/R`; a diode
   obeys the Shockley equation; a capacitor obeys `i = C·dv/dt`). This is intrinsic
   to the element and independent of how any solver assembles a system.
2. **Topology** — which of the element's terminals attach to which circuit nodes.
   This is a property of the assembled circuit, not the element.

The primitive must express both **without committing to a solver**. That
constraint is load-bearing: the two downstream solver families think in
incompatible terms. MNA wants to **stamp conductances and currents into a nodal
system matrix** (`i = f(v)`, companion models for reactive elements). WDF wants
components that expose a **port resistance and scatter wave variables**
(`b = f(a)` on a tree). If the shared foundation adopts either worldview, it
couples the whole circuit-modeling stack to one solver and forces the other to
work around it — exactly the inward-only-dependency coupling the acfx core is
architected to avoid ("platform-independent core, thin adapters").

### Constraints (from the acfx constitution and repo conventions)

- **RT-safe:** no heap allocation and no locks in the per-sample solve path.
  Allocation is permitted only at `prepare()`.
- **Platform-independent:** the primitive knows nothing of JUCE / libDaisy /
  Teensy. C++17-compatible so the Teensy target (no concepts) still compiles.
- **No fallbacks / no mock data outside tests:** missing or ill-posed topology
  raises a descriptive error; it never silently substitutes a default.
- **No inheritance, no vtable in the audio path:** consistent with
  `core/dsp/effect.h` (concept + template, duck-typed on C++17).
- **Small modules:** header files within ~300–500 lines; split by concern.
- **Inhabit before creating** (`core/primitives/README.md`, FR-008): the
  `circuit/` category folder is materialized only in the same commit as its first
  real inhabitant.

### Boundary — deferred to later phases (progressive-learning discipline)

The primitive deliberately does **not** contain a general circuit solver, does
**not** choose a numerical integration method for reactive elements, and does
**not** implement production nonlinear iteration. Those are the explicit subjects
of Phase 5 (MNA / Newton / implicit integration) and Phase 6 (WDF). Pulling them
forward would collapse the phase boundary and make the "primitive" large and
solver-opinionated. What Phase 4 needs to *run and validate* circuits before MNA
exists is provided by a deliberately-naive reference solver that lives in the
**lab**, not the primitive (see Decisions D3, D6).

---

## Solution space

The design turns on three orthogonal choices. Each is presented with the chosen
option and the rejected alternatives (with the reason each was rejected).

### Chosen — Solver-neutral typed netlist; components own their physics; solvers are adapters

The primitive defines three concepts:

- **`NodeId` / node set** — integer node handles, ground ≡ node `0`. A circuit is
  a count of nodes plus a set of components.
- **Component** — a typed value struct carrying element type, terminal→node
  mapping, parameters, and the element's *own physics*: `admittance()` for linear
  elements, `evaluate(terminalVoltages) → { current, conductance/Jacobian }` for
  nonlinear elements, and a continuous relation + companion hook for reactive
  elements. No solver state lives on the component.
- **`Netlist` / `Circuit`** — a fixed-capacity container of components plus a node
  count, owning the topology. Assembled at `prepare()`; zero heap in the solve.

The seam: **components own their physics; solvers own the assembly.** An MNA
adapter (Phase 5) stamps from `evaluate`/`admittance`; a WDF adapter (Phase 6)
derives port resistance from the same physics. Neither worldview leaks into the
primitive. This matches the repo's foundational principle (inward-only
dependencies, thin adapters) and is the better teaching artifact — a reader sees
"a resistor is `i = v/R` between two nodes," not "a resistor is a matrix stamp."

### Rejected representation — MNA-oriented first

Shape the abstraction around nodal stamping (`component.stamp(matrix)`, companion
models baked in), since MNA is the more general solver and arrives first.
**Rejected:** it bakes matrix/nodal assumptions into the supposedly-neutral
foundation, so the Phase 6 WDF adapter must either work around a nodal bias or
re-abstract the components — reintroducing exactly the solver coupling this
primitive exists to prevent.

### Rejected representation — Port / n-port oriented (WDF-friendly)

Components are one-ports/n-ports exposing `portResistance()` and `scatter(wave)`.
**Rejected:** elegant for WDF series/parallel trees but awkward for the arbitrary
topologies, multiport nonlinearities, and ideal op-amps (nullors) that MNA — and
Phase 4's own op-amp-stages deliverable — handle naturally. It optimizes for the
later, more specialized solver at the expense of the nearer, more general one.

### Rejected physics encoding — Dumb descriptor (solver owns all physics)

Components are pure tagged data (type + nodes + params); every solver
re-implements the Shockley equation, companion models, etc.
**Rejected:** duplicates each element's physics across every solver (MNA, WDF, the
lab solver), a direct DRY violation and a divergence hazard — the diode model
could drift between solvers. Co-locating physics with the element is the correct
seam.

### Rejected runnability — Analytic-only, no solver anywhere

Ship only the vocabulary plus per-component unit tests; never assemble and run a
circuit until MNA lands in Phase 5.
**Rejected:** Phase 4's *other* deliverables (passive tone stacks, diode clippers)
cannot demonstrate an assembled circuit at all until Phase 5, stalling the whole
phase and leaving the vocabulary unvalidated in situ.

### Rejected runnability — Fold a real solver into the primitive

Pull MNA forward; `component-abstractions` includes a general nonlinear nodal
solver now.
**Rejected:** collapses the Phase 4/Phase 5 boundary, makes the "primitive" large
and no longer minimal, and pre-commits solver design before its own dedicated
phase — contradicting the progressive-learning arc.

### Rejected container — Inheritance + virtual dispatch / heap of pointers

A `Component` base class with virtual `stamp()`/`evaluate()`, stored as a
`std::vector<std::unique_ptr<Component>>`.
**Rejected twice over:** violates the no-inheritance guideline and puts a vtable in
the audio path; and the heap-of-pointers allocates. The chosen container is a
`std::variant<Resistor, Capacitor, …>` in a fixed-capacity array, visited via
`std::visit` — heap-free, vtable-free, and idiomatic to this codebase's
concept/template style.

---

## Decisions

- **D1 — Solver-neutral netlist is the core representation.** Components carry
  their own physics; MNA and WDF become adapters that read the neutral model.
  (Chosen option above; rejects MNA-first and WDF/port-first.)

- **D2 — Physics lives on the component; assembly lives in the solver.** Linear
  elements expose `admittance()`; nonlinear elements expose
  `evaluate(v) → {current, conductance}`; reactive elements expose a continuous
  relation plus a companion hook. No solver re-implements element physics.

- **D3 — Runnability comes from a naive reference solver in the LAB, not the
  primitive.** `core/labs/component-abstractions/` carries a deliberately-naive
  fixed-size linear nodal solve (Gaussian elimination) plus a bounded
  fixed-iteration Newton for the single-diode clipper. The primitive stays pure.
  Phase 5 MNA supersedes the lab solver.

- **D4 — Container is `std::variant<…>` in a fixed-capacity array, visited via
  `std::visit`.** Heap-free, vtable-free, C++17-compatible; no inheritance.

- **D5 — Layering follows the three-layer structure.**
  `core/primitives/circuit/` holds the pure vocabulary (`node.h`, `components.h`,
  `netlist.h`, `models/diode.h`); `core/labs/component-abstractions/` holds the
  reference solver + validation harness; the effects layer is untouched (Phase 4's
  later deliverables consume this later). The `circuit/` folder is created in the
  same commit as its first inhabitant (inhabit-before-creating).

- **D6 — Reactive elements are integration-method-neutral in the primitive.** A
  capacitor exposes `i = C·dv/dt` plus a companion hook; the *lab* solver supplies
  **backward Euler** to demonstrate. Integration-method choice is Phase 5
  (implicit integration); the primitive never hard-codes one.

- **D7 — Error handling: validate at `prepare()`, never fall back.** Floating
  nodes, missing ground reference, and over-capacity raise descriptive errors at
  `prepare()`. The per-sample solve is then well-posed and never throws. Newton
  non-convergence yields a bounded iteration count and a reported residual/status,
  never a silent fallback or mock output.

- **D8 — Full component taxonomy is captured now; the v1 inhabitant set is fixed
  by third-party review (see Open questions, resolved).** Captured (per
  capture-over-YAGNI): R, C, L; independent V and I sources; controlled sources
  VCVS/VCCS/CCVS/CCCS; diode (Shockley) as the reference nonlinearity; ideal op-amp
  (nullor). Captured but deferred as inhabitants: BJT, MOSFET, triode/pentode,
  op-amp macromodels.
  **v1 inhabitants (decided): R, C, L, independent voltage source, independent
  current source, diode.** Controlled sources / op-amp / nullor are deferred until
  the op-amp-stages deliverable needs them (the next two Phase 4 deliverables —
  passive-tone-stacks and diode-clippers — do not). L is retained despite not being
  strictly required by those two, because it exercises the reactive-companion seam
  a second time (guarding against over-fitting the abstraction to the capacitor)
  and enables the RLC analytic-validation circuit (D9) at near-zero marginal cost
  (L is the dual of C's companion).

- **D9 — Validation is analytic where a closed form exists.** Voltage divider
  (exact), RC low-pass (analytic magnitude/phase), RLC, diode clipper (static
  transfer curve). Plus per-component unit tests and no-alloc assertions on the
  solve path.

---

## Open questions

OQ1–OQ4 were **resolved by a third-party design review** (2026-07-03) and are
recorded here as decided so `/stack-control:define` inherits them; OQ5 remains
open for measurement. None blocks the design.

- **OQ1 — Reactive discretization for the lab solver. → RESOLVED: backward Euler.**
  Simple, unconditionally stable, slightly lossy; explicitly marked **non-normative**
  (the lab's naive choice). Trapezoidal and the general treatment are Phase 5
  (implicit integration).

- **OQ2 — How far the lab's bounded Newton goes. → RESOLVED: hold the line at the
  clipper.** The lab solves only voltage divider / RC / RLC (linear) and the
  single-diode or antiparallel-diode clipper (a fixed ≤N-iteration Newton). Anything
  with multiple interacting nonlinearities waits for Phase 5 MNA/Newton — the lab
  must not become MNA. Exact N and residual tolerance are a spec-time detail.

- **OQ3 — Capacity limits. → RESOLVED: templated fixed capacity.** Use
  `Netlist<MaxNodes, MaxComponents>` (and a matching fixed-size solver matrix)
  rather than hard-coded global limits — no hard ceiling, heap-free by construction.
  Spec-time note: each distinct `<N,M>` instantiation is separate code, so pick sane
  defaults and watch Teensy code size (feeds OQ5).

- **OQ4 — Controlled sources now or deferred. → RESOLVED: defer.** VCVS/VCCS/CCVS/
  CCCS, the ideal op-amp, and the nullor are deferred until the op-amp-stages
  deliverable needs them; they are captured in the taxonomy but are not v1
  inhabitants. The v1 inhabitant set is R, C, L, independent V/I sources, diode
  (see D8).

- **OQ5 — `std::variant` vs a hand-rolled tagged union on the C++17/Teensy
  target. → OPEN.** `std::variant` is C++17 and expected to be fine; confirm
  code-size and `std::visit` cost on the embedded target (now also weighing the
  templated-capacity instantiation cost from OQ3), and keep a hand-rolled tagged
  union as a fallback shape if measurement demands it.

---

## Provenance

- **Design frontend:** `/stack-control:design` driving `superpowers:brainstorming`
  in-session, under house-rules block `stack-control-design-v1`
  (capture-over-YAGNI, ≥2 solution-space alternatives, required sections,
  operator-approval marker, installation-anchored record, handoff to
  `/stack-control:define`).
- **Compass verdict at open:** `on-course` — `designing` is the legitimate next
  move from `planned` for `design:primitive/component-abstractions`.
- **Roadmap context:** first deliverable of Phase 4 (Circuit Modeling) per
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` §"Phase 4
  — Circuit Modeling"; part-of `multi:feature/phase-circuit-modeling`; the
  substrate MNA (Phase 5) and WDF (Phase 6) adapt.
- **Repo conventions consulted:** `core/primitives/README.md` (taxonomy,
  inhabit-before-creating), `core/dsp/effect.h` (concept/template, vtable-free
  contract), the three-layer lab→primitive→effect structure
  (`docs/superpowers/specs/2026-06-29-three-layer-structure-design.md`), and the
  acfx constitution / CLAUDE.md standards.
- **Two crux decisions (core representation, runnability) delegated by the
  operator** to the design frontend during the in-session brainstorm; resolved to
  the solver-neutral netlist (D1) and the naive-solver-in-the-lab (D3) with the
  reasoning recorded above.
- **Third-party design review (2026-07-03):** resolved OQ1 (backward Euler,
  non-normative), OQ2 (lab solver held at the clipper; never MNA), OQ3 (templated
  `Netlist<MaxNodes, MaxComponents>`), and OQ4 (defer controlled sources / op-amp /
  nullor). The one point pushed back on and adjusted: the inductor L is **retained**
  in the v1 set (reviewer proposed deferring it) for reactive-seam generality and
  the RLC validation circuit, at near-zero marginal cost.
- **Terminal handoff:** `/stack-control:define` to author the Spec Kit spec from
  this record (the `design-to-spec` transition), after the operator records the
  `design-approved:` marker.

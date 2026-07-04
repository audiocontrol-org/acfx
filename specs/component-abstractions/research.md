# Phase 0 Research — component-abstractions

All decisions below feed Phase 1 (data-model + contracts) and Phase 2 (tasks). No
spec-level `NEEDS CLARIFICATION` remain — the numeric-precision question was resolved
in `/speckit-clarify` (double solve, float boundary; FR-022). These are the
implementation-formulation decisions the spec deliberately left to planning.

---

## R1 — Ideal voltage source in a nodal solve, without a forbidden fallback

**Decision:** the reference solver handles an independent **voltage source** by
**fixed-node reduction**, not by a large-conductance (`gmin`) approximation and not by
full MNA augmentation. A node pinned by an ideal source to a known voltage becomes a
**known** in the system: its column is multiplied out and moved to the right-hand side,
and its row is removed — leaving a reduced, well-conditioned conductance system
`G' · v_unknown = i'` over the remaining nodes, solved by Gaussian elimination.

**Rationale:** a pure nodal conductance matrix `G·v = i` cannot represent an ideal
voltage source directly. The two escape routes and why they are rejected:

- *Large series/parallel conductance ("gmin"/stiff-source hack)* — a **fallback/mock**:
  it approximates an ideal source with a finite conductance, silently trading exactness
  for solvability. Constitution V (no fallbacks) and the spec's SC-002 ("exact analytic
  node voltage") forbid it.
- *Full MNA augmentation (add the source branch current as an extra unknown row/column)*
  — correct, but it **is** the Modified Nodal Analysis technique that Phase 5 owns. Using
  it here would collapse the phase boundary (OQ2, design D3).

Fixed-node reduction is the technique one uses to hand-solve a divider: it is exact, it is
not MNA, and it needs no fabricated conductance. It cleanly supports every v1 validation
circuit (all are driven by one grounded ideal source).

**Alternatives considered:** MNA augmentation (deferred to Phase 5); Thévenin source with a
small series resistance (changes the modeled circuit — rejected); Norton current-source
substitution (only valid for a source *with* a series resistance, not an ideal source).

**Consequence for the contract:** the reference solver requires exactly **one ideal voltage
reference path to ground** among the sources for the v1 circuits; the netlist validator's
"missing ground" / "floating node" checks (FR-010) are what guarantee the reduced system is
non-singular.

---

## R2 — Diode: Shockley formulation + Newton voltage limiting

**Decision:** the diode implements the **Shockley** law
`I = Is · (exp(V / (n·Vt)) − 1)` with small-signal conductance
`g = dI/dV = (Is / (n·Vt)) · exp(V / (n·Vt))`, computed in `double`. The bounded Newton
loop in the lab uses **junction voltage limiting** (the standard SPICE `pnjlim`-style step
clamp) to prevent `exp` overflow between iterations, plus a `Vcrit` cap.

**Rationale:** unguarded Newton on `exp` diverges/overflows for even modest reverse steps;
voltage limiting is the textbook, non-heuristic fix and is what makes the fixed iteration
bound (FR-015) reliably converge on the clipper. Computing in `double` (FR-022) keeps
`exp(V/nVt)` from saturating prematurely.

**Alternatives considered:** tanh/algebraic soft-clip approximation of the diode (rejected —
it is a *different model*, not the diode's physics, and US1.2 asserts the true Shockley
current); damped Newton without voltage limiting (rejected — still overflows before the
damping engages on `exp`).

**Open, captured (not blocking):** exact `Is`, `n`, `Vt` defaults and the Newton iteration
bound `N` + residual tolerance are parameter/tuning values fixed during implementation
against the harness, not architecture (mirrors how `saturation` voicings were tuned).

---

## R3 — Backward-Euler companion for C and L

**Decision:** reactive elements expose a companion hook parameterized by the solver-supplied
timestep `dt` and the previous node voltages/branch state (the **solver owns the history**;
the component stays a pure value — resolving the "no solver state on the component" tension).

- **Capacitor** (backward Euler): equivalent conductance `Geq = C/dt`; history current
  `Ieq = Geq · v_prev` (companion = conductance in parallel with a current source).
- **Inductor** (backward Euler): equivalent conductance `Geq = dt/L`; history current
  `Ieq = −i_prev` (the dual; branch current carried in solver state).

**Rationale:** backward Euler is the simplest unconditionally-stable companion and is the
resolved OQ1 choice, explicitly **non-normative** — the RC/RLC test tolerances (SC-003) are
set to absorb its known numerical damping. The C/L duality is exactly why L was kept in v1
(exercises the companion seam twice).

**Alternatives considered:** trapezoidal companion (more accurate, can ring; deferred to
Phase 5 implicit-integration); storing history on the component (rejected — breaks the
pure-value/solver-owns-state seam and RT-cleanliness).

---

## R4 — Heap-free, vtable-free component container (and the OQ5 measurement)

**Decision:** components are held as `std::variant<Resistor, Capacitor, Inductor,
VoltageSource, CurrentSource, Diode>` in a `std::array` of capacity `MaxComponents`, visited
with `std::visit`. C++17, no inheritance, no vtable, no heap (FR-002/008).

**Rationale:** `std::variant` gives closed-set tagged dispatch that the compiler can devirtualize/
inline — the idiomatic match for this codebase's concept/template, vtable-free style
(`core/dsp/effect.h`). A fixed `std::array` keeps `Netlist` allocation-free.

**OQ5 (captured as a risk to measure, not pre-decided):** on the Teensy target, confirm the
code-size / `std::visit` cost of `std::variant` **and** the templated-`Netlist<N,M>` +
`double` instantiation footprint. If measurement demands it, a hand-rolled tagged union is the
fallback shape with the same public contract. This is an implementation-time measurement task,
surfaced in `tasks.md`, not a blocker for the host-side vocabulary.

**Alternatives considered:** inheritance + virtual dispatch (rejected — Constitution VII, and a
vtable in any future audio path); `std::vector` of components (rejected — heap).

---

## R5 — Build & test integration (follow existing conventions)

**Decision:** register the lab harness as an `acfx_lab_component_abstractions_harness`
executable in the root `CMakeLists.txt` (mirroring the existing `acfx_lab_svf_harness` /
`acfx_lab_tape_dynamics_harness` blocks), and the three `tests/core/circuit-*-test.cpp`
suites in `tests/CMakeLists.txt` (mirroring `core/svf-test.cpp` / `core/hysteresis-test.cpp`).
The primitive headers themselves are header-only and need no build target.

**Rationale:** reuse the established, working wiring; no new build machinery. Keeps the
"primitive compiles with the lab absent" property (SC-007) — the test suites depend only on
`core/primitives/circuit/`, while the harness depends on the lab solver.

**Alternatives considered:** a dedicated CMake library target for the primitive (rejected —
header-only, unnecessary); folding the reference solver into `acfx_core` (rejected — it must
stay lab-local so the primitive is provably solver-independent).

---

## Summary of decisions

| # | Decision | Binds |
|---|---|---|
| R1 | Ideal voltage source via **fixed-node reduction** (exact, not gmin, not MNA) | reference-solver contract; SC-002/005 |
| R2 | **Shockley** diode + **voltage-limited** bounded Newton | diode physics; US4; FR-004/015 |
| R3 | **Backward-Euler** companions; **solver owns history** | capacitor/inductor; FR-005; SC-003 |
| R4 | `std::variant` container; **OQ5** code-size measured, not pre-decided | FR-008; tasks risk item |
| R5 | Reuse existing CMake lab-harness + tests/core wiring | build tasks; SC-007 |

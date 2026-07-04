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

### OQ5 measurement (2026-07-03)

**Teensy on-device measurement: OUTSTANDING.** The Teensy toolchain is not available in this
environment — `make teensy` fails at CMake configure time because the local
`arm-none-eabi-g++` (Homebrew) is a C-only install with no `libstdc++` headers
(`cmake/toolchains/teensy.cmake`'s own guard catches this: "Homebrew's arm-none-eabi-gcc is
C-only... install a complete ARM embedded toolchain"). No on-device code-size number exists yet;
this remains a follow-up for whoever next has the real Teensy/Arm-GNU toolchain on `PATH`.

**Host `-Os` proxy measurement (what was actually run here).** A throwaway translation unit
(`core/primitives/circuit/netlist.h` only, no lab) built two `Netlist` instantiations
(`Netlist<8,16>` and `Netlist<32,64>`), populated one of each of the six v1 component kinds,
called `prepare()`, and read every component back through the classifiers
(`isNonlinear`/`isReactive`) plus `admittance()` / `companion()` / `evaluate()`. Compiled with
`c++ -Os -std=c++17 -I core -c ca_size.cpp -o ca_size.o` (clang 17 / libc++, Apple Silicon,
host — not the Teensy Cortex-M/newlib target) and measured with `size`:

| Variant | `__TEXT` (code) bytes | Notes |
|---|---|---|
| `std::variant` + `std::get_if` chain (as production `terminalsOf`/`components.h` actually dispatch) | 5526 | both `Netlist<8,16>` and `Netlist<32,64>` instantiations in one TU |
| same, dispatched via `std::visit` + an overloaded-lambda visitor instead of `get_if` | 5622 (+96 `__TEXT`, +96 `__DATA`) | `std::visit` is not used by production code today (only mentioned in a comment); this isolates its marginal cost over the `get_if` chain actually in use |
| hand-rolled `enum`-tag + `union`-free struct (`ManualComponent`/`ManualNetlist`, no `std::variant` at all, same computation) | 5103 | baseline with no variant machinery, for contrast |

Reading the linked object's per-function sizes (`nm -n` sorted, address deltas) for the two
`Netlist<8,16>` vs. `Netlist<32,64>` instantiations shows `addNode()`/`add()` at **identical**
code size across capacities (360 / 384 bytes respectively, both instantiations) and `prepare()`
growing only ~20–36 bytes (936 → 956–972 bytes) between the small and large capacity — the
`MaxNodes`/`MaxComponents` template parameters size a stack `std::array` and appear as literal
immediates in the thrown error strings, but do **not** unroll any loop, so growing capacity
4× (8→32 nodes, 16→64 components) costs single-digit-percent extra code, not a multiplier.

**Interpretation:**
- `std::variant` dispatch via the `get_if`/`holds_alternative` chain the production code actually
  uses costs roughly 423 bytes (~8%) of `__TEXT` in this small proxy over a hand-rolled tagged
  union doing the identical work — a modest, not alarming, delta at this scale (six component
  kinds, two Netlist instantiations, one representative read loop).
- `std::visit` (not currently used anywhere in `core/primitives/circuit/` — only referenced in a
  code comment) would add a further ~96 bytes of code plus a ~96-byte read-only function-pointer
  jump table per call site/instantiation (clang/libc++'s `std::visit` emits one dispatch table per
  distinct visitor type). This is a small absolute cost here, but the "one table per
  instantiation" shape is the known-general risk with `std::visit` at larger fan-out — worth
  re-checking if the visitor/alternative count grows materially beyond v1's six kinds.
- Templated-capacity growth (`Netlist<8,16>` → `Netlist<32,64>`) is **not** a source of
  meaningful bloat: the per-instantiation cost is dominated by the fixed logic of
  `addNode`/`add`/`prepare`, not by capacity.
- Caveat: this was measured with clang/libc++ on host, not GCC/newlib on the Teensy target;
  absolute numbers will differ on-device (Thumb-2 encoding is typically denser than x86-64/ARM64
  desktop code, and `std::visit`'s codegen shape differs between libc++ and libstdc++). The
  *qualitative* conclusions (get_if-chain overhead is modest; capacity growth is cheap) are
  expected to carry over, but the exact byte counts do not.
- Related, incidental observation (not part of OQ5 itself, surfaced by trying to reason about the
  Teensy build): `cmake/toolchains/teensy.cmake` compiles with `-fno-exceptions -fno-rtti`.
  `Netlist::addNode`/`add`/`prepare` (and `terminalsOf`'s `std::get<Diode>`) rely on
  exceptions/`std::variant`'s throwing accessors, matching the same already-accepted pattern as
  `core/primitives/nonlinear/adaa-waveshaper.h`'s control-thread `throw` — these are template
  member functions, so they are only instantiated (and only need to compile under
  `-fno-exceptions`) if actual embedded firmware code calls them. This is not a new problem
  introduced by this feature and is not part of the OQ5 code-size question, but it is worth
  keeping in mind if a future Teensy adapter ever needs to build/validate a `Netlist` on-device
  rather than only running a pre-validated one.

**Conclusion: keep `std::variant`.** Nothing measured here — on host, at v1's scale (six
component kinds, small `Netlist` instantiations) — demonstrates a real Teensy code-size problem.
The measured `std::variant`-over-hand-rolled-union delta (~8% of a ~5.5 KB proxy TU) is small in
absolute terms and is exactly the kind of cost the spec anticipated paying for closed-set tagged
dispatch without a vtable (R4's rationale). Per the spec (OQ5 is a measurement task, not a
redesign trigger unless a real problem is demonstrated), the hand-rolled tagged-union fallback
described above is **not** adopted. The on-device Teensy number remains outstanding and should be
captured once a real Teensy/Arm-GNU toolchain is available; if that on-device measurement someday
shows a real problem, it should be reported rather than silently worked around.

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

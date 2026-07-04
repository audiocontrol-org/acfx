# Component Abstractions — Lab

The `core/primitives/circuit/` primitive gives the codebase a solver-independent
circuit vocabulary — resistors, capacitors, inductors, ideal voltage/current
sources, and a diode — each a pure value that knows only its own physics
(`admittance()`, `companion(dt, state)`, `evaluate(v)`). A vocabulary of parts is
not, by itself, a circuit you can run. This lab is the first thing that makes the
vocabulary *runnable*: a reference solver that assembles a netlist of those parts
into a system of equations and solves it, plus a harness that checks the answers
against hand analysis. It is the first deliverable of Phase 4 (Circuit Modeling).

## The load-bearing boundary

**This solver is deliberately naive and non-normative.** It exists only to make
the component vocabulary testable before real Modified Nodal Analysis (MNA)
exists. Phase 5 (MNA + a general Newton solver + implicit integration) supersedes
it entirely, and this solver must never grow into MNA (design D3, OQ2 in
`specs/component-abstractions/research.md`).

Two things follow from that:

- **The primitive does not depend on this lab.** `core/primitives/circuit/` has
  no include of anything under `core/labs/component-abstractions/`. Delete this
  whole lab directory and the primitive — and its own physics/netlist tests —
  keep working; only the assembled-circuit validations here go away
  (contract `reference-solver.md` §Isolation guarantee, SC-007). The solver
  *reads* each component's physics; it never re-derives it, and the component
  never reads back a solver internal.
- **Superseded is not the same as disposable.** Per Constitution IX, lab code
  graduates or is retained as reference — it is not thrown away once Phase 5
  lands. This solver stays here as an educational artifact and a regression
  reference even after MNA exists.

If you find yourself reaching for `gmin`, a source-branch unknown (MNA
augmentation), trapezoidal/higher-order integration, or a general
multi-nonlinearity Newton loop in this directory: stop. Those are Phase 5.

## How it works

### `solver/linear-solver.h` — `LinearSolver<MaxNodes, MaxComponents>`

Advances a netlist one backward-Euler timestep and solves for node voltages.
Each call to `solve(netlist, dt)`:

1. **Assembles the reduced conductance system `G'·v = i'`** by reading every
   component's own physics — `Resistor::admittance()`, `Capacitor`/`Inductor`
   `::companion(dt, state)`, `CurrentSource::current()` — and stamping it into a
   fixed-size working matrix. The solver never re-derives a component's
   constitutive law; that seam (FR-006) is the whole point of the primitive.
2. **Imposes ideal `VoltageSource`s by fixed-node reduction** (research.md R1),
   not a `gmin`/large-conductance approximation and not MNA branch-current
   augmentation: a node pinned to a known voltage has its column multiplied out
   into the other rows' RHS, and its own row replaced by the trivial equation
   `v = V`. Only grounded ideal sources are supported — a floating source
   throws a descriptive `std::runtime_error` rather than being silently
   approximated.
3. **Solves by Gaussian elimination with partial pivoting** over the active
   `n x n` block (`n` = node count minus ground). A zero pivot (a singular,
   ill-posed reduced system) is a hard, descriptive error, never a silent wrong
   answer.
4. **Advances the reactive history.** Reactive companions are backward-Euler
   (research.md R3, explicitly non-normative): a capacitor becomes
   `Geq = C/dt` in parallel with a history current `Ieq = Geq·v_prev`; an
   inductor becomes `Geq = dt/L` with `Ieq = -i_prev`. The component classes
   are pure values and hold no history of their own, so `LinearSolver` owns it
   — the previous solved node voltages (for capacitor `v_prev`) and the
   previous inductor branch currents — and advances both after every step.

Every buffer (`g_`, `rhs_`, `solution_`, `voltage_`, the history arrays) is a
fixed-size `std::array` sized from the `MaxNodes`/`MaxComponents` template
parameters. There is no `new`/`delete`, no `std::vector`, and no allocation
anywhere in `solve()`.

### `solver/newton-clipper.h` — `NewtonClipper<MaxNodes, MaxComponents>`

Layers a bounded, voltage-limited Newton iteration on top of `LinearSolver` for
exactly **one** nonlinearity: a single diode, or an antiparallel pair of diodes
across the same node pair (their currents just sum at one port, so it still
counts as one nonlinearity). Each Newton iteration:

1. Reads `Diode::evaluate(vAK)` for the current voltage guess to get
   `{current, conductance}`, and linearizes the diode into a Norton companion
   — a conductance `Geq` in parallel with an equivalent current source `Ieq`.
2. Injects that companion as two ordinary *linear* components (a `Resistor` and
   a `CurrentSource`) into a copy of the netlist, and hands the whole thing to
   `LinearSolver` — the diode's physics is the only nonlinear thing in the
   loop; the linear assembly/solve machinery is reused verbatim, never
   reimplemented.
3. Applies **junction voltage limiting** (`Diode::limitJunctionVoltage`, the
   textbook SPICE `pnjlim` step clamp) to the new port-voltage guess before the
   next iteration, so `exp(v/nVt)` cannot overflow between steps.
4. Stops when both the voltage and current residuals fall under tolerance, or
   when the iteration bound `N` is reached — whichever comes first.

`solve()` returns a `NewtonStatus{converged, iterations, voltageResidual,
currentResidual}` rather than trusting the last iterate blindly: on
`converged == false` the caller must not treat the node voltages as a physical
answer (FR-015 — no fallback, no fabricated output on non-convergence).

`NewtonClipper` also enforces the scope boundary directly: a second,
independent nonlinearity (a diode on a different node pair, or more than an
antiparallel pair) is refused with `"out of reference-solver scope — deferred
to Phase 5"`, and a reactive element (`Capacitor`/`Inductor`) alongside the
diode is refused outright — re-solving the linear network on every Newton
iteration would corrupt `LinearSolver`'s per-timestep reactive history, so this
lab layer only claims a *static* soft-clip transfer curve, not a reactive
nonlinear transient.

## What it validates

The host-only harness at
`core/labs/component-abstractions/harness/component-abstractions-harness.cpp`
drives `LinearSolver` directly (no effect layer, no DAW) and prints PASS/FAIL
lines with measured-vs-expected numbers; it mirrors the doctest assertions in
`tests/core/circuit-solver-test.cpp` so the same checks are readable outside
the test framework. Currently:

- **Resistive divider** — `Vin` through `R1`/`R2` to ground must match the
  exact ratio `Vin * R2 / (R1 + R2)` to `1e-9`. A purely resistive circuit has
  no `dt` dependence, so this is the sharpest check on the fixed-node-reduction
  assembly itself.
- **RC low-pass** — checked against the *closed-form backward-Euler
  recurrence* for the same `R`, `C`, `dt` (`v[n] = alpha*Vin + (1-alpha)*v[n-1]`,
  `alpha = dt/(dt + RC)`), not the continuous-time response — both sides are
  backward Euler, so they must agree to numerical precision, which proves the
  solver's assembly rather than the accuracy of backward Euler itself. DC
  steady state is checked separately, since that's where backward Euler and
  the continuous system agree exactly.
- **Series RLC** — an underdamped second-order step response (`zeta ~= 0.158`)
  must stay finite over 20 000 steps, show the overshoot past `Vin` that marks
  a damped second-order system (a first-order RC never overshoots), and settle
  to the same DC target as the RC case.

- **Diode clipper** (`solver/newton-clipper.h`) — a single-diode and an
  antiparallel-pair DC sweep, each checked against an *independent* bisection
  root-find of the clipper's transcendental equation (a genuine cross-check, not
  the solver compared to itself); the Newton loop must converge within its
  iteration bound at every sweep point, and a netlist with two interacting
  nonlinearities must be refused. Covered in both the harness and
  `tests/core/circuit-solver-test.cpp`.

## Running it

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_component_abstractions_harness
./build/test/acfx_lab_component_abstractions_harness
```

The unit tests (`tests/core/circuit-components-test.cpp`,
`circuit-netlist-test.cpp`, `circuit-solver-test.cpp`) run with the rest of the
suite:

```
make test
```

## Scope — what's intentionally not here

This lab is bounded on purpose (contract `reference-solver.md` §Non-goals); each
of these is a Phase 5 subject, not a gap to fill in here:

- No general multi-nonlinearity Newton solver — two or more interacting
  nonlinear components are refused, not solved.
- No Modified Nodal Analysis (no source-branch-current unknowns); ideal voltage
  sources are handled only by fixed-node reduction, and only when grounded.
- No trapezoidal or higher-order integration — reactive companions are
  backward Euler only.
- No sparse or large-system handling — the working matrix is a fixed-size,
  dense `std::array` sized by the template parameters.

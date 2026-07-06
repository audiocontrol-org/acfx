# Diode Clippers — Lab

The `core/primitives/circuit/diode-clipper/` primitive composes the frozen
`component-abstractions` vocabulary (resistors, capacitors, junction diodes,
ideal sources, the `Netlist`) into **diode clipping-stage topologies** — a
symmetric shunt clipper, an asymmetric shunt clipper, and a series (inline)
clipper — driven by a bill of materials. The builders emit **topology only**: a
`Netlist` of ordinary components, ready for any solver, with no solved response
and no audio path. This is the third deliverable of Phase 4 (Circuit Modeling),
after `component-abstractions` and `passive-tone-stacks`, and the first that
assembles a **nonlinear circuit with reactive memory**.

A clipping topology whose reactive tone-shaping you cannot observe is not yet a
recognizable clipper. This lab is what makes the assembled clippers *runnable*: a
**bounded transient nonlinear solver** (`solver/transient-clipper.h`) that
advances an assembled clipper one timestep — discretizing each reactive element
as a backward-Euler companion and resolving the diode nonlinearity by a bounded
Newton iteration — plus a harness that validates each clipper against a
proven-exact solver, an independent DC-limit oracle, and behavioral invariants.

## The load-bearing boundary

**This transient solver is deliberately bounded and non-normative.** It exists
only to make the diode-clipper topologies solvable and testable in the time
domain. It is a **bounded transient — NOT general Modified Nodal Analysis**:

- **Single port, single nonlinearity location.** The solver handles exactly one
  clipper port — one node pair carrying the diode string (up to `MaxDiodes`
  diodes summing at that pair). A second, *interacting* nonlinearity at a
  distinct node pair is refused with a descriptive `std::runtime_error` (out of
  scope; Phase 5). No `gmin` stepping, no branch-current unknowns, no general
  multi-nonlinearity Newton engine. If you reach for any of those in this
  directory: stop.
- **The primitive does not depend on this lab.** `core/primitives/circuit/diode-clipper/`
  includes nothing under `core/labs/`. Delete this whole lab directory and the
  builders — and their own Tier-1 topology test — keep compiling and passing;
  only the transient-response validations here go away (spec FR-019 / SC-007).
  The solver *reads* each component's own physics (`Diode::evaluate`,
  `Capacitor::companion`); it never re-derives a constitutive law.
- **The load-bearing mechanism is loop separation.** Reactive companions are
  computed **once per timestep** from held history; the inner Newton iterations
  hold them **fixed**; reactive history is advanced **exactly once**, after
  Newton converges. This is precisely the reactive+nonlinear case the
  `component-abstractions` static `NewtonClipper` deliberately *refuses* (reusing
  its per-iteration linear solve would advance capacitor history N times per
  sample and corrupt the transient). Separating the loops is the fix.
- **Superseded is not disposable** (Constitution IX): when Phase-5 MNA / Phase-6
  WDF land, this transient solver is retained as an educational artifact and a
  regression reference, not deleted.

## How it works

### `solver/transient-clipper.h` — `TransientClipper<MaxNodes, MaxComponents, MaxDiodes>`

`step(netlist, dt)` advances the assembled clipper one backward-Euler timestep:
it computes each reactive element's companion `{Geq, Ieq}` once from
solver-held history, then runs a bounded, voltage-limited Newton loop over the
diode string (each iteration companion-linearizes the diodes into a Norton pair,
appends them as ordinary linear stamps to a stack-allocated augmented netlist,
reuses the `component-abstractions` `LinearSolver`, and damps the junction
voltage through `Diode::limitJunctionVoltage` / pnjlim), and advances the
reactive history exactly once after convergence. It returns a `NewtonStatus`
`{ converged, iterations, voltageResidual, currentResidual }`; non-convergence is
**reported, never faked**. Every buffer is a fixed-size `std::array`; the
`step()` path allocates nothing. `double` throughout.

### `harness/diode-clippers-harness.cpp`

Host-only. Drives `TransientClipper` directly (no effect layer, no DAW) and
prints PASS/FAIL lines with measured-vs-expected numbers, mirroring the doctest
assertions in `tests/core/diode-clipper-transient-test.cpp`. Validates, in
order: a linear-only RC network against the analytic backward-Euler recurrence
(the solver proven exact before any nonlinearity is trusted), each clipper's DC
steady state against an independent bisection oracle (and the static
`NewtonClipper` curve), then the assembled-clipper invariants — symmetry /
DC-offset, forward saturation, passivity, the reactive signature (larger `Cf` ⇒
less post-clip HF for the shunt clippers) — **and an explicit non-convergence
check**. Exits nonzero on any failure.

## Running it

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_diode_clippers_harness
./build/test/acfx_lab_diode_clippers_harness
```

The unit tests (`tests/core/diode-clipper-builder-test.cpp`,
`diode-clipper-transient-test.cpp`) run with the rest of the suite:

```
make test
```

## Scope — what's intentionally not here

- No realtime `process()` / audio-path realization and no oversampling / ADAA
  (a later effect feature).
- No op-amp-feedback (Tube Screamer) clipper — blocked on the deferred op-amp /
  nullor element (the separate `opamp-stages` deliverable).
- No named-product BOMs / voicings (Rat / DS-1 / Big Muff / Tube Screamer) —
  those are their own `design:feature/*` items built on this primitive.
- No general multi-nonlinearity / MNA / `gmin` / Newton engine — Phase 5.
- No DAW / hardware acceptance.

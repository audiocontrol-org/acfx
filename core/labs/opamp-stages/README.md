# Op-Amp Stages — Lab

The `core/primitives/circuit/opamp-stage/` primitive composes the frozen
`component-abstractions` vocabulary (resistors, capacitors, ideal sources, and
the `Netlist`) into **op-amp circuit topologies** — feedback stages, summing
amplifiers, and other feedback-dependent configurations — driven by a bill of
materials. The builders emit **topology only**: a `Netlist` of ordinary
components, ready for any solver, with no solved response and no audio path.

An op-amp topology whose behavior you cannot observe is not yet a usable stage.
This lab is what makes the assembled op-amp stages *runnable*: a **bounded nullor
MNA augmentation solver** (`solver/opamp-stage-solver.h`) that advances an
assembled stage one timestep — augmenting the linearized MNA with a single row and
column per op-amp nullor constraint — plus a harness that validates each stage
against proven reference solutions and behavioral invariants.

## The load-bearing boundary

**This nullor MNA augmentation is deliberately bounded and non-normative.** It
exists only to make the op-amp-stage topologies solvable and testable in the time
domain. It is a **bounded nullor augmentation — NOT general Modified Nodal
Analysis**:

- **Single nonlinearity location.** The solver handles assembled stages with
  op-amp output nonlinearities (rail clipping). A fundamentally different
  nonlinearity (e.g., a diode string interacting with the op-amp feedback) at a
  distinct node pair is out of scope; Phase 5 multi-nonlinearity engine is
  required. If you reach for multi-nonlinearity handling in this directory: stop.
- **The primitive does not depend on this lab.** `core/primitives/circuit/opamp-stage/`
  includes nothing under `core/labs/`. Delete this whole lab directory and the
  builders — and their own Tier-1 topology test — keep compiling and passing;
  only the transient-response validations here go away. The solver *reads* each
  component's own physics; it never re-derives a constitutive law.
- **The load-bearing mechanism is augmentation separation.** The nullor constraint
  (zero output voltage, zero input current) is enforced **once per timestep** by
  augmenting the linear MNA with a single row and column per op-amp, then solving
  the augmented system. Reactive history is advanced **exactly once**, after the
  augmented solve completes. This is the reactive+feedback case the static linear
  solver deliberately *refuses*.
- **Superseded is not disposable** (Constitution IX): when Phase-5 general MNA /
  Phase-6 WDF land, this nullor augmentation solver is retained as an educational
  artifact and a regression reference, not deleted.

## How it works

### `solver/opamp-stage-solver.h` — `OpAmpStageSolver<MaxNodes, MaxComponents, MaxOpAmps>`

`step(netlist, dt)` advances the assembled stage one backward-Euler timestep:
it computes each reactive element's companion `{Geq, Ieq}` once from
solver-held history, then augments the linearized MNA with the nullor constraints
(one row + one column per op-amp), solves the augmented system, and advances the
reactive history exactly once after completion. It returns a `SolveStatus`
`{ success, numIterations, residual }`; non-convergence is **reported, never
faked**. Every buffer is a fixed-size `std::array`; the `step()` path allocates
nothing. `double` throughout.

### `harness/opamp-stages-harness.cpp`

Host-only. Drives `OpAmpStageSolver` directly (no effect layer, no DAW) and
prints PASS/FAIL lines with measured-vs-expected numbers. Validates, in order:
a linear-only RC network against the analytic backward-Euler recurrence (the
solver proven exact before any nonlinearity is trusted), each stage's DC
steady state against an independent oracle, then the assembled-stage invariants
— unity-gain stability, feedback accuracy, passivity, the reactive signature —
**and an explicit non-convergence check**. Exits nonzero on any failure.

## Running it

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_opamp_stages_harness
./build/test/acfx_lab_opamp_stages_harness
```

The unit tests (`tests/core/opamp-stage-builder-test.cpp`,
`opamp-stage-solve-test.cpp`) run with the rest of the suite:

```
make test
```

## Scope — what's intentionally not here

- No realtime `process()` / audio-path realization and no oversampling / ADAA
  (a later effect feature).
- No op-amp feedback slew-rate limiting or full nonlinear rail clipping model
  (a later refinement).
- No named-product topologies / voicings (classic preamp / summing amp / EQ
  stage) — those are their own `design:feature/*` items built on this primitive.
- No general multi-nonlinearity / MNA / Newton engine — Phase 5.
- No DAW / hardware acceptance.

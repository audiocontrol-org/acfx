# Passive Tone Stacks — Lab

The `core/primitives/circuit/tone-stack/` primitive composes the frozen
`component-abstractions` vocabulary (resistors, capacitors, ideal sources, the
`Netlist`) into **passive tone-stack topologies** — the Fender/Marshall/Vox
(FMV) 3-band guitar stack and the passive Baxandall/James 2-band hi-fi control —
driven by potentiometers modelled as build-time wiper/taper math. The builders
emit **topology only**: a `Netlist` of ordinary components, ready for any solver,
with no solved response and no audio path.

A topology you cannot see the frequency response of is not yet a tone control.
This lab is the first thing that makes the tone stacks *readable*: a **complex
`.ac` solver** that computes the exact continuous-time transfer function
`H(jω)` of an assembled passive netlist, plus a harness that checks the FMV and
Baxandall responses against independent hand analysis (the published Duncan
tone-stack transfer functions). It is the second deliverable of Phase 4 (Circuit
Modeling), after `component-abstractions`.

## The load-bearing boundary

**This AC solver is deliberately bounded and non-normative.** It exists only to
make the tone-stack topologies testable in the frequency domain. It is a
**linear complex `.ac` solve** — nothing more:

- **The primitive does not depend on this lab.** `core/primitives/circuit/tone-stack/`
  includes nothing under `core/labs/`. Delete this whole lab directory and the
  builders — and their own wiper/taper and topology tests — keep working; only
  the assembled-response validations here go away (spec SC-006 / FR-016). The
  solver *reads* each component's admittance; it never re-derives it.
- **The lab must never grow into MNA.** No branch-current unknowns (not MNA), no
  Newton loop, no trapezoidal/higher-order integration — those are Phase 5/6.
  This carries forward the exact boundary the `component-abstractions` lab
  established. If you reach for `gmin`, a source-branch unknown, or a nonlinear
  iteration in this directory: stop.
- **Superseded is not disposable** (Constitution IX): when Phase-5 MNA / Phase-6
  WDF land, this `.ac` solver is retained as an educational artifact and a
  regression reference, not deleted.

## How it works

### `solver/ac-solver.h` — `solveAC(netlist, ω, inNode, outNode)`

Stamps each component's admittance at `jω` (`R → 1/R`, `C → jωC`, `L → 1/(jωL)`)
into a reduced nodal admittance matrix over `std::complex<double>`, imposes the
ideal input `VoltageSource` by fixed-node reduction (the method reused from the
`component-abstractions` `LinearSolver`), solves by complex Gaussian elimination
with partial pivoting, and returns `H = V(outNode) / V(inNode)`. A singular
system at some `ω` is a hard, descriptive `std::runtime_error` naming `ω` —
never a silent wrong answer. Every buffer is a fixed-size `std::array`; the solve
allocates nothing.

### `harness/passive-tone-stacks-harness.cpp`

Host-only. Drives `solveAC` directly (no effect layer, no DAW) and prints
PASS/FAIL lines with measured-vs-analytic numbers, mirroring the doctest
assertions in `tests/core/tone-stack-ac-test.cpp`. Validates, in order: RC
low-pass and resistive-divider sanity networks (closed form), then FMV and
Baxandall `|H(f)|` against the analytic Duncan curves.

## Running it

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_passive_tone_stacks_harness
./build/test/acfx_lab_passive_tone_stacks_harness
```

The unit tests (`tests/core/tone-stack-taper-test.cpp`,
`tone-stack-builder-test.cpp`, `tone-stack-ac-test.cpp`) run with the rest of the
suite:

```
make test
```

## Scope — what's intentionally not here

- No realtime `process()` / audio-path realization (Phase-6 WDF or a later
  lowering).
- No MNA, no Newton, no trapezoidal integration — a linear complex `.ac` solve
  only.
- No named-product BOMs/voicings (Marshall, Vox, Big Muff, Neve …) — those are
  later `design:feature/*` items built on this primitive.

# Quickstart — diode-clippers validation

How to build and run the validations that prove the feature end-to-end. Implementation lives in
`tasks.md`; this is the run/validation guide.

## Prerequisites

- The repo's standard host toolchain (a C++20 compiler for the lab; the primitive is C++17-clean).
- Offline CPM cache, as elsewhere in acfx:
  ```
  export CPM_SOURCE_CACHE=external/.cpm-cache
  ```

## Build & run the lab harness

```
cmake --build --preset test --target acfx_lab_diode_clippers_harness
./build/test/acfx_lab_diode_clippers_harness
```

Expected: PASS lines for each check, then `ALL PASS`, exit 0. Any failure prints
measured-vs-expected and exits nonzero. Checks mirror the Tier-2 doctest:

1. **Linear RC step** — solver (no diode) matches the analytic backward-Euler recurrence (~1e-9).
2. **DC-limit oracle** — each clipper's settled DC port voltage matches the independent bisection
   root-find (~1e-6) and the static `NewtonClipper` curve.
3. **Symmetry** — symmetric clipper `y(−x)=−y(x)`; asymmetric clipper shows a measurable DC offset.
4. **Saturation** — output clamps near the diode drop under large drive.
5. **Passivity** — output energy ≤ input energy over a bounded excitation.
6. **Reactive signature** — 1 kHz sine into clipping at `dt=1e-5 s`; output energy above 5 kHz
   strictly decreases across an ascending `Cf` sweep.
7. **Non-convergence** — a starved iteration budget against a stiff input reports
   `converged=false` (surfaced, not swallowed).

## Run the unit tests

```
make test
```

- **Tier-1** (`diode-clipper-builder-test.cpp`): each builder `prepare()`s cleanly, holds only
  frozen-vocabulary elements, counts match the BOM, and the primitive TU includes nothing under
  `core/labs/` — **no solver required**.
- **Tier-2** (`diode-clipper-transient-test.cpp`): the seven checks above.

## Isolation check (SC-007)

Deleting `core/labs/diode-clippers/` must leave the `core/primitives/circuit/diode-clipper/`
primitive and the Tier-1 test building and passing; only the transient-response validations
disappear. (Verified as a dependency-level guarantee, mirroring `passive-tone-stacks`.)

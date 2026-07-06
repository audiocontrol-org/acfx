# Quickstart — opamp-stages validation

How to build and run the validations that prove the feature end-to-end. Implementation lives in
`tasks.md`; this is the run/validation guide.

## Prerequisites

- The repo's standard host toolchain (a C++20 compiler for the lab; the primitive — including the
  new `OpAmp` element — is C++17-clean for the Teensy target).
- Offline CPM cache, as elsewhere in acfx:
  ```
  export CPM_SOURCE_CACHE=external/.cpm-cache
  ```

## Build & run the lab harness

```
cmake --build --preset test --target acfx_lab_opamp_stages_harness
./build/test/acfx_lab_opamp_stages_harness
```

Expected: PASS lines for each check, then `ALL PASS`, exit 0. Any failure prints
measured-vs-expected and exits nonzero. Checks mirror the Tier-2 doctest (the validation ladder):

1. **Analytic gains** — non-inverting stage solves to `1 + Rf/Rg`, inverting to `−Rf/Rin` (~1e-9);
   the nullor augmentation is exact.
2. **First-order response** — the active first-order low-pass (`Cf ∥ Rf`) matches its analytic
   backward-Euler recurrence (~1e-9); nullor+reactive exact before any nonlinearity.
3. **DC-limit oracle** — the clipper's settled DC output matches the independent bisection
   root-find at the virtual-short node (~1e-6).
4. **Saturation** — clipper output clamps near the feedback-diode drop under large drive.
5. **Symmetry** — symmetric feedback-diode population ⇒ `y(−x)=−y(x)`; asymmetric ⇒ measurable
   DC offset / even harmonics.
6. **Passivity** — the passive sub-network's output energy ≤ input energy over a bounded excitation.
7. **Reactive signature** — 1 kHz sine into clipping at `dt = 1e-5 s`; output energy above 5 kHz
   strictly decreases across an ascending `Cf` sweep.
8. **Non-convergence** — a starved iteration budget against a stiff input reports
   `converged=false` (surfaced, not swallowed).
9. **Bounded charter** — a netlist with ≥2 interacting nonlinearities raises a descriptive
   out-of-scope error; the augmentation touches only `OpAmp` branches.

## Run the unit tests

```
make test
```

- **Tier-1** (`opamp-stage-builder-test.cpp`): each builder `prepare()`s cleanly, holds only the
  circuit vocabulary (including the `OpAmp` element and nothing beyond it), counts match the BOM,
  and the primitive TU includes nothing under `core/labs/` — **no solver required**.
- **Tier-2** (`opamp-stage-solve-test.cpp`, possibly split under the ~500-line budget): the checks
  above (analytic gains, first-order response, DC-limit oracle, saturation, symmetry, passivity,
  reactive signature, non-convergence, bounded-charter refusal).

## Isolation check (SC-008)

Deleting `core/labs/opamp-stages/` must leave the `core/primitives/circuit/opamp-stage/` primitive
**and the `OpAmp` element in `core/primitives/circuit/`** and the Tier-1 test building and passing;
only the solved validations disappear. (Verified as a dependency-level guarantee, mirroring
`component-abstractions` / `diode-clippers`.)

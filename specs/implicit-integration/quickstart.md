# Quickstart: validating the implicit-integration primitive

A run/validation guide — not implementation code. It proves the feature end-to-end via the
host-side test suite. Implementation lives in `tasks.md`.

## Prerequisites

- The acfx core build (CMake + CPM). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.
- doctest (already vendored for `tests/core/`).
- `tests/support/allocation-sentinel.h` (existing).
- The shipped MNA primitive (`core/primitives/circuit/mna/`), the shipped Newton primitive
  (`core/primitives/circuit/newton/`), and `models/capacitor.h` / `models/inductor.h` (consumed).

## Build & run the tests

```bash
# configure + build the core test target (adjust preset to your local setup)
cmake --build build --target acfx_core_tests
./build/tests/core/acfx_core_tests --test-suite=integration*
```

## Validation scenarios (map to spec Success Criteria)

1. **Exact closed-forms (SC-001)** — `integration-closed-form-test.cpp`
   - RC low-pass driven by a step, backward-Euler → capacitor node voltage matches the exact
     discrete response `v[n] = v[n−1] + (dt/(RC+dt))·(Vin − v[n−1])` each sample; monotone to the
     DC steady state. RL dual for the inductor current.
   - Same networks under **trapezoidal** → match the trapezoidal closed-form discrete response.
   - Series/parallel LC → matches the analytic discrete solution.

2. **Convergence order — the rule is genuinely selectable (SC-002)** —
   `integration-convergence-order-test.cpp`
   - Integrate an RC (or LC) network under each rule across a sequence of shrinking timesteps;
     regress global error vs `dt` (log–log). Assert slope ≈1 for backward-Euler (band `[0.9,1.2]`)
     and ≈2 for trapezoidal (band `[1.8,2.2]`). This is the test that proves trapezoidal is not
     aliased to backward-Euler.

3. **Companion composition (US3)** — `integration-composition-test.cpp`
   - Linear reactive netlist → the reactive supply is fed to `MnaAssembler::refresh` directly and
     `MnaSystem::solve()` produces the expected voltages.
   - Reactive + diode netlist → the reactive supply is Newton's `base`; reactive companions are
     stamped unchanged across Newton's iterations while diode companions update; `at()` is
     `noexcept`.

4. **History advance contract (SC-005)** — `integration-integrator-test.cpp`
   - After each converged step, assert stored `{vPrev, iPrev}` equals `{v^n, Geq·v^n − Ieq}` using
     that step's stamped companion, for every reactive element, under both rules.
   - Assert history advances exactly once per step (not per Newton iteration) on a reactive+diode
     transient.

5. **Time-stepping / transient (US5)** — `integration-composition-test.cpp`
   - Integrate a reactive diode-clipper transient sample-by-sample; confirm a stable output
     waveform, warm-start carried across samples by the integrator, companions computed once per
     sample.

6. **RT-safety / zero heap (SC-004)** — `integration-integrator-test.cpp`
   - `AllocationSentinel::reset()` around a `plan()`-once / `step()`-many loop →
     `allocations() == 0 && deallocations() == 0`. No exception escapes `step()`. Companions not
     recomputed per Newton iteration.

7. **Physical invariants (SC-006)** — `integration-invariants-test.cpp`
   - DC steady state: driven to settle, a capacitor → open (`i → 0`), an inductor → short
     (`v → 0`); both rules reach the correct DC operating point.
   - Passivity: a passive RLC network exhibits no energy gain across a transient.

8. **No fallback (SC-007)** — `integration-integrator-test.cpp`
   - Forced non-convergence on a composed nonlinear step → `StepResult.converged == false`
     surfaced by value; reactive history is **not** advanced from the untrustworthy iterate; a
     following clean step is unaffected.
   - The selected rule is used verbatim: trapezoidal ringing on a stiff node is produced, not
     silently switched to backward-Euler.

9. **Lab equivalence oracle (SC-003)** — `integration-equivalence-test.cpp`
   - Reactive (and reactive+diode) topologies integrated by both the primitive (rule =
     backward-Euler) and a lab solver (`LinearSolver` / `TransientClipper` / `OpAmpClipperSolver`)
     across a transient → node voltages agree to tolerance at matched samples.

10. **Construction validation (C1)** — `integration-integrator-test.cpp`
    - `dt <= 0` (or invalid forwarded Newton config) → descriptive throw at construction, off the
      hot path.

## Portability gate (SC-008)

```bash
scripts/check-portability.sh    # C++17, header-only, no platform headers; files within budget
```

## Done when

All `integration*` doctest suites pass, the convergence-order test confirms ≈1 (BE) / ≈2 (trap),
the `AllocationSentinel` reports zero heap on the `step()` path, and `check-portability.sh` is
clean.

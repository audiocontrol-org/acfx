# Quickstart: validating the Newton–Raphson iteration primitive

A run/validation guide — not implementation code. It proves the feature end-to-end via the
host-side test suite. Implementation lives in `tasks.md`.

## Prerequisites

- The acfx core build (CMake + CPM). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.
- doctest (already vendored for `tests/core/`).
- `tests/support/allocation-sentinel.h` (existing).
- The shipped MNA primitive (`core/primitives/circuit/mna/`) and `models/diode.h` (consumed).

## Build & run the tests

```bash
# configure + build the core test target (adjust preset to your local setup)
cmake --build build --target acfx_core_tests
./build/tests/core/acfx_core_tests --test-suite=newton*
```

## Validation scenarios (map to spec Success Criteria)

1. **Exact closed-forms (SC-001)** — `newton-closed-form-test.cpp`
   - Single diode + series resistor + DC source at several levels (forward & reverse) →
     converged node voltage matches an in-test Lambert-W / independently-iterated operating
     point to tolerance; transfer curve monotonic.
   - Symmetric antiparallel diode pair at **zero drive** → port voltage exactly 0 V
     (to solver tolerance) by symmetry.

2. **Coupled multi-diode charter (SC-005)** — `newton-solver-test.cpp`
   - Antiparallel pair / longer string / bridge (≥2 interacting nonlinearities at distinct
     node pairs) → solved (or honestly reported non-converged), **never refused** (contrast
     the lab, which throws). All diodes updated within one global step per iteration.

3. **Companion composition (US3)** — `newton-solver-test.cpp`
   - Hand-written base supply returns a fixed companion for a reactive slot; Newton overrides
     the diode slots → base companion passes through unchanged each iteration while diode
     companions update. Empty base supply (v1 DC case) → only diode indices populated.

4. **Stateless / warm start (SC-007)** — `newton-solver-test.cpp`
   - Two `solve()` calls with identical `(netlist, base, guess)` → bit-identical
     `NewtonStatus` and node voltages. Varying only the guess (cold zero vs. near-solution)
     → same fixed point; warm start converges in no more iterations.
   - Guess is the full node-voltage array (branch currents not part of it).

5. **RT-safety / zero heap (SC-003)** — `newton-solver-test.cpp`
   - `AllocationSentinel::reset()` around a `plan()`-once / `solve()`-many loop →
     `allocations() == 0 && deallocations() == 0`. No exception escapes `solve()`.

6. **No fallback (SC-004)** — `newton-nofallback-test.cpp`
   - Forced non-convergence (tight tol, low bound) → `converged == false`,
     `iterations == maxIterations`, residuals reported, last iterate returned, no state
     corruption (a following identical solve is unaffected).
   - Singular linearized system → `MnaSystem::solve()` false → surfaced by value, no throw on
     the hot path, no gmin/substituted output.

7. **Physical invariants (SC-006)** — `newton-invariants-test.cpp`
   - Antiparallel symmetry: odd transfer curve; `I(0) = 0`.
   - Monotonic transfer; passivity (dissipated ≤ source energy).

8. **Lab equivalence oracle (SC-002)** — `newton-equivalence-test.cpp`
   - TS808 diode-clipper core solved by both the primitive and the lab
     `OpAmpClipperSolver` / `TransientClipper` at matched inputs → node voltages agree to
     tolerance.

9. **Construction validation (C1)** — `newton-solver-test.cpp`
   - `maxIterations < 1` or non-positive tolerance → descriptive throw at construction, off
     the hot path.

## Portability gate (SC-008)

```bash
scripts/check-portability.sh    # C++17, header-only, no platform headers; files within budget
```

## Done when

All `newton*` doctest suites pass, the `AllocationSentinel` reports zero heap on the
`solve()` path, and `check-portability.sh` is clean.

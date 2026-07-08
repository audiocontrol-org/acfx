# Quickstart: validating the MNA primitive

A run/validation guide — not implementation code. It proves the feature end-to-end
via the host-side test suite. Implementation lives in `tasks.md`.

## Prerequisites

- The acfx core build (CMake + CPM). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.
- doctest (already vendored for `tests/core/`).
- `tests/support/allocation-sentinel.h` (existing).

## Build & run the tests

```bash
# configure + build the core test target (adjust preset to your local setup)
cmake --build build --target acfx_core_tests
./build/tests/core/acfx_core_tests --test-suite=mna*
```

## Validation scenarios (map to spec Success Criteria)

1. **Exact closed-forms (SC-001)** — `mna-assembler-test.cpp`
   - Resistive divider → `V(mid) == Vin·R2/(R1+R2)` to FP precision.
   - **Floating** voltage source between two non-ground nodes → imposed `V(a)−V(b)`
     and branch current exact (SC-005 — the new capability).
   - Ideal inverting amp → `Vout == −Vin·Rf/Rin`; non-inverting → `Vin·(1+Rf/Rg)`.
   - Fed-companion RC step (harness supplies `{C/dt, (C/dt)·vPrev}`) → matches the
     backward-Euler recurrence step-for-step.

2. **Stateless / pure (US3-AS2)** — `mna-system-test.cpp`
   - Two successive `refresh+solve` with identical inputs → bit-identical solutions.

3. **RT-safety / zero heap (SC-003)** — `mna-system-test.cpp`
   - `AllocationSentinel::reset()` around a plan-once / refresh-and-solve-many loop →
     `allocations() == 0 && deallocations() == 0`. No exception escapes `solve()`.

4. **Ill-posed → not-solved (SC-004)** — `mna-invariants-test.cpp`
   - Floating subgraph / redundant nullor → `solve()` returns `false`, no throw, no
     NaN in `nodeVoltage`.
   - Poorly-scaled-but-well-posed system → solves correctly (relative threshold, D1).

5. **Physical invariants (FR-022)** — `mna-invariants-test.cpp`
   - Passivity: dissipated energy ≤ source energy.
   - Reciprocity / symmetry of the conductance block.

6. **Lab equivalence oracle (SC-002)** — `mna-equivalence-test.cpp`
   - For each topology within `LinearSolver` scope → node voltages agree ≤ 1e-12.
   - For each topology within `NullorSolver` scope → voltages + branch currents ≤ 1e-12.

7. **Plan-time validation (SC-006)** — `mna-assembler-test.cpp`
   - Branch overflow / out-of-range node / degenerate value → descriptive throw at
     `plan()`, before any solve.

## Portability gate (SC-007)

```bash
scripts/check-portability.sh    # C++17, header-only, no platform headers; files within budget
```

## Done when

All `mna*` doctest suites pass, the `AllocationSentinel` reports zero heap on the
solve path, and `check-portability.sh` is clean.

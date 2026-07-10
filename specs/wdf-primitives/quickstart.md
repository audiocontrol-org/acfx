# Quickstart: validating the WDF-primitives family

A run/validation guide — not implementation code. It proves the feature end-to-end via the
host-side test suite. Implementation lives in `tasks.md`.

## Prerequisites

- The acfx core build (CMake + CPM). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.
- doctest (already vendored for `tests/core/`).
- `tests/support/allocation-sentinel.h` (existing).
- The frozen circuit vocabulary (`core/primitives/circuit/models/`) — reused for physical
  constants only. No dependency on the nodal solvers.

## Build & run the tests

```bash
# configure + build the core test target (adjust preset to your local setup)
cmake --build build --target acfx_core_tests
./build/tests/core/acfx_core_tests --test-suite=wdf*
```

## Validation scenarios (map to spec Success Criteria)

1. **Exact scattering closed forms (SC-001)** — `wdf-resistor-test.cpp`, `wdf-sources-test.cpp`,
   `wdf-terminations-test.cpp`
   - Adapted resistor → `reflected() == 0` for any incident history; `portResistance() == R`.
   - Resistive voltage source → `reflected() == E`; after `setVoltage(E')`, `reflected() == E'`.
   - Resistive current source → `reflected() == R·I`.
   - Short → `reflected() == −a` after `incident(a)`; open → `reflected() == +a`.
   - Resistive termination → `reflected() == 0` (matched).

2. **Unit-delay reactive scattering + port resistance (SC-002)** — `wdf-reactive-test.cpp`
   - `Capacitor(C, dt)` → `portResistance() == T/(2C)`; drive a sequence and assert
     `reflected()` this sample equals the previous `incident` (`b[n] = a[n−1]`, `b[0] = 0`).
   - `Inductor(L, dt)` → `portResistance() == 2L/T`; `b[n] = −a[n−1]`.
   - Bilinear-impedance agreement: the reactive leaf's discrete port impedance matches the
     bilinear-discretized analog impedance at test frequencies.

3. **Reactive wave-power balance — the correct passivity criterion (SC-003)** —
   `wdf-passivity-test.cpp`
   - Drive a capacitor (and inductor) with an incident-wave sequence; accumulate
     `Σ(a[k]² − b[k]²)` and assert it stays `≥ 0` at every prefix and equals the stored `a[N]²`
     for the lossless capacitor (energy stored and returned). **Do NOT** assert same-sample
     `|b| ≤ |a|` for reactive leaves (it is invalid — `b[n] = a[n−1]`).

4. **Memoryless passivity (SC-003)** — `wdf-passivity-test.cpp`
   - Adapted resistor and resistive termination: over a range of incident waves,
     `|b| ≤ |a|` and `portResistance() > 0`.

5. **Duality (SC-008)** — `wdf-reactive-test.cpp`
   - Under the same `dt`, capacitor (`Rp = T/2C`, `b = +state`) and inductor (`Rp = 2L/T`,
     `b = −state`) exhibit the dual relationship.

6. **Port interface / generic sweep, no virtual dispatch (SC-005)** — `wdf-port-interface-test.cpp`
   - A single templated up-sweep/down-sweep driver over a heterogeneous set of leaves invokes
     `portResistance()` / `reflected()` / `incident()` on each with no runtime dispatch;
     `isAdaptable` sequences `reflected()` vs. `incident()` correctly (adaptable: reflected
     before incident; reflective: incident before reflected).

7. **RT-safety / zero heap (SC-004)** — `wdf-rt-safety-test.cpp`
   - `AllocationSentinel::reset()` around a many-sample `reflected()`/`incident()` loop over each
     leaf → `allocations() == 0 && deallocations() == 0`; no exception escapes the wave path.

8. **No fallback (SC-006)** — `wdf-rt-safety-test.cpp`
   - Construct each leaf with a non-physical parameter (`R/C/L/dt ≤ 0`) → descriptive throw at
     construction (off the hot path); assert no reflection is clamped to force `|b| ≤ |a|` when a
     parameter is out of range.

## Portability gate (SC-007)

```bash
scripts/check-portability.sh    # C++17, header-only, no platform headers; files within budget
```

## Done when

All `wdf*` doctest suites pass; the reactive suite confirms the unit-delay relation and the
wave-power balance `Σ(a²−b²) ≥ 0` (not same-sample `|b|≤|a|`); the memoryless suite confirms
`|b|≤|a|`; the `AllocationSentinel` reports zero heap on the wave path; and
`check-portability.sh` is clean. Full-circuit response and adaptation are **not** validated here
— they belong to the adaptor/network sibling nodes.

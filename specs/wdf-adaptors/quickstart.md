# Quickstart: validating the WDF-adaptors

A run/validation guide — not implementation code. It proves the feature end-to-end via the
host-side test suite. Implementation lives in `tasks.md`. All validation here is
**root-driver-free** (single-sample scattering, invariants); full network transfer-function
tests belong to `wdf-passive-networks`.

## Prerequisites

- The acfx core build (CMake + CPM). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.
- doctest (already vendored for `tests/core/`).
- `tests/support/allocation-sentinel.h` (existing).
- The shipped `wdf-primitives` headers (`core/primitives/circuit/wdf/one-port.h` and the
  leaves) — consumed as the `OnePort` concept and as test children. No dependency on the nodal
  solvers.

## Build & run the tests

```bash
# configure + build the core test target (adjust preset to your local setup)
cmake --build build --target acfx_core_tests
./build/tests/core/acfx_core_tests --test-suite=wdf*adaptor*
```

## Validation scenarios (map to spec Success Criteria)

1. **Exact series scattering closed form (SC-001)** — `wdf-series-adaptor-test.cpp`
   - `SeriesAdaptor(Resistor(Ra), Resistor(Rb))` → `portResistance() == Ra + Rb`.
   - For a chosen upward incident `a_u`, run the down-sweep and assert each child's delivered
     incident equals `b_k = a_k − (2·R_k/R)·Σ_i a_i`; recover child voltages via
     `waveToVoltage`/`waveToCurrent` and assert they match the Ohm's-law **series divider** to
     `≤ 1e-12` relative.

2. **Exact parallel scattering closed form (SC-001)** — `wdf-parallel-adaptor-test.cpp`
   - `ParallelAdaptor(Resistor(Ra), Resistor(Rb))` → `1/portResistance() == 1/Ra + 1/Rb`.
   - Assert child incidents equal `b_k = 2·(Σ_i G_i a_i)/G − a_k`; recovered quantities match
     the **current divider** to `≤ 1e-12` relative.

3. **Reflection-free adapted port (SC-002)** — `wdf-adaptor-reflection-free-test.cpp`
   - Hold child state fixed; read `reflected()` for two different `a_u` (e.g. via a preceding
     `incident()` on a fresh sample) and assert the up-sweep `reflected()` value is invariant to
     `< 1e-15` absolute. Confirm the closed forms `b_u = −Σ a_child` (series) and
     `b_u = (Σ G_child a_child)/G_up` (parallel).

4. **Nested tree composability (SC-003)** — `wdf-adaptor-tree-test.cpp`
   - `SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>>`: assert
     `is_one_port_v<...> == true`, `portResistance()` equals the recursive series/parallel
     combination, and one up-sweep + down-sweep reproduces the by-hand nested scattering at a
     sampled instant to `≤ 1e-12`.
   - **Single-child pass-through**: `SeriesAdaptor(Resistor(R))` and `ParallelAdaptor(Resistor(R))`
     → `portResistance() == R` and transparent wave behavior.

5. **Conductance-weighted passivity (SC-004)** — `wdf-adaptor-passivity-test.cpp`
   - For randomized admissible child resistances and incident waves, assert
     `|Σ_k G_k a_k² − Σ_k G_k b_k²| / (Σ_k G_k a_k²) < 1e-12` for both adaptor kinds. Assert the
     **unweighted** `Σ a_k² = Σ b_k²` is NOT relied on (holds only for equal resistances).

6. **RT-safety / zero heap (SC-005)** — `wdf-adaptor-rt-safety-test.cpp`
   - `AllocationSentinel::reset()` around a many-sample up-sweep/down-sweep over a nested tree →
     `allocations() == 0 && deallocations() == 0`; no exception escapes the wave path.

7. **No fallback / construction validation (SC-006)** — `wdf-adaptor-rt-safety-test.cpp`
   - Construct an adaptor with a non-positive or non-finite child `Rp` → descriptive
     `std::invalid_argument` at construction naming the child; assert no coefficient is clamped
     or fabricated.

8. **Compile-time child rejection (SC-007)** — `wdf-adaptor-tree-test.cpp` (documented static
   assertion)
   - A commented negative-compilation example: `SeriesAdaptor<Resistor, ShortCircuit>` must fail
     to compile (`ShortCircuit::isAdaptable == false`), demonstrating the delay-free-loop guard.
     Enforcement is the `static_assert` in `adaptor-detail.h`.

9. **Typed child access (US6 / FR-012)** — `wdf-adaptor-child-access-test.cpp`
   - Build a tree with a nested `ResistiveVoltageSource`; via `child<I>()` read its port
     resistance and mutate its drive value; a subsequent sweep reflects the change.

## Portability gate

```bash
scripts/check-portability.sh    # C++17, header-only, no platform headers; files within budget
```

## Done when

All `wdf*adaptor*` doctest suites pass; the scattering suites confirm the exact series/parallel
divider closed forms; the reflection-free suite confirms `b_u` is invariant to `a_u`; the
passivity suite confirms the conductance-weighted balance (not the unweighted one); the
`AllocationSentinel` reports zero heap on the wave path; construction throws on non-physical
input; a non-adaptable child is rejected at compile time; and `check-portability.sh` is clean.
Whole-tree response, root termination, and adaptation are **not** validated here — they belong
to the sibling nodes.

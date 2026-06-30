# Quickstart — Waveshapers validation

How to build and validate the waveshapers primitive end-to-end. This is a run/validate
guide; implementation detail lives in `tasks.md` and the source. Build uses CMake
presets (CPM-pinned deps). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.

## Prerequisites

- The repo builds today: `cmake --preset test` succeeds on the branch.
- The shipped measurement infrastructure (Goertzel/THD analyzer, sine stimulus,
  allocation sentinel, analytic-bound helpers) is present under `tests/`.

## Build + run the host test suite

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Expected: the new waveshaper doctest suites pass alongside the existing suites.

## Validation scenarios (map to user stories / success criteria)

1. **Memoryless shape correctness (US2 / SC-001)** — `waveshaper-shapes-test.cpp`
   sweeps each pure `acfx::shape::*` function and asserts its closed-form definition
   (range, symmetry class, monotonicity, anchor points) within the named tolerance.

2. **Wrapper signal chain + DC-block (US1 / SC-002)** — `waveshaper-test.cpp` drives a
   sine through `Waveshaper` with symmetric and asymmetric shapes; asserts the
   drive→bias→shape→dcBlock→gainComp order, odd-only vs even+odd harmonics, and
   steady-state output DC within tolerance of zero. Includes silence-in→silence-out and
   gain-compensation-toward-unity checks.

3. **Harmonic signatures (US1/US2 / SC-001)** — `waveshaper-harmonics-test.cpp` uses the
   Goertzel/THD analyzer + sine stimulus to assert each shape's harmonic series against
   its analytic prediction (named tolerance).

4. **LUT vs closed-form (US3 / SC-004)** — `waveshaper-lut-test.cpp` evaluates each
   table-backed shape and asserts max deviation from closed-form (the reference) ≤ the
   named interpolation-error bound at the stated table resolution; asserts the table is
   built in `init()` (no per-sample allocation).

5. **ADAA aliasing reduction (US4 / SC-003)** — `waveshaper-adaa-test.cpp` drives a
   high-frequency tone (harmonics beyond Nyquist) through the naive shaper and the ADAA
   variant; asserts the ADAA variant's inharmonic energy is lower by ≥ the named margin
   for a covered aggressive shape; asserts an uncovered shape raises a descriptive error.

6. **Real-time safety (SC-005)** — the allocation sentinel confirms zero heap allocation
   / no locks on `process()`; stress inputs produce no NaN/Inf/denormal.

## Run the lab harness (host-only)

```bash
cmake --build --preset test --target acfx_lab_waveshaping_harness   # host-only target
./build/.../acfx_lab_waveshaping_harness                            # exact path per preset
```

Expected: per-shape harmonic evidence and the naive-vs-ADAA aliasing comparison
(optionally a CSV harmonic-spectrum dump — Open Question). The harness is never linked
into portable core or an MCU target.

## Portability / layering gate

```bash
scripts/check-portability.sh
```

Expected (after the FR-022 extension): passes for `core/labs/waveshaping/**` and
`core/primitives/nonlinear/**` — no platform headers in core, no portable include of a
harness, `core/primitives` never includes `core/effects`, all files within the size budget.

## Embedded compile (cross-compile only; no on-hardware step here)

```bash
cmake --preset daisy  && cmake --build --preset daisy    # requires ARM toolchain
cmake --preset teensy && cmake --build --preset teensy
```

Expected: the nonlinear primitive compiles for the embedded targets with no
platform-framework headers (SC-006). On-hardware measurement is a separate, later concern.

## Graduation check

After `git mv`-ing the kernel from `core/labs/waveshaping/` into
`core/primitives/nonlinear/` and updating includes: the suite + harness + portability
gate all still pass, and the graduated primitive is the relocated lab kernel (SC-007) —
proving the Theory→Lab→Primitive pattern forward.

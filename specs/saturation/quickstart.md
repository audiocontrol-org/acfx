# Quickstart — Saturation validation

How to build and validate the saturation effect end-to-end. This is a run/validate
guide; implementation detail lives in `tasks.md` and the source. Build uses CMake
presets (CPM-pinned deps). Offline sandbox: `export CPM_SOURCE_CACHE=external/.cpm-cache`.

## Prerequisites

- The repo builds today: `cmake --preset test` succeeds on the branch.
- The composed primitives are already shipped: `acfx::Waveshaper` /
  `acfx::ADAAWaveshaper` (`core/primitives/nonlinear/`) and `acfx::SvfPrimitive`
  (`core/primitives/filters/`). This feature adds **no** new primitive.
- The base measurement infrastructure (Goertzel/THD analyzer, sine stimulus, allocation
  sentinel, analytic-bound helpers) is already shipped under `tests/`. Any
  saturation-specific measurement helpers (per-voicing signature, mix-balance, aliasing)
  are added by this feature on top of that base.

## Build + run the host test suite

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Expected: the new saturation doctest suites pass alongside the existing suites.

## Validation scenarios (map to user stories / success criteria)

1. **Composition signal chain + mix (US1/US3 / SC-003)** — `saturation-core-test.cpp`
   drives a sine through `SaturationCore`; asserts the pre-emphasis → waveshaper →
   post-de-emphasis → tone → mix → output order, that a fully-dry `mix` reproduces the
   input within tolerance, a fully-wet `mix` is the saturated path, and the
   steady-state (post-settling) output is DC-free for biased settings — the DC-blocker's
   startup transient is excluded. Silence-in→silence-out is asserted at zero bias.

2. **Per-voicing distinctness (US2 / SC-001)** — `saturation-voicings-test.cpp` runs an
   identical stimulus through each of the four voicings; asserts each matches its
   documented shape+emphasis signature within tolerance and that the four are mutually
   distinguishable by ≥ the named margin; asserts a voicing switch carries no stale
   filter/DC state.

3. **Harmonic signatures + drive→THD monotonicity (US1/US2 / SC-001, SC-002)** —
   `saturation-harmonics-test.cpp` uses the Goertzel/THD analyzer + sine stimulus to
   assert each voicing's harmonic series against its analytic prediction, that rising
   `drive` raises measured THD monotonically, and that gain-compensation holds output
   loudness within the named band.

4. **Anti-aliasing quality (US4 / SC-004)** — `saturation-aliasing-test.cpp` drives a
   high-frequency tone (harmonics beyond Nyquist) through the effect in `naive` vs
   `adaa` quality; asserts the ADAA mode's inharmonic energy is lower by ≥ the named
   margin; asserts the parameter surface is unchanged by the quality switch; asserts the
   reserved `oversampled` selection resolves to the ADAA path (a defined, bounded, non-
   aliased interim, not user-selectable — excluded from `kQualityLabels`).

5. **Effect contract + cross-thread params (US3 / SC-005)** — `saturation-effect-test.cpp`
   covers the descriptor-surface cases (parameter table compile-time invariants, id/label
   mapping); `saturation-effect-rt-test.cpp` publishes parameter edits from a non-audio
   thread while processing and asserts they take effect at the block boundary with no
   allocation/lock, `mix` blends per the documented law, and `bias` introduces the
   expected even-harmonic content DC-free.

6. **Real-time safety (SC-005)** — `saturation-effect-rt-test.cpp`'s allocation sentinel
   confirms zero heap allocation / no locks on `process()`; stress inputs (extreme drive,
   voicing/quality switches) produce no NaN/Inf/denormal.

## Run the lab harness (host-only)

```bash
cmake --build --preset test --target acfx_lab_saturation_harness   # host-only target
./build/test/acfx_lab_saturation_harness                            # human-readable report
./build/test/acfx_lab_saturation_harness --csv                      # machine-readable CSV dump
```

Expected: per-voicing harmonic evidence and the naive-vs-ADAA aliasing comparison. The
harness is never linked into portable core or an MCU target. CSV columns (finalized in
T023): `voicing,harmonic_index,frequency_hz,magnitude`.

## Portability / layering gate

```bash
scripts/check-portability.sh
```

Expected (after the FR-022 extension): passes for `core/labs/saturation/**` and
`core/effects/saturation/**` — no platform headers in core, no portable include of a
harness, the effect may include `core/primitives` but nothing portable includes a
harness, all files within the size budget.

## Embedded compile (cross-compile only; no on-hardware step here)

```bash
cmake --preset daisy  && cmake --build --preset daisy    # requires ARM toolchain
cmake --preset teensy && cmake --build --preset teensy
```

Expected: the saturation effect compiles for the embedded targets with no
platform-framework headers (SC-006). On-hardware measurement is a separate, later concern.
Note: this is an operator-run step requiring the ARM toolchain; it was NOT exercised in
the host validation runs (T025 `[~]`) — SC-006 rests on the structural
platform-independence gate, not a run cross-compile.

## Graduation check

After `git mv`-ing the composition kernel from `core/labs/saturation/` into
`core/effects/saturation/`, adding the `SaturationEffect` wrapper, and updating includes:
the suite + harness + portability gate all still pass, and the graduated effect is the
relocated lab kernel (SC-007) — proving the Theory→Lab→Effect pattern reaches the
Production-Effect stage.

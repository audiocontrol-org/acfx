# Quickstart — tape-dynamics (Hysteresis Primitive + TapeDynamicsEffect)

**Feature**: `specs/tape-dynamics` | **Date**: 2026-07-03 | **Phase**: 1

A validation/run guide that proves the feature works end-to-end. Implementation lives in `tasks.md`;
API and state details are in `contracts/{hysteresis,tape-dynamics-effect}-api.md` and `data-model.md`.

## Prerequisites
- The acfx host toolchain (CMake + CPM, C++20 desktop build; `acfx_core` floor `cxx_std_17`).
- Offline CPM cache if sandboxed: `export CPM_SOURCE_CACHE=external/.cpm-cache` (see project memory).
- The composed primitives are shipped: `Oversampler<Factor>`, `EnvelopeFollower`, `GainComputer`.

## Build & run the host test suite
```bash
make test          # configures + builds the desktop test target and runs the doctest suite
```
Expected: all `tests/core/` suites pass, including the new tape-dynamics suites:
- `hysteresis-test.cpp` — closed-loop area > 0, `reset()` reproducibility, all three solvers finite,
  solver agreement, `k`/`Ms` parameter response (US2, SC-001/002/005)
- `tape-dynamics-effect-test.cpp` — Effect concept + param handoff, `drive`=0 unity, emergent
  compression, trim on/off bit-exact equivalence (US1/US4/US5/US6, SC-003/005)
- `tape-dynamics-alias-test.cpp` — alias-sweep metric falls as oversampling rises (US7, SC-004)
- `no-allocation-test.cpp` (extended) — zero heap allocation on `process()` across all configs (SC-007)

## Run the lab harness (measurement)
```bash
# Host-only measurement harness — prints loop/solver/compression/alias measurements
make build && ./build/desktop/core/labs/tape-dynamics/tape-dynamics-harness   # exact path per CMake target
```
Expected: prints the `M`-vs-`H` loop area (> 0), per-solver loop agreement, the output-vs-input level
curve + dynamic-range-reduction metric across `drive`, and the alias metric across oversampling factors.

## Portability / layering gate
```bash
scripts/check-portability.sh      # explicit CI step (never a git hook, Constitution II)
```
Expected: passes over the new `core/primitives/nonlinear/hysteresis.h`,
`core/effects/tape-dynamics/`, and `core/labs/tape-dynamics/` paths — no platform headers in core, lab
host-only.

## Validation scenarios (what each proves)
| Scenario | Setup | Expected outcome | Spec |
|---|---|---|---|
| Memory (closed loop) | sinusoidal `H` into `Hysteresis` | `M`-vs-`H` traces a closed loop, area > threshold | SC-001 |
| Static contrast | same drive into a memoryless waveshaper | single-valued curve, area ≈ 0 | SC-001 |
| Reset reproducibility | run, `reset()`, run identical input | identical output both runs | FR-003 |
| Solver agreement | rk2/rk4/newton, fixed input + OS factor | loops agree within tol; tighter as OS ↑ | SC-002 |
| Solver stability | hot transient, each solver, low OS | output finite (no NaN/Inf); recovers | SC-005 |
| Unity passthrough | `drive` = 0 / bypass | output ≈ input, unity gain | SC-005 |
| Emergent compression | trim OFF; input-level sweep at fixed drive | output-vs-input monotonic + compressive above thr | SC-003 |
| Compression vs drive | trim OFF; DRR metric at low vs high drive | DRR(high) > DRR(low) | SC-003 |
| Emergent not a param | inspect parameter list | no "compression" parameter present | FR-012 |
| Aliasing vs OS | hot tone at 2×/4×/8×/16× | alias-sweep metric decreases monotonically | SC-004 |
| Trim no-op | `trim.enabled` = false | output bit-exact the magnetics-only core | FR-011 |
| Trim active | `trim.enabled` = true, set attack/release/amount | envelope-driven gain reduction tracks timing | US6 |
| RT-safety | allocation sentinel over `process()` all configs | zero heap allocation, no locks | SC-007 |
| Graduation | inspect tree + README | `nonlinear/hysteresis.h` exists; README lists it (first stateful) | SC-006 |

## Definition of done (this feature)
- All new `tests/core/` suites green via `make test`.
- Harness prints loop area > 0, solver agreement, drive-increasing DRR, OS-decreasing alias metric.
- `scripts/check-portability.sh` passes over the new paths.
- `core/primitives/README.md` lists `nonlinear/hysteresis.h` as the first *stateful* inhabitant.

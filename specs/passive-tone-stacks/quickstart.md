# Quickstart — passive-tone-stacks validation

How to build and run the two validation tiers. This is a run/validation guide; implementation lives in `tasks.md` and the sources.

## Prerequisites

- The repo's normal host toolchain (CMake + a C++20-capable compiler for the lab; the primitive headers are C++17).
- Offline CPM cache (per repo convention): `export CPM_SOURCE_CACHE=external/.cpm-cache` before configuring/building.

## Tier 1 — portable primitive tests (no solver)

Prove the wiper/taper math and the builder topology **without** the lab. These run with the normal core suite:

```
export CPM_SOURCE_CACHE=external/.cpm-cache
make test        # runs tests/core/, including tone-stack-taper-test + tone-stack-builder-test
```

Expected:

- `tone-stack-taper-test` — legs sum to `rTrack` within the 10 Ω floor; `pos=0.5` Linear → equal legs; `Log` matches its reference fraction; `pos=0`/`pos=1` legs equal 10 Ω, never 0; bad `pos`/`rTrack` throw `std::invalid_argument`.
- `tone-stack-builder-test` — `toneStackFMV` / `toneStackBaxandall` return a `prepare()`-valid `Netlist` at all-0 / all-1 / mixed / center; component and node counts match the BOM; only frozen-vocabulary components present.

## Tier 2 — lab AC solver + analytic match

Prove `solveAC` on sanity networks, then the FMV/Baxandall analytic cross-check.

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_passive_tone_stacks_harness
./build/test/acfx_lab_passive_tone_stacks_harness      # PASS/FAIL lines, exits nonzero on any failure
```

Expected PASS lines (measured vs expected):

- **RC low-pass** — `−20 dB/decade` roll-off; phase → `−90°`; matched to closed form (~`1e-9`).
- **Resistive divider** — flat at `R2/(R1+R2)`.
- **FMV** — `|H(f)|` within 0.1 dB of the analytic FMV transfer function at ≥3 control settings incl. a low-mid (scoop) setting; the mid scoop deepens as the mid pot lowers; HF rises with the treble pot.
- **Baxandall** — `|H(f)|` within 0.1 dB of the analytic James curve at ≥3 settings; bass/treble shelves move the expected asymptotes; center near-flat.

The same assertions run inside the suite as `tests/core/tone-stack-ac-test.cpp` (the harness mirrors the doctests so the validation is readable outside the framework).

## Isolation check (SC-006)

With the lab deleted, the primitive and Tier-1 tests must still build and pass:

```
rm -rf core/labs/passive-tone-stacks         # (in a throwaway checkout)
export CPM_SOURCE_CACHE=external/.cpm-cache
make test                                     # Tier-1 tone-stack tests still green; only Tier-2/harness gone
```

## What you will NOT find

- No realtime `process()` / audio-path demo — this deliverable is a builder + validation only (FR-017).
- No MNA/Newton in the lab — `solveAC` is a linear complex `.ac` solve (FR-013).

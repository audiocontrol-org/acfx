# Quickstart — component-abstractions (build & validate)

A run/validation guide, not an implementation guide. It proves the feature end-to-end:
the component vocabulary, the netlist validation, and the lab reference solver against
analytic references. Implementation detail lives in `tasks.md` and the sources.

## Prerequisites

- The acfx host toolchain (CMake + a C++20 host compiler for the lab harness; the primitive
  headers are C++17-clean). No hardware, no JUCE — this is core-only.
- Offline builds: `export CPM_SOURCE_CACHE=external/.cpm-cache` (per the repo's offline note);
  the `test` preset is core-only and needs no plugin deps.

## Run the host test suite (the everyday loop)

```bash
make test          # configure + build the core tests + run ctest (no hardware)
```

Expected: the three new suites pass alongside the existing core tests —

- `circuit-components-test` — per-component physics (US1): resistor `G=1/R`, Shockley
  current/conductance, C/L backward-Euler companions.
- `circuit-netlist-test` — netlist assembly + `prepare()` validation (US2): a good divider
  validates; floating-node / missing-ground / over-capacity each throw a **distinct**
  descriptive error; a representative solve loop asserts **zero allocation**.
- `circuit-solver-test` — assembled circuits vs analytic (US3/US4): divider exact; RC/RLC
  within backward-Euler tolerance; single & antiparallel diode clipper transfer within
  tolerance; a ≥2-nonlinearity netlist is **refused** with the Phase-5 message.

## Run the lab harness (assembled-circuit validations)

```bash
cmake --build --preset test --target acfx_lab_component_abstractions_harness
./build/test/acfx_lab_component_abstractions_harness
```

Expected output: each validation circuit reports its measured-vs-analytic error under
tolerance —

| Circuit | Reference | Pass condition |
|---|---|---|
| Resistive divider | `Vin·R2/(R1+R2)` | exact (to `double` precision) — SC-002 |
| RC low-pass sweep | `\|1/(1+jωRC)\|`, phase | within documented backward-Euler tolerance — SC-003 |
| RLC network | analytic 2nd-order | within tolerance — SC-003 |
| Diode clipper (single / antiparallel) | analytic soft-clip curve | within tolerance; Newton within its bound — SC-004 |

## What "done" looks like (maps to Success Criteria)

- `ls core/primitives/circuit/` shows only inhabited, documented headers; `core/primitives/
  README.md` lists the `circuit/` category + its six inhabitants (SC-008).
- The primitive headers compile under `-std=c++17` (SC-006) and their tests pass **with the lab
  removed** (SC-007) — proving the solver-independent seam.
- Every ill-posed topology and the out-of-scope circuit produce distinct descriptive errors,
  with no fallback path exercised (SC-005).

## Where to look

- Vocabulary contract: `contracts/component-physics.md`
- Netlist contract: `contracts/netlist.md`
- Reference-solver contract + hard scope boundary: `contracts/reference-solver.md`
- Formulation decisions (voltage-source reduction, Shockley/Newton, companions): `research.md`

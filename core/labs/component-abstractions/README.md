# Component Abstractions — Lab

A reference solver for the component-abstractions primitive: demonstrates how
assembled circuits are solved using nodal analysis and automated equation generation
via the component-abstractions framework.

This solver is deliberately naive and non-normative — Phase 5 (MNA / Newton / implicit
integration) supersedes it. It is not thrown away but retained as a reference and
educational artifact.

## Solver

The `solver/` subdirectory will contain the reference solver implementation in Phase 2
(task T015/T017). It will demonstrate:
- Netlist parsing and circuit representation
- Nodal analysis equation setup
- Linear system solution (DC operating point)

## Harness

The host-only `harness/` directory contains diagnostic and validation executables
for the component-abstractions primitive:

### Build and run

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_component_abstractions_harness
./build/test/acfx_lab_component_abstractions_harness
```

Assembled-circuit validations and spectral evidence are added in later tasks (T016/T019).

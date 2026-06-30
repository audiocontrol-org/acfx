---
doc-grammar: roadmap
---

# Roadmap

The governed dependency graph of this project's features. Each item is a
heading-keyed unit identified by its `<phase>:<kind>/<slug>` id.

Mutate the graph with `stackctl roadmap` verbs (run `stackctl roadmap --help`
for the full surface): `add` a new item, `advance` its status, `decompose`,
`reclassify`, `defer`, and `cluster` / `group` to gather existing items under a
created-or-reused parent. Example — cluster items under a new epic with a
dependency chain:

    stackctl roadmap cluster multi:feature/epic --children design:feature/a,impl:feature/b --chain --apply

For an edit that has no verb yet (e.g. moving a `part-of` / `depends-on` edge):
edit this file directly, then run `stackctl roadmap order` to revalidate the
graph (it fails loud on a cycle / dangling ref / duplicate id).

## design:feature/svf-vertical-slice
- status: closed
- part-of: multi:feature/phase-digital-fundamentals
- validated: yes
- analyze-clean: yes
- spec: specs/svf-vertical-slice
Milestone 1: prove the acfx spine end-to-end with a State-Variable Filter — core abstractions (Effect concept, ProcessContext, AudioBlock, Parameter model, ProcessorNode boundary), the SVF effect (host-tested), desktop workbench + plugin fully working, Daisy+Teensy cross-compile/link proven, CMake presets + CPM pinning + CI

## design:feature/workbench-audio-config
- status: closed
- part-of: multi:feature/phase-digital-fundamentals
- validated: yes
- analyze-clean: yes
- spec: specs/workbench-audio-config

## impl:feature/modulated-delay
- status: closed
- validated: yes
- part-of: multi:feature/phase-digital-fundamentals
- spec: specs/modulated-delay

## design:feature/program-scaffolding
- status: closed
- validated: yes
- analyze-clean: yes
- spec: specs/program-scaffolding

## multi:feature/progressive-dsp-platform
- status: planned
- ref: docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md

## multi:feature/phase-digital-fundamentals
- status: closed
- validated: yes
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-nonlinear-dsp
- status: planned
- depends-on: multi:feature/phase-digital-fundamentals, design:gap/three-layer-structure, design:feature/measurement-infrastructure
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-dynamic-systems
- status: planned
- depends-on: multi:feature/phase-nonlinear-dsp
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-circuit-modeling
- status: planned
- depends-on: multi:feature/phase-dynamic-systems
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-numerical-solvers
- status: planned
- depends-on: multi:feature/phase-circuit-modeling
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-wave-digital-filters
- status: planned
- depends-on: multi:feature/phase-numerical-solvers
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-physical-modeling
- status: planned
- depends-on: multi:feature/phase-wave-digital-filters
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-convolution
- status: planned
- depends-on: multi:feature/phase-physical-modeling
- part-of: multi:feature/progressive-dsp-platform

## multi:feature/phase-reference-hardware
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/progressive-dsp-platform

## design:feature/measurement-infrastructure
- status: closed
- validated: yes
- analyze-clean: yes
- spec: specs/measurement-infrastructure
- design-approved: yes
- design: docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md
- part-of: multi:feature/phase-digital-fundamentals

## design:gap/three-layer-structure
- status: closed
- validated: yes
- analyze-clean: yes
- spec: specs/three-layer-structure
- design-approved: yes
- design: docs/superpowers/specs/2026-06-29-three-layer-structure-design.md
- part-of: multi:feature/phase-digital-fundamentals

## design:primitive/waveshapers
- status: planned
- depends-on: multi:feature/phase-digital-fundamentals
- part-of: multi:feature/phase-nonlinear-dsp

## design:feature/saturation
- status: planned
- depends-on: multi:feature/phase-digital-fundamentals
- part-of: multi:feature/phase-nonlinear-dsp

## design:primitive/oversampling
- status: planned
- depends-on: multi:feature/phase-digital-fundamentals
- part-of: multi:feature/phase-nonlinear-dsp

## design:gap/harmonic-analysis
- status: planned
- depends-on: multi:feature/phase-digital-fundamentals
- part-of: multi:feature/phase-nonlinear-dsp

## design:primitive/envelope-followers
- status: planned
- depends-on: multi:feature/phase-nonlinear-dsp
- part-of: multi:feature/phase-dynamic-systems

## design:feature/compressors
- status: planned
- depends-on: multi:feature/phase-nonlinear-dsp
- part-of: multi:feature/phase-dynamic-systems

## design:feature/program-dependent-saturation
- status: planned
- depends-on: multi:feature/phase-nonlinear-dsp
- part-of: multi:feature/phase-dynamic-systems

## design:feature/tape-dynamics
- status: planned
- depends-on: multi:feature/phase-nonlinear-dsp
- part-of: multi:feature/phase-dynamic-systems

## design:primitive/component-abstractions
- status: planned
- depends-on: multi:feature/phase-dynamic-systems
- part-of: multi:feature/phase-circuit-modeling

## design:primitive/passive-tone-stacks
- status: planned
- depends-on: multi:feature/phase-dynamic-systems
- part-of: multi:feature/phase-circuit-modeling

## design:feature/diode-clippers
- status: planned
- depends-on: multi:feature/phase-dynamic-systems
- part-of: multi:feature/phase-circuit-modeling

## design:primitive/opamp-stages
- status: planned
- depends-on: multi:feature/phase-dynamic-systems
- part-of: multi:feature/phase-circuit-modeling

## design:primitive/modified-nodal-analysis
- status: planned
- depends-on: multi:feature/phase-circuit-modeling
- part-of: multi:feature/phase-numerical-solvers

## design:primitive/newton-iteration
- status: planned
- depends-on: multi:feature/phase-circuit-modeling
- part-of: multi:feature/phase-numerical-solvers

## design:primitive/implicit-integration
- status: planned
- depends-on: multi:feature/phase-circuit-modeling
- part-of: multi:feature/phase-numerical-solvers

## design:primitive/wdf-primitives
- status: planned
- depends-on: multi:feature/phase-numerical-solvers
- part-of: multi:feature/phase-wave-digital-filters

## design:primitive/wdf-adaptors
- status: planned
- depends-on: multi:feature/phase-numerical-solvers
- part-of: multi:feature/phase-wave-digital-filters

## design:primitive/wdf-passive-networks
- status: planned
- depends-on: multi:feature/phase-numerical-solvers
- part-of: multi:feature/phase-wave-digital-filters

## design:feature/wdf-complete-analog-stages
- status: planned
- depends-on: multi:feature/phase-numerical-solvers
- part-of: multi:feature/phase-wave-digital-filters

## design:primitive/digital-waveguides
- status: planned
- depends-on: multi:feature/phase-wave-digital-filters
- part-of: multi:feature/phase-physical-modeling

## design:primitive/resonators
- status: planned
- depends-on: multi:feature/phase-wave-digital-filters
- part-of: multi:feature/phase-physical-modeling

## design:feature/spring-models
- status: planned
- depends-on: multi:feature/phase-wave-digital-filters
- part-of: multi:feature/phase-physical-modeling

## design:feature/string-models
- status: planned
- depends-on: multi:feature/phase-wave-digital-filters
- part-of: multi:feature/phase-physical-modeling

## design:primitive/fir-convolution
- status: planned
- depends-on: multi:feature/phase-physical-modeling
- part-of: multi:feature/phase-convolution

## design:primitive/fft-convolution
- status: planned
- depends-on: multi:feature/phase-physical-modeling
- part-of: multi:feature/phase-convolution

## design:primitive/partitioned-convolution
- status: planned
- depends-on: multi:feature/phase-physical-modeling
- part-of: multi:feature/phase-convolution

## design:feature/cabinet-simulation
- status: planned
- depends-on: multi:feature/phase-physical-modeling
- part-of: multi:feature/phase-convolution

## design:feature/reverb-engines
- status: planned
- depends-on: multi:feature/phase-physical-modeling
- part-of: multi:feature/phase-convolution

## design:feature/tube-screamer
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware

## design:feature/rat-distortion
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware

## design:feature/big-muff
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware

## design:feature/fender-tone-stack
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware

## design:feature/neve-preamp
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware

## design:feature/tape-machine
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware

## design:feature/optical-compressor
- status: planned
- depends-on: multi:feature/phase-convolution
- part-of: multi:feature/phase-reference-hardware
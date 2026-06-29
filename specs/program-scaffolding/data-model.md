# Phase 1 Data Model: acfx Program Scaffolding

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

The "data" here is governed artifacts: a document, constitution principle entries, and
roadmap graph nodes/edges. No runtime data structures.

## Entities

### Prospectus document
- **Path**: `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
- **Content**: the prospectus text, verbatim.
- **References (inbound)**: constitution governance section; roadmap program node.

### Constitution principle (IX, X, XI)
Each is a `### ` section in `.specify/memory/constitution.md`, guidance tier, after VIII.
| Principle | Title | Core content |
|---|---|---|
| IX | Progressive Layered Architecture | `labs/ → primitives/ → effects/`; Theory→Lab→Primitive→Production; no disposable educational code; effects document composed primitives |
| X | Measurable Engineering | objective metric suite (freq/impulse/phase response, THD, latency, CPU, allocation, stability); listening complements not replaces |
| XI | One Concept at a Time | one major idea per phase/lab, applied to a complete effect; no black boxes |
- **Version**: bump `1.2.0 → 1.3.0` (MINOR). **Templates**: kept in sync per governance policy.

### Roadmap nodes (within the `<phase>:<kind>/<slug>` grammar)
| Node | Suggested id | Status | Edges |
|---|---|---|---|
| Program | `multi:feature/progressive-dsp-platform` | planned | (root) → spec/ref = prospectus |
| Phase 1 Digital Fundamentals | `multi:feature/phase-digital-fundamentals` | in-flight | `part-of` program |
| Phase 2 Nonlinear DSP | `multi:feature/phase-nonlinear-dsp` | planned | `part-of` program; `depends-on` Phase 1 |
| Phase 3 Dynamic Systems | `multi:feature/phase-dynamic-systems` | planned | `part-of` program; `depends-on` Phase 2 |
| Phase 4 Circuit Modeling | `multi:feature/phase-circuit-modeling` | planned | `part-of` program; `depends-on` Phase 3 |
| Phase 5 Numerical Circuit Solvers | `multi:feature/phase-numerical-solvers` | planned | `part-of` program; `depends-on` Phase 4 |
| Phase 6 Wave Digital Filters | `multi:feature/phase-wave-digital-filters` | planned | `part-of` program; `depends-on` Phase 5 |
| Phase 7 Physical Modeling | `multi:feature/phase-physical-modeling` | planned | `part-of` program; `depends-on` Phase 6 |
| Phase 8 Convolution | `multi:feature/phase-convolution` | planned | `part-of` program; `depends-on` Phase 7 |
| Phase 9 Reference Hardware Models | `multi:feature/phase-reference-hardware` | planned | `part-of` program; `depends-on` Phase 8 |

### Headline deliverables (items `part-of` their phase)
Recorded as `design:<kind>/<slug>` (unspecced, `planned`) — headline granularity only:
- **Phase 1**: measurement-infrastructure *(next-ready)*; plus the SHIPPED filters (SVF),
  delay+modulation (modulated-delay), parameter-system mapped here at real status.
- **Phase 2**: waveshapers, saturation, oversampling, harmonic-analysis.
- **Phase 3**: envelope-followers, compressors, program-dependent-saturation, tape-dynamics.
- **Phase 4**: component-abstractions, passive-tone-stacks, diode-clippers, opamp-stages.
- **Phase 5**: modified-nodal-analysis, newton-iteration, implicit-integration.
- **Phase 6**: wdf-primitives, adaptors, passive-networks, complete-analog-stages.
- **Phase 7**: digital-waveguides, resonators, spring-models, string-models.
- **Phase 8**: fir-convolution, fft-convolution, partitioned-convolution, cabinet-sim, reverb-engines.
- **Phase 9**: tube-screamer, rat, big-muff, fender-tone-stack, neve-preamp, tape-machine, optical-compressor.

### Cross-cutting item
- **Three-layer structure + primitive taxonomy** — `design:gap/three-layer-structure`,
  `part-of` Phase 1 (or the program), sequenced so Phase-2 implementation `depends-on` it.

### Shipped-work mapping
- `impl:feature/modulated-delay` (shipped) → `part-of` Phase 1.
- `design:feature/svf-vertical-slice` (closed), `design:feature/workbench-audio-config`
  (closed) → `part-of` Phase 1 (SVF filter + workbench/parameter work).

## Validation rules (mapped to requirements)
- Prospectus exists at its path and is referenced (FR-001/002) — file + link check.
- Constitution has IX–XI, version 1.3.0, templates in sync (FR-003/004) — read + version check.
- Program + 9 phases + deliverables + edges present (FR-005..008) — `roadmap graph`.
- Shipped work under Phase 1 at real status (FR-009) — graph inspection.
- Three-layer item present + sequenced (FR-010) — graph inspection.
- next-ready surfaces measurement-infrastructure (FR-011) — `roadmap next`.
- Idempotent (FR-012) — dry-run shows "would add"; re-apply no-ops/duplicates-free.
- No code / no labs/ / no reorg (FR-013) — diff inspection.
- This spec has a node (FR-014) — capture-fusion node for `program-scaffolding`.

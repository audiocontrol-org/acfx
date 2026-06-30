> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Three-Layer DSP Core Structure

**Feature Branch**: `three-layer-structure`

**Created**: 2026-06-29

**Status**: Draft

**Input**: Approved design record `docs/superpowers/specs/2026-06-29-three-layer-structure-design.md` (roadmap item `design:gap/three-layer-structure`, part-of `multi:feature/phase-digital-fundamentals`).

## User Scenarios & Testing *(mandatory)*

The "users" of this feature are the engineers who build DSP code in the acfx core:
the **downstream phase author** who needs a defined place to land a new concept, the
**reviewer** who upholds the layering, and the **maintainer** who must keep the core
real-time-safe and portable across every target. The feature delivers a *structure*
and one *worked example*, not a new effect.

### User Story 1 - The three-layer structure exists, proven by SVF end-to-end (Priority: P1)

A DSP developer opens `core/` and finds the three layers Constitution Principle IX
declares — `labs/`, `primitives/` (organized into a taxonomy), and `effects/` — sitting
on the existing `core/dsp/` substrate. The **State-Variable Filter** has been migrated
all the way through as the reference pattern: a `core/labs/state-variable-filter/`
laboratory (theory + walkthrough + host-only harness) whose kernel has *graduated* into
`core/primitives/filters/svf-primitive.h`, which `core/effects/svf/` composes. The
developer can read one real example of every stage and copy it.

**Why this priority**: This is the MVP. Without a real, populated three-layer structure
the gap is not closed — every downstream phase has nowhere defined to land. One concept
migrated end-to-end proves the structure rather than merely declaring it, and surfaces
the `#include`/build implications immediately.

**Independent Test**: Build the host test target and run the suite — it passes unchanged
(the SVF behaves identically after migration). `core/labs/state-variable-filter/` exists
with a README naming its graduation target and a host-only `harness/`. `core/effects/svf/`
includes the primitive from its new `primitives/filters/` path.

**Acceptance Scenarios**:

1. **Given** the repository after this story, **When** a developer lists `core/`, **Then** `labs/`, `primitives/`, `effects/`, and `dsp/` are all present and `core/primitives/filters/svf-primitive.h` exists.
2. **Given** the migrated SVF, **When** the host test suite runs, **Then** every existing SVF test (per-mode frequency response, high-resonance stability, no-allocation invariant) passes with no change in expected values.
3. **Given** `core/labs/state-variable-filter/`, **When** a developer reads its `README.md`, **Then** it states the theory/walkthrough and names `core/primitives/filters/svf-primitive.h` as the graduation target.
4. **Given** the lab harness, **When** it is built and run host-side, **Then** it produces per-mode frequency-response + stability evidence and is never part of any MCU cross-compile.

---

### User Story 2 - The remaining flat primitives are migrated into the taxonomy (Priority: P2)

The two other concepts that already exist flat in `core/primitives/` — the delay line and
the LFO — are moved into their taxonomy categories (`delays/` and `modulation/`), and the
`modulated-delay` effect that composes them is updated to the new include paths. No flat,
un-categorized primitive remains.

**Why this priority**: Leaving some primitives flat would let two conventions coexist
indefinitely — the "declared but not real" state this gap exists to end. It is P2 (not
P1) because the structure and its proof already exist after US1; this completes the
migration of pre-existing code.

**Independent Test**: After this story, `core/primitives/` contains no header directly in
its root; `delay-line.h` is under `delays/` and `lfo.h` under `modulation/`. The host test
suite and the `modulated-delay` effect build and pass with updated includes.

**Acceptance Scenarios**:

1. **Given** the repository after this story, **When** a developer lists `core/primitives/`, **Then** it contains only category subdirectories (e.g. `filters/`, `delays/`, `modulation/`) and no loose `.h` files.
2. **Given** the migrated delay line and LFO, **When** the host tests for the modulated-delay effect run, **Then** they pass with no change in expected behavior.
3. **Given** the taxonomy, **When** a developer opens the core taxonomy document, **Then** it records the full intended taxonomy (the prospectus categories `filters/ nonlinear/ dynamics/ analog/ circuit/ convolution/ wdf/ physical/`) plus the code-forced categories `delays/` and `modulation/`, and states that empty categories are documented, not pre-created.

---

### User Story 3 - The portability gate enforces the layering invariants (Priority: P3)

The existing `scripts/check-portability.sh` quality gate — run on purpose and in CI, never
as a git hook — is extended to mechanically guard the two load-bearing invariants: a lab's
host-only harness can never be `#include`d by portable core nor cross-compiled to an MCU,
and the dependency direction (`effects → primitives → dsp`, never the reverse) holds. The
gate also learns the new folder layout so its existing platform-independence and file-size
checks cover `core/labs/**` kernels.

**Why this priority**: The invariants protect exactly the project's core promises
(real-time safety, one-source-many-targets). Mechanical enforcement keeps them from
silently rotting as downstream phases add labs. It is P3 because US1/US2 deliver a
conformant tree first; the gate then locks it in.

**Independent Test**: Run `scripts/check-portability.sh` on the conformant tree — it exits
clean. Introduce a deliberate violation (a portable-core file including a `labs/*/harness/`
header, or a `primitives/` file including an `effects/` header) — the gate exits non-zero
and names the violation.

**Acceptance Scenarios**:

1. **Given** the conformant repository, **When** `scripts/check-portability.sh` runs, **Then** it exits 0 and reports the layering checks among its passes.
2. **Given** a portable-core source edited to `#include` a `labs/*/harness/` header, **When** the gate runs, **Then** it exits non-zero and identifies the harness-isolation violation.
3. **Given** a `core/primitives/` header edited to `#include` a `core/effects/` header, **When** the gate runs, **Then** it exits non-zero and identifies the dependency-direction violation.
4. **Given** the MCU cross-compile target sets, **When** they are configured, **Then** no `labs/*/harness/` source is included in any of them.

---

### Edge Cases

- **Pre-graduation vs graduated lab**: a lab whose kernel has not yet graduated keeps the kernel inside the lab folder; a graduated lab's kernel lives in `core/primitives/<category>/` and the lab retains only README + harness. Both shapes are valid and the gate must accept both.
- **Empty taxonomy categories**: categories named in the prospectus but with no inhabitant yet are documented, never materialized as empty `.gitkeep` directories — so listing `core/primitives/` shows only populated categories.
- **Harness leak**: a portable-core or kernel file that includes a harness header is a hard gate failure, not a warning.
- **MCU file-size / platform-independence on kernels**: a `core/labs/**` kernel that pulls in a platform header or exceeds the file-size budget is caught by the same existing checks, now extended over the new layout.
- **Include-path breakage**: any effect or test still referencing a pre-migration flat include path fails the build — all references must be updated in the same change as the move.
- **A category forced by code but absent from the prospectus list** (e.g. `delays/`, `modulation/`): recorded in the taxonomy doc as a first-class category, not treated as an error.

## Requirements *(mandatory)*

### Functional Requirements

#### Layer structure

- **FR-001**: The DSP core MUST contain a `core/labs/` layer as a sibling of `core/primitives/`, `core/effects/`, and `core/dsp/`.
- **FR-002**: `core/dsp/` MUST remain the shared substrate (contracts only — `Effect` concept, `ProcessContext`, `AudioBlock`, parameter model) beneath the three layers, and MUST NOT itself be counted as one of the three layers.
- **FR-003**: A laboratory MUST be a directory `core/labs/<concept>/` containing: a `README.md` (theory, implementation walkthrough, and the named graduation-target primitive path); a portable real-time-safe **kernel** header; and a `harness/` subdirectory for host-only tooling (plots, measurements, listening drivers).
- **FR-004**: A lab **kernel** MUST meet the same real-time-safety bar as a primitive — no heap allocation, locks, or unbounded work in any `process()` / audio-callback path (Constitution Principle VI).
- **FR-005**: A lab **harness** MUST be host-only: nothing in portable core (`dsp/`, `primitives/`, `effects/`, or any lab kernel) may `#include` a harness header, and harness sources MUST NOT appear in any microcontroller cross-compile set.
- **FR-006**: The lab lifecycle MUST support two states — *pre-graduation* (kernel inside the lab, harness drives it) and *graduated* (kernel relocated into `core/primitives/<category>/` and refined in place, never re-derived; the lab folder persists as README + harness now driving the graduated primitive). Educational code MUST evolve, never be discarded (Constitution Principle IX).

#### Primitive taxonomy & migration

- **FR-007**: `core/primitives/` MUST be organized into category subdirectories; primitives MUST NOT remain as loose headers in the `core/primitives/` root after this feature.
- **FR-008**: A taxonomy category folder MUST be created only when it has a real inhabitant; categories named but not yet inhabited MUST be recorded in a core taxonomy document rather than materialized as empty directories.
- **FR-009**: A core taxonomy document MUST enumerate the full intended taxonomy — the prospectus categories (`filters/`, `nonlinear/`, `dynamics/`, `analog/`, `circuit/`, `convolution/`, `wdf/`, `physical/`) plus categories forced by existing code (`delays/`, `modulation/`).
- **FR-010**: `core/primitives/svf-primitive.h` MUST move to `core/primitives/filters/svf-primitive.h`.
- **FR-011**: `core/primitives/delay-line.h` MUST move to `core/primitives/delays/delay-line.h` and `core/primitives/lfo.h` MUST move to `core/primitives/modulation/lfo.h`.
- **FR-012**: Every `#include` reference to a migrated primitive — in effects, tests, harnesses, and adapters — MUST be updated to the new path in the same change as the move; no dangling reference to a pre-migration path may remain.

#### Worked example (SVF)

- **FR-013**: `core/labs/state-variable-filter/` MUST be authored with a `README.md` whose walkthrough names `core/primitives/filters/svf-primitive.h` as the graduation target, and a host-only `harness/` that produces per-mode frequency-response and stability evidence (reusing what the existing host tests already verify).
- **FR-014**: The SVF primitive MUST be in its graduated location (`core/primitives/filters/`) and `core/effects/svf/` MUST compose it from that path.

#### Dependency direction

- **FR-015**: The dependency direction MUST be `effects/ → primitives/ → dsp/`; a lab **kernel** MUST depend only on `dsp/`; a lab **harness** (host-only) MAY consume primitives/kernels but nothing portable may depend on a harness. No reverse dependency is permitted at any level (a primitive never includes an effect; portable core never includes a harness).

#### Enforcement

- **FR-016**: `scripts/check-portability.sh` MUST be extended to mechanically verify lab-harness isolation (FR-005) and dependency direction (FR-015).
- **FR-017**: The portability gate MUST learn the new folder layout so its existing platform-independence and file-size-budget checks cover `core/labs/**` kernels.
- **FR-018**: The portability gate MUST remain an explicit, on-purpose step runnable locally and in CI, and MUST NOT be installed as a git hook (Constitution Principle II / Commandment 2).
- **FR-019**: The "each effect documents which primitives it uses" promise remains a reviewed convention in this feature (not mechanically gated); the machine-readable-manifest option is captured in the design record's open questions and is out of scope here.

#### Scope discipline

- **FR-020**: This feature MUST introduce no new DSP concept — SVF, the delay line, and the LFO all already exist. New concepts belong to `phase-digital-fundamentals` and later phases.
- **FR-021**: All source files touched or created MUST stay within the ~300–500-line budget (Constitution Principle VII).

### Key Entities

- **Layer**: one of `labs/`, `primitives/`, `effects/` within `core/`; `dsp/` is the substrate beneath them. Each layer has a defined allowed-dependency set.
- **Laboratory**: a `core/labs/<concept>/` unit — README + portable kernel + host-only harness — representing the Theory and Laboratory stages of a concept.
- **Kernel**: the portable, RT-safe algorithmic code inside a lab; the graduable artifact that relocates into a primitive.
- **Harness**: host-only tooling within a lab that plots/measures/auditions the kernel; never portable, never cross-compiled.
- **Primitive**: a reusable, tested component under `core/primitives/<category>/`; the Reusable Primitive stage.
- **Taxonomy category**: a subdirectory of `core/primitives/` grouping related primitives (e.g. `filters/`, `delays/`, `modulation/`).
- **Effect**: a production effect under `core/effects/<name>/` composed from primitives; the Production Effect stage.
- **Portability gate**: `scripts/check-portability.sh` — the explicit, CI-run quality gate enforcing the layering invariants.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After migration, 100% of the pre-existing host tests pass with no change to expected values — the structural change alters no DSP behavior.
- **SC-002**: A developer can point to one concept (SVF) that exists at every stage simultaneously — a lab (README + harness), a graduated primitive, and an effect that composes it — as a copyable end-to-end pattern.
- **SC-003**: `core/primitives/` contains zero loose (un-categorized) primitive headers after the feature.
- **SC-004**: The portability gate exits 0 on the conformant tree and exits non-zero — naming the violation — on each of the two deliberate violations (harness included by portable core; primitive including an effect).
- **SC-005**: No `labs/*/harness/` source appears in any microcontroller cross-compile set.
- **SC-006**: The repository contains zero empty taxonomy directories and zero numeric-prefixed names introduced by this feature (Commandment 3); uninhabited categories live only in the taxonomy document.
- **SC-007**: A reviewer can determine, from the taxonomy document alone, where any of the prospectus's planned concept families is intended to land.

## Assumptions

- The existing `scripts/check-portability.sh` is the single home for mechanical layering enforcement; no new gate script or framework is introduced.
- The repository convention of **descriptive spec-dir and path names without numeric prefixes** governs this feature (so the spec dir is `specs/three-layer-structure`, not a numbered variant), per Commandment 3 and the existing `specs/` layout.
- `delays/` and `modulation/` are accepted as first-class taxonomy categories even though the prospectus's example list did not name them (that list was illustrative, not exhaustive); their long-term boundaries are flagged in the design record's open questions for revisit at the second inhabitant.
- The SVF lab's harness reuses the measurement intent already encoded in the existing host tests rather than introducing a new measurement framework (the lab/harness output contract is a parked open question).
- The host build/test target is the primary acceptance surface; cross-compile targets (Daisy, Teensy) are exercised for the harness-exclusion invariant but flashing on physical hardware remains a separate checkpoint.
- The design record's open questions (machine-readable primitive manifest, lab/harness output contract, taxonomy category boundaries, shared visualization tooling, graduation provenance back-reference) are captured-but-parked and out of scope for this feature.

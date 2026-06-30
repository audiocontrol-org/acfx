---
title: Three-Layer DSP Core Structure — Design Record
date: 2026-06-29
roadmap-item: design:gap/three-layer-structure
status: awaiting-operator-approval
---

# Three-Layer DSP Core Structure

Establish the `labs/ -> primitives/ -> effects/` three-layer organization of the
DSP core declared by Constitution Principle IX and the progressive-DSP prospectus,
so that the downstream phases (`phase-nonlinear-dsp` and everything depending on
it) have a defined place to land and a proven graduation pattern to copy.

## Problem domain

Constitution **Principle IX (Progressive Layered Architecture)** and the program
prospectus declare the DSP core is organized into **three layers — `labs/ ->
primitives/ -> effects/`** — with every concept graduating through four stages:
**Theory -> Laboratory -> Reusable Primitive -> Production Effect**. Educational
code is explicitly *not* disposable: laboratory implementations evolve into
production primitives rather than being thrown away.

On disk today, that structure does not exist:

- `core/` contains only `dsp/` (the `Effect` concept, `ProcessContext`,
  `AudioBlock`, parameter model), `primitives/`, and `effects/`. **There is no
  `labs/` layer.**
- `core/primitives/` is **flat** — `svf-primitive.h`, `delay-line.h`, `lfo.h` sit
  directly in it, with no taxonomy. The prospectus names an intended taxonomy
  (`filters/ nonlinear/ dynamics/ analog/ circuit/ convolution/ wdf/ physical/`)
  that is nowhere on disk.
- There is **no defined contract** for how a concept moves between the four
  stages, nor any mechanical guard that the layering's load-bearing invariants
  (real-time safety, one-source-many-targets portability) hold as code is added.

Principle IX itself records that the physical `labs/` layer and primitive taxonomy
"do not yet exist on disk and are not assumed present" — naming this exact gap and
attributing its closure to `the three-layer-structure roadmap work`. This is a
**structural/convention gap**, not a single-effect feature: its job is to make the
declared architecture real and to prove it with one concept migrated end-to-end.

### Forces and constraints

- **Real-time safety (Principle VI):** no heap allocation, locks, or unbounded
  work in any `process()` / audio-callback path. Whatever lab code can graduate to
  a primitive must already meet this bar.
- **Platform-independent core, thin adapters (Principle IV):** the core compiles
  with no JUCE / libDaisy / Teensy knowledge; dependencies point only inward. A
  lab's educational tooling (plots, measurements, listening drivers) is inherently
  host-side and must never leak into the portable core or an MCU cross-compile.
- **Evolve, don't discard (Principle IX):** graduation must *relocate and refine*
  lab code, not duplicate-then-abandon it.
- **Explicit gates, never hooks (Commandment II / Principle II):** any enforcement
  extends the existing on-purpose `scripts/check-portability.sh` (already in CI),
  never a git hook.
- **Small modules, strict typing (Principle VII):** files within ~300-500 lines.
- **One concept at a time (Principle XI):** this gap establishes *structure*; it
  must not smuggle in new DSP concepts that belong to `phase-digital-fundamentals`
  proper.

## Solution space

Each major decision was explored against alternatives. The chosen positions
compose into the design in `## Decisions`; the rejected alternatives and their
reasons are recorded here.

### Chosen — Lab as a hybrid kernel + host-only harness (C)

A lab folder splits into a **portable, RT-safe kernel** (held to the same bar as a
primitive — the graduable code) and a **host-only harness** (plots, measurements,
listening drivers). Graduation extracts/moves the kernel into
`primitives/<category>/`; the harness stays behind as the lab's living
documentation, now driving the graduated primitive.

Honors "evolve, don't throw away" (the kernel relocates and refines in place) while
keeping the educational tooling where it belongs and isolatable by the gate.

### Rejected — Labs are host-only experiments (A)

`labs/` compiles host-side only, may allocate/plot freely, never cross-compiled;
graduation means rewriting an RT-safe kernel out of exploratory code. **Rejected:**
forces a rewrite at graduation (the kernel is re-derived, not evolved), which
contradicts Principle IX's "evolve, not throw away," and risks the graduated
primitive diverging from the experiment that taught it.

### Rejected — Labs are portable core at the same bar, no harness split (B)

`labs/` is just a less-polished primitive held to the full RT bar, with
visualization living in entirely separate host harnesses unassociated with the
lab. **Rejected:** loses the cohesion of "a lab is a teachable unit" — the theory,
kernel, and its measurements scatter; the prospectus's "each laboratory contains
theory + walkthrough + visualization + measurements + listening examples" is
weakened.

### Chosen — Establish taxonomy now and migrate existing flat primitives (A)

Create the category folders that have real inhabitants now, migrate the three
existing flat primitives into them, and migrate one concept (SVF) all the way
through as a worked example. The structure is *proven*, not merely declared, and
the `#include` / portability-gate implications surface immediately.

### Rejected — Declare structure, leave existing primitives flat (B)

New work lands in the taxonomy; the three existing headers stay flat until
naturally touched. **Rejected:** two conventions coexist indefinitely and the
structure is declared but unproven by any real graduation — exactly the "declared
but not real" state this gap exists to end.

### Rejected — Lazy/templated taxonomy, nothing created until needed (C)

Create only the contract/templates; no folders until the first primitive needs
them. **Rejected:** the shape lives only in docs until `phase-nonlinear-dsp`, so
this gap would close without producing a single on-disk example — it would defer
its own proof.

### Chosen — Extend the portability gate for the two load-bearing invariants (B)

Mechanically guard exactly **lab-harness isolation** and **dependency direction**
in `scripts/check-portability.sh`. These are the invariants whose violation breaks
the project's core promises (RT-safety, one-source-many-targets). The "documents
its primitives" promise stays a reviewed convention this round.

### Rejected — Documented convention only (A)

All structure/graduation rules live in prose; humans uphold them. **Rejected:**
drift in the RT-safety / portability invariants would be invisible until someone
notices — precisely the silent rot the existing portability gate exists to
prevent.

### Rejected — Full enforcement incl. machine-readable primitive manifest (C)

B plus a gated manifest declaring each effect's primitive dependencies.
**Rejected for this round (captured, not discarded — see `## Open questions`):**
heaviest option; risks fixing a manifest format before the taxonomy has enough
inhabitants to know its real shape. Parked, not dropped.

### Chosen — `core/labs/` location; SVF as the worked example

Labs live at `core/labs/` (the kernel is core; only the harness is host-only and
the gate isolates it). The existing **SVF** is migrated end-to-end as the real
migration proof, with its lab README + harness authored retroactively so the lab
layer has one genuine inhabitant.

### Rejected — top-level `labs/` and/or a greenfield one-pole example

A top-level `labs/` would signal "educational, not shipped," contradicting the
C-hybrid decision that the kernel *is* core. A brand-new one-pole-filter lab would
prove the greenfield path but introduces a new DSP concept that is
`phase-digital-fundamentals`' actual content, violating "one concept at a time"
for a *structural* gap. **Rejected** in favor of migrating code we already own.

## Decisions

1. **Three layers on the existing substrate.** Add `core/labs/` beside
   `core/primitives/`, `core/effects/`, and `core/dsp/`. `core/dsp/` remains the
   shared substrate (contracts only, no algorithms) **beneath** the three layers —
   it is not itself one of the three.

2. **Lab folder shape (C-hybrid):**
   ```
   core/labs/<concept>/
     README.md            theory + walkthrough + named graduation target
     <concept>-kernel.h   RT-safe, portable, same bar as a primitive
     harness/             host-only: plots, measurements, listening drivers
   ```

3. **Lab lifecycle has two states.** *Pre-graduation:* kernel lives in the lab,
   harness drives it. *Graduated:* kernel is `git mv`'d into
   `core/primitives/<category>/` and refined in place (never re-derived); the lab
   folder persists as README + harness, now driving the graduated primitive.

4. **Dependency direction (load-bearing).** `effects/ -> primitives/ -> dsp/`; a
   lab **kernel** depends on `dsp/` only; a lab **harness** is host-only and may
   consume primitives/kernels but **nothing portable may include a harness**.
   Never the reverse at any level (a primitive never includes an effect; portable
   core never includes a harness).

5. **Taxonomy: create-when-inhabited, document-the-rest.** Create category folders
   that have real inhabitants now; record the full intended taxonomy (the
   prospectus categories plus any forced by existing code) in a `core/README` /
   taxonomy doc rather than committing empty `.gitkeep` directories.

6. **Migrate the three existing flat primitives:**
   | today | -> taxonomy | consumed by |
   |---|---|---|
   | `core/primitives/svf-primitive.h` | `core/primitives/filters/svf-primitive.h` | `effects/svf/` |
   | `core/primitives/delay-line.h` | `core/primitives/delays/delay-line.h` | `effects/modulated-delay/` |
   | `core/primitives/lfo.h` | `core/primitives/modulation/lfo.h` | `effects/modulated-delay/` |

   `delays/` and `modulation/` are not in the prospectus's illustrative example
   list; they are forced by real code and recorded in the taxonomy doc alongside
   the prospectus categories. All `#include` paths in effects and tests update to
   match.

7. **Worked example: SVF, end-to-end.** Author
   `core/labs/state-variable-filter/` with a README (theory + walkthrough naming
   `primitives/filters/svf-primitive.h` as its graduation target) and a host-only
   `harness/` producing the per-mode frequency-response + stability evidence
   (reusing what the existing host tests already check); migrate the primitive into
   `primitives/filters/`; update `effects/svf/` to the new include path.

8. **Enforcement extends the portability gate.** Extend
   `scripts/check-portability.sh` (already in CI, never a hook) to mechanically
   guard: (a) **lab-harness isolation** — nothing under any `labs/*/harness/` is
   included by portable core, and harness sources never appear in an MCU
   cross-compile set; (b) **dependency direction** — `primitives/` must not include
   `effects/`, portable core must not include a harness. The gate also learns the
   new layout so its existing platform-independence / file-size checks cover
   `core/labs/**` kernels. "Each effect documents which primitives it uses" stays a
   reviewed convention this round.

9. **Scope discipline.** This gap establishes *structure* only. It introduces no
   new DSP concept (SVF, delay-line, and lfo already exist); new concepts belong to
   `phase-digital-fundamentals` and later phases.

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later
scoping pass, **not** discarded:

- **Machine-readable primitive manifest (the C enforcement option).** Should each
  effect declare its primitive dependencies in a gated, machine-readable form so
  the "documents its primitives" promise becomes enforced rather than conventional?
  Deferred until the taxonomy has enough inhabitants to know the manifest's real
  shape.
- **Lab/harness output contract.** Should a harness emit a standardized artifact
  (plot image? CSV of measurements? a doctest assertion set?) so labs are
  comparable and their measurements feed the measurable-engineering principle
  uniformly? What is the minimum a harness must produce to count as "a lab"?
- **Taxonomy category boundaries.** Are `delays/` and `modulation/` the right
  long-term categories, or should they fold into a broader scheme (e.g. `time/`)
  once a second delay/modulation primitive arrives? Settled enough to migrate now;
  flagged for revisit at the second inhabitant.
- **Shared visualization tooling.** Harnesses will want plotting/measurement
  helpers. Should there be a shared host-only `labs/harness-support/` library, and
  what plotting/measurement dependency does it pull in? Out of scope until a second
  lab exists to share with.
- **Graduation provenance.** Should a graduated primitive carry a back-reference to
  the lab it came from (a header comment? a doc link?) so the educational lineage
  stays discoverable from the production code?

## Provenance

- **Roadmap item:** `design:gap/three-layer-structure` (status `planned` ->
  `designing`), `part-of: multi:feature/phase-digital-fundamentals`. Design pointer
  set via `stackctl workflow link-design`.
- **Constitution:** Principle IX (Progressive Layered Architecture), with
  supporting Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII
  (Strict Typing & Small Modules), X (Measurable Engineering), XI (One Concept at a
  Time). `.specify/memory/constitution.md`.
- **Program vision:** `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`
  ("Project Architecture" — Laboratories / Primitives / Effects; the four-stage
  graduation model).
- **Current code surveyed:** `core/dsp/` (Effect concept, ProcessContext,
  AudioBlock, parameter model), `core/primitives/{svf-primitive,delay-line,lfo}.h`,
  `core/effects/{svf,modulated-delay}/`, `scripts/check-portability.sh`, `README.md`.
- **Design method:** `superpowers:brainstorming` driven in-session under the
  `/stack-control:design` frontend; house-rules block `stack-control-design-v1`
  injected (capture-over-YAGNI, >=2 solution-space alternatives, required sections,
  operator-approval marker, handoff to `/stack-control:define`).
- **Decisions driven by the operator** across five forks: lab relationship
  (C-hybrid), taxonomy + migration (A), enforcement line (B, portability-gate
  extension), `core/labs/` location, and SVF as the worked example.
- **Next step:** operator records the `design-approved:` marker on the roadmap
  node; on a met `design-to-spec` gate, hand off to `/stack-control:define` to
  author the Spec Kit spec.

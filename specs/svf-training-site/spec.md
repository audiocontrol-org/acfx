> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> **4. ALL UI/UX WORK GOES THROUGH `/frontend-design`** — no exceptions, no offroading; every
> user-facing visual/interaction decision routes through the frontend-design skill.
> **5. SCOPE IS THE OPERATOR'S CALL** — never cut/defer/drop scope on "YAGNI" or "simplicity";
> when scope is open, present options and ASK. The operator decides scope, not the agent.
> (acfx Constitution, Principles I–V — `.specify/memory/constitution.md`.)

# Feature Specification: SVF Training Site (Vertical Slice)

**Feature Branch**: `main` (unnumbered spec dir `specs/svf-training-site`, per Commandment III)

**Created**: 2026-07-14

**Status**: Draft

**Input**: Governed, operator-approved design record `docs/superpowers/specs/2026-07-14-svf-training-site-design.md` (design-to-spec gate 7/7). Roadmap node `design:feature/svf-training-site`, part-of `multi:feature/companion-training-site`.

## Overview

The first vertical slice of a **companion guided-training website** for acfx. It teaches, together, the **DSP domain** (what a State-Variable Filter is and does) and **how to build effects with acfx**, using the **real production code as the worked example** — never a lookalike. This slice ships ONE complete SVF lesson with a **six-part anatomy**: Concept → Hear it → Observe it → Play with it → Build it with acfx → Go deeper. It proves the entire site machinery (real-DSP-in-browser, generated assets, lesson model, publish pipeline) on one effect before any scale-out.

The design is settled (two third-party reviews + operator infra decisions); this spec translates it, it does not re-open it.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Learn the SVF by hearing and playing with the real filter (Priority: P1)

A learner opens the SVF lesson, reads what a state-variable filter is (its three simultaneous LP/BP/HP outputs, cutoff/resonance), hears it applied to a source at preset settings, then manipulates cutoff/resonance/mode controls and hears the audio change in real time — driven by the **actual production SVF DSP**, not a browser reimplementation.

**Why this priority**: This is the core value — an interactive, audible lesson whose executable figure IS the code being taught. Without it there is no training site.

**Independent Test**: Load the lesson, start audio on a user gesture, move the cutoff control, and confirm the audible output changes and matches the production DSP (verified by the parity test); "Hear it" presets play real generated clips.

**Acceptance Scenarios**:

1. **Given** the SVF lesson is open, **When** the learner starts audio and drags the cutoff control, **Then** the processed audio changes in real time and is produced by the real compiled SVF (not a reimplementation).
2. **Given** the lesson's "Hear it" section, **When** the learner plays a preset, **Then** they hear a clip generated offline from the real core, with its frequency-response plot.
3. **Given** a browser without AudioWorklet/WASM support, **When** the learner opens the demo, **Then** a content fallback (real pre-generated audio + static plot) renders — never a substitute DSP and never a broken widget.

---

### User Story 2 - See the filter, then build it the acfx way (Priority: P2)

A learner watches a **live visualization** ("Observe it") — frequency response, pole/zero positions, and impulse response updating as parameters change — and then follows a **build-along track** that replays the repository's actual `lab → primitive → effect → adapter` development history with verifiable checkpoints.

**Why this priority**: Visual intuition and the "replay the real engineering history" build-along are the distinctive pedagogy, but they build on US1's engine.

**Independent Test**: Change a parameter and confirm the visualization updates live from the real compiled analysis capability; walk each build-along checkpoint and confirm each corresponds to a real, verifiable repository state.

**Acceptance Scenarios**:

1. **Given** the "Observe it" artifact, **When** the learner changes resonance, **Then** the response curve, poles/zeros, and impulse response update live, computed by the real compiled target.
2. **Given** the "Build it with acfx" track, **When** the learner reaches a checkpoint, **Then** it names a real repository milestone (e.g. "primitive wraps DaisySP, host test green") they can independently verify.

---

### User Story 3 - Publish updated assets from local hardware without CI builds (Priority: P2)

A maintainer changes the core, rebuilds the WASM and generated assets **on local hardware**, publishes them to the **CDN**, and updates the committed manifest — and the site serves the new assets. **CI builds nothing.**

**Why this priority**: The operator's hard infrastructure constraint; the site cannot ship or update without this pipeline, but it is downstream of the learner-facing experience existing.

**Independent Test**: Run the local publish step, confirm assets land on the CDN with correct headers, confirm the committed manifest pins the new immutable URLs, and confirm no CI job compiled anything.

**Acceptance Scenarios**:

1. **Given** a core change, **When** the maintainer runs the local publish step, **Then** the WASM + generated assets are uploaded to the public bucket behind the CDN and the committed manifest pins their immutable content-hashed URLs.
2. **Given** the CDN-served assets, **When** the browser fetches the `.wasm` cross-origin, **Then** it is served with the correct CORS and content-type headers and loads successfully.
3. **Given** the core changed but assets were not republished, **When** the non-building staleness guard runs, **Then** it fails, naming the drift — without compiling anything.

---

### User Story 4 - "Go deeper" links stay synchronized with the repo (Priority: P3)

A learner clicks "Go deeper" and reaches the effect's **current** spec, plan, tasks, tests, implementation, and roadmap node — links generated from lesson metadata, not hand-maintained.

**Why this priority**: Keeps the lesson honest as the repo evolves, but is the least load-bearing part of the learner experience.

**Independent Test**: Build the site and confirm the resolved links point at the real current repo artifacts; introduce a stale metadata anchor and confirm the build fails rather than emitting a dead link.

**Acceptance Scenarios**:

1. **Given** valid lesson metadata, **When** the site builds, **Then** "Go deeper" links resolve to the effect's current repo artifacts.
2. **Given** a metadata anchor that no longer resolves, **When** the site builds, **Then** the build fails naming the unresolved anchor.

### Edge Cases

- Browser lacks AudioWorklet/WASM → content fallback (real pre-generated output), never a substitute DSP (US1-3).
- Autoplay policy → audio starts only on an explicit user gesture.
- CDN `.wasm` fetch/instantiate failure → visible descriptive error in the artifact card; no lookalike DSP.
- Two asset producers must not both write the manifest → a single assembler is the sole writer.
- Non-audio analysis needs (poles/zeros) are computed by the real compiled target, never re-derived in TypeScript.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The site MUST present ONE complete SVF lesson with the six-part anatomy: Concept, Hear it, Observe it, Play with it, Build it with acfx, Go deeper.
- **FR-002**: The interactive audio demo ("Play with it") MUST be driven by the **real** `core/effects/svf` DSP compiled to WebAssembly and run in an AudioWorklet — not a JavaScript/Web-Audio reimplementation.
- **FR-003**: The live visualization ("Observe it") MUST be computed by the **real compiled target** (impulse/sweep through the actual `process()`, or from the target's own coefficients) — never re-derived in TypeScript.
- **FR-004**: A new `adapters/web` target MUST compile the SVF audio target to WASM and MUST depend only inward on `acfx_core`; `core/` MUST NOT be modified and MUST gain no web/JS knowledge (Principle VI).
- **FR-005**: The WASM audio path MUST be proven equivalent to the production DSP by a **parity test** comparing a native reference and the WASM module against **shared, versioned input vectors** (no numeric constants copied into the test).
- **FR-006**: "Hear it" audio clips and static plots MUST be generated offline from the real core (no faked/mock data — Principle VII).
- **FR-007**: Interactive artifacts MUST be resolved through a **typed registry** (metadata-driven `kind → component`, discriminated union on `kind`), not ad-hoc component imports.
- **FR-008**: All lesson assets MUST be described by a single **manifest contract**; the WASM producer and the native host asset-tool each write only a fragment, and a **single assembler** is the sole writer of the committed manifest, which records kind, absolute CDN URL, content hash, capabilities/version, params, and source **provenance**.
- **FR-009**: "Go deeper" links MUST be generated at build time by a **doc auto-resolver** from typed lesson metadata; an unresolved anchor MUST fail the build.
- **FR-010**: Binaries MUST be built on **local hardware** and published to a **public Backblaze B2 bucket fronted by a Cloudflare Worker** read-through cache (worker in strict TypeScript, modeled on `oletizi/colony-cults infra/cloudflare-cdn`); the CDN MUST serve `.wasm` with correct CORS and `application/wasm` content-type at immutable content-hashed URLs. The committed manifest points at these URLs; binaries are NOT committed.
- **FR-011**: **CI MUST build nothing.** Every build/test (WASM, host asset-tool, native parity reference, site build, Playwright) runs as a local step; any CI usage is limited to non-building validation.
- **FR-012**: A **non-building staleness guard** MUST detect (by hash comparison, no compile) when `core/`/`adapters/web` changed but assets were not republished.
- **FR-013**: The slice MUST include **one Playwright** E2E smoke test covering AudioWorklet init, cross-origin CDN asset load, user-gesture audio startup, and visualization render.
- **FR-014**: `npm run build` MUST emit a self-contained, **Netlify-ready static bundle** referencing only the committed manifest + CDN URLs (no server runtime); the actual hosting/deploy pipeline is out of scope for this slice.
- **FR-015**: When the live engine cannot run, the artifact MUST render a **content fallback** (real pre-generated core output); a **DSP fallback** (substitute processor) is PROHIBITED (Principle VII).
- **FR-016**: All user-facing visual/interaction design MUST be produced via `/frontend-design` (Commandment IV).
- **FR-017**: All JavaScript-runtime code (`site/`, `adapters/web` glue + worklet, tooling, the Cloudflare worker) MUST be **TypeScript in strict mode** — no plain `.js`, no `any`, no `@ts-ignore` (Principle IX).
- **FR-018**: No secret (B2 key id/application key) MUST appear in the repository; the publish step reads credentials from the local gitignored file by path only.

### Key Entities

- **Lesson**: narrative that embeds 0..N interactive artifacts; has typed metadata (effect id, roadmap node, repo anchors).
- **Interactive Artifact**: an embeddable island backed by the real core (this slice: `svf-demo` audio, `svf-visualizer` visualization), resolved by kind through the registry.
- **Lesson-asset manifest**: the single committed contract listing every asset (kind, CDN URL, content hash, capabilities/version, params, provenance).
- **Executable target (`adapters/web`)**: the real DSP compiled to WASM, exporting declared capabilities (audio now; analysis in Phase 5).
- **Asset CDN**: public B2 bucket + Cloudflare Worker read-through cache serving the binaries.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A learner can move a filter control and hear the processed audio change with no perceptible lag (real-time, on a user gesture).
- **SC-002**: The interactive output matches the production DSP within a tight numeric tolerance (parity test: every sample within 1e-6 of the native reference on the shared vectors).
- **SC-003**: The lesson renders all **six** parts, and both interactive artifacts drive the real compiled DSP.
- **SC-004**: A maintainer can publish updated assets from local hardware and see them served live, with **zero** CI build steps, and the committed manifest pinning the new immutable URLs.
- **SC-005**: Every "Go deeper" link resolves to a current repository artifact; a stale anchor fails the build rather than shipping a dead link.
- **SC-006**: The one Playwright smoke test passes locally, exercising cross-origin CDN load, AudioWorklet init, user-gesture startup, and visualization render.

## Assumptions

- **Settled design**: `docs/superpowers/specs/2026-07-14-svf-training-site-design.md` is authoritative; this spec captures, not re-litigates, its decisions.
- **Toolchains on local hardware**: Emscripten SDK, Node ≥ 22, and `npx wrangler` are available locally (not in CI); the operator builds/publishes there.
- **Infra precedent**: the CDN follows `oletizi/colony-cults infra/cloudflare-cdn` (public bucket, read-through Worker, `npx wrangler deploy`).
- **Bucket**: `audiocontrol-acfx` (B2 `us-west-004`); public; credentials at `~/.config/backblaze/b2-audiocontrol-acfx-credentials.yaml` (gitignored).
- **Phasing**: implementation is sequenced into six dependency-ordered phases; Phase 1 (`adapters/web` WASM audio ABI + parity) already has a plan at `docs/superpowers/plans/2026-07-14-svf-web-adapter-parity.md`. The **analysis ABI** is sequenced to Phase 5 (visualizer-coupled) — surfaced per Commandment V, not a scope cut.
- **Deferred (operator-set slice boundary)**: lessons beyond the SVF, a multi-lesson navigation shell, adapters beyond `web`, and the actual hosting/deploy pipeline. Open items (exact CDN config, whether CI runs non-building checks, accounts/backend, test-runner choice) are surfaced for the operator, not cut.

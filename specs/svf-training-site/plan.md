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

# Implementation Plan: SVF Training Site (Vertical Slice)

**Branch**: `main` | **Date**: 2026-07-14 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/svf-training-site/spec.md`; governed design record `docs/superpowers/specs/2026-07-14-svf-training-site-design.md`; Phase-1 plan `docs/superpowers/plans/2026-07-14-svf-web-adapter-parity.md`.

## Summary

A companion guided-training website, first vertical slice: ONE complete SVF lesson (six-part anatomy) whose interactive figures run the **real** production DSP. The browser becomes a new thin adapter (`adapters/web`) compiling `core/effects/svf` to WebAssembly; an Astro + strict-TypeScript `site/` hosts the lesson with metadata-driven interactive artifacts; a native host asset-tool + the WASM producer feed a single-writer, provenance-bearing lesson-asset manifest; binaries are built on **local hardware** and served from a public B2 bucket behind a Cloudflare Worker (CI builds nothing). Delivered in six dependency-ordered phases.

## Technical Context

**Language/Version**: C++20 (core + `adapters/web` ABI, unchanged core); TypeScript 5.x **strict** (site, loaders, worklet, host-tooling glue, Cloudflare worker); Emscripten (emcc) for WASM.

**Primary Dependencies**: Emscripten SDK; DaisySP (existing CPM pin, wrapped by the SVF primitive); Astro; vitest (unit/parity, pending final choice); Playwright (one E2E smoke); `npx wrangler` (Cloudflare Worker); an S3/B2 upload client (`rclone` or B2 CLI — open, §research).

**Storage**: Static assets only. Binaries (`.wasm`, audio) on a **public Backblaze B2 bucket** (`audiocontrol-acfx`, us-west-004) behind a **Cloudflare Worker** read-through cache; the **committed** lesson-asset manifest (JSON) pins immutable content-hashed CDN URLs. No database.

**Testing**: native doctest (existing host suite unchanged); native-reference-vs-WASM **parity test** on shared versioned input vectors; `tsc --noEmit` + `astro build`; one **Playwright** E2E smoke. All run **locally** (CI builds nothing).

**Target Platform**: modern browsers with AudioWorklet + WASM (learner runtime); local dev/build hardware (macOS/Linux) for producers; Netlify (deploy target, pipeline deferred).

**Project Type**: web application layered on a platform-independent C++ core — a new inward-only adapter (`adapters/web`) + an Astro site (`site/`) + build tooling (`tools/`, `infra/`).

**Performance Goals**: real-time audio in the browser (allocation-free `process()` on the audio thread — Principle VIII holds in WASM); perceptually-immediate control response; CDN cache HIT never touches B2.

**Constraints**: core untouched (Principle VI); no faked/mock DSP (Principle VII); TypeScript strict, no `any`/`@ts-ignore` (Principle IX); all UI via `/frontend-design` (Commandment IV); scope is the operator's (Commandment V); **CI builds nothing**; no secrets in the repo (creds read from a gitignored local file).

**Scale/Scope**: ONE SVF lesson, TWO interactive artifacts (`svf-demo`, `svf-visualizer`), ONE effect target compiled to WASM. Six implementation phases.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **I — Commit/push early & often**: PASS — small atomic commits per task; proactive push.
- **II — No git hooks**: PASS — all gates are explicit local `make`/npm steps; none added.
- **III — Descriptive names**: PASS — `svf-training-site`, `adapters/web`, `svf-demo`; no numeric prefixes.
- **IV — All UI via /frontend-design**: PASS (binding) — every visual/interaction surface (lesson layout, both artifacts) is produced through `/frontend-design`; no hand-rolled UI.
- **V — Scope is the operator's**: PASS — six phases sequence (not cut) the settled scope; open items surfaced (research), never YAGNI-trimmed. Analysis ABI is re-sequenced to Phase 5, surfaced.
- **VI — Platform-independent core, thin adapters**: PASS — `adapters/web` depends only inward on `acfx_core`; `core/` unchanged; site depends only on the browser ABI + manifest, never core internals.
- **VII — No fallbacks/mock outside tests**: PASS — assets generated from the real core; content-fallback (real precomputed output) allowed, DSP-fallback prohibited.
- **VIII — Real-time safety**: PASS — WASM `process()` reuses a heap buffer, no per-block allocation.
- **IX — Strict typing (TS mandate)**: PASS (binding) — all JS-runtime code TypeScript strict.
- **X — Test core host-side**: PASS — existing doctest suite unchanged; the WASM path is proven equivalent by the parity test rather than re-testing the DSP.
- **XI — Measurable engineering**: PASS — parity tolerance (1e-6), staleness guard, build-fail gates are objective.

No violations → Complexity Tracking below is empty.

## Project Structure

### Documentation (this feature)

```text
specs/svf-training-site/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (ABI, manifest, registry, metadata, worker)
├── checklists/          # requirements.md (from /speckit-specify)
└── tasks.md             # /speckit-tasks output
```

### Source Code (repository root)

```text
adapters/web/                     # NEW inward-only adapter (Phase 1, +analysis Phase 5)
├── svf-web-abi.{h,cpp}           # extern-C audio ABI over acfx::SvfEffect
├── svf-reference-main.cpp        # native reference CLI (parity oracle)
├── loader/svf-module.ts          # strict-TS Emscripten module loader
├── worklet/                      # AudioWorklet processor (Phase 5)
├── test/                         # parity test + vectors
└── CMakeLists.txt                # EMSCRIPTEN branch (WASM) vs host branch (reference)

tools/lesson-assets/              # NEW native host asset-tool (Phase 2)
└── ...                           # sweeps the real core → audio/response/pole-zero/impulse JSON + fragment

infra/cloudflare-cdn/             # NEW asset CDN (Phase 3, colony-cults model)
├── worker.ts                     # strict-TS read-through cache worker
└── wrangler.toml

site/                             # NEW Astro site (Phase 4–6)
├── src/content/svf/              # lesson.mdx + lesson.meta.ts
├── src/components/artifacts/     # SvfDemo, SvfVisualizer (Phase 5, via /frontend-design)
├── src/lib/                      # artifacts/registry.ts, lesson-assets/manifest.ts, repo-refs/resolver.ts
├── public/manifest/svf.json      # committed asset manifest (binaries live on the CDN)
├── astro.config.mjs, tsconfig.json, package.json, netlify.toml, .nvmrc

core/                             # UNCHANGED (Principle VI)
```

**Structure Decision**: extend the existing `adapters/` fleet with `web` (inward-only, like `daisy`/`teensy`/`workbench`/`plugin`); add three new top-level subtrees (`tools/`, `infra/`, `site/`) that consume the core's outputs but never its internals. `core/` is untouched.

## Complexity Tracking

No constitution violations — none to justify.

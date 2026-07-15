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

# Tasks: SVF Training Site (Vertical Slice)

**Spec**: [spec.md](./spec.md) · **Plan**: [plan.md](./plan.md) · **Design**: `docs/superpowers/specs/2026-07-14-svf-training-site-design.md`

**Organization**: by the six dependency-ordered implementation phases from plan.md (operator-directed, Commandment V). Every task carries `[tier:fast|balanced|powerful]` (this installation's `tier_map`: fast=haiku, balanced=sonnet, powerful=opus), `[P]` when parallelizable, and `[US n]` when it serves a specific user story.

**Global task constraints** (apply to every task): CI builds nothing (all local `make`/`npm`); TypeScript strict for all JS-runtime code; `core/` untouched; no faked/mock DSP; no secrets committed; parity within 1e-6; all UI via `/frontend-design`.

---

## Phase 1 — `adapters/web` WASM audio ABI + parity (Foundational; serves US1, US2)

> A complete bite-sized TDD plan exists at `docs/superpowers/plans/2026-07-14-svf-web-adapter-parity.md` (7 tasks). Execute it there; the tasks below are its checkpoints, not a duplicate.

- [x] T001 [tier:balanced] Build-surface plumbing: `ACFX_BUILD_WEB` option + `web`/`web-ref` presets + `.gitignore` (`CMakeLists.txt`, `CMakePresets.json`, `adapters/web/CMakeLists.txt`) — per plan Task 1.
- [x] T002 [US1] [tier:balanced] extern-C audio ABI over `acfx::SvfEffect`, native doctest parity (`adapters/web/svf-web-abi.{h,cpp}`, `adapters/web/test/svf-web-abi-native-test.cpp`) — per plan Task 2.
- [x] T003 [US1] [tier:balanced] Emscripten WASM build `svf.mjs`+`svf.wasm` (`adapters/web/CMakeLists.txt` EMSCRIPTEN branch) — per plan Task 3.
- [x] T004 [P] [US1] [tier:balanced] Strict-TS Emscripten loader + toolchain (`adapters/web/loader/svf-module.ts`, `package.json`, `tsconfig.json`, `vitest.config.ts`) — per plan Task 4.
- [x] T005 [P] [US1] [tier:fast] Versioned input-vector fixtures + native reference CLI (`adapters/web/test/vectors/lowpass-sweep.json`, `adapters/web/svf-reference-main.cpp`) — per plan Task 5.
- [x] T006 [US1] [tier:balanced] Parity test WASM vs native reference, ≤1e-6 (`adapters/web/test/svf-parity.test.ts`) — per plan Task 6.
- [x] T007 [tier:fast] Local `make` targets (`web-ref`/`web-wasm`/`web-parity`) + `adapters/web/README.md` (`Makefile`) — per plan Task 7.

**Checkpoint (SC-002)**: `make web-parity` green — WASM output equals native reference within 1e-6.

---

## Phase 2 — Host asset-tool + lesson-asset manifest (Foundational; serves US1 Hear-it, US3)

- [x] T008 [P] [tier:balanced] Native host asset-tool scaffold: links `acfx_core`, CLI, CMake target (`tools/lesson-assets/CMakeLists.txt`, `tools/lesson-assets/asset-tool-main.cpp`).
- [x] T009 [US1] [tier:balanced] Asset-tool sweeps the real SVF → audio clips + frequency-response/pole-zero/impulse JSON, each content-hashed (`tools/lesson-assets/`). No faked data (Principle VII).
- [x] T010 [US1] [tier:balanced] Producer B emits `static.fragment.json` with provenance (source hash) (`tools/lesson-assets/`).
- [x] T011 [tier:balanced] Producer A (Emscripten) emits `wasm.fragment.json` (extend `adapters/web` build to write the fragment).
- [x] T012 [tier:balanced] Manifest **assembler** (sole writer): inventories + validates fragments → committed `site/public/manifest/svf.json` per `contracts/lesson-asset-manifest.md` (`tools/manifest-assembler/` in strict TS).
- [x] T013 [P] [US3] [tier:fast] Non-building **staleness guard**: hash-compare manifest `sourceProvenance` vs current `core/`+`adapters/web` source; local `make staleness-guard` (`tools/staleness-guard.ts`).
- [x] T014 [tier:fast] `make lesson-assets` target wiring both producers + assembler (`Makefile`).

**Checkpoint (FR-006, FR-008)**: assets generated from the real core; a single valid manifest is produced by one writer; staleness guard flags source drift without compiling.

---

## Phase 3 — Asset CDN: public B2 + Cloudflare Worker (serves US3)

> Modeled on `oletizi/colony-cults infra/cloudflare-cdn`; contract in `contracts/cdn-worker.md`.

- [x] T015 [P] [US3] [tier:balanced] `infra/cloudflare-cdn/worker.ts` (strict TS): read-through cache, 2xx-only, `ACAO: *`, immutable `Cache-Control`, GET/HEAD, no image-resize branch.
- [x] T016 [P] [US3] [tier:fast] `infra/cloudflare-cdn/wrangler.toml` (`B2_DOWNLOAD_BASE`, `EDGE_TTL_SECONDS`) + README.
- [x] T017 [US3] [tier:balanced] Local publish step `make publish-assets`: S3-API upload of content-hashed objects to the public `audiocontrol-acfx` bucket, `Content-Type: application/wasm` on `.wasm`, creds read from the gitignored file by path — no secret committed (FR-018) (`tools/publish-assets.ts`).
- [x] T018 [US3] [tier:fast] Deploy + verify: `npx wrangler deploy`; `curl -D -` confirms `ACAO`, `application/wasm`, MISS→HIT (record in `infra/cloudflare-cdn/README.md`).
- [x] T019 [tier:balanced] Assembler writes absolute `CDN_BASE` URLs into the manifest (wire `CDN_BASE` through T012).

**Checkpoint (FR-010, SC-004)**: assets served from the CDN with correct headers; committed manifest pins immutable content-hashed CDN URLs; no CI build ran.

---

## Phase 4 — Site core: Astro + strict TS (Foundational for US1, US2, US4)

- [x] T020 [tier:balanced] `site/` Astro scaffold, strict `tsconfig.json`, `package.json`, `astro.config.mjs`, `.nvmrc` (Node ≥ 22), `.gitignore` (`site/`).
- [x] T021 [P] [tier:fast] Manifest reader `site/src/lib/lesson-assets/manifest.ts` (typed reader for `svf.json` per `contracts/lesson-asset-manifest.md`).
- [x] T022 [P] [tier:balanced] Typed artifact **registry** `site/src/lib/artifacts/registry.ts` (discriminated union `ArtifactKind`, `<Artifact kind=.../>`) per `contracts/artifact-registry.md`.
- [x] T023 [US4] [tier:powerful] Doc auto-resolver `site/src/lib/repo-refs/resolver.ts`: `LessonMeta.repoAnchors` → spec/plan/tasks/tests/impl/roadmap links at build time; unresolved anchor FAILS the build; no C++ semantics parsed (FR-009).
- [x] T024 [P] [US4] [tier:fast] `site/src/content/svf/lesson.meta.ts` typed metadata (effect id, roadmap node, repo anchors).

**Checkpoint (FR-007, FR-009, FR-017)**: `tsc --noEmit` + `astro build` clean; registry resolves kinds; resolver produces Go-deeper links; a stale anchor fails the build.

---

## Phase 5 — Interactive artifacts + analysis ABI + lesson (serves US1, US2, US4)

> ALL visual/UI work MUST be produced via `/frontend-design` (Commandment IV) — an explicit step below.

- [x] T025 [US2] [tier:powerful] Analysis capability C ABI in `adapters/web` (`getFrequencyResponse/getPoleZeroData/renderImpulseResponse`) computed by the real compiled target; extend loader + parity coverage per `contracts/web-abi.md`. **No TS re-derivation** (FR-003).
- [x] T026 [US1] [tier:balanced] AudioWorklet processor (strict TS) wrapping the audio ABI; heap buffer reused (allocation-free) (`adapters/web/worklet/`).
- [x] T027 [US1] [tier:powerful] `/frontend-design` for the SvfDemo artifact (audio: cutoff/resonance/mode controls, transport, response curve) — produce the visual/interaction design via the skill.
- [x] T028 [US1] [tier:balanced] Implement `site/src/components/artifacts/SvfDemo/` from the frontend-design output; drives the real WASM audio path; content-fallback on no-WASM, DSP-fallback prohibited (FR-002, FR-015).
- [x] T029 [US2] [tier:powerful] `/frontend-design` for the SvfVisualizer artifact (live response curve, pole/zero plot, impulse) — produce the visual/interaction design via the skill.
- [x] T030 [US2] [tier:balanced] Implement `site/src/components/artifacts/SvfVisualizer/` from the frontend-design output; driven by the analysis ABI (FR-003).
- [x] T031 [tier:powerful] `/frontend-design` for the overall six-part lesson layout/typography.
- [x] T032 [US1] [tier:balanced] Author `site/src/content/svf/lesson.mdx` — six parts (Concept, Hear it, Observe it, Play with it, Build it, Go deeper); embed `<Artifact>` declarations; Build-it checkpoints replay real repo milestones (FR-001).

**Checkpoint (SC-001, SC-003)**: lesson renders all six parts; both artifacts drive the real compiled DSP; audio changes in real time on a user gesture.

---

## Phase 6 — E2E smoke + deploy contract (Polish; serves US3, all)

- [x] T033 [P] [tier:balanced] One **Playwright** E2E smoke test: AudioWorklet init, cross-origin CDN asset load, user-gesture audio start, visualizer render (`site/tests/e2e/svf-lesson.spec.ts`) (FR-013, SC-006).
- [x] T034 [P] [tier:fast] `site/netlify.toml` (build `npm ci && npm run build`, publish `site/dist`, `NODE_VERSION` pinned to `.nvmrc`, `CDN_BASE` env) + verify **static-build contract**: `site/dist` self-contained, references only committed manifest + CDN URLs (FR-014).
- [x] T035 [tier:fast] Root `Makefile`/docs roll-up of all local targets; confirm **no CI builds anything** (FR-011); final `quickstart.md` walkthrough passes.

**Checkpoint (FR-011, FR-013, FR-014, SC-006)**: Playwright smoke green locally; static bundle deployable to Netlify; zero CI build steps.

---

## Dependencies & story completion order

- **Phase 1 → Phase 2 → Phase 3**: the WASM producer feeds fragments; the manifest needs CDN URLs.
- **Phase 4** depends on the manifest contract (Phase 2) shape but can scaffold in parallel with Phase 3.
- **Phase 5** depends on Phases 1 (audio), 5-T025 (analysis), 3 (CDN-served assets), 4 (site core).
- **Phase 6** depends on Phase 5.
- **User stories**: US1 (P1) spans Phases 1,2,5; US2 (P2) spans Phases 1,5; US3 (P2) spans Phases 2,3,6; US4 (P3) spans Phase 4.

## MVP scope

**US1 (P1)** — a learner hears and plays with the real SVF — is the MVP: Phase 1 (T001–T007) + the audio slice of Phases 2/4/5 (assets, site scaffold, SvfDemo, lesson). Delivers the core "executable figure IS the code" value; US2/US3/US4 layer on.

## Parallel opportunities

- Within Phase 1: T004 (loader) ∥ T005 (fixtures/reference).
- Phase 3: T015 (worker) ∥ T016 (wrangler config).
- Phase 4: T021 (manifest reader) ∥ T022 (registry) ∥ T024 (metadata).
- Phase 6: T033 (Playwright) ∥ T034 (netlify).

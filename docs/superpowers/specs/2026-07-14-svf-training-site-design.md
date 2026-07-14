# SVF Training Site — Design (Vertical Slice)

**Date**: 2026-07-14
**Status**: Design approved (brainstorming) — pending spec review → implementation plan
**Scope**: ONE vertical slice — a single, complete SVF lesson proving the whole
companion-training-site machinery before any scale-out.

## 1. Purpose

A companion **guided-training website** for the acfx project. It teaches, together:

1. **The DSP domain** — the audio/analog-circuit concept behind each effect.
2. **Using acfx** — how to build/extend that effect with this platform.

The learner comes away understanding *what* a State-Variable Filter is **and** able
to build one the acfx way. The repository's real code is the worked example — never
a lookalike.

This design covers only the **first vertical slice**: the SVF lesson, end-to-end.
It deliberately mirrors how the repo itself began (the `svf-vertical-slice` proved
the whole platform spine with one effect before scaling out). Additional lessons are
out of scope here and will each get their own spec once this slice validates the
format.

**Mental model — an executable engineering notebook.** The end state this site is
building toward is not "a docs site with a demo bolted on"; it is an executable
engineering notebook whose figures are *live*. Each lesson is
explanation → experiment → visualization → implementation → references, and every
interactive element is an *executable figure* backed by the production library. The
SVF slice is the first page of that notebook. This framing informs the boundaries
chosen below (assets, artifacts, targets) but does **not** expand this slice's scope
beyond the single SVF lesson — the notebook is the direction, not this deliverable.

## 2. Governing constraints (constitution)

This work is bound by the acfx constitution (v1.6.0), notably:

- **Commandment IV — All UI/UX through `/frontend-design`.** Every visual/interaction
  decision on this site (lesson layout, the interactive artifacts, typography, color,
  spacing) MUST be produced via the `frontend-design` plugin skill. This design fixes
  *what* is built and *how it is structured*; `/frontend-design` owns *how it looks* at
  implementation time.
- **Commandment V — Scope Is the Operator's Call.** Every scope boundary in this
  document is the operator's decision, not an agent-side cut. Nothing here is trimmed on
  "YAGNI"/"simplicity" grounds; where a boundary exists it was chosen by the operator
  (§10), and genuinely open items are surfaced for decision rather than silently dropped.
- **Principle IX — Strict Typing.** All JavaScript-runtime code (`site/`, `adapters/web`
  glue + AudioWorklet, build tooling) MUST be TypeScript in `strict` mode: no plain
  `.js`, no `any`, no `@ts-ignore`, no unchecked casts. Type errors are build failures.
- **Principle VI — Platform-Independent Core, Thin Adapters.** The browser is treated as
  *just another target*. `adapters/web` is a thin shell; `core/` is not modified and
  gains no web/JS knowledge. Dependencies point only inward.
- **Principle VII — No Fallbacks / Mock Data Outside Tests.** Audio clips and response
  plots are generated from the *real* core. A WASM load failure surfaces a descriptive
  error; it never silently substitutes fake DSP.
- **Principle XI — Progressive Layered Architecture.** The repo's own
  Theory → Laboratory → Reusable Primitive → Production Effect ladder (the SVF has a
  `core/labs/state-variable-filter/` harness) is the backbone of the build-along track.

## 3. Learner experience — lesson anatomy

A lesson is **narrative that embeds one or more *interactive artifacts*** (§4.2). An
interactive artifact is a self-contained, embeddable island backed by the real core
(the live demo, the live visualizer), resolved through a **typed artifact registry**
(§4.2). This slice ships two artifacts and builds the registry now, so future lessons
can declare more (spectrum analyzer, oscilloscope, code sandbox, exercise) by type
without reworking the lesson model.

The SVF lesson is the reusable **six-part** skeleton every future lesson will follow:

1. **Concept** — what an SVF is: the three simultaneous outputs (LP/BP/HP), the
   cutoff/resonance controls, why it is the "hello world" of analog-style filters.
   Prose + a signal-flow diagram + the transfer-function math.
2. **Hear it** — embedded audio: a source (drums/synth) run through the filter at a few
   preset settings, plus a static frequency-response plot. Assets generated offline from
   the real core (§4.4).
3. **Observe it** — a **live, interactive visualization artifact** driven by the WASM
   module: as parameters change, the learner watches the filter's response curve, its
   pole/zero movement, and its impulse response update in real time. Visual intuition
   *before* audio manipulation — for many concepts the picture teaches more than the
   sound. Shares the WASM engine with "Play with it" (§4.1) but renders visuals, not
   audio out.
4. **Play with it** — the live AudioWorklet demo artifact: sliders for
   cutoff / resonance / mode driving the WASM-compiled SVF target, audible output, with
   the response curve tracking the controls as the learner drags.
5. **Build it with acfx** — a build-along track framed as **replaying the repository's
   actual development history**. The learner walks the same
   Laboratory → Reusable Primitive → Production Effect → Adapter ladder the SVF really
   traveled (`core/labs/state-variable-filter/` → `core/primitives/filters/svf-primitive.h`
   → `core/effects/svf/svf-effect.h` → an adapter). Checkpoints are **repository
   milestones**, not invented tutorial steps: each is a real, verifiable state the
   project passed through (e.g. "primitive wraps DaisySP, host test green"). This is the
   most novel part of the design — learners replay engineering history, they do not read
   a manufactured tutorial.
6. **Go deeper** — the real spec / quickstart / tests / roadmap node for this effect,
   surfaced from lesson **metadata** (§4.5) rather than hand-maintained links, plus a
   pointer to what comes next.

## 4. Architecture

### 4.1 `adapters/web` — the browser adapter (the DSP engine)

A new thin adapter alongside `workbench, plugin, daisy, teensy`.

- Compiles an **executable audio-processing target** to a **WebAssembly** module via
  **Emscripten**. For this slice the target is `core/effects/svf` (which pulls
  `core/primitives/filters/svf-primitive.h` → `daisysp::Svf`, the same CPM-pinned
  DaisySP). "Target," not "`SvfEffect`," is deliberate: the adapter is not hardwired to
  one effect, so a later lesson can compile a *primitive* or a *lab harness* through the
  same path.
- A target **exports one or more declared capabilities** (not "every target has one
  fixed API"). The manifest (§4.4) records which capabilities a target supports and their
  version. This slice's SVF target exports two capability groups:
  - **Audio capability (C ABI):**
    `create / prepare(sampleRate, blockSize) / setParam(id, norm) / process(ptr, numSamples)`.
    Drives "Play with it." A TypeScript **AudioWorklet processor** wraps it and runs
    `process()` on the audio thread; the WASM heap buffer is reused per block (no
    per-block allocation — Principle VIII holds in the browser too).
  - **Analysis capability (C ABI) — review #1:** `getFrequencyResponse / getPoleZeroData /
    renderImpulseResponse`, feeding "Observe it." **These are computed by the real
    compiled target** — impulse/sweep run through the actual `process()`, or derived from
    the target's own internal coefficients — **never re-derived in TypeScript** (a TS
    reformulation would be the lookalike Principle VII forbids). SVF-specific for this
    slice; generalized only when a second target needs it.
- **Non-audio target kinds — decision (provisional, reversible):** define the
  capability/version envelope in the manifest (above), but add **no** speculative
  non-audio target kind now. Rationale is *technical*, not YAGNI (Commandment V): a
  measurement/benchmark target is not `process()`-shaped and its ABI cannot be designed
  until a lesson defines its real shape. The envelope makes adding one later additive.
- Output artifacts: `svf.wasm` + a TypeScript glue/worklet module, **published to the
  external asset CDN** and referenced by the manifest (§4.4).

### 4.2 `site/` — the Astro training site

First Node/TypeScript subtree in the repo. Astro chosen for a content-first site with
isolated islands of interactivity (ships zero JS except the interactive artifact
islands a lesson embeds).

```
site/
  src/content/svf/
    lesson.mdx                     # the six-part SVF lesson (prose / math / diagram)
    lesson.meta.ts                 # typed lesson metadata (§4.5): effect id, repo paths, roadmap node
  src/lib/
    artifacts/registry.ts          # typed interactive-artifact registry (§4.2): kind → component
    lesson-assets/manifest.ts      # typed reader for the lesson-asset manifest/contract (§4.4)
    repo-refs/resolver.ts          # doc auto-resolver (§4.5): spec/plan/tasks/tests/roadmap from metadata
  src/components/artifacts/        # embeddable interactive-artifact islands (registered by kind)
    SvfDemo/                       # audio artifact:        worklet + WASM + controls + audible out
    SvfVisualizer/                 # visualization artifact: live response curve / poles-zeros / impulse
  public/manifest/svf.json         # lesson-asset manifest (assembled, §4.4); binaries live on the CDN
  astro.config.mjs
  tsconfig.json                    # strict: true
  package.json
```

- Lessons authored in **MDX** (prose + embedded components + math).
- **Interactive artifacts are metadata-driven, resolved through a typed registry
  (review #4).** Lessons declare artifacts by kind — `<Artifact kind="svf-visualizer"
  lesson="svf" />` — not by importing components ad hoc; the registry earns its place
  precisely because declaration is data, not hardcoded JSX. The shape is pinned and
  strictly typed via a **discriminated union on `kind`** (no `unknown` at consumption):
  ```ts
  type ArtifactKind = "svf-demo" | "svf-visualizer";
  interface ArtifactBase<K extends ArtifactKind, P> {
    kind: K; assetBundle: string; props: Readonly<P>;
  }
  type ArtifactDeclaration =
    | ArtifactBase<"svf-demo", SvfDemoProps>
    | ArtifactBase<"svf-visualizer", SvfVisualizerProps>;
  ```
  This slice registers and ships two kinds; future kinds (`spectrum`, `oscilloscope`,
  `sandbox`, `exercise`) extend the union.
- **Hard dependency invariant (Principle VI) — clarified (review #2).** Two different
  kinds of dependency, and only one is allowed:
  - **Runtime / build dependency on DSP internals — PROHIBITED.** The site runtime and
    interactive components depend *only* on the exported browser ABI (§4.1) and the
    lesson-asset manifest (§4.4). No core headers, no core source, no coupling to an
    effect's internals. The arrow is `core → adapters/web → site`, never backward.
  - **Build-time repository-reference indexing — PERMITTED.** The doc auto-resolver
    (§4.5) may inspect repository paths (`specs/…`, `core/…`, `ROADMAP.md`) as
    *documentation metadata* to produce links, but must **not import, parse, or depend on
    C++ implementation semantics**. It reads the repo as a filesystem index, not as a
    program.
- The visual design of both artifacts — and the overall lesson layout/typography — is
  produced via `/frontend-design` (Commandment IV).
- All `.ts` / `.astro` / component / config source is TypeScript strict (Principle IX).

### 4.3 Build wiring — two stages, deliberately split

Binaries are **built on local hardware and published to the CDN**, never in CI
(Emscripten in CI is slow; B2↔Cloudflare has free egress). CI and Netlify only ever
consume the committed manifest. Two stages:

**Stage 1 — asset publish (local, operator-run: `make publish-assets`):**
1. **Emscripten** build `adapters/web` → `svf.<hash>.wasm` + TypeScript worklet.
2. **Host asset-tool** build + run → audio clips + response/pole-zero/impulse JSON, each
   content-hashed.
3. Each producer writes a **fragment** (`wasm.fragment.json`, `static.fragment.json`)
   recording its objects, content hashes, and **source provenance** (the core/adapter
   source hash it was built from).
4. Upload objects to **Backblaze B2**; they serve behind **Cloudflare** (immutable,
   content-hashed URLs).
5. A single **manifest assembler** (review #3) inventories the fragments, validates them,
   and writes the authoritative `site/public/manifest/svf.json` with absolute Cloudflare
   URLs + provenance. **The manifest (small JSON) is committed to git; the binaries are
   not.**

**Stage 2 — site build (local for dev; Netlify for deploy — never CI):**
- Astro builds the static site, reading the committed manifest; at runtime the browser
  fetches `.wasm` / audio from Cloudflare (cross-origin — §4.4 CORS).

**CI builds nothing (operator directive).** Every compile/build/test in this design runs
on **local hardware** as an explicit `make` target (Emscripten, the host asset-tool, the
native parity reference, `astro build`, `tsc`, Playwright). Netlify runs the deploy build
(hosting is deferred, §10). CI, if used at all, is restricted to **non-building**
validation (manifest JSON validity, the provenance staleness hash-check of §6) — whether
even that runs is an open operator decision (§10). No git hooks either (Commandment II).

Dev loop: `npm run dev` serves content with hot reload against the committed manifest
(pointing at already-published CDN assets); re-run Stage 1 locally only when the core,
adapter, or asset-tool changes.

### 4.4 Lesson assets — generated from the real core (two producers, one writer)

All artifacts the site consumes are generated from the *real* core and described by a
single typed **manifest contract** (`svf.json` + a `manifest.ts` reader). Two producers
feed it — kept separate because they are genuinely different build kinds (a cross-compiler
is not a data tool) — but **each producer only writes its own fragment; a single manifest
assembler is the sole writer of the manifest (review #3)** — no two writers racing one
file:

- **Producer A — WASM (Emscripten):** cross-compiles the `adapters/web` audio target →
  `svf.<hash>.wasm` + worklet; exports the audio + analysis capabilities (§4.1). Runtime
  engine for "Observe it" and "Play with it." Emits `wasm.fragment.json`.
- **Producer B — host asset-tool (native):** links the same `core/effects/svf`, sweeps
  the filter, emits rendered audio clips + frequency-response / pole-zero / impulse JSON.
  Feeds "Hear it" and seeds the visualizer's reference overlays. Emits
  `static.fragment.json`.

The **manifest** is the contract the site binds to. It records, per asset: kind,
**absolute Cloudflare URL**, content hash, declared **capabilities + version envelope**
(§4.1), params, sample rate, and **provenance** (the core/adapter source hash it was
built from). Consequences of the CDN model:

- **Cross-origin serving (B2 + Cloudflare):** the `.wasm`, worklet, and audio are fetched
  cross-origin, so they MUST be served with `Access-Control-Allow-Origin` for the site
  origin and the `.wasm` with `Content-Type: application/wasm` (for
  `WebAssembly.instantiateStreaming`). Content-hashed URLs are treated as immutable and
  long-cached.
- **No hardcoded filenames:** the site reads URLs from the manifest; future asset kinds
  (spectrogram, phase, bode) extend the manifest, not the site.
- **Provenance enables a non-building staleness guard (§6):** because binaries are built
  locally, the committed manifest's source-hash lets a pure **hash comparison** (no
  compile) detect "core changed but assets weren't rebuilt/published" — runnable as a
  local `make` step and, optionally, as a non-building CI check.

This keeps §3 "Hear it" / "Observe it" assets faithful to the real DSP (Principle VII —
no faked data).

### 4.5 Lesson metadata + repo-doc auto-resolver

"Go deeper" (§3.6) links stay synchronized with the repository automatically rather than
by hand. Two pieces:

- **Typed lesson metadata** (`lesson.meta.ts`): the lesson declares an `effect` id
  (`svf`), its roadmap node, and the anchors it maps to (spec dir, feature slug). Typed,
  so a bad field is a build error (Principle IX).
- **Doc auto-resolver** (`src/lib/repo-refs/resolver.ts`): from that metadata it resolves,
  at build time, the effect's current **spec / plan / tasks / tests / implementation /
  roadmap node** to concrete repo paths + links — walking the real
  `specs/<slug>/`, `core/…`, and roadmap sources. The resolver is built **now** (this
  slice), so "Go deeper" is generated, not hand-maintained, from the first lesson onward.
  A resolver miss (a declared anchor that no longer resolves) is a build failure, not a
  silent dead link — surfacing repo drift instead of hiding it.

## 5. Data flow

- **Asset load**: committed manifest → absolute Cloudflare URLs → browser fetches
  `.wasm` / worklet / audio cross-origin from B2-behind-Cloudflare (CORS + immutable
  content-hashed URLs, §4.4).
- **Play with it (audio)**: UI slider → normalized param → `setParam` on the worklet →
  WASM `process()` (audio capability) on the real SVF target → audio out + response curve
  redrawn.
- **Observe it (visualization)**: same WASM module's **analysis capability**
  (`getFrequencyResponse / getPoleZeroData / renderImpulseResponse`, computed by the real
  compiled target) → live response curve, pole/zero positions, and impulse response
  redrawn as params change (no audio output on this path).
- **Static assets**: host asset-tool → JSON + audio (on the CDN) → manifest → rendered by
  the lesson. Static overlays seed the visualizer's reference state before the live
  module loads.

## 6. Testing

All tests run **locally** as explicit `make` targets (CI builds nothing, §4.3).

- **Core DSP**: already covered by the existing host-side doctest suite. The WASM path
  runs *that same code*; we do not re-test the DSP, we test the bridge.
- **WASM parity test — durable contract (review #6):** no numeric constants copied into a
  TS test. A small **native reference executable** and the **WASM module** are both run
  against the **same versioned input vectors**; their emitted output buffers are compared
  within tolerance. The input-vector fixture is versioned with provenance recorded in the
  manifest, so a stale fixture is detectable rather than silently trusted.
- **Non-building staleness guard**: a pure hash comparison (§4.4) asserts the committed
  manifest's provenance matches the current `core/` + `adapters/web` source — catching
  "core changed, assets not republished." No compile; runs as a local `make` step (and
  optionally a non-building CI check).
- **Type + build check (local)**: `tsc --noEmit` and `astro build` must pass locally;
  broken MDX, a manifest miss, or a type error fails the build.
- **E2E smoke test — Playwright (decided, in scope):** one minimal Playwright test,
  run locally, covering the browser-integration surface unit/build checks miss —
  AudioWorklet init, **cross-origin asset load from the CDN**, user-gesture audio startup,
  and the visualizer rendering.

## 7. Error handling

Two senses of "fallback" are distinguished (review #5) so the static presentation is
never misread as violating the no-fallback rule:

- **Content fallback — ALLOWED.** When the live engine can't run (no AudioWorklet/WASM
  support), the artifact renders **real, pre-generated core output** — the host-tool audio
  clips + static response plot from the manifest. This is genuine DSP output, just
  precomputed; never a broken widget.
- **DSP fallback — PROHIBITED (Principle VII).** Silently replacing a failed WASM module
  with a *substitute processor* (a TS reimplementation, a stand-in filter) is forbidden.
- **WASM fetch/instantiate failure** → a visible, descriptive message in the artifact
  card, optionally alongside the content fallback. The failure is surfaced, not hidden,
  and never papered over with a lookalike DSP.
- **Autoplay policy** → audio starts only on a user gesture (a "▶ Start" control).

## 8. New toolchains (scoped, contained)

Introduced for the first time, confined to the new subtrees; the C++ core and its
CMake build are untouched. **All of these run on local hardware, not CI (§4.3).**

- **Node / npm** — for `site/` (Astro, TypeScript). New `.gitignore` entries
  (`node_modules/`, Astro build output).
- **Emscripten SDK** — local build dependency for `adapters/web`. A new CMake preset
  `web` using the Emscripten toolchain.
- **Playwright** — local E2E smoke test (§6).
- **CDN publish tooling** — a B2 upload client (e.g. `rclone` / B2 CLI) plus the
  Cloudflare config (CORS allow-origin, `Content-Type: application/wasm`, immutable
  caching for content-hashed objects). Invoked by the local `make publish-assets` step.
- **Static-build contract** — `npm run build` MUST emit a self-contained, deployable
  static bundle (the Netlify-ready artifact) that references only the committed manifest +
  CDN URLs; no server runtime. Hosting/deploy pipeline itself is out of scope (§10).

## 9. Definition of done (this slice)

1. `adapters/web` compiles the real SVF audio target to WASM; the parity test is green.
2. `site/` builds (`astro build` + `tsc --noEmit` clean); the SVF lesson renders all
   **six** parts.
3. **Play with it** plays audio and the response curve tracks the sliders, driven by the
   real core.
4. **Observe it** renders live response-curve / pole-zero / impulse visualization,
   driven by the same WASM module.
5. Both artifacts are resolved through the **typed artifact registry**.
6. The **lesson-asset manifest** describes every generated asset; both producers (WASM +
   host asset-tool) emit into it; nothing is faked.
7. The **doc auto-resolver** generates "Go deeper" links from lesson metadata; a resolver
   miss fails the build.
8. Static "Hear it" assets are generated from the host asset-tool (on the CDN).
9. Binaries are built locally and published to B2/Cloudflare; the committed manifest
   pins immutable content-hashed URLs; the non-building staleness guard passes.
10. The Playwright smoke test passes locally (AudioWorklet init, cross-origin CDN load,
    user-gesture startup, visualizer render).
11. `npm run build` emits a self-contained, Netlify-ready static bundle (no CI build).
12. The visual layer was produced via `/frontend-design`.
13. All JS-runtime source is TypeScript strict; no `any` / `@ts-ignore`.

## 10. Scope

Per Commandment V, scope is the operator's. This section separates what the operator has
**decided** from what remains **open** — nothing here is an agent-side "YAGNI" cut.

### 10a. Operator-set slice boundary (decided)

The operator scoped this to **one vertical SVF lesson**. Consequences of that decision:

- Any lesson beyond the SVF (saturation, diode clipper, WDF, …) — each a future spec.
- A multi-lesson curriculum / global navigation shell (there is only one lesson to route).
- Additional `adapters/*` beyond `web`.

The registry (§4.2), asset manifest (§4.4), and doc auto-resolver (§4.5) **are in scope**
this slice — the operator elected to build these generalizing layers now, not defer them.

### 10b. Operator decisions taken this round (resolved)

- **Assets** → built on **local hardware**, published to **B2 + Cloudflare**; manifest
  (JSON) committed, binaries not; **CI builds nothing** (§4.3).
- **E2E** → one **Playwright** smoke test now, run locally (§6).
- **Non-audio target kinds** → capability/version envelope in the manifest, **no
  speculative kind** now (provisional, reversible — technical limit, §4.1).
- **Deployment** → hosting out of scope; **Netlify** is the eventual target; the slice
  must produce a **static-build contract** (§8) so it is deploy-ready.

### 10c. Still open — awaiting operator decision (surfaced, not cut)

- **Exact Cloudflare/B2 config** — CORS allow-origin value, cache-control policy, bucket
  layout, and the upload client (`rclone` vs B2 CLI): pin during the implementation plan.
- **Whether CI runs the non-building checks at all** (§4.3/§6), or validation stays
  purely local `make` steps.
- **Accounts / progress tracking / backend** — not needed for a training lesson; flagged
  rather than silently excluded, to confirm.

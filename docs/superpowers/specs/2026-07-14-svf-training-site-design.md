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
  - **Surfaced technical limit (operator's call — not a YAGNI cut):** the audio-target
    ABI below is `process(block)`-shaped. Non-audio executables (measurements, benchmarks,
    component models) are *not* `process()`-shaped, so a single ABI covering them cannot
    be designed until at least one such lesson defines its real shape — this is a
    genuine "shape unknown" limit, named per Commandment V, not an "we don't need it yet"
    trim. If the operator wants a non-audio target-kind stubbed now, say so and it goes in;
    otherwise its ABI is designed when the first such lesson is specced.
- Exposes a minimal C ABI for an audio target:
  `create / prepare(sampleRate, blockSize) / setParam(id, norm) / process(ptr, numSamples)`.
- A TypeScript **AudioWorklet processor** wraps the module and runs `process()` on the
  audio thread; the WASM heap buffer is reused per block (no per-block allocation —
  Principle VIII holds in the browser too).
- Output artifacts: `svf.wasm` + a TypeScript glue/worklet module, emitted into the
  lesson-asset output (§4.4) for the site to consume.

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
  public/assets/svf/               # lesson-asset output (§4.4): svf.wasm, worklet, audio, json, manifest.json
  astro.config.mjs
  tsconfig.json                    # strict: true
  package.json
```

- Lessons authored in **MDX** (prose + embedded components + math).
- **Interactive artifacts are embeddable islands resolved through a typed registry.** A
  lesson embeds 0..N artifacts; the registry maps an artifact *kind* → its component, so
  a lesson declares `demo` / `visualizer` (and, later, `spectrum` / `oscilloscope` /
  `sandbox` / `exercise`) without importing components ad hoc. This slice registers and
  ships two: `visualizer` (`<SvfVisualizer>`, "Observe it") and `demo` (`<SvfDemo>`,
  "Play with it").
- **Hard dependency invariant (Principle VI):** `site/` depends *only* on the exported
  browser ABI (§4.1) and the lesson-asset contract (§4.4). It knows **nothing** about
  `core/` internals — no core headers, no core source paths, no coupling to a specific
  effect's internals. This mirrors how `plugin` / `daisy` / `workbench` adapters relate
  to the core, and it is non-negotiable: the arrow is `core → adapters/web → site`, never
  backward.
- The visual design of both artifacts — and the overall lesson layout/typography — is
  produced via `/frontend-design` (Commandment IV).
- All `.ts` / `.astro` / component / config source is TypeScript strict (Principle IX).

### 4.3 Build wiring

One entry point (`make site` / equivalent) runs the two producers (§4.4) then the site:

1. **Emscripten** build `adapters/web` → `svf.wasm` + TypeScript worklet.
2. **Host asset-tool** build + run → audio clips + response/pole-zero/impulse JSON.
3. Both emit into `site/public/assets/svf/` and write/refresh `manifest.json` (§4.4).
4. `npm run build` (Astro) in `site/` consumes the manifest.

Dev loop: `npm run dev` serves content with hot reload; re-run producer step 1 and/or 2
when the core, adapter, or asset-tool changes.

### 4.4 Lesson assets — generated from the real core (two producers, one contract)

All artifacts the site consumes are generated from the *real* core and described by a
single typed **manifest contract** (`manifest.json` + `manifest.ts` reader). Two
distinct producers write into that one contract — kept separate because they are
genuinely different build kinds, not merged (a cross-compiler is not a data tool):

- **Producer A — WASM (Emscripten):** cross-compiles the `adapters/web` audio target →
  `svf.wasm` + worklet. Runtime engine for "Observe it" and "Play with it."
- **Producer B — host asset-tool (native):** links the same `core/effects/svf`, sweeps
  the filter, and emits rendered audio clips + frequency-response / pole-zero / impulse
  JSON. Feeds "Hear it" and seeds the visualizer's reference overlays.

The **manifest** is the contract the site binds to: it lists every asset (kind, path,
params, sample rate, provenance) so the site never hardcodes filenames and future asset
kinds (spectrogram, phase, bode) extend the manifest, not the site. This keeps §3
"Hear it" / "Observe it" assets faithful to the real DSP (Principle VII — no faked data).

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

- **Play with it (audio)**: UI slider → normalized param → `setParam` on the worklet →
  WASM `process()` on the real SVF target → audio out + response curve redrawn from the
  module's coefficients.
- **Observe it (visualization)**: same WASM module → the visualizer reads coefficients /
  runs an offline sweep/impulse through `process()` → live response curve, pole/zero
  positions, and impulse response redrawn as params change (no audio output on this path).
- **Static assets**: host asset-tool → JSON + audio → manifest → rendered by the lesson.
  Static overlays seed the visualizer's reference state before the live module loads.

**Open decision (operator's call, §10):** whether generated assets are committed into the
repo or produced at build time. Both are viable; surfaced here rather than chosen
unilaterally.

## 6. Testing

- **Core DSP**: already covered by the existing host-side doctest suite. The WASM path
  runs *that same code*; we do not re-test the DSP, we test the bridge.
- **WASM parity test** (Node, TypeScript): load the module, push a known impulse/sine
  through `process()`, assert output matches the host suite's reference values — proving
  the browser path does not distort the DSP.
- **Site build check**: `astro build` (and `tsc --noEmit`) must pass in CI; broken MDX,
  a missing WASM asset, or a type error fails the build.
- **Interactive artifacts**: hand smoke-test at implementation (audio start/stop, sliders
  move the curve, the visualizer tracks params). Automated E2E browser testing is an
  **open operator decision** (§10), not excluded by default.

## 7. Error handling

- **No AudioWorklet/WASM support** → the demo island renders a graceful fallback: the
  pre-generated audio clips + static response plot. Never a broken widget.
- **Autoplay policy** → audio starts only on a user gesture (a "▶ Start" control).
- **WASM fetch/instantiate failure** → a visible, descriptive message in the demo card.
  No mock/fallback DSP (Principle VI) — the failure is surfaced, not hidden.

## 8. New toolchains (scoped, contained)

Introduced for the first time, confined to the new subtrees; the C++ core and its
CMake build are untouched:

- **Node / npm** — for `site/` (Astro, TypeScript). New `.gitignore` entries
  (`node_modules/`, Astro build output).
- **Emscripten SDK** — build dependency for `adapters/web`. A new CMake preset `web`
  using the Emscripten toolchain.

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
8. Static "Hear it" assets are generated from the host asset-tool.
9. The visual layer was produced via `/frontend-design`.
10. All JS-runtime source is TypeScript strict; no `any` / `@ts-ignore`.

## 10. Scope

Per Commandment V, scope is the operator's. This section separates what the operator has
**decided is out** from what is **open and awaiting the operator's decision** — nothing
here is an agent-side "YAGNI" cut.

### 10a. Operator-set slice boundary (decided)

The operator scoped this to **one vertical SVF lesson**. Consequences of that decision:

- Any lesson beyond the SVF (saturation, diode clipper, WDF, …) — each a future spec.
- A multi-lesson curriculum / global navigation shell (there is only one lesson to route).
- Additional `adapters/*` beyond `web`.

The registry (§4.2), asset manifest (§4.4), and doc auto-resolver (§4.5) **are in scope**
this slice — the operator elected to build these generalizing layers now, not defer them.

### 10b. Open — awaiting operator decision (not cut, surfaced)

- **Assets: commit vs. build-time generation** (§5) — both viable; operator to choose.
- **Automated E2E browser testing** (§6) — include now or add later; operator to choose.
- **Non-audio target kinds** in `adapters/web` (§4.1) — stub now or design when the first
  such lesson exists (a genuine "shape unknown" technical limit, not a YAGNI trim).
- **Accounts / progress tracking / backend / hosting + deployment pipeline** — the site
  must eventually be served; whether any of this belongs in this slice is the operator's
  call, flagged rather than silently excluded.

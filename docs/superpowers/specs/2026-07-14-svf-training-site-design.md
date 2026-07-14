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

## 2. Governing constraints (constitution)

This work is bound by the acfx constitution (v1.5.0), notably:

- **Commandment IV — All UI/UX through `/frontend-design`.** Every visual/interaction
  decision on this site (lesson layout, the interactive demo component, typography,
  color, spacing) MUST be produced via the `frontend-design` plugin skill. This design
  fixes *what* is built and *how it is structured*; `/frontend-design` owns *how it
  looks* at implementation time.
- **Principle VIII — Strict Typing.** All JavaScript-runtime code (`site/`,
  `adapters/web` glue + AudioWorklet, build tooling) MUST be TypeScript in `strict`
  mode: no plain `.js`, no `any`, no `@ts-ignore`, no unchecked casts. Type errors are
  build failures.
- **Principle V — Platform-Independent Core, Thin Adapters.** The browser is treated as
  *just another target*. `adapters/web` is a thin shell; `core/` is not modified and
  gains no web/JS knowledge. Dependencies point only inward.
- **Principle VI — No Fallbacks / Mock Data Outside Tests.** Audio clips and response
  plots are generated from the *real* core. A WASM load failure surfaces a descriptive
  error; it never silently substitutes fake DSP.
- **Principle X — Progressive Layered Architecture.** The repo's own
  Theory → Laboratory → Reusable Primitive → Production Effect ladder (the SVF has a
  `core/labs/state-variable-filter/` harness) is the backbone of the build-along track.

## 3. Learner experience — lesson anatomy

The SVF lesson is the reusable five-part skeleton every future lesson will follow:

1. **Concept** — what an SVF is: the three simultaneous outputs (LP/BP/HP), the
   cutoff/resonance controls, why it is the "hello world" of analog-style filters.
   Prose + a signal-flow diagram + the transfer-function math.
2. **Hear it** — embedded audio: a source (drums/synth) run through the filter at a few
   preset settings, plus the frequency-response plot. Assets generated offline from the
   real core (§5.4).
3. **Play with it** — the live AudioWorklet demo: sliders for cutoff / resonance / mode
   driving the WASM-compiled `core/effects/svf`, with a response curve that updates live
   as the learner drags.
4. **Build it with acfx** — a build-along track mirroring the actual `svf-vertical-slice`
   spec and the labs→primitives→effects ladder: the `Effect` concept, the constexpr
   parameter table, wiring one adapter, running the host tests. Each step has a
   verifiable checkpoint.
5. **Go deeper** — links to the real spec / quickstart / tests in the repo and a pointer
   to what comes next.

## 4. Architecture

### 4.1 `adapters/web` — the browser adapter (the DSP engine)

A new thin adapter alongside `workbench, plugin, daisy, teensy`.

- Compiles `core/effects/svf` (which pulls `core/primitives/filters/svf-primitive.h`
  → `daisysp::Svf`, the same CPM-pinned DaisySP) to a **WebAssembly** module via
  **Emscripten**.
- Exposes a minimal C ABI: `create / prepare(sampleRate, blockSize) / setParam(id, norm)
  / process(ptr, numSamples)`.
- A TypeScript **AudioWorklet processor** wraps the module and runs `process()` on the
  audio thread; the WASM heap buffer is reused per block (no per-block allocation —
  Principle VII holds in the browser too).
- Output artifacts: `svf.wasm` + a TypeScript glue/worklet module, emitted for the site
  to consume.

### 4.2 `site/` — the Astro training site

First Node/TypeScript subtree in the repo. Astro chosen for a content-first site with
isolated islands of interactivity (ships zero JS except the one demo component).

```
site/
  src/content/svf.mdx              # the five-part SVF lesson (prose / math / diagram)
  src/components/SvfDemo/          # interactive island: worklet + WASM + controls + live curve
  public/wasm/                     # build output copied from adapters/web (svf.wasm, worklet)
  astro.config.mjs
  tsconfig.json                    # strict: true
  package.json
```

- Lessons authored in **MDX** (prose + embedded components + math).
- The single interactive island `<SvfDemo>` is the only hydrated component. Its visual
  design — and the overall lesson layout/typography — is produced via `/frontend-design`.
- All `.ts` / component / config source is TypeScript strict (Principle VIII).

### 4.3 Build wiring

One entry point (`make site` / equivalent):

1. Emscripten-build `adapters/web` → `svf.wasm` + worklet.
2. Copy artifacts into `site/public/wasm/`.
3. `npm run build` (Astro) in `site/`.

Dev loop: `npm run dev` serves content with hot reload; re-run the emcc step when the
core or adapter changes.

### 4.4 Real-core-generated static assets

A small **host tool** (native, not WASM) links the same `core/effects/svf`, sweeps the
filter, and emits:

- frequency-response JSON (for the "Hear it" plot and as reference data), and
- rendered audio clips at the preset settings.

This keeps §3.2 assets faithful to the real DSP (Principle VI — no faked data).

## 5. Data flow

- **Live demo**: UI slider → normalized param → `setParam` on the worklet → WASM
  `process()` on real `SvfPrimitive`/`SvfEffect` → audio out + response curve redrawn
  from the module's coefficients/sweep.
- **Static assets**: host tool → JSON + audio files → committed into `site/` (or
  generated at build) → rendered by the lesson.

## 6. Testing

- **Core DSP**: already covered by the existing host-side doctest suite. The WASM path
  runs *that same code*; we do not re-test the DSP, we test the bridge.
- **WASM parity test** (Node, TypeScript): load the module, push a known impulse/sine
  through `process()`, assert output matches the host suite's reference values — proving
  the browser path does not distort the DSP.
- **Site build check**: `astro build` (and `tsc --noEmit`) must pass in CI; broken MDX,
  a missing WASM asset, or a type error fails the build.
- **Interactive demo**: hand smoke-test at implementation (audio start/stop, sliders move
  the curve). No heavy E2E harness for a one-lesson slice (YAGNI).

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

1. `adapters/web` compiles the real SVF core to WASM; the parity test is green.
2. `site/` builds (`astro build` + `tsc --noEmit` clean); the SVF lesson renders all
   five parts.
3. The live demo plays audio and the response curve tracks the sliders, driven by the
   real core.
4. Static "Hear it" assets are generated from the host tool.
5. The visual layer was produced via `/frontend-design`.
6. All JS-runtime source is TypeScript strict; no `any` / `@ts-ignore`.

## 10. Explicitly out of scope

- Any lesson beyond the SVF (saturation, diode clipper, WDF, etc.) — future specs.
- A full curriculum / navigation shell beyond what one lesson needs.
- Accounts, progress tracking, backend, deployment/hosting pipeline.
- E2E browser test automation.
- Additional adapters beyond `web`.

# Data Model: SVF Training Site (Vertical Slice)

Phase 1. The "data" here is mostly build-time/contract structures (no database). Entities,
their fields, relationships, and validation rules.

## Lesson

A narrative unit that embeds interactive artifacts.

- **Fields**: `slug` (e.g. `svf`), `title`, ordered `parts` (the six-part anatomy), embedded
  `artifacts` (0..N `ArtifactDeclaration`), `meta` (`LessonMeta`).
- **Relationships**: has one `LessonMeta`; embeds N `InteractiveArtifact` by declaration.
- **Validation**: renders all six parts (FR-001); every embedded artifact `kind` resolves in
  the registry.

## LessonMeta (typed metadata)

- **Fields**: `effect` id (`svf`), `roadmapNode` (`design:feature/svf-training-site`),
  `repoAnchors` (spec dir, feature slug, key source paths).
- **Validation**: typed — a bad field is a build error (Principle IX). Every declared anchor
  MUST resolve at build time or the build fails (FR-009).

## InteractiveArtifact

An embeddable island backed by the real core, resolved by `kind`.

- **Fields**: discriminated union on `kind` (`"svf-demo" | "svf-visualizer"`), `assetBundle`
  (manifest key), typed `props` (per `kind`, never `unknown` at consumption).
- **Relationships**: consumes an `AssetBundle` from the manifest; drives an `ExecutableTarget`
  capability (audio for demo, analysis for visualizer).
- **State**: `loading → ready → live` on success; `loading → content-fallback` when the live
  engine is unavailable; `loading → error(descriptive)` on a fetch/instantiate failure.
  DSP-fallback state is PROHIBITED (FR-015).

## ExecutableTarget (`adapters/web` WASM)

- **Fields**: `capabilities` (declared: `audio` now; `analysis` Phase 5), `version`.
- **Audio capability (C ABI)**: `create / destroy / prepare(sampleRate,maxBlockSize,numChannels)
  / setParam(id,normalized) / process(ptr,numSamples)`.
- **Analysis capability (C ABI, Phase 5)**: `getFrequencyResponse / getPoleZeroData /
  renderImpulseResponse` — computed by the real compiled target.
- **Validation**: `process()` allocation-free (Principle VIII); output equals the native
  reference within 1e-6 on shared vectors (FR-005).

## LessonAssetManifest

The single committed contract the site binds to.

- **Fields**: `version`, `assets[]` where each asset = `{ kind, url (absolute CDN), sha256,
  capabilities, capabilityVersion, params, sampleRate, provenance }`, `sourceProvenance`
  (the `core/`+`adapters/web` source hash the binaries were built from).
- **Relationships**: written by exactly ONE assembler from per-producer fragments
  (`wasm.fragment.json`, `static.fragment.json`); read by the site (never hardcoded filenames).
- **Validation**: single writer (FR-008); the non-building staleness guard fails when
  `sourceProvenance` ≠ current source hash (FR-012).

## AssetCDN

- **Fields**: `bucket` (`audiocontrol-acfx`, public), `endpoint` (s3.us-west-004), `workerBase`
  (`CDN_BASE`), `edgeTtl`.
- **Validation**: serves `.wasm` with `application/wasm` + `Access-Control-Allow-Origin`; caches
  only 2xx; immutable content-hashed URLs (FR-010). No secret in the repo (FR-018).

## RepoDocResolver (build-time)

- **Fields**: input `LessonMeta.repoAnchors`; output resolved links to spec/plan/tasks/tests/
  implementation/roadmap.
- **Validation**: build-time only; MUST NOT import/parse/depend on C++ semantics (reads repo
  paths as documentation metadata); an unresolved anchor fails the build (FR-009).

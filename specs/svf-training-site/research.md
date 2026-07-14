# Research: SVF Training Site (Vertical Slice)

Phase 0. The technical approach is settled in the governed design record
(`docs/superpowers/specs/2026-07-14-svf-training-site-design.md`, two third-party reviews +
operator infra decisions). This file records the decisions, rationale, and rejected
alternatives, and resolves the remaining open questions.

## Settled decisions

### D1 — In-browser DSP: real core → WASM adapter
- **Decision**: compile `core/effects/svf` to WebAssembly as a new inward-only `adapters/web` target; run it in an AudioWorklet.
- **Rationale**: the interactive figure IS the taught code; extends "same source everywhere"; honors Principle VII (no lookalike DSP).
- **Alternatives rejected**: JS/Web-Audio reimplementation (drifts from real C++, violates VII); pre-rendered clips only (not interactive).

### D2 — Site: Astro + strict TypeScript
- **Decision**: content-first Astro site, MDX lessons, interactive artifacts as hydrated islands; all JS-runtime code TypeScript strict (Principle IX).
- **Alternatives rejected**: Vite+React SPA (heavy, hand-rolled content plumbing); Docusaurus (fights a custom AudioWorklet demo).

### D3 — Artifacts: typed registry, metadata-driven
- **Decision**: lessons declare artifacts by `kind` (`<Artifact kind="svf-visualizer" .../>`); a typed registry (discriminated union on `kind`) resolves kind→component. Build the registry now.
- **Rationale**: the registry earns its place only if declaration is data; enables future artifact kinds without lesson rework.

### D4 — Lesson assets: two producers, one writer
- **Decision**: the Emscripten WASM producer and the native host asset-tool each emit a **fragment**; a single **assembler** writes the committed manifest (absolute CDN URLs + content hash + capabilities/version + provenance).
- **Rationale**: a cross-compiler and a data tool are different build kinds (keep separate); one authoritative manifest writer avoids lost-update races.

### D5 — Analysis capability
- **Decision**: the WASM target exports declared capabilities; audio ABI in Phase 1, **analysis ABI** (`getFrequencyResponse/getPoleZeroData/renderImpulseResponse`) with the visualizer in Phase 5. Computed by the real compiled target, never re-derived in TS.
- **Rationale**: the analysis contract is defined by its consumer (the visualizer); re-sequenced (surfaced per Commandment V), not cut.

### D6 — Asset CDN: public B2 + Cloudflare Worker (colony-cults model)
- **Decision**: binaries built on local hardware, uploaded (S3 API) to the public `audiocontrol-acfx` B2 bucket, served through a Cloudflare Worker read-through cache (`infra/cloudflare-cdn/worker.ts`, strict TS), modeled on `oletizi/colony-cults`. `npx wrangler deploy`; only 2xx cached; `ACAO` + `application/wasm`; immutable content-hashed URLs. Manifest committed, binaries not.
- **Alternatives rejected**: commit binaries to git; build in CI (operator: too slow); same-origin Netlify serving (operator chose a dedicated CDN).

### D7 — CI builds nothing
- **Decision**: every build/test runs on local hardware as explicit `make`/npm steps; any CI usage is limited to non-building validation. No git hooks (Commandment II).

### D8 — Parity oracle: durable contract
- **Decision**: a native reference exe and the WASM module run against shared **versioned input vectors**; output buffers compared within tolerance (1e-6). No numeric constants copied into the test; fixture provenance recorded in the manifest.

## Resolved open questions (were §10c of the design record)

- **RQ1 — CDN provider & upload client**: RESOLVED to B2 + Cloudflare per D6; upload client is `rclone` **or** the B2 CLI — pinned at Phase-3 implementation (both satisfy the S3 write contract; `rclone` is the default candidate). Bucket `audiocontrol-acfx`, endpoint `s3.us-west-004.backblazeb2.com`, download host `f004` (confirm via `b2 account get`).
- **RQ2 — CORS policy**: `Access-Control-Allow-Origin: *` on the Worker (origin-independent, safe to cache), matching the colony-cults precedent; a site-origin allowlist is a later tightening if desired.
- **RQ3 — Worker host**: `*.workers.dev` now; a custom-domain zone (purge-by-URL) is a later upgrade.
- **RQ4 — CI non-building checks**: the staleness/manifest-validity checks MAY run in CI (non-building) or stay local `make` steps — operator's call at Phase 6; default local.
- **RQ5 — Test runner**: **vitest** for unit/parity (default), Playwright for the E2E smoke. `node:test` is an acceptable substitute if the operator prefers zero extra deps.
- **RQ6 — Accounts/backend**: out — not needed for a training lesson (flagged, confirmed).

## Prerequisites (local)

Emscripten SDK (`emcc`), Node ≥ 22, `npx wrangler`, an S3/B2 upload client. `emcc` is not
assumed installed on every machine — the operator builds/publishes on capable local hardware.

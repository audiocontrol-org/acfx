# Quickstart / Validation: SVF Training Site (Vertical Slice)

Runnable validation that the slice works end-to-end. All steps are **local** (CI builds
nothing). Prereqs: Emscripten SDK (`source ~/emsdk/emsdk_env.sh`), Node ≥ 22, `npx wrangler`,
an S3/B2 upload client, and the gitignored creds file
`~/.config/backblaze/b2-audiocontrol-acfx-credentials.yaml`.

## Phase 1 — `adapters/web` WASM audio ABI + parity (SC-002)

```bash
source ~/emsdk/emsdk_env.sh
make web-parity          # native ABI test + WASM build + parity test on shared vectors
```
Expected: native ABI test PASS; parity test PASS (every sample within 1e-6 of the native
reference). See `docs/superpowers/plans/2026-07-14-svf-web-adapter-parity.md`.

## Phase 2 — host asset-tool + manifest (FR-006, FR-008)

```bash
make lesson-assets       # native tool sweeps the real core → audio + response/pole-zero/impulse JSON + fragment
```
Expected: `static.fragment.json` produced; the assembler merges fragments into a valid manifest;
assets are real (generated from the core), nothing faked.

## Phase 3 — asset CDN (FR-010)

```bash
cd infra/cloudflare-cdn && npx wrangler deploy      # local
make publish-assets                                 # upload content-hashed objects to B2 (S3 API)
curl -D - "$CDN_BASE/svf.<hash>.wasm" -o /dev/null  # verify headers
```
Expected: `Access-Control-Allow-Origin: *`, `Content-Type: application/wasm`, and a second read
is a cache HIT. No secret printed or committed.

## Phase 4 — site core (FR-007, FR-009, FR-017)

```bash
cd site && npm install && npm run typecheck && npm run build
```
Expected: `tsc --noEmit` clean; `astro build` succeeds; the artifact registry resolves both
kinds; the doc auto-resolver produces "Go deeper" links; a stale metadata anchor FAILS the build.

## Phase 5 — interactive artifacts + lesson (FR-001, FR-002, FR-003, SC-001, SC-003)

Open the built lesson locally; on a user gesture start audio.
Expected: all six parts render; "Play with it" changes audio in real time driven by the real
WASM; "Observe it" updates response/poles-zeros/impulse live from the analysis capability. Visual
design was produced via `/frontend-design`.

## Phase 6 — E2E + deploy contract (FR-011, FR-012, FR-013, FR-014, SC-004, SC-006)

```bash
make staleness-guard     # non-building hash check: manifest provenance vs current source
cd site && npm run test:e2e   # one Playwright smoke: worklet init, cross-origin CDN load, gesture start, viz render
npm run build            # emits a self-contained Netlify-ready site/dist
```
Expected: staleness guard passes (no compile); Playwright smoke PASS; `site/dist` is
self-contained (references only the committed manifest + CDN URLs). No CI build ran.

## Definition of done (traceable to SC)

- SC-001/003 → Phase 5; SC-002 → Phase 1; SC-004 → Phases 3+6; SC-005 → Phase 4;
  SC-006 → Phase 6.

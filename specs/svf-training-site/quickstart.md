# Quickstart / Validation: SVF Training Site (Vertical Slice)

Runnable validation that the slice works end-to-end. All steps are **local** (CI builds
nothing). Prereqs (installed 2026-07-14): Emscripten via Homebrew (`emcc`/`emcmake` on
PATH — `brew install emscripten`), Node ≥ 22, `rclone` (`brew install rclone`), `npx wrangler`,
and the gitignored creds file `~/.config/backblaze/b2-audiocontrol-acfx-credentials.yaml`.

## Phase 1 — `adapters/web` WASM audio ABI + parity (SC-002)

```bash
make web-parity          # native ABI test + WASM build (emcmake) + parity test on shared vectors
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
make site-build          # build the static site; emits site/dist/ (Netlify ready)
make e2e                 # build site + run Playwright E2E smoke test (installs Chromium if needed)
```
Expected: staleness guard passes (no compile); site builds to a self-contained `site/dist/`
(references only the committed manifest + CDN URLs); Playwright smoke PASS. No CI build ran.

## Definition of done (traceable to SC)

- SC-001/003 → Phase 5; SC-002 → Phase 1; SC-004 → Phases 3+6; SC-005 → Phase 4;
  SC-006 → Phase 6.

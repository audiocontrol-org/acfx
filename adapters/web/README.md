# adapters/web — WebAssembly adapter (Phase 1)

The browser adapter: one extern-"C" ABI over `acfx::SvfEffect`, compiled to
`svf.wasm` (Emscripten) and to a native `svf-reference` CLI from the same source.
A parity test proves the two agree.

## Build & test (local only — CI builds nothing)

Prereq: Emscripten on PATH (`brew install emscripten` → `emcc`/`emcmake`), Node >= 22.

- `make web-ref`    — native ABI test + reference CLI
- `make web-wasm`   — the WASM module (`build/web/svf.{mjs,wasm}`)
- `make web-parity` — typecheck + WASM/native parity test

Depends only inward on `acfx_core` (Constitution VI). The analysis ABI
(frequency response / poles-zeros / impulse) arrives with the visualizer (Phase 5).

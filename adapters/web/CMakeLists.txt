# adapters/web — the browser adapter. One extern-"C" ABI over acfx::SvfEffect
# compiles two ways: to svf.wasm via Emscripten (EMSCRIPTEN set by the `web`
# preset toolchain), and to a native reference CLI + test via the host compiler
# (the `web-ref` preset). Depends only inward on acfx_core (Constitution VI).
message(STATUS "adapters/web: EMSCRIPTEN=${EMSCRIPTEN}")

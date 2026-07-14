# Contract: `adapters/web` executable-target ABI

The C ABI the WASM module exports; consumed by the TypeScript loader and (Phase 5) the
AudioWorklet + visualizer. Computed by the real compiled `acfx::SvfEffect`; never re-derived
in TS (FR-002/003, Principle VII).

## Audio capability (Phase 1)

```c
typedef struct SvfHandle SvfHandle;
SvfHandle* svf_create(void);
void       svf_destroy(SvfHandle* h);
void       svf_prepare(SvfHandle* h, double sampleRate, int maxBlockSize, int numChannels);
void       svf_set_param(SvfHandle* h, unsigned char paramId, float normalized); // 0=cutoff,1=resonance,2=mode; norm 0..1
void       svf_process(SvfHandle* h, float* samples, int numSamples);            // mono, in place; allocation-free
```

- Emscripten exports: the above + `_malloc`/`_free`; runtime methods `ccall`/`cwrap`/`HEAPF32`;
  `MODULARIZE` ES6 module `svf.mjs` + `svf.wasm`.
- **Invariant**: `svf_process` allocates nothing (Principle VIII). Allocation only in
  `svf_create`/`svf_prepare`.
- **Parity**: output equals the native `svf-reference` within 1e-6 on shared vectors (FR-005).

## Analysis capability (Phase 5, visualizer-coupled)

```c
// Exact shape finalized with the visualizer (its consumer). All computed by the real target.
void svf_get_frequency_response(SvfHandle* h, const float* freqsHz, float* magsOut, int n);
void svf_get_pole_zero(SvfHandle* h, float* polesOut, float* zerosOut, int* countsOut);
void svf_render_impulse(SvfHandle* h, float* out, int numSamples);
```

- Frequency response derived from the real impulse response / target coefficients; pole/zero from
  the target's own coefficients. **No TS re-derivation.**

## TypeScript loader (strict)

`SvfModule.load(mjsUrl): Promise<SvfModule>` with `create/destroy/prepare/setParam/process`.
The Emscripten surface is declared explicitly (no `any`). A failed load throws (no silent
fallback — Principle VII).

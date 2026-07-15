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
// All computed by the real target (declared in svf-web-analysis.h).
void svf_render_impulse(SvfHandle* h, float* out, int numSamples);
void svf_get_frequency_response(SvfHandle* h, const float* freqsHz, float* magsOut, int n);
void svf_get_pole_zero(SvfHandle* h, float* polesOut, float* zerosOut, float* gainOut, int* countsOut);
```

- **`svf_render_impulse`** — runs a literal unit impulse through the real `SvfEffect::process()`
  (a fresh at-rest effect with the handle's sample rate + published params) and writes the impulse
  response. Trivially authoritative.
- **`svf_get_frequency_response`** — MEASURED linear magnitude `|H(f)|`: renders the real
  (small-signal) impulse response, then DFTs it at each requested frequency (same measurement as
  `tools/lesson-assets/dft.h`). Small amplitude keeps DaisySP's `Svf` in its linear regime, so this
  is the true transfer-function magnitude (de-scaled), not resonance distortion. `magsOut` is
  **linear** (not dB).
- **`svf_get_pole_zero`** — poles/zeros of the 2nd-order system the SVF realizes for the current
  cutoff/resonance/mode, derived from DaisySP's ACTUAL `Svf` difference equations (the double-sampled
  state map `A`, `M = A²`, `N = (A+I)B`; see `svf-web-analysis.cpp`). Layout:
  - `polesOut` / `zerosOut`: interleaved `[re0, im0, re1, im1]`.
  - `gainOut`: leading numerator coefficient `b0`, so `H(z) = gain · Π(z − zero) / Π(z − pole)`.
  - `countsOut`: `[numPoles, numZeros, modeIndex]` (poles/zeros buffers ≥ 4 floats; counts ≥ 3 ints).
- **Authoritativeness (Principle VII):** the analytic pole/zero magnitude is cross-validated against
  the measured `svf_get_frequency_response` in `adapters/web/test/svf-analysis.test.ts` (max error
  ≈ 0.001 dB across LP/HP/BP settings). **No TS re-derivation.**

## TypeScript loader (strict)

`SvfModule.load(mjsUrl): Promise<SvfModule>` with `create/destroy/prepare/setParam/process`.
The Emscripten surface is declared explicitly (no `any`). A failed load throws (no silent
fallback — Principle VII).

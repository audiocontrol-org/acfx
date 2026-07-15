// Glue-free analysis-capable SVF wasm loader (T031).
//
// The training site's visualizer (site/src/components/artifacts/SvfVisualizer)
// needs the REAL compiled analysis ABI (svf_get_frequency_response /
// svf_get_pole_zero / svf_render_impulse) but must NOT depend on the generated
// Emscripten glue (`svf.mjs`), which is emitted only by the local `build/web`
// Emscripten build and is never present on Netlify/CI (the static-build
// contract, FR-014, forbids depending on `build/`).
//
// This mirrors the exact pattern already proven for the AudioWorklet audio
// engine (worklet/svf-engine-core.ts, T026): svf.wasm imports only two host
// functions and exports its own memory, so it can be `fetch`ed and
// instantiated directly with a tiny hand-written import object — no glue, no
// `import()` of a generated module. The shared instantiation + fetch/compile
// helpers live in wasm/svf-wasm-runtime.ts and are used by BOTH engines.
//
// Unlike the audio engine's `SvfHeapProcessor`, this runs on the main/UI
// thread (never inside an audio callback), so Principle VIII's "no allocation
// in process()" does not apply here: each analysis call `malloc`s scratch
// space for its arguments/results and frees it before returning — the same
// pattern the Emscripten-glue `SvfModule` loader (loader/svf-module.ts) uses
// for the same analysis calls, just without the glue runtime underneath it.

import type { Complex, PoleZero } from "../loader/svf-module.ts";
import {
  compileSvfWasmFromUrl,
  instantiateSvfWasm,
  readF32Heap,
  readI32Heap,
  writeF32Heap,
  type SvfAnalysisWasmExports,
} from "../wasm/svf-wasm-runtime.ts";

export type { Complex, PoleZero };

function mallocOrThrow(wasm: SvfAnalysisWasmExports, bytes: number, what: string): number {
  const ptr = wasm.malloc(bytes);
  if (ptr === 0) throw new Error(`svf.wasm: ${what} malloc failed`);
  return ptr;
}

/**
 * Analysis-capable SVF engine: the real compiled `acfx::SvfEffect` exposed
 * through its analysis ABI, instantiated directly from a `.wasm` URL with no
 * Emscripten glue. A failed load throws (Principle VII — no silent fallback
 * DSP; the caller is expected to show a visible error and the content
 * fallback, per FR-015).
 */
export class SvfAnalysisWasm {
  private constructor(private readonly wasm: SvfAnalysisWasmExports) {}

  static async load(wasmUrl: string | URL): Promise<SvfAnalysisWasm> {
    const module = await compileSvfWasmFromUrl(wasmUrl);
    const wasm = instantiateSvfWasm<SvfAnalysisWasmExports>(module);
    return new SvfAnalysisWasm(wasm);
  }

  create(): number {
    return this.wasm.svf_create();
  }

  destroy(handle: number): void {
    this.wasm.svf_destroy(handle);
  }

  prepare(handle: number, sampleRate: number, maxBlockSize: number, numChannels: number): void {
    this.wasm.svf_prepare(handle, sampleRate, maxBlockSize, numChannels);
  }

  setParam(handle: number, paramId: number, normalized: number): void {
    this.wasm.svf_set_param(handle, paramId, normalized);
  }

  /** Unit-impulse response of the real filter, `n` samples. */
  renderImpulse(handle: number, n: number): Float32Array {
    const ptr = mallocOrThrow(this.wasm, n * 4, "renderImpulse");
    try {
      this.wasm.svf_render_impulse(handle, ptr, n);
      return readF32Heap(this.wasm.memory, ptr, n);
    } finally {
      this.wasm.free(ptr);
    }
  }

  /** MEASURED linear magnitude response |H(f)| at each requested frequency (Hz). */
  getFrequencyResponse(handle: number, freqsHz: Float32Array): Float32Array {
    const n = freqsHz.length;
    const inPtr = mallocOrThrow(this.wasm, n * 4, "getFrequencyResponse (in)");
    const outPtr = mallocOrThrow(this.wasm, n * 4, "getFrequencyResponse (out)");
    try {
      writeF32Heap(this.wasm.memory, inPtr, freqsHz);
      this.wasm.svf_get_frequency_response(handle, inPtr, outPtr, n);
      return readF32Heap(this.wasm.memory, outPtr, n);
    } finally {
      this.wasm.free(inPtr);
      this.wasm.free(outPtr);
    }
  }

  /** Poles/zeros/gain/mode of the current filter setting (interleaved re/im in the ABI). */
  getPoleZero(handle: number): PoleZero {
    // polesOut/zerosOut hold up to 2 complex (4 floats); countsOut >= 3 ints.
    const polesPtr = mallocOrThrow(this.wasm, 4 * 4, "getPoleZero (poles)");
    const zerosPtr = mallocOrThrow(this.wasm, 4 * 4, "getPoleZero (zeros)");
    const gainPtr = mallocOrThrow(this.wasm, 4, "getPoleZero (gain)");
    const countsPtr = mallocOrThrow(this.wasm, 3 * 4, "getPoleZero (counts)");
    try {
      this.wasm.svf_get_pole_zero(handle, polesPtr, zerosPtr, gainPtr, countsPtr);
      const counts = readI32Heap(this.wasm.memory, countsPtr, 3);
      const numPoles = counts[0] ?? 0;
      const numZeros = counts[1] ?? 0;
      const mode = counts[2] ?? 0;
      const readComplex = (ptr: number, count: number): Complex[] => {
        const floats = readF32Heap(this.wasm.memory, ptr, count * 2);
        const out: Complex[] = [];
        for (let i = 0; i < count; i++) {
          out.push({ re: floats[2 * i] ?? 0, im: floats[2 * i + 1] ?? 0 });
        }
        return out;
      };
      const gain = readF32Heap(this.wasm.memory, gainPtr, 1)[0] ?? 0;
      return {
        poles: readComplex(polesPtr, numPoles),
        zeros: readComplex(zerosPtr, numZeros),
        gain,
        mode,
      };
    } finally {
      this.wasm.free(polesPtr);
      this.wasm.free(zerosPtr);
      this.wasm.free(gainPtr);
      this.wasm.free(countsPtr);
    }
  }
}

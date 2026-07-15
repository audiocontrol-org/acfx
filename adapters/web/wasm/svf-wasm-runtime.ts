// Shared glue-free WASM runtime for the compiled SVF binary (T031).
//
// svf.wasm imports exactly two host functions and EXPORTS its own memory (see
// the worklet's svf-processor.ts for the AudioWorklet motivation, T026), so it
// can be instantiated directly against a tiny hand-written import object — no
// Emscripten JS glue (`svf.mjs`), which is generated only by the local
// Emscripten `build/web` build and is never available on Netlify/CI (the
// static-build contract, FR-014, forbids depending on it).
//
// This module holds the parts of that pattern shared by BOTH capability
// profiles of the same binary:
//   - the AudioWorklet audio engine (worklet/svf-engine-core.ts, T026), which
//     re-exports `instantiateSvfWasm`/`SvfWasmExports` from here for its own
//     allocation-free `SvfHeapProcessor` (Principle VIII: no heap allocation
//     in the audio callback path);
//   - the main-thread analysis engine (analysis/svf-analysis-wasm.ts, T031),
//     which instantiates the analysis-extended export surface and uses the
//     non-realtime heap helpers below (analysis never runs in an audio
//     callback, so per-call `malloc`/`free` is fine there).

/** The raw svf.wasm export surface every capability profile shares (audio). */
export interface SvfWasmExports {
  readonly memory: WebAssembly.Memory;
  svf_create(): number;
  svf_destroy(h: number): void;
  svf_prepare(h: number, sampleRate: number, maxBlockSize: number, numChannels: number): void;
  svf_set_param(h: number, paramId: number, normalized: number): void;
  svf_process(h: number, samplesPtr: number, numSamples: number): void;
  malloc(bytes: number): number;
  free(ptr: number): void;
}

/** The analysis-capable wasm export surface: adds the three analysis calls. */
export interface SvfAnalysisWasmExports extends SvfWasmExports {
  svf_render_impulse(h: number, outPtr: number, numSamples: number): void;
  svf_get_frequency_response(h: number, freqsPtr: number, magsPtr: number, n: number): void;
  svf_get_pole_zero(
    h: number,
    polesPtr: number,
    zerosPtr: number,
    gainPtr: number,
    countsPtr: number,
  ): void;
}

interface SvfWasmImports {
  readonly env: {
    emscripten_resize_heap(requestedSize: number): number;
    _abort_js(): void;
  };
}

const WASM_PAGE_BYTES = 65536;
const WASM_MAX_HEAP_BYTES = 2147483648; // matches Emscripten getHeapMax()

/**
 * Instantiate svf.wasm synchronously against a minimal host import object.
 *
 * Generic over the export surface `T` so callers can request either the base
 * audio-only exports or the analysis-extended set — the instantiation and
 * import object are identical either way; the extra analysis exports are just
 * additional entries in the same export table.
 *
 * `new WebAssembly.Instance(module, ...)` is synchronous and works for an
 * already-compiled module even inside restricted scopes (e.g. AudioWorklet).
 * `emscripten_resize_heap` grows the wasm's own exported memory.
 */
export function instantiateSvfWasm<T extends SvfWasmExports = SvfWasmExports>(
  module: WebAssembly.Module,
): T {
  // Closure over `memory`: the import functions are supplied BEFORE the memory
  // exists (it is created inside the instance). They are only invoked on a
  // later grow/abort, by which point `memory` is set.
  let memory: WebAssembly.Memory | undefined;

  const imports: SvfWasmImports = {
    env: {
      emscripten_resize_heap(requestedSize: number): number {
        if (memory === undefined) return 0;
        const req = requestedSize >>> 0;
        if (req > WASM_MAX_HEAP_BYTES) return 0;
        const oldBytes = memory.buffer.byteLength;
        const deltaPages = Math.ceil((req - oldBytes) / WASM_PAGE_BYTES);
        if (deltaPages <= 0) return 1;
        try {
          memory.grow(deltaPages);
          return 1;
        } catch {
          return 0;
        }
      },
      _abort_js(): void {
        throw new Error("svf.wasm aborted");
      },
    },
  };

  const instance = new WebAssembly.Instance(module, imports as unknown as WebAssembly.Imports);
  const exports = instance.exports as unknown as T;
  memory = exports.memory;
  return exports;
}

/**
 * Fetch + compile a wasm binary from a URL (main-thread only: uses `fetch`).
 * Shared by the main-thread worklet bootstrap (svf-audio-node.ts) and the
 * analysis engine (analysis/svf-analysis-wasm.ts) — both need the exact same
 * "fetch bytes, compile, throw a descriptive error on a bad response" step.
 */
export async function compileSvfWasmFromUrl(url: string | URL): Promise<WebAssembly.Module> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`failed to fetch svf.wasm (${response.status}) from ${String(url)}`);
  }
  const bytes = await response.arrayBuffer();
  return WebAssembly.compile(bytes);
}

// --- Non-realtime heap marshalling ------------------------------------------
//
// Fresh-view-per-call helpers for callers that are NOT on the audio callback
// path (Principle VIII only constrains `process()`/audio-callback code). The
// AudioWorklet's allocation-free steady state lives in
// worklet/svf-engine-core.ts's `SvfHeapProcessor`, which caches its heap view
// and only re-derives it when the underlying buffer is detached by a grow;
// these helpers intentionally do NOT share that cache since analysis calls
// `malloc`/`free` per call regardless.

/** Copy `data` into wasm heap at byte offset `ptr`. */
export function writeF32Heap(memory: WebAssembly.Memory, ptr: number, data: Float32Array): void {
  new Float32Array(memory.buffer).set(data, ptr >> 2);
}

/** Copy `count` floats out of wasm heap at byte offset `ptr` into a fresh, detached array. */
export function readF32Heap(memory: WebAssembly.Memory, ptr: number, count: number): Float32Array {
  const base = ptr >> 2;
  return new Float32Array(memory.buffer).slice(base, base + count);
}

/** Copy `count` int32s out of wasm heap at byte offset `ptr` into a fresh, detached array. */
export function readI32Heap(memory: WebAssembly.Memory, ptr: number, count: number): Int32Array {
  const base = ptr >> 2;
  return new Int32Array(memory.buffer).slice(base, base + count);
}

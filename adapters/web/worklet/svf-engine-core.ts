// Pure, DOM-free core of the SVF AudioWorklet engine.
//
// Everything here is testable in Node (no AudioWorklet / DOM globals): the
// parameter/message protocol, the minimal WebAssembly runtime shim, and the
// allocation-free heap processor. The worklet processor (svf-processor.ts) and
// the main-thread helper (svf-audio-node.ts) are thin wiring over this module.
//
// The audio path is the REAL compiled `acfx::SvfEffect` (svf.wasm) — the same
// binary proven at native parity in svf-parity.test.ts (Principle VII: no
// BiquadFilter, no JS reimplementation).

// --- Parameter identity (mirrors the C++ ABI param table) ------------------

export const SVF_PARAM_CUTOFF = 0;
export const SVF_PARAM_RESONANCE = 1;
export const SVF_PARAM_MODE = 2;

/** Valid parameter ids accepted by `svf_set_param`. */
export type SvfParamId =
  | typeof SVF_PARAM_CUTOFF
  | typeof SVF_PARAM_RESONANCE
  | typeof SVF_PARAM_MODE;

/** Filter modes: the discrete `mode` param buckets (lowpass/highpass/bandpass). */
export type SvfMode = 0 | 1 | 2;

/** Number of discrete `mode` buckets in the C++ param table. */
export const SVF_MODE_COUNT = 3;

export function isSvfParamId(id: number): id is SvfParamId {
  return id === SVF_PARAM_CUTOFF || id === SVF_PARAM_RESONANCE || id === SVF_PARAM_MODE;
}

/** Clamp a value into the normalized [0, 1] range the ABI expects. */
export function clampNormalized(value: number): number {
  if (Number.isNaN(value)) return 0;
  if (value < 0) return 0;
  if (value > 1) return 1;
  return value;
}

/**
 * Normalized value that selects a given discrete `mode`.
 *
 * The C++ `denormalize` quantizes a discrete param as `floor(norm * count)`
 * (clamped to `count - 1`). Picking the bucket CENTER `(mode + 0.5) / count`
 * lands squarely inside the target bucket, robust to float rounding.
 */
export function modeToNormalized(mode: SvfMode): number {
  return (mode + 0.5) / SVF_MODE_COUNT;
}

// --- Main-thread -> worklet message protocol --------------------------------

/** A single parameter update carried over the processor `port`. */
export interface SvfParamMessage {
  readonly type: "svf-param";
  readonly id: SvfParamId;
  readonly value: number;
}

/** Build a validated, clamped param message (main-thread side). */
export function makeParamMessage(id: SvfParamId, value: number): SvfParamMessage {
  if (!isSvfParamId(id)) {
    throw new RangeError(`invalid SVF param id: ${String(id)}`);
  }
  return { type: "svf-param", id, value: clampNormalized(value) };
}

/** Type guard for messages arriving on the worklet `port` (untrusted input). */
export function isSvfParamMessage(msg: unknown): msg is SvfParamMessage {
  if (typeof msg !== "object" || msg === null) return false;
  const m = msg as Record<string, unknown>;
  return (
    m["type"] === "svf-param" &&
    typeof m["id"] === "number" &&
    isSvfParamId(m["id"]) &&
    typeof m["value"] === "number"
  );
}

/** Options handed to the worklet processor via `processorOptions`. */
export interface SvfProcessorOptions {
  /** Compiled svf.wasm module (structured-cloned to the worklet). */
  readonly wasmModule: WebAssembly.Module;
  /** AudioContext sample rate; the WASM is `prepare`d with this. */
  readonly sampleRate: number;
  /** Max frames per `process()` block (AudioWorklet render quantum, typ. 128). */
  readonly maxBlockSize: number;
}

// --- Minimal WebAssembly runtime shim --------------------------------------
//
// svf.wasm imports exactly two host functions and EXPORTS its own memory, so we
// can instantiate it directly in the AudioWorklet global scope with no Emscripten
// JS glue (the glue is an ES module that relies on fetch/import — neither exists
// inside AudioWorkletGlobalScope). See svf-processor.ts for why this matters.
//
// The instantiation itself (import object + `WebAssembly.Instance`) is shared
// with the main-thread analysis engine (analysis/svf-analysis-wasm.ts, T031)
// via wasm/svf-wasm-runtime.ts — re-exported here so existing worklet callers
// (svf-processor.ts) and this module's own tests are unaffected.
import {
  instantiateSvfWasm,
  type SvfWasmExports,
} from "../wasm/svf-wasm-runtime.ts";

export { instantiateSvfWasm };
export type { SvfWasmExports };

// --- Allocation-free heap processor ----------------------------------------

/**
 * Owns one WASM filter handle + a pre-allocated scratch buffer in the wasm heap
 * and processes mono blocks in place. After construction, `process()` performs
 * NO heap allocation and NO wasm `malloc` (Principle VIII holds in the browser):
 *
 *  - the scratch heap region is `malloc`d once in the constructor,
 *  - the `HEAPF32` view is refreshed ONLY if the wasm memory grew (which never
 *    happens in the allocation-free audio path), so steady state is a no-op,
 *  - the input/output copies use `set()` / indexed writes (no new objects).
 */
export class SvfHeapProcessor {
  private readonly handle: number;
  private readonly scratchPtr: number;
  private readonly scratchIndex: number; // scratchPtr >> 2, into HEAPF32
  private heap: Float32Array;
  private disposed = false;

  constructor(
    private readonly wasm: SvfWasmExports,
    sampleRate: number,
    private readonly maxBlockSize: number,
  ) {
    if (!Number.isFinite(sampleRate) || sampleRate <= 0) {
      throw new RangeError(`invalid sampleRate: ${sampleRate}`);
    }
    if (!Number.isInteger(maxBlockSize) || maxBlockSize <= 0) {
      throw new RangeError(`invalid maxBlockSize: ${maxBlockSize}`);
    }
    this.handle = wasm.svf_create();
    wasm.svf_prepare(this.handle, sampleRate, maxBlockSize, 1);
    this.scratchPtr = wasm.malloc(maxBlockSize * 4);
    if (this.scratchPtr === 0) {
      wasm.svf_destroy(this.handle);
      throw new Error("svf.wasm: scratch malloc failed");
    }
    this.scratchIndex = this.scratchPtr >> 2;
    this.heap = new Float32Array(wasm.memory.buffer);
  }

  /** Publish a normalized parameter value to the filter. */
  setParam(id: SvfParamId, normalized: number): void {
    this.wasm.svf_set_param(this.handle, id, clampNormalized(normalized));
  }

  /**
   * Filter `input` (mono) into `output` (mono), block-by-block, allocation-free.
   * `count` frames are processed (default: the shorter of the two). Frames beyond
   * `input.length` are treated as silence; `count` must not exceed `maxBlockSize`.
   */
  process(input: Float32Array, output: Float32Array, count?: number): void {
    if (this.disposed) throw new Error("SvfHeapProcessor used after dispose");
    const n = count ?? Math.min(input.length, output.length);
    if (n > this.maxBlockSize) {
      throw new RangeError(`block ${n} exceeds maxBlockSize ${this.maxBlockSize}`);
    }
    if (n <= 0) return;

    // Refresh the heap view only if the wasm grew its memory (detaching the old
    // buffer). In the allocation-free audio path this never fires — steady state
    // is a cheap identity check, no allocation.
    if (this.heap.buffer !== this.wasm.memory.buffer) {
      this.heap = new Float32Array(this.wasm.memory.buffer);
    }
    const base = this.scratchIndex;
    const inLen = input.length;

    // Copy input -> wasm heap (no allocation: `set()` for the exact-length fast
    // path, indexed writes otherwise — never `subarray`, which allocates a view).
    if (inLen === n) {
      this.heap.set(input, base);
    } else {
      const copy = inLen < n ? inLen : n;
      for (let i = 0; i < copy; i++) this.heap[base + i] = input[i] ?? 0;
      for (let i = copy; i < n; i++) this.heap[base + i] = 0;
    }

    // Run the REAL SVF over the scratch block, in place.
    this.wasm.svf_process(this.handle, this.scratchPtr, n);

    // Copy result back out with an indexed loop (no `subarray`/`slice` object).
    for (let i = 0; i < n; i++) output[i] = this.heap[base + i] ?? 0;
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.wasm.free(this.scratchPtr);
    this.wasm.svf_destroy(this.handle);
  }
}

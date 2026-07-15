// Typed wrapper over the Emscripten SVF module. No `any`: the Emscripten surface
// we use is declared explicitly. A failed load throws (Principle VII — no
// silent fallback DSP).
interface EmscriptenSvf {
  _svf_create(): number;
  _svf_destroy(h: number): void;
  _svf_prepare(h: number, sampleRate: number, maxBlockSize: number, numChannels: number): void;
  _svf_set_param(h: number, paramId: number, normalized: number): void;
  _svf_process(h: number, samplesPtr: number, numSamples: number): void;
  _svf_render_impulse(h: number, outPtr: number, numSamples: number): void;
  _svf_get_frequency_response(h: number, freqsPtr: number, magsPtr: number, n: number): void;
  _svf_get_pole_zero(
    h: number,
    polesPtr: number,
    zerosPtr: number,
    gainPtr: number,
    countsPtr: number,
  ): void;
  _malloc(bytes: number): number;
  _free(ptr: number): void;
  HEAPF32: Float32Array;
  HEAP32: Int32Array;
}

type ModuleFactory = (opts?: Record<string, unknown>) => Promise<EmscriptenSvf>;

/** A complex value on the z-plane (re + j*im). */
export interface Complex {
  readonly re: number;
  readonly im: number;
}

/**
 * Poles/zeros of the 2nd-order system the SVF realizes for the current
 * cutoff/resonance/mode, plus the leading numerator gain so the transfer
 * function is fully recoverable: `H(z) = gain * prod(z - zero) / prod(z - pole)`.
 * All derived from the real DaisySP Svf difference equations (Principle VII);
 * validated against the measured `getFrequencyResponse` in the cross-validation
 * test. `mode`: 0=lowpass, 1=highpass, 2=bandpass.
 */
export interface PoleZero {
  readonly poles: Complex[];
  readonly zeros: Complex[];
  readonly gain: number;
  readonly mode: number;
}

export class SvfModule {
  private constructor(private readonly m: EmscriptenSvf) {}

  static async load(mjsUrl: string): Promise<SvfModule> {
    const mod = (await import(/* @vite-ignore */ mjsUrl)) as { default: ModuleFactory };
    const instance = await mod.default();
    return new SvfModule(instance);
  }

  create(): number { return this.m._svf_create(); }
  destroy(h: number): void { this.m._svf_destroy(h); }
  prepare(h: number, sampleRate: number, maxBlockSize: number, numChannels: number): void {
    this.m._svf_prepare(h, sampleRate, maxBlockSize, numChannels);
  }
  setParam(h: number, paramId: number, normalized: number): void {
    this.m._svf_set_param(h, paramId, normalized);
  }

  // In-place mono process: copies into the WASM heap, runs, copies back.
  process(h: number, samples: Float32Array): void {
    const n = samples.length;
    const ptr = this.m._malloc(n * 4);
    try {
      this.m.HEAPF32.set(samples, ptr >> 2);
      this.m._svf_process(h, ptr, n);
      samples.set(this.m.HEAPF32.subarray(ptr >> 2, (ptr >> 2) + n));
    } finally {
      this.m._free(ptr);
    }
  }

  // --- Analysis capability (computed by the real compiled SvfEffect) ---

  private static f32(heap: Float32Array, index: number): number {
    return heap[index] ?? 0;
  }
  private static i32(heap: Int32Array, index: number): number {
    return heap[index] ?? 0;
  }

  /** Unit-impulse response of the real filter, `n` samples. */
  renderImpulse(h: number, n: number): Float32Array {
    const ptr = this.m._malloc(n * 4);
    try {
      this.m._svf_render_impulse(h, ptr, n);
      // .slice copies out of the heap so the result survives the _free below.
      return this.m.HEAPF32.slice(ptr >> 2, (ptr >> 2) + n);
    } finally {
      this.m._free(ptr);
    }
  }

  /** MEASURED linear magnitude response |H(f)| at each requested frequency (Hz). */
  getFrequencyResponse(h: number, freqsHz: Float32Array): Float32Array {
    const n = freqsHz.length;
    const inPtr = this.m._malloc(n * 4);
    const outPtr = this.m._malloc(n * 4);
    try {
      this.m.HEAPF32.set(freqsHz, inPtr >> 2);
      this.m._svf_get_frequency_response(h, inPtr, outPtr, n);
      return this.m.HEAPF32.slice(outPtr >> 2, (outPtr >> 2) + n);
    } finally {
      this.m._free(inPtr);
      this.m._free(outPtr);
    }
  }

  /** Poles/zeros/gain/mode of the current filter setting (interleaved re/im in the ABI). */
  getPoleZero(h: number): PoleZero {
    // polesOut/zerosOut hold up to 2 complex (4 floats); countsOut >= 3 ints.
    const polesPtr = this.m._malloc(4 * 4);
    const zerosPtr = this.m._malloc(4 * 4);
    const gainPtr = this.m._malloc(4);
    const countsPtr = this.m._malloc(3 * 4);
    try {
      this.m._svf_get_pole_zero(h, polesPtr, zerosPtr, gainPtr, countsPtr);
      const numPoles = SvfModule.i32(this.m.HEAP32, countsPtr >> 2);
      const numZeros = SvfModule.i32(this.m.HEAP32, (countsPtr >> 2) + 1);
      const mode = SvfModule.i32(this.m.HEAP32, (countsPtr >> 2) + 2);
      const readComplex = (base: number, count: number): Complex[] => {
        const out: Complex[] = [];
        for (let i = 0; i < count; i++) {
          out.push({
            re: SvfModule.f32(this.m.HEAPF32, (base >> 2) + 2 * i),
            im: SvfModule.f32(this.m.HEAPF32, (base >> 2) + 2 * i + 1),
          });
        }
        return out;
      };
      return {
        poles: readComplex(polesPtr, numPoles),
        zeros: readComplex(zerosPtr, numZeros),
        gain: SvfModule.f32(this.m.HEAPF32, gainPtr >> 2),
        mode,
      };
    } finally {
      this.m._free(polesPtr);
      this.m._free(zerosPtr);
      this.m._free(gainPtr);
      this.m._free(countsPtr);
    }
  }
}

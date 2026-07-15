// Typed wrapper over the Emscripten SVF module. No `any`: the Emscripten surface
// we use is declared explicitly. A failed load throws (Principle VII — no
// silent fallback DSP).
interface EmscriptenSvf {
  _svf_create(): number;
  _svf_destroy(h: number): void;
  _svf_prepare(h: number, sampleRate: number, maxBlockSize: number, numChannels: number): void;
  _svf_set_param(h: number, paramId: number, normalized: number): void;
  _svf_process(h: number, samplesPtr: number, numSamples: number): void;
  _malloc(bytes: number): number;
  _free(ptr: number): void;
  HEAPF32: Float32Array;
}

type ModuleFactory = (opts?: Record<string, unknown>) => Promise<EmscriptenSvf>;

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
}

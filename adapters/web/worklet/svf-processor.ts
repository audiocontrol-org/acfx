// The SVF AudioWorkletProcessor — runs the REAL compiled SVF (svf.wasm) on the
// audio thread, block by block, allocation-free in `process()`.
//
// WASM-in-AudioWorklet approach (why it is reliable + allocation-free):
//
//   The Emscripten glue (svf.mjs) is an ES module that loads the wasm via
//   fetch/import — NEITHER exists in AudioWorkletGlobalScope, and the worklet
//   cannot `import` an ES module at all. So we bypass the glue entirely:
//   svf.wasm imports only two host functions and exports its own memory (see
//   svf-engine-core.ts), so the MAIN THREAD compiles the bytes to a
//   `WebAssembly.Module` and hands it to this processor via `processorOptions`
//   (structured-clone of a compiled module is supported into worklets). Here we
//   instantiate it SYNCHRONOUSLY with a tiny hand-written import object — no
//   async, no fetch, no glue. The scratch heap buffer is `malloc`d once at
//   construction; `process()` only copies samples and drains a pre-allocated
//   param queue, so it never allocates and never blocks (Principle VIII).
//
// This file is browser-only (AudioWorklet globals); it is not imported by the
// Node unit test. Its non-DOM logic lives in svf-engine-core.ts and is tested
// there. Live end-to-end playback is covered by the Phase 6 Playwright smoke.

import {
  instantiateSvfWasm,
  isSvfParamMessage,
  SvfHeapProcessor,
  SVF_PARAM_CUTOFF,
  SVF_PARAM_RESONANCE,
  SVF_PARAM_MODE,
  type SvfProcessorOptions,
} from "./svf-engine-core.ts";

// --- Ambient AudioWorklet global-scope declarations ------------------------
// lib.dom.d.ts covers the MAIN-thread AudioWorkletNode but not the worklet-scope
// base class / registrar. Declared minimally here (typed to our options — no
// `any`) since this module runs only inside AudioWorkletGlobalScope.
declare global {
  interface AudioWorkletProcessorImpl {
    readonly port: MessagePort;
    process(
      inputs: Float32Array[][],
      outputs: Float32Array[][],
      parameters: Record<string, Float32Array>,
    ): boolean;
  }
  const AudioWorkletProcessor: {
    new (options: { readonly processorOptions: SvfProcessorOptions }): AudioWorkletProcessorImpl;
  };
  function registerProcessor(
    name: string,
    ctor: new (options: {
      readonly processorOptions: SvfProcessorOptions;
    }) => AudioWorkletProcessorImpl,
  ): void;
}

const NUM_PARAMS = 3;

class SvfProcessor extends AudioWorkletProcessor {
  private readonly engine: SvfHeapProcessor;

  // Pre-allocated param queue drained inside process() — no allocation in the
  // hot path, mirrors the C++ effect's own pending-parameter pattern.
  private readonly pendingValue = new Float32Array(NUM_PARAMS);
  private readonly pendingDirty = new Uint8Array(NUM_PARAMS);

  constructor(options: { readonly processorOptions: SvfProcessorOptions }) {
    super(options);
    const { wasmModule, sampleRate, maxBlockSize } = options.processorOptions;
    const wasm = instantiateSvfWasm(wasmModule);
    this.engine = new SvfHeapProcessor(wasm, sampleRate, maxBlockSize);

    this.port.onmessage = (event: MessageEvent): void => {
      const msg: unknown = event.data;
      if (isSvfParamMessage(msg)) {
        this.pendingValue[msg.id] = msg.value;
        this.pendingDirty[msg.id] = 1;
      }
    };
  }

  override process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    // Drain pending param updates (allocation-free; fixed-size arrays).
    if (this.pendingDirty[SVF_PARAM_CUTOFF]) {
      this.engine.setParam(SVF_PARAM_CUTOFF, this.pendingValue[SVF_PARAM_CUTOFF] ?? 0);
      this.pendingDirty[SVF_PARAM_CUTOFF] = 0;
    }
    if (this.pendingDirty[SVF_PARAM_RESONANCE]) {
      this.engine.setParam(SVF_PARAM_RESONANCE, this.pendingValue[SVF_PARAM_RESONANCE] ?? 0);
      this.pendingDirty[SVF_PARAM_RESONANCE] = 0;
    }
    if (this.pendingDirty[SVF_PARAM_MODE]) {
      this.engine.setParam(SVF_PARAM_MODE, this.pendingValue[SVF_PARAM_MODE] ?? 0);
      this.pendingDirty[SVF_PARAM_MODE] = 0;
    }

    const output = outputs[0];
    if (output === undefined || output.length === 0) return true;
    const outChannel0 = output[0];
    if (outChannel0 === undefined) return true;
    const frames = outChannel0.length;

    // Mono filter (the ABI is single-channel): filter input channel 0 if
    // connected, else emit silence. Fan the result out to all output channels.
    const input = inputs[0];
    const inChannel0 = input !== undefined ? input[0] : undefined;

    if (inChannel0 !== undefined && inChannel0.length > 0) {
      this.engine.process(inChannel0, outChannel0, frames);
    } else {
      for (let i = 0; i < frames; i++) outChannel0[i] = 0;
    }

    for (let ch = 1; ch < output.length; ch++) {
      const dst = output[ch];
      if (dst === undefined) continue;
      for (let i = 0; i < frames; i++) dst[i] = outChannel0[i] ?? 0;
    }

    return true;
  }
}

registerProcessor("svf-processor", SvfProcessor);

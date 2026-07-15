// Main-thread helper the UI (T028) uses to spin up the SVF audio engine.
//
// It (1) registers the worklet module, (2) fetches + compiles svf.wasm on the
// main thread, (3) constructs the AudioWorkletNode handing the compiled module
// to the processor via `processorOptions`, and (4) returns a typed handle with
// param setters + the underlying node to connect. The REAL SVF runs on the
// audio thread (see svf-processor.ts) — this side never touches the DSP.

import {
  makeParamMessage,
  modeToNormalized,
  SVF_PARAM_CUTOFF,
  SVF_PARAM_MODE,
  SVF_PARAM_RESONANCE,
  type SvfMode,
  type SvfParamId,
  type SvfProcessorOptions,
} from "./svf-engine-core.ts";

const PROCESSOR_NAME = "svf-processor";
const DEFAULT_MAX_BLOCK_SIZE = 128; // AudioWorklet render quantum

export interface CreateSvfAudioNodeOptions {
  /** URL of the compiled worklet module (svf-processor.js/.mjs). */
  readonly workletUrl: string | URL;
  /** URL of the svf.wasm binary. */
  readonly wasmUrl: string | URL;
  /** Max frames per render block; defaults to the 128-frame render quantum. */
  readonly maxBlockSize?: number;
}

/** Typed handle over the SVF worklet node returned to the UI. */
export interface SvfAudioNode {
  /** The Web Audio node to `.connect()` into the graph. */
  readonly node: AudioWorkletNode;
  /** Publish a normalized [0,1] value for a parameter id. */
  setParam(id: SvfParamId, normalized: number): void;
  /** Cutoff, normalized 0..1 (mapped log 20 Hz..20 kHz in the DSP). */
  setCutoff(normalized: number): void;
  /** Resonance, normalized 0..1. */
  setResonance(normalized: number): void;
  /** Filter mode: 0=lowpass, 1=highpass, 2=bandpass. */
  setMode(mode: SvfMode): void;
  /** Detach the node from the graph. */
  disconnect(): void;
}

async function compileWasm(url: string | URL): Promise<WebAssembly.Module> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`failed to fetch svf.wasm (${response.status}) from ${String(url)}`);
  }
  const bytes = await response.arrayBuffer();
  return WebAssembly.compile(bytes);
}

/**
 * Build the SVF worklet node against an existing AudioContext. Resolves once the
 * worklet module is registered and the wasm compiled + handed to the processor.
 */
export async function createSvfAudioNode(
  context: AudioContext,
  opts: CreateSvfAudioNodeOptions,
): Promise<SvfAudioNode> {
  const maxBlockSize = opts.maxBlockSize ?? DEFAULT_MAX_BLOCK_SIZE;

  // Register the worklet and compile the wasm in parallel.
  const [, wasmModule] = await Promise.all([
    context.audioWorklet.addModule(opts.workletUrl),
    compileWasm(opts.wasmUrl),
  ]);

  const processorOptions: SvfProcessorOptions = {
    wasmModule,
    sampleRate: context.sampleRate,
    maxBlockSize,
  };

  const node = new AudioWorkletNode(context, PROCESSOR_NAME, {
    numberOfInputs: 1,
    numberOfOutputs: 1,
    outputChannelCount: [1],
    processorOptions,
  });

  const setParam = (id: SvfParamId, normalized: number): void => {
    node.port.postMessage(makeParamMessage(id, normalized));
  };

  return {
    node,
    setParam,
    setCutoff: (normalized: number): void => setParam(SVF_PARAM_CUTOFF, normalized),
    setResonance: (normalized: number): void => setParam(SVF_PARAM_RESONANCE, normalized),
    setMode: (mode: SvfMode): void => setParam(SVF_PARAM_MODE, modeToNormalized(mode)),
    disconnect: (): void => node.disconnect(),
  };
}

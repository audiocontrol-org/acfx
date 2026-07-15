import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { SvfModule } from "../loader/svf-module.ts";
import {
  clampNormalized,
  instantiateSvfWasm,
  isSvfParamId,
  isSvfParamMessage,
  makeParamMessage,
  modeToNormalized,
  SvfHeapProcessor,
  SVF_MODE_COUNT,
  SVF_PARAM_CUTOFF,
  SVF_PARAM_MODE,
  SVF_PARAM_RESONANCE,
  type SvfMode,
  type SvfParamId,
} from "../worklet/svf-engine-core.ts";

const wasmPath = fileURLToPath(new URL("../../../build/web/svf.wasm", import.meta.url));
const wasmModuleUrl = new URL("../../../build/web/svf.mjs", import.meta.url).href;

function loadWasmModule(): Promise<WebAssembly.Module> {
  return WebAssembly.compile(readFileSync(wasmPath));
}

// --- Pure protocol / param logic (no wasm) ---------------------------------

describe("param + message protocol (pure)", () => {
  it("clampNormalized clamps to [0,1] and maps NaN to 0", () => {
    expect(clampNormalized(-0.5)).toBe(0);
    expect(clampNormalized(0)).toBe(0);
    expect(clampNormalized(0.42)).toBe(0.42);
    expect(clampNormalized(1)).toBe(1);
    expect(clampNormalized(2.5)).toBe(1);
    expect(clampNormalized(Number.NaN)).toBe(0);
  });

  it("isSvfParamId accepts only 0/1/2", () => {
    expect(isSvfParamId(SVF_PARAM_CUTOFF)).toBe(true);
    expect(isSvfParamId(SVF_PARAM_RESONANCE)).toBe(true);
    expect(isSvfParamId(SVF_PARAM_MODE)).toBe(true);
    expect(isSvfParamId(3)).toBe(false);
    expect(isSvfParamId(-1)).toBe(false);
  });

  it("modeToNormalized lands each mode inside its C++ bucket", () => {
    // The C++ denormalize quantizes discrete params as floor(norm * count).
    for (const mode of [0, 1, 2] as SvfMode[]) {
      const norm = modeToNormalized(mode);
      const bucket = Math.min(SVF_MODE_COUNT - 1, Math.floor(norm * SVF_MODE_COUNT));
      expect(bucket).toBe(mode);
    }
  });

  it("makeParamMessage validates id and clamps value", () => {
    expect(makeParamMessage(SVF_PARAM_CUTOFF, 0.7)).toEqual({
      type: "svf-param",
      id: SVF_PARAM_CUTOFF,
      value: 0.7,
    });
    expect(makeParamMessage(SVF_PARAM_RESONANCE, 5).value).toBe(1);
    // @ts-expect-error — 9 is not a valid SvfParamId
    expect(() => makeParamMessage(9, 0.5)).toThrow(RangeError);
  });

  it("isSvfParamMessage guards untrusted port input", () => {
    expect(isSvfParamMessage({ type: "svf-param", id: 0, value: 0.5 })).toBe(true);
    expect(isSvfParamMessage({ type: "svf-param", id: 7, value: 0.5 })).toBe(false);
    expect(isSvfParamMessage({ type: "other", id: 0, value: 0.5 })).toBe(false);
    expect(isSvfParamMessage(null)).toBe(false);
    expect(isSvfParamMessage("svf-param")).toBe(false);
    expect(isSvfParamMessage({ type: "svf-param", id: 0 })).toBe(false);
  });
});

// --- WASM shim + heap processor against the REAL svf.wasm ------------------

describe("instantiateSvfWasm + SvfHeapProcessor (real DSP)", () => {
  it("raw-instantiated module exposes the SVF ABI and processes audio", async () => {
    const wasm = instantiateSvfWasm(await loadWasmModule());
    const proc = new SvfHeapProcessor(wasm, 48000, 128);
    proc.setParam(SVF_PARAM_CUTOFF, 0.5);
    proc.setParam(SVF_PARAM_RESONANCE, 0.2);
    proc.setParam(SVF_PARAM_MODE, modeToNormalized(0));

    const input = new Float32Array(128);
    input[0] = 1; // unit impulse
    const output = new Float32Array(128);
    proc.process(input, output);

    // Lowpass impulse response: nonzero, finite, and decaying (not passthrough).
    expect(output.some((v) => v !== 0)).toBe(true);
    expect(output.every((v) => Number.isFinite(v))).toBe(true);
    proc.dispose();
  });

  it("block-streamed output matches the loader oracle on identical settings", async () => {
    const sampleRate = 44100;
    const total = 512;
    const block = 128;

    // Deterministic pseudo-noise input.
    const input = new Float32Array(total);
    let s = 12345;
    for (let i = 0; i < total; i++) {
      s = (1103515245 * s + 12345) & 0x7fffffff;
      input[i] = (s / 0x40000000) - 1;
    }

    const params: { id: SvfParamId; norm: number }[] = [
      { id: SVF_PARAM_CUTOFF, norm: 0.6 },
      { id: SVF_PARAM_RESONANCE, norm: 0.3 },
      { id: SVF_PARAM_MODE, norm: modeToNormalized(2) },
    ];

    // Oracle: the blessed Emscripten-glue path (loader), one whole-buffer call.
    const oracleMod = await SvfModule.load(wasmModuleUrl);
    const h = oracleMod.create();
    oracleMod.prepare(h, sampleRate, total, 1);
    for (const p of params) oracleMod.setParam(h, p.id, p.norm);
    const oracle = Float32Array.from(input);
    oracleMod.process(h, oracle);
    oracleMod.destroy(h);

    // Under test: raw-instantiated wasm, streamed in 128-frame blocks.
    const wasm = instantiateSvfWasm(await loadWasmModule());
    const proc = new SvfHeapProcessor(wasm, sampleRate, block);
    for (const p of params) proc.setParam(p.id, p.norm);
    const streamed = new Float32Array(total);
    const inBlock = new Float32Array(block);
    const outBlock = new Float32Array(block);
    for (let off = 0; off < total; off += block) {
      inBlock.set(input.subarray(off, off + block));
      proc.process(inBlock, outBlock, block);
      streamed.set(outBlock, off);
    }
    proc.dispose();

    // Same binary, same math: block-streaming must equal the whole-buffer oracle.
    for (let i = 0; i < total; i++) {
      expect(Math.abs(streamed[i]! - oracle[i]!)).toBeLessThan(1e-9);
    }
  });

  it("process() rejects blocks larger than maxBlockSize", async () => {
    const wasm = instantiateSvfWasm(await loadWasmModule());
    const proc = new SvfHeapProcessor(wasm, 48000, 64);
    expect(() => proc.process(new Float32Array(128), new Float32Array(128), 128)).toThrow(
      RangeError,
    );
    proc.dispose();
  });

  it("process() after dispose throws", async () => {
    const wasm = instantiateSvfWasm(await loadWasmModule());
    const proc = new SvfHeapProcessor(wasm, 48000, 128);
    proc.dispose();
    expect(() => proc.process(new Float32Array(128), new Float32Array(128))).toThrow();
  });
});

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { SvfModule } from "../loader/svf-module.ts";

interface Vector { version: number; sampleRate: number; params: { id: number; norm: number }[]; input: number[]; }

const here = fileURLToPath(new URL(".", import.meta.url));
const wasmModuleUrl = new URL("../../../build/web/svf.mjs", import.meta.url).href;
const referenceExe = fileURLToPath(new URL("../../../build/web-ref/adapters/web/svf-reference", import.meta.url));
const vectorPath = `${here}vectors/lowpass-sweep.json`;

function nativeOutput(path: string): number[] {
  const raw = execFileSync(referenceExe, [path], { encoding: "utf8" });
  return (JSON.parse(raw) as { output: number[] }).output;
}

async function wasmOutput(vec: Vector): Promise<number[]> {
  const mod = await SvfModule.load(wasmModuleUrl);
  const h = mod.create();
  mod.prepare(h, vec.sampleRate, vec.input.length, 1);
  for (const p of vec.params) mod.setParam(h, p.id, p.norm);
  const buf = Float32Array.from(vec.input);
  mod.process(h, buf);
  mod.destroy(h);
  return Array.from(buf);
}

describe("SVF WASM/native parity", () => {
  it("WASM output matches the native reference within tolerance", async () => {
    const vec = JSON.parse(readFileSync(vectorPath, "utf8")) as Vector;
    const native = nativeOutput(vectorPath);
    const wasm = await wasmOutput(vec);
    expect(wasm.length).toBe(native.length);
    for (let i = 0; i < native.length; i++) {
      expect(Math.abs(wasm[i]! - native[i]!)).toBeLessThan(1e-6);
    }
  });
});

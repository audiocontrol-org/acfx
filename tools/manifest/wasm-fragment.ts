// T011 -- WASM fragment producer.
//
// Reads the built `build/web/svf.wasm` (produced by `make web-wasm`, out of
// scope to modify here), content-hashes it, and emits `wasm.fragment.json`
// describing it as a single `wasm`-kind AssetEntry per
// specs/svf-training-site/contracts/lesson-asset-manifest.md. The
// AudioWorklet + `analysis` capability arrive in a later phase; this
// fragment lists only the audio-capable `svf.wasm` for now.
//
// This is a producer, not the manifest writer: it emits ITS fragment only.
// `assemble.ts` is the sole writer of the committed manifest.

import { createHash } from "node:crypto";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

import { computeSourceProvenance } from "./provenance.js";
import type { Capability, Fragment, FragmentAssetEntry } from "./types.js";

interface Args {
  readonly wasmPath: string;
  readonly outDir: string;
}

function parseArgs(argv: readonly string[]): Args {
  const flags = new Map<string, string>();
  for (const arg of argv) {
    const match = /^--([^=]+)=(.*)$/.exec(arg);
    const key = match?.[1];
    const value = match?.[2];
    if (key !== undefined && value !== undefined) {
      flags.set(key, value);
    }
  }
  return {
    wasmPath: flags.get("wasm") ?? "build/web/svf.wasm",
    outDir: flags.get("out") ?? "build/web",
  };
}

function sha256File(path: string): string {
  const data = readFileSync(path);
  return createHash("sha256").update(data).digest("hex");
}

// Exported for straightforward unit testing / reuse; `main()` below is the
// CLI entry point that resolves paths and writes the fragment file.
// Advertise only the capabilities the wasm ACTUALLY exports (detected from the
// module's export table) — so the fragment never claims a capability the binary
// lacks (e.g. before the analysis ABI was compiled in).
function detectCapabilities(wasmPath: string): Capability[] {
  const mod = new WebAssembly.Module(readFileSync(wasmPath));
  const exports = new Set(WebAssembly.Module.exports(mod).map((e) => e.name));
  const caps: Capability[] = [];
  if (exports.has("svf_process")) caps.push("audio");
  if (exports.has("svf_get_pole_zero")) caps.push("analysis");
  if (caps.length === 0) {
    throw new Error(`${wasmPath} exports no recognized SVF capability (svf_process/svf_get_pole_zero)`);
  }
  return caps;
}

export function buildWasmFragment(wasmPath: string, sourceProvenance: string): Fragment {
  const sha256 = sha256File(wasmPath);
  const entry: FragmentAssetEntry = {
    kind: "wasm",
    path: `svf.${sha256.slice(0, 8)}.wasm`,
    sha256,
    contentType: "application/wasm",
    capabilities: detectCapabilities(wasmPath),
    capabilityVersion: 1,
    provenance: `tools/manifest/wasm-fragment@${sourceProvenance}`,
  };
  return { sourceProvenance, assets: [entry] };
}

function main(): void {
  const args = parseArgs(process.argv.slice(2));
  const cwd = process.cwd();
  const wasmPath = resolve(cwd, args.wasmPath);
  const sourceProvenance = computeSourceProvenance(cwd);
  const fragment = buildWasmFragment(wasmPath, sourceProvenance);

  const outDir = resolve(cwd, args.outDir);
  mkdirSync(outDir, { recursive: true });
  const outPath = resolve(outDir, "wasm.fragment.json");
  writeFileSync(outPath, `${JSON.stringify(fragment, null, 2)}\n`, "utf8");
  process.stdout.write(`wrote ${outPath}\n`);
}

main();

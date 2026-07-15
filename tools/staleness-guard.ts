// T013 -- non-building staleness guard (FR-012).
//
// Compares the committed manifest's `sourceProvenance` (`specs/svf-training-site/
// contracts/lesson-asset-manifest.md`) against the CURRENT `core/` +
// `adapters/web` source-tree hashes, computed the exact same way the two
// fragment producers compute it (`tools/manifest/provenance.ts`, mirrored in
// `tools/lesson-assets/asset-tool-main.cpp`). Pure hash comparison -- NO
// compile, NO build: this is the "non-building" guard.
//
// Exit 0: the manifest still describes the current DSP source ("assets
// current"). Exit 1: `core/` or `adapters/web` changed since the assets
// backing `svf.json` were built -- rebuild + republish (`make lesson-assets`).

import { readFileSync } from "node:fs";
import { resolve } from "node:path";

import { computeSourceProvenance } from "./manifest/provenance.js";

interface Args {
  readonly manifestPath: string;
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
    manifestPath: flags.get("manifest") ?? "site/public/manifest/svf.json",
  };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

// Reads just the one field this guard needs. Deliberately not the full
// `LessonAssetManifest` parser (`manifest/types.ts` parses `Fragment`
// shapes, whose `assets[].path` differs from the manifest's `assets[].url`)
// -- this guard only ever reads `sourceProvenance`.
export function readManifestSourceProvenance(path: string): string {
  let raw: string;
  try {
    raw = readFileSync(path, "utf8");
  } catch (err) {
    throw new Error(`failed to read manifest ${path}: ${String(err)}`);
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    throw new Error(`failed to parse JSON at ${path}: ${String(err)}`);
  }
  if (!isRecord(parsed)) {
    throw new Error(`${path}: manifest root is not an object`);
  }
  const value = parsed["sourceProvenance"];
  if (typeof value !== "string") {
    throw new Error(`${path}: "sourceProvenance" is not a string`);
  }
  return value;
}

export function checkStaleness(
  manifestProvenance: string,
  currentProvenance: string,
): { readonly current: boolean; readonly message: string } {
  if (manifestProvenance === currentProvenance) {
    return { current: true, message: `staleness-guard: assets current (sourceProvenance=${currentProvenance})` };
  }
  return {
    current: false,
    message:
      "staleness-guard: core/ or adapters/web changed since assets were built -- rebuild+republish " +
      "(`make lesson-assets`).\n" +
      `  manifest sourceProvenance = ${manifestProvenance}\n` +
      `  current  sourceProvenance = ${currentProvenance}`,
  };
}

function main(): void {
  const args = parseArgs(process.argv.slice(2));
  const cwd = process.cwd();
  const manifestPath = resolve(cwd, args.manifestPath);

  const manifestProvenance = readManifestSourceProvenance(manifestPath);
  const currentProvenance = computeSourceProvenance(cwd);
  const result = checkStaleness(manifestProvenance, currentProvenance);

  if (result.current) {
    process.stdout.write(`${result.message}\n`);
    return;
  }
  process.stderr.write(`${result.message}\n`);
  process.exitCode = 1;
}

main();

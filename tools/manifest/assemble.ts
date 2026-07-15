// T012 -- Manifest assembler. SOLE writer of the committed
// `site/public/manifest/svf.json` per
// specs/svf-training-site/contracts/lesson-asset-manifest.md.
//
// Reads BOTH producer fragments (`static.fragment.json` from the native
// `tools/lesson-assets` host tool, `wasm.fragment.json` from
// `wasm-fragment.ts`), validates them, and writes the one authoritative
// `LessonAssetManifest`. Two producers, one writer -- no races, and this
// script never has a second writer for `svf.json`.
//
// CDN_BASE: the Cloudflare Worker CDN (contracts/cdn-worker.md) is not
// deployed until Phase 3. Until then this defaults to the documented
// eventual worker origin, `https://audiocontrol-acfx-cdn.oletizi.workers.dev`
// -- a later Phase-3 task (T019) re-runs this assembler with the confirmed
// CDN_BASE once the worker is live. Override via `--cdn-base=<url>` or the
// `CDN_BASE` environment variable.

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";

import type { AssetEntry, Fragment, FragmentAssetEntry, LessonAssetManifest } from "./types.js";
import { parseFragment } from "./types.js";

const DEFAULT_CDN_BASE = "https://audiocontrol-acfx-cdn.oletizi.workers.dev";

interface Args {
  readonly staticFragmentPath: string;
  readonly wasmFragmentPath: string;
  readonly outPath: string;
  readonly cdnBase: string;
}

function parseArgs(argv: readonly string[], env: NodeJS.ProcessEnv): Args {
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
    staticFragmentPath: flags.get("static") ?? "build/lesson-assets/svf-out/static.fragment.json",
    wasmFragmentPath: flags.get("wasm") ?? "build/web/wasm.fragment.json",
    outPath: flags.get("out") ?? "site/public/manifest/svf.json",
    cdnBase: flags.get("cdn-base") ?? env["CDN_BASE"] ?? DEFAULT_CDN_BASE,
  };
}

function readFragment(path: string, label: string): Fragment {
  let raw: string;
  try {
    raw = readFileSync(path, "utf8");
  } catch (err) {
    throw new Error(`${label}: failed to read ${path}: ${String(err)}`);
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    throw new Error(`${label}: failed to parse JSON at ${path}: ${String(err)}`);
  }
  return parseFragment(parsed, label);
}

function toAssetEntry(entry: FragmentAssetEntry, cdnBase: string): AssetEntry {
  return {
    kind: entry.kind,
    url: `${cdnBase}/${entry.path}`,
    sha256: entry.sha256,
    contentType: entry.contentType,
    capabilities: entry.capabilities,
    capabilityVersion: entry.capabilityVersion,
    params: entry.params,
    sampleRate: entry.sampleRate,
    provenance: entry.provenance,
  };
}

// Fails loud when the two fragments disagree on sourceProvenance: that means
// the wasm and static assets were built from different source trees, and
// silently picking one would produce a manifest that lies about what it
// describes.
function reconcileSourceProvenance(staticFragment: Fragment, wasmFragment: Fragment): string {
  if (staticFragment.sourceProvenance !== wasmFragment.sourceProvenance) {
    throw new Error(
      "sourceProvenance mismatch between fragments: " +
        `static.fragment.json=${staticFragment.sourceProvenance} ` +
        `wasm.fragment.json=${wasmFragment.sourceProvenance} -- ` +
        "the wasm and static assets were built from different source trees. " +
        "Rebuild both producers from the same commit before assembling.",
    );
  }
  return staticFragment.sourceProvenance;
}

export function assemble(staticFragment: Fragment, wasmFragment: Fragment, cdnBase: string): LessonAssetManifest {
  const sourceProvenance = reconcileSourceProvenance(staticFragment, wasmFragment);
  const assets: AssetEntry[] = [
    ...wasmFragment.assets.map((entry) => toAssetEntry(entry, cdnBase)),
    ...staticFragment.assets.map((entry) => toAssetEntry(entry, cdnBase)),
  ];
  return { version: 1, lesson: "svf", sourceProvenance, assets };
}

function main(): void {
  const args = parseArgs(process.argv.slice(2), process.env);
  const cwd = process.cwd();

  const staticFragment = readFragment(resolve(cwd, args.staticFragmentPath), "static.fragment.json");
  const wasmFragment = readFragment(resolve(cwd, args.wasmFragmentPath), "wasm.fragment.json");
  const manifest = assemble(staticFragment, wasmFragment, args.cdnBase);

  const outPath = resolve(cwd, args.outPath);
  mkdirSync(dirname(outPath), { recursive: true });
  writeFileSync(outPath, `${JSON.stringify(manifest, null, 2)}\n`, "utf8");
  process.stdout.write(`wrote ${outPath} (${manifest.assets.length} assets, cdnBase=${args.cdnBase})\n`);
}

main();

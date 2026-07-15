// Lesson-asset manifest — build/SSR-time (Node) reader.
//
// Contract: specs/svf-training-site/contracts/lesson-asset-manifest.md
// Reads + validates the committed `site/public/manifest/svf.json`, written
// by the single assembler described in the contract. This module owns the
// only `node:fs` access; the types + pure validation/accessors it wraps live
// in `./types.ts` so browser-side client islands can reuse them without
// pulling Node APIs into a client bundle.
//
// Fails loud (throws) on a missing file, malformed JSON, a manifest that
// doesn't match the contract shape, or a requested asset that isn't present
// — no silent fallback (acfx Principle VII).

import { readFileSync } from 'node:fs';

import {
  type AssetEntry,
  type AssetKind,
  type ArtifactCapability,
  type LessonAssetManifest,
  ManifestValidationError,
  assetsByKind as pureAssetsByKind,
  assetUrl as pureAssetUrl,
  parseManifest,
  wasmAsset as pureWasmAsset,
} from './types';

export type { AssetEntry, AssetKind, ArtifactCapability, LessonAssetManifest };
export { ManifestValidationError };

/**
 * `site/public/manifest/svf.json` resolved relative to this module (not to
 * `process.cwd()`), so `loadManifest()` works the same from `astro dev`,
 * `astro build`, and `astro check` regardless of invocation directory.
 */
const DEFAULT_MANIFEST_URL = new URL('../../../public/manifest/svf.json', import.meta.url);

let cachedManifest: LessonAssetManifest | undefined;

/**
 * Load + validate the lesson-asset manifest. Memoized for the default path
 * (one manifest per build/SSR process); an explicit `manifestPath` bypasses
 * the cache, mainly for tests.
 */
export function loadManifest(manifestPath?: string | URL): LessonAssetManifest {
  const usesDefault = manifestPath === undefined;
  if (usesDefault && cachedManifest !== undefined) {
    return cachedManifest;
  }
  const target = manifestPath ?? DEFAULT_MANIFEST_URL;

  let raw: string;
  try {
    raw = readFileSync(target, 'utf-8');
  } catch (error) {
    throw new Error(
      `lesson-asset manifest not found at ${String(target)}: ${error instanceof Error ? error.message : String(error)}`,
    );
  }

  let parsedJson: unknown;
  try {
    parsedJson = JSON.parse(raw);
  } catch (error) {
    throw new Error(
      `lesson-asset manifest at ${String(target)} is not valid JSON: ${error instanceof Error ? error.message : String(error)}`,
    );
  }

  const manifest = parseManifest(parsedJson);
  if (usesDefault) {
    cachedManifest = manifest;
  }
  return manifest;
}

/** Every asset entry of the given kind from the loaded manifest, in manifest order. */
export function assetsByKind(kind: AssetKind): readonly AssetEntry[] {
  return pureAssetsByKind(loadManifest(), kind);
}

/** The single audio-capable `wasm` asset. Throws if the manifest has none. */
export function wasmAsset(): AssetEntry {
  return pureWasmAsset(loadManifest());
}

/** The URL of the `index`-th asset of `kind`. Throws if absent. */
export function assetUrl(kind: AssetKind, index = 0): string {
  return pureAssetUrl(loadManifest(), kind, index);
}

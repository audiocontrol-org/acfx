// Lesson-asset manifest — types + pure validation/accessors.
//
// Contract: specs/svf-training-site/contracts/lesson-asset-manifest.md
//
// This module is deliberately free of `node:fs` (and any other Node-only
// API) so it can be imported by browser-side client islands that fetch
// `/manifest/svf.json` themselves (it is a static asset under `public/`) and
// want the same types + the same field-by-field validation the build/SSR
// reader (`./manifest.ts`) uses. Never trust a cast — parse `unknown` into
// these types by checking every field.

/** Asset kinds a lesson-asset manifest entry may declare. */
export const ASSET_KINDS = [
  'wasm',
  'worklet',
  'audio',
  'response',
  'pole-zero',
  'impulse',
] as const;
export type AssetKind = (typeof ASSET_KINDS)[number];

/** Capability tags an asset (today: only `wasm`) may advertise. */
export const ARTIFACT_CAPABILITIES = ['audio', 'analysis'] as const;
export type ArtifactCapability = (typeof ARTIFACT_CAPABILITIES)[number];

export interface AssetEntry {
  readonly kind: AssetKind;
  /** Absolute, immutable, content-hashed Cloudflare CDN URL. */
  readonly url: string;
  readonly sha256: string;
  readonly contentType: string;
  readonly capabilities?: readonly ArtifactCapability[];
  readonly capabilityVersion?: number;
  readonly params?: Readonly<Record<string, number>>;
  readonly sampleRate?: number;
  /** Producer identifier + source hash, e.g. "tools/lesson-assets@<hash>". */
  readonly provenance: string;
}

export interface LessonAssetManifest {
  readonly version: 1;
  readonly lesson: 'svf';
  /** git hash of core/ + adapters/web at build time (staleness guard). */
  readonly sourceProvenance: string;
  readonly assets: readonly AssetEntry[];
}

/** Thrown for any structurally-invalid manifest — fail loud, no fallback. */
export class ManifestValidationError extends Error {
  constructor(message: string) {
    super(`invalid lesson-asset manifest: ${message}`);
    this.name = 'ManifestValidationError';
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isAssetKind(value: unknown): value is AssetKind {
  return typeof value === 'string' && (ASSET_KINDS as readonly string[]).includes(value);
}

function isArtifactCapability(value: unknown): value is ArtifactCapability {
  return typeof value === 'string' && (ARTIFACT_CAPABILITIES as readonly string[]).includes(value);
}

function requireNonEmptyString(value: unknown, field: string, context: string): string {
  if (typeof value !== 'string' || value.length === 0) {
    throw new ManifestValidationError(`${context}.${field} must be a non-empty string, got ${JSON.stringify(value)}`);
  }
  return value;
}

function parseCapabilities(value: unknown, context: string): readonly ArtifactCapability[] | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!Array.isArray(value) || !value.every(isArtifactCapability)) {
    throw new ManifestValidationError(
      `${context}.capabilities must be an array of ${ARTIFACT_CAPABILITIES.join('|')}, got ${JSON.stringify(value)}`,
    );
  }
  return value;
}

function parseCapabilityVersion(value: unknown, context: string): number | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== 'number') {
    throw new ManifestValidationError(`${context}.capabilityVersion must be a number, got ${JSON.stringify(value)}`);
  }
  return value;
}

function parseParams(value: unknown, context: string): Readonly<Record<string, number>> | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!isRecord(value)) {
    throw new ManifestValidationError(`${context}.params must be an object, got ${JSON.stringify(value)}`);
  }
  const parsed: Record<string, number> = {};
  for (const [key, entryValue] of Object.entries(value)) {
    if (typeof entryValue !== 'number') {
      throw new ManifestValidationError(`${context}.params.${key} must be a number, got ${JSON.stringify(entryValue)}`);
    }
    parsed[key] = entryValue;
  }
  return parsed;
}

function parseSampleRate(value: unknown, context: string): number | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== 'number') {
    throw new ManifestValidationError(`${context}.sampleRate must be a number, got ${JSON.stringify(value)}`);
  }
  return value;
}

function parseAssetEntry(value: unknown, index: number): AssetEntry {
  const context = `assets[${index}]`;
  if (!isRecord(value)) {
    throw new ManifestValidationError(`${context} must be an object, got ${JSON.stringify(value)}`);
  }
  if (!isAssetKind(value.kind)) {
    throw new ManifestValidationError(`${context}.kind must be one of ${ASSET_KINDS.join('|')}, got ${JSON.stringify(value.kind)}`);
  }
  return {
    kind: value.kind,
    url: requireNonEmptyString(value.url, 'url', context),
    sha256: requireNonEmptyString(value.sha256, 'sha256', context),
    contentType: requireNonEmptyString(value.contentType, 'contentType', context),
    capabilities: parseCapabilities(value.capabilities, context),
    capabilityVersion: parseCapabilityVersion(value.capabilityVersion, context),
    params: parseParams(value.params, context),
    sampleRate: parseSampleRate(value.sampleRate, context),
    provenance: requireNonEmptyString(value.provenance, 'provenance', context),
  };
}

/**
 * Parse + validate an already-JSON.parsed value into a `LessonAssetManifest`.
 * Never casts — every field is checked. Throws `ManifestValidationError`
 * (with a field path) on the first structural problem found.
 */
export function parseManifest(value: unknown): LessonAssetManifest {
  if (!isRecord(value)) {
    throw new ManifestValidationError(`root must be an object, got ${JSON.stringify(value)}`);
  }
  if (value.version !== 1) {
    throw new ManifestValidationError(`version must be 1, got ${JSON.stringify(value.version)}`);
  }
  if (value.lesson !== 'svf') {
    throw new ManifestValidationError(`lesson must be "svf", got ${JSON.stringify(value.lesson)}`);
  }
  const sourceProvenance = requireNonEmptyString(value.sourceProvenance, 'sourceProvenance', 'root');
  if (!Array.isArray(value.assets)) {
    throw new ManifestValidationError(`assets must be an array, got ${JSON.stringify(value.assets)}`);
  }
  const assets = value.assets.map((entry: unknown, index: number) => parseAssetEntry(entry, index));
  if (assets.length === 0) {
    throw new ManifestValidationError('assets must not be empty');
  }
  return { version: 1, lesson: 'svf', sourceProvenance, assets };
}

/** Pure accessor: every asset entry of the given kind, in manifest order. */
export function assetsByKind(manifest: LessonAssetManifest, kind: AssetKind): readonly AssetEntry[] {
  return manifest.assets.filter((entry) => entry.kind === kind);
}

/** Pure accessor: the single audio-capable `wasm` asset. Throws if absent. */
export function wasmAsset(manifest: LessonAssetManifest): AssetEntry {
  const candidate = assetsByKind(manifest, 'wasm').find((entry) => entry.capabilities?.includes('audio') === true);
  if (candidate === undefined) {
    throw new ManifestValidationError('no audio-capable "wasm" asset found');
  }
  return candidate;
}

/** Pure accessor: the URL of the `index`-th asset of `kind`. Throws if absent. */
export function assetUrl(manifest: LessonAssetManifest, kind: AssetKind, index = 0): string {
  const entry = assetsByKind(manifest, kind)[index];
  if (entry === undefined) {
    throw new ManifestValidationError(`no "${kind}" asset at index ${index}`);
  }
  return entry.url;
}

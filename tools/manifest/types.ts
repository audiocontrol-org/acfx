// Shared shapes for the manifest pipeline, mirroring
// specs/svf-training-site/contracts/lesson-asset-manifest.md.
//
// Two producers (the native `tools/lesson-assets` host tool and this
// package's `wasm-fragment` script) each write ONE fragment file shaped
// like `Fragment` below (`{ sourceProvenance, assets: FragmentAssetEntry[] }`,
// with a `path` relative to the producer's own output directory). The
// assembler (`assemble.ts`) is the SOLE writer of the committed
// `LessonAssetManifest`, resolving each fragment asset's `path` to an
// absolute, content-hashed CDN `url`.
//
// All fragment/manifest JSON is read as `unknown` and validated with the
// parse functions below -- never cast or trusted blindly (acfx Principle IX:
// strict typing, no `any`, no unchecked casts).

export type AssetKind = "wasm" | "worklet" | "audio" | "response" | "pole-zero" | "impulse";

const ASSET_KINDS: readonly AssetKind[] = ["wasm", "worklet", "audio", "response", "pole-zero", "impulse"];

export type Capability = "audio" | "analysis";

export interface FragmentAssetEntry {
  readonly kind: AssetKind;
  readonly path: string;
  readonly sha256: string;
  readonly contentType: string;
  readonly capabilities?: readonly Capability[];
  readonly capabilityVersion?: number;
  readonly params?: Readonly<Record<string, number>>;
  readonly sampleRate?: number;
  readonly provenance: string;
}

export interface Fragment {
  readonly sourceProvenance: string;
  readonly assets: readonly FragmentAssetEntry[];
}

export interface AssetEntry {
  readonly kind: AssetKind;
  readonly url: string;
  readonly sha256: string;
  readonly contentType: string;
  readonly capabilities?: readonly Capability[];
  readonly capabilityVersion?: number;
  readonly params?: Readonly<Record<string, number>>;
  readonly sampleRate?: number;
  readonly provenance: string;
}

export interface LessonAssetManifest {
  readonly version: 1;
  readonly lesson: "svf";
  readonly sourceProvenance: string;
  readonly assets: readonly AssetEntry[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function assertString(value: unknown, field: string): string {
  if (typeof value !== "string") {
    throw new Error(`expected string for "${field}", got ${typeof value}`);
  }
  return value;
}

function assertOptionalNumber(value: unknown, field: string): number | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "number") {
    throw new Error(`expected number for "${field}", got ${typeof value}`);
  }
  return value;
}

function assertAssetKind(value: unknown, field: string): AssetKind {
  const kind = assertString(value, field);
  if (!(ASSET_KINDS as readonly string[]).includes(kind)) {
    throw new Error(`invalid asset kind "${kind}" for "${field}" (expected one of ${ASSET_KINDS.join(", ")})`);
  }
  return kind as AssetKind;
}

function assertOptionalCapabilities(value: unknown, field: string): readonly Capability[] | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!Array.isArray(value)) {
    throw new Error(`expected array for "${field}"`);
  }
  return (value as readonly unknown[]).map((entry, index) => {
    if (entry !== "audio" && entry !== "analysis") {
      throw new Error(`invalid capability "${String(entry)}" at "${field}[${index}]"`);
    }
    return entry;
  });
}

function assertOptionalParams(value: unknown, field: string): Readonly<Record<string, number>> | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!isRecord(value)) {
    throw new Error(`expected object for "${field}"`);
  }
  const out: Record<string, number> = {};
  for (const [key, entry] of Object.entries(value)) {
    if (typeof entry !== "number") {
      throw new Error(`expected number for "${field}.${key}"`);
    }
    out[key] = entry;
  }
  return out;
}

export function parseFragmentAssetEntry(value: unknown, index: number, label: string): FragmentAssetEntry {
  if (!isRecord(value)) {
    throw new Error(`${label}: assets[${index}] is not an object`);
  }
  const prefix = `${label}.assets[${index}]`;
  return {
    kind: assertAssetKind(value["kind"], `${prefix}.kind`),
    path: assertString(value["path"], `${prefix}.path`),
    sha256: assertString(value["sha256"], `${prefix}.sha256`),
    contentType: assertString(value["contentType"], `${prefix}.contentType`),
    capabilities: assertOptionalCapabilities(value["capabilities"], `${prefix}.capabilities`),
    capabilityVersion: assertOptionalNumber(value["capabilityVersion"], `${prefix}.capabilityVersion`),
    params: assertOptionalParams(value["params"], `${prefix}.params`),
    sampleRate: assertOptionalNumber(value["sampleRate"], `${prefix}.sampleRate`),
    provenance: assertString(value["provenance"], `${prefix}.provenance`),
  };
}

// Validates and narrows a fragment JSON payload of unknown shape. `label`
// identifies the source file in thrown error messages (fail loud, not
// silently on malformed producer output).
export function parseFragment(value: unknown, label: string): Fragment {
  if (!isRecord(value)) {
    throw new Error(`${label}: fragment root is not an object`);
  }
  const sourceProvenance = assertString(value["sourceProvenance"], `${label}.sourceProvenance`);
  const rawAssets = value["assets"];
  if (!Array.isArray(rawAssets)) {
    throw new Error(`${label}.assets is not an array`);
  }
  const assets = (rawAssets as readonly unknown[]).map((entry, index) =>
    parseFragmentAssetEntry(entry, index, label),
  );
  return { sourceProvenance, assets };
}

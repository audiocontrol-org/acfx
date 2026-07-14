# Contract: Lesson-asset manifest

The single committed contract the site binds to (FR-008). Written by ONE assembler from
per-producer fragments; the site reads URLs from it (never hardcoded filenames).

## Shape (committed `site/public/manifest/svf.json`)

```ts
interface LessonAssetManifest {
  version: 1;
  lesson: "svf";
  sourceProvenance: string;          // git hash of core/ + adapters/web at build time (staleness guard)
  assets: AssetEntry[];
}

interface AssetEntry {
  kind: "wasm" | "worklet" | "audio" | "response" | "pole-zero" | "impulse";
  url: string;                       // absolute Cloudflare CDN URL (immutable, content-hashed)
  sha256: string;
  contentType: string;               // e.g. "application/wasm"
  capabilities?: ("audio" | "analysis")[];
  capabilityVersion?: number;
  params?: Record<string, number>;
  sampleRate?: number;
  provenance: string;                // producer + source hash
}
```

## Producers → fragments → assembler

- `wasm.fragment.json` (Emscripten producer): the `.wasm` + worklet entries.
- `static.fragment.json` (native host asset-tool): audio + response/pole-zero/impulse entries.
- **Assembler** (sole writer): inventories + validates fragments → writes `svf.json`. Two
  producers, one writer (no races).

## Rules

- `url` is absolute + immutable; the site never constructs filenames.
- Adding a future asset kind extends `AssetEntry.kind`, not the site's fetch logic.
- Binaries are NOT committed; the manifest IS.
- `sourceProvenance` drives the **non-building staleness guard** (hash comparison, no compile).

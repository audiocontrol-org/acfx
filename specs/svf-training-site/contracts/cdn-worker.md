# Contract: Asset CDN — Cloudflare Worker read-through cache

Modeled on `oletizi/colony-cults` `infra/cloudflare-cdn/` (FR-010). Reads only; writes go to
B2 directly from the local publish step.

## Config (`infra/cloudflare-cdn/wrangler.toml`)

```toml
name = "audiocontrol-acfx-cdn"
main = "worker.ts"                    # strict TypeScript (Principle IX; precedent used worker.js)
compatibility_date = "2026-07-01"
workers_dev = true
[vars]
B2_DOWNLOAD_BASE = "https://f004.backblazeb2.com/file/audiocontrol-acfx"  # confirm f004 + bucket via `b2 account get`
EDGE_TTL_SECONDS = "2592000"
```

## Worker behavior (`worker.ts`)

- Maps `https://<worker>/<key>` → `${B2_DOWNLOAD_BASE}/<key>`.
- Caches **only 2xx** via the explicit Cache API under a `CACHE_VERSION`-namespaced key;
  **never caches errors**.
- Adds `Access-Control-Allow-Origin: *` (cross-origin `.wasm` fetch requires it).
- Sets `Cache-Control: public, max-age=<ttl>, immutable`. GET/HEAD only.
- Passes through B2's `Content-Type` — so the upload MUST set `application/wasm` on the `.wasm`.
- Drops the precedent's `?w=` image-resize branch (we serve wasm/audio/json, not images).

## Deploy + publish (local, not CI)

```bash
cd infra/cloudflare-cdn && npx wrangler login && npx wrangler deploy   # local
# publish: local `make publish-assets` uploads content-hashed objects to B2 over the S3 API,
# reading creds from ~/.config/backblaze/b2-audiocontrol-acfx-credentials.yaml (gitignored).
```

- **No secret in the repo** (FR-018): the publish step reads the gitignored creds file by path.
- `CDN_BASE` (e.g. `https://audiocontrol-acfx-cdn.oletizi.workers.dev`) is what the manifest's
  absolute URLs are built from.

# Cloudflare read-through CDN for the training-site assets (T015/T016)

A Cloudflare Worker on `*.workers.dev` that fronts the **public**
`audiocontrol-acfx` B2 bucket for the **read/consumption** side, caching
published assets (wasm modules, audio, JSON) at the edge so a cache HIT never
incurs a B2 Class B (download) transaction. Writes go to B2 **directly** via
the local publish step and never through this Worker.

Reads resolve as `https://<worker>.workers.dev/<key>`, where `<key>` is the
published-asset relative path. The training site build points its asset base
(`CDN_BASE`) at this Worker's deployed origin so every absolute asset URL is
`${CDN_BASE}/<key>`.

Adapted from the `oletizi/colony-cults` `infra/cloudflare-cdn/` precedent (see
`specs/svf-training-site/contracts/cdn-worker.md`); this bucket serves
wasm/audio/json rather than images, so the precedent's `?w=` image-resizing
branch is dropped entirely.

## Caching contract

- **Only `2xx` is cached**, via the explicit Cache API (`caches.default`)
  under a version-namespaced key. Errors are returned `no-store` and never
  cached — a transient error must not be served for the whole TTL.
- **`Cache-Control: public, max-age=<EDGE_TTL_SECONDS>, immutable`** is set on
  every cached (2xx) response, and `X-CDN-Cache` reports `MISS` or `HIT`.
- **CORS.** Every response — hit, miss, or error — carries
  `Access-Control-Allow-Origin: *`, since a cross-origin `.wasm` fetch
  requires it and B2 sends no CORS header on the public bucket itself.
- **`Content-Type` passthrough.** The Worker does not override B2's
  `Content-Type`; the publish step is responsible for setting the correct
  type (e.g. `application/wasm`) on upload.
- **GET/HEAD only** — any other method gets `405 Method Not Allowed`.
- **Invalidation.** `workers.dev` has no zone, so there is no global
  purge-by-URL API. Bump `CACHE_VERSION` in `worker.ts` to invalidate the
  whole namespace at once (old entries at every PoP are orphaned and age
  out); moving to a custom domain (a real zone) would unlock per-key
  purge-by-URL.

## Writes never go through this Worker

The bucket is **public** for reads, and this Worker is a read-only cache in
front of it. Publishing new/updated assets (`PutObject`, Class A) talks to B2
**directly** from the local `make publish-assets` step, reading credentials
from a gitignored local credentials file — never through this cache, and
never checked into the repo.

## Deploy

```bash
cd infra/cloudflare-cdn
npm install
npx wrangler login        # once, interactive
npx wrangler deploy
```

Confirm `B2_DOWNLOAD_BASE` in `wrangler.toml` matches the bucket's real
`downloadUrl` host (`f004` at time of writing; reconfirm via `b2 account get`
if it ever changes).

`CDN_BASE` for the site build is the deployed Worker origin, e.g.
`https://audiocontrol-acfx-cdn.<account>.workers.dev`.

## Verify caching (after a real deploy)

Use `GET` (`-D -`), NOT `HEAD` (`-I`): the Cache API does not populate/serve
HEAD the same way, so a HEAD probe reports `MISS` every time and is
misleading.

```bash
K=<published-asset-key>
URL=https://audiocontrol-acfx-cdn.<account>.workers.dev/$K
curl -s -o /dev/null -D - "$URL" | grep -iE 'x-cdn-cache|cf-cache-status'  # run twice: MISS then HIT
```

## Typecheck

```bash
cd infra/cloudflare-cdn
npm install
npx tsc --noEmit
```

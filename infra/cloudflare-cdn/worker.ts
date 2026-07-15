/**
 * acfx training-site read-through CDN (T015).
 *
 * A Cloudflare Worker that fronts the PUBLIC `audiocontrol-acfx` B2 bucket for
 * the READ/consumption side only. It maps `https://<worker>.workers.dev/<key>`
 * to the B2 friendly download URL `${B2_DOWNLOAD_BASE}/<key>` and serves the
 * response from Cloudflare's edge cache, so a cache HIT never touches B2 (no
 * Class B download transaction; B2->Cloudflare egress is free via the
 * Bandwidth Alliance).
 *
 * Reads only. Writes (PutObject) talk to B2 DIRECTLY via the local
 * `make publish-assets` step, never through this Worker.
 *
 * `<key>` is the published-asset relative path (e.g. a content-hashed
 * `.wasm`/`.json`/audio object key). The training site builds
 * `${CDN_BASE}/<key>` for every absolute asset URL, so `CDN_BASE` must point
 * at this Worker's deployed origin.
 *
 * Adapted from the `oletizi/colony-cults` `infra/cloudflare-cdn/worker.js`
 * precedent (see `specs/svf-training-site/contracts/cdn-worker.md`). The
 * precedent's optional `?w=` image-resizing branch (`cf.image`) is DROPPED
 * here on purpose — this Worker serves wasm/audio/json, not images.
 *
 * Caching contract (IMPORTANT):
 *   - ONLY 2xx responses are cached, via the explicit Cache API
 *     (`caches.default`) under a versioned key. Error responses are NEVER
 *     cached -- otherwise a transient B2 error would be served for the whole
 *     TTL even after the underlying condition clears. (`cacheTtlByStatus`
 *     would fix this at the `cf` fetch-option level but is Enterprise-only,
 *     so we cache explicitly instead.)
 *   - Bump `CACHE_VERSION` to invalidate the whole namespace without a
 *     zone-level purge (workers.dev has no zone to purge). The version also
 *     rides on the origin fetch as `?ccv=` (B2 ignores unknown query params)
 *     so a new version bypasses any stale edge entry under the old key.
 *   - CORS: every response carries `Access-Control-Allow-Origin: *` (a
 *     cross-origin `.wasm` fetch requires it). `*` is origin-independent, so
 *     it is safe to bake into the cached entry.
 *   - `Content-Type` is passed through from B2 untouched -- the publish step
 *     is responsible for setting the correct type (e.g. `application/wasm`)
 *     on upload.
 *
 * Config (wrangler `[vars]`):
 *   - B2_DOWNLOAD_BASE   e.g. https://f004.backblazeb2.com/file/audiocontrol-acfx
 *   - EDGE_TTL_SECONDS   edge + browser cache lifetime for 2xx (string int)
 */

export interface Env {
  readonly B2_DOWNLOAD_BASE: string;
  readonly EDGE_TTL_SECONDS: string;
}

const CACHE_VERSION = '1';
const DEFAULT_EDGE_TTL_SECONDS = 2_592_000;

function withCors(response: Response): Response {
  response.headers.set('Access-Control-Allow-Origin', '*');
  return response;
}

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return new Response('Method Not Allowed', {
        status: 405,
        headers: { Allow: 'GET, HEAD' },
      });
    }

    const base = env.B2_DOWNLOAD_BASE;
    if (!base) {
      return new Response('CDN misconfigured: B2_DOWNLOAD_BASE unset', {
        status: 500,
      });
    }
    const parsedTtl = Number.parseInt(env.EDGE_TTL_SECONDS, 10);
    const ttl = Number.isFinite(parsedTtl) && parsedTtl > 0 ? parsedTtl : DEFAULT_EDGE_TTL_SECONDS;

    const url = new URL(request.url);
    const key = url.pathname.replace(/^\/+/, '');
    if (key === '') {
      return new Response('Not Found (no object key)', { status: 404 });
    }

    // Our own edge-cache entry, namespaced by version. Only 2xx ever land here.
    const cache = caches.default;
    const cacheKey = new Request(`${url.origin}/__cdn/v${CACHE_VERSION}/${key}`);

    const cached = await cache.match(cacheKey);
    if (cached) {
      const hit = new Response(cached.body, cached);
      hit.headers.set('X-CDN-Cache', 'HIT');
      return withCors(hit);
    }

    // Miss: fetch B2 directly. `cacheEverything: false` keeps Cloudflare from
    // implicitly caching the subrequest (so errors are never cached); the
    // `?ccv=` version busts any stale entry left by an earlier code version.
    const origin = await fetch(`${base}/${key}?ccv=${CACHE_VERSION}`, {
      cf: { cacheEverything: false },
    });

    if (!origin.ok) {
      // Never cache an error.
      const err = new Response(origin.body, origin);
      err.headers.set('Cache-Control', 'no-store');
      err.headers.set('X-CDN-Cache', 'BYPASS-ERR');
      err.headers.set('X-CDN-Origin', 'b2');
      return withCors(err);
    }

    // Content-Type is passed through from `origin` untouched (B2 sets it).
    const ok = new Response(origin.body, origin);
    ok.headers.set('Cache-Control', `public, max-age=${ttl}, immutable`);
    ok.headers.set('X-CDN-Cache', 'MISS');
    ok.headers.set('X-CDN-Origin', 'b2');
    withCors(ok);
    ctx.waitUntil(cache.put(cacheKey, ok.clone()));
    return ok;
  },
};

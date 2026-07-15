// @ts-check
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'astro/config';
import mdx from '@astrojs/mdx';

// SVF training site — first vertical slice (specs/svf-training-site).
// Static output; CI builds nothing, so this is built locally only.

// Cross-package integration (T028): the site consumes the `adapters/web`
// AudioWorklet engine that runs the real svf.wasm. `@acfx/web/*` resolves into
// the sibling `adapters/web` package (matching its package name + tsconfig
// paths), and `server.fs.allow` lets the dev server read it since it lives
// outside the Astro/Vite root. The worklet PROCESSOR is bundled to a loadable
// URL via Vite's `?worker&url` suffix at its import site (svf-demo-controller).
const adaptersWeb = fileURLToPath(new URL('../adapters/web', import.meta.url));
const repoRoot = fileURLToPath(new URL('..', import.meta.url));

export default defineConfig({
  site: 'https://acfx-training.example.com',
  output: 'static',
  integrations: [mdx()],
  vite: {
    resolve: {
      alias: {
        '@acfx/web': adaptersWeb,
      },
    },
    server: {
      fs: {
        allow: [repoRoot],
      },
    },
  },
});

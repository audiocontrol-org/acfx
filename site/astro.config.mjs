// @ts-check
import { defineConfig } from 'astro/config';
import mdx from '@astrojs/mdx';

// SVF training site — first vertical slice (specs/svf-training-site).
// Static output; CI builds nothing, so this is built locally only.
export default defineConfig({
  site: 'https://acfx-training.example.com',
  output: 'static',
  integrations: [mdx()],
});

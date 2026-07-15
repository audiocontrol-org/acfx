// Playwright E2E config for the SVF lesson site (T033).
//
// This exercises the browser-integration surface unit/build checks cannot:
// AudioWorklet init, cross-origin CDN wasm/audio fetches, user-gesture audio
// startup, and canvas-based visualizer rendering. It runs entirely LOCALLY —
// CI builds nothing (FR-011) — as an operator-run `npm run test:e2e` / `make
// e2e` target, against the STATIC production build served by `astro preview`
// (the same static output Netlify would serve), not the dev server.
import { defineConfig, devices } from '@playwright/test';

const PORT = 4321;
// `astro preview` binds the IPv6 loopback ([::1]) by default, not 127.0.0.1 —
// use `localhost` so it resolves to whichever loopback the server is on.
const BASE_URL = `http://localhost:${PORT}`;

export default defineConfig({
  testDir: './tests/e2e',
  fullyParallel: true,
  forbidOnly: Boolean(process.env['CI']),
  retries: 0,
  workers: 1,
  reporter: 'list',
  timeout: 60_000,
  expect: {
    // The CDN wasm fetch + AudioContext resume are async and cross-origin;
    // give assertions room before failing.
    timeout: 15_000,
  },
  use: {
    baseURL: BASE_URL,
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
  // `astro preview` serves the static `dist/` produced by `npm run build` —
  // the same artifact that would ship to Netlify (FR-014's static-build
  // contract). The test run assumes `npm run build` has already populated
  // `dist/`; `make e2e` wires that ordering.
  webServer: {
    command: `npm run preview -- --port ${PORT}`,
    url: BASE_URL,
    reuseExistingServer: !process.env['CI'],
    timeout: 30_000,
  },
});

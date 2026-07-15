// E2E smoke test for the SVF lesson page (T033, FR-013, SC-006).
//
// This is the one test in the suite that exercises real browser integration
// unit/build checks cannot reach: AudioWorklet init, a cross-origin CDN wasm
// fetch, a user-gesture audio start, and canvas-based visualizer rendering.
// It runs against the STATIC production build (`astro preview` over `dist/`,
// see playwright.config.ts) and asserts real page state — nothing here is
// stubbed or mocked.
import { expect, test, type Page, type Response } from '@playwright/test';

const CDN_WASM_PATTERN = /svf\.[0-9a-f]+\.wasm$/;
const LESSON_SECTION_IDS = [
  'concept',
  'hear-it',
  'observe-it',
  'play-with-it',
  'build-it',
  'go-deeper',
] as const;

/** True for a same-origin request (the preview server itself). */
function isCrossOrigin(page: Page, url: string): boolean {
  return new URL(url).origin !== new URL(page.url()).origin;
}

test.describe('SVF lesson page', () => {
  test('loads the six-part lesson with both live artifacts, a cross-origin CDN wasm fetch, a user-gesture audio start, and a live visualizer', async ({
    page,
  }) => {
    // Arm the cross-origin CDN wasm wait BEFORE navigating: the visualizer
    // fetches its analysis wasm automatically on load (no user gesture is
    // required to instantiate WebAssembly, only to resume an AudioContext),
    // so the request can fire as soon as the page's hydration script runs.
    const cdnWasmResponsePromise: Promise<Response> = page.waitForResponse(
      (response) => CDN_WASM_PATTERN.test(new URL(response.url()).pathname) && isCrossOrigin(page, response.url()),
      { timeout: 30_000 },
    );

    await page.goto('/');

    // --- 1. All six lesson sections are present -----------------------------
    for (const id of LESSON_SECTION_IDS) {
      await expect(page.locator(`#${id}`)).toBeVisible();
    }

    // --- 2. Both artifact islands are in the DOM ----------------------------
    const demo = page.locator('[data-svf-demo]');
    const visualizer = page.locator('[data-svf-visualizer]');
    await expect(demo).toBeAttached();
    await expect(visualizer).toBeAttached();

    // --- 3. Cross-origin CDN asset load (FR-013) ----------------------------
    // The visualizer's analysis-wasm fetch is cross-origin (a Cloudflare
    // Worker CDN host, not the preview server) and must resolve 200 with a
    // wasm content type — this is the integration surface a unit test can't
    // reach.
    const cdnWasmResponse = await cdnWasmResponsePromise;
    expect(cdnWasmResponse.status()).toBe(200);
    expect(cdnWasmResponse.headers()['content-type']).toContain('wasm');

    // The visualizer island only reveals its live section once that wasm has
    // loaded and initialized successfully — confirms the island actually
    // mounted on the real fetched asset, not merely that the markup exists.
    const visualizerLive = visualizer.locator('[data-role="live"]');
    await expect(visualizerLive).toBeVisible({ timeout: 20_000 });
    const visualizerError = visualizer.locator('[data-role="error"]');
    await expect(visualizerError).toBeHidden();

    // --- 4. Visualizer reaches its live analysis state (real, not faked) ---
    await expect(visualizer.locator('[data-role="status"]')).toHaveText('Live · real compiled analysis', {
      timeout: 20_000,
    });
    const radiusReadout = visualizer.locator('[data-role="radius-readout"]');
    await expect(radiusReadout).not.toHaveText('—');
    const radiusText = (await radiusReadout.textContent())?.trim() ?? '';
    expect(radiusText.length).toBeGreaterThan(0);
    expect(Number.isNaN(Number(radiusText))).toBe(false);

    // All three LIVE canvases (scoped past the hidden content-fallback
    // section, which also has three canvases of its own) are present and have
    // been laid out with real pixel dimensions (a canvas Astro never sizes
    // never paints).
    const canvases = visualizerLive.locator('canvas');
    await expect(canvases).toHaveCount(3);
    const boxes = await canvases.evaluateAll((elements) =>
      elements.map((el) => {
        const canvas = el as HTMLCanvasElement;
        return { width: canvas.width, height: canvas.height };
      }),
    );
    for (const box of boxes) {
      expect(box.width).toBeGreaterThan(0);
      expect(box.height).toBeGreaterThan(0);
    }

    // --- 5. User-gesture audio start (FR-013) -------------------------------
    // The demo island must have revealed its live (interactive) section too —
    // it needs no network fetch to do so (the wasm loads lazily on Start).
    await expect(demo.locator('[data-role="live"]')).toBeVisible();

    const startButton = demo.locator('[data-role="start"]');
    await expect(startButton).toBeVisible();
    // A real, trusted Playwright click — this is what satisfies Chromium's
    // autoplay/user-gesture policy for AudioContext.resume(); the test
    // deliberately does NOT relax the browser's autoplay policy, since the
    // gesture requirement itself is part of what's under test.
    await startButton.click();

    await expect(demo).toHaveAttribute('data-playing', 'true', { timeout: 20_000 });
    await expect(demo.locator('[data-role="status"]')).toHaveText(/Live · real WASM DSP/, { timeout: 20_000 });
    await expect(demo.locator('[data-role="error"]')).toBeHidden();
  });
});

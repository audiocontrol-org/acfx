// E2E smoke test for the SVF lesson page (T033, FR-013, SC-006).
//
// This is the one test in the suite that exercises real browser integration
// unit/build checks cannot reach: AudioWorklet init, a cross-origin CDN wasm
// fetch, a user-gesture audio start, and canvas-based visualizer rendering.
// It runs against the STATIC production build (`astro preview` over `dist/`,
// see playwright.config.ts) and asserts real page state — nothing here is
// stubbed or mocked.
//
// The lesson is a TABBED layout: the six parts are ARIA tabpanels, only one
// visible at a time, and the live artifact islands are never unmounted across
// tab switches (their AudioContext + WASM persist). So the test activates the
// relevant tab before interacting with each island, and asserts the tab
// semantics themselves (single visible panel, switching, hash deep-linking).
import { expect, test, type Locator, type Page, type Response } from '@playwright/test';

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

/** Activate a tab by its panel id and assert it became the single visible panel. */
async function openTab(page: Page, id: string): Promise<Locator> {
  await page.locator(`#${id}-tab`).click();
  const panel = page.locator(`#${id}`);
  await expect(panel).toBeVisible();
  await expect(page.locator(`#${id}-tab`)).toHaveAttribute('aria-selected', 'true');
  // Exactly one tabpanel is ever visible.
  await expect(page.locator('[role="tabpanel"]:not([hidden])')).toHaveCount(1);
  return panel;
}

test.describe('SVF lesson page', () => {
  test('tabbed six-part lesson: both islands persist across tabs, cross-origin CDN wasm, user-gesture audio start, live visualizer', async ({
    page,
  }) => {
    // Arm the cross-origin CDN wasm wait BEFORE navigating: the visualizer
    // fetches its analysis wasm automatically on load — even though its tab
    // panel starts hidden, the island still mounts and instantiates WASM (no
    // user gesture is required for WebAssembly, only to resume an AudioContext).
    const cdnWasmResponsePromise: Promise<Response> = page.waitForResponse(
      (response) => CDN_WASM_PATTERN.test(new URL(response.url()).pathname) && isCrossOrigin(page, response.url()),
      { timeout: 30_000 },
    );

    await page.goto('/');

    // --- 1. All six lesson sections exist as tabpanels ----------------------
    for (const id of LESSON_SECTION_IDS) {
      await expect(page.locator(`#${id}`)).toBeAttached();
    }

    // --- 2. Tab semantics: six tabs, one panel visible on load (Concept) ----
    await expect(page.getByRole('tab')).toHaveCount(6);
    await expect(page.getByRole('tabpanel', { includeHidden: true })).toHaveCount(6);
    await expect(page.locator('#concept')).toBeVisible();
    await expect(page.locator('#concept-tab')).toHaveAttribute('aria-selected', 'true');
    await expect(page.locator('[role="tabpanel"]:not([hidden])')).toHaveCount(1);
    // Panels for the other tabs start hidden.
    await expect(page.locator('#play-with-it')).toBeHidden();
    await expect(page.locator('#observe-it')).toBeHidden();

    // --- 3. Both artifact islands are in the DOM (mounted, even while hidden) -
    const demo = page.locator('[data-svf-demo]');
    const visualizer = page.locator('[data-svf-visualizer]');
    await expect(demo).toBeAttached();
    await expect(visualizer).toBeAttached();

    // --- 4. Cross-origin CDN asset load (FR-013) ----------------------------
    // The visualizer's analysis-wasm fetch is cross-origin (a Cloudflare
    // Worker CDN host, not the preview server) and must resolve 200 with a
    // wasm content type — this is the integration surface a unit test can't
    // reach. It fires on load despite the Observe-it tab starting hidden.
    const cdnWasmResponse = await cdnWasmResponsePromise;
    expect(cdnWasmResponse.status()).toBe(200);
    expect(cdnWasmResponse.headers()['content-type']).toContain('wasm');

    // --- 5. Open "Observe it": the live visualizer (real, not faked) --------
    await openTab(page, 'observe-it');
    await expect(page.locator('#concept')).toBeHidden();

    const visualizerLive = visualizer.locator('[data-role="live"]');
    await expect(visualizerLive).toBeVisible({ timeout: 20_000 });
    await expect(visualizer.locator('[data-role="error"]')).toBeHidden();

    await expect(visualizer.locator('[data-role="status"]')).toHaveText('Live · analysis', {
      timeout: 20_000,
    });
    const radiusReadout = visualizer.locator('[data-role="radius-readout"]');
    await expect(radiusReadout).not.toHaveText('—');
    const radiusText = (await radiusReadout.textContent())?.trim() ?? '';
    expect(radiusText.length).toBeGreaterThan(0);
    expect(Number.isNaN(Number(radiusText))).toBe(false);

    // All three LIVE canvases are present and laid out with real pixel
    // dimensions once the tab is shown (a canvas Astro never sizes never
    // paints; a canvas in a hidden panel has no layout size — showing the tab
    // is what gives it one).
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

    // --- 6. Open "Play with it": user-gesture audio start (FR-013) ----------
    await openTab(page, 'play-with-it');
    await expect(page.locator('#observe-it')).toBeHidden();

    // The demo island revealed its live (interactive) section — it needs no
    // network fetch to do so (the wasm loads lazily on Start).
    await expect(demo.locator('[data-role="live"]')).toBeVisible();

    const startButton = demo.locator('[data-role="start"]');
    await expect(startButton).toBeVisible();
    // A real, trusted Playwright click — this is what satisfies Chromium's
    // autoplay/user-gesture policy for AudioContext.resume(); the test
    // deliberately does NOT relax the browser's autoplay policy, since the
    // gesture requirement itself is part of what's under test.
    await startButton.click();

    await expect(demo).toHaveAttribute('data-playing', 'true', { timeout: 20_000 });
    await expect(demo.locator('[data-role="status"]')).toHaveText(/Playing/, { timeout: 20_000 });
    await expect(demo.locator('[data-role="error"]')).toBeHidden();

    // --- 7. Deep-linking: activating a tab syncs the URL hash ---------------
    expect(new URL(page.url()).hash).toBe('#play-with-it');

    // --- 8. The islands persisted across the tab switches (never remounted) --
    // Switch back to Observe-it: the visualizer is still live (its WASM was not
    // re-instantiated) and the demo is still playing (its AudioContext persists,
    // just paused off-screen) — proving CSS visibility toggling, not remount.
    await openTab(page, 'observe-it');
    await expect(page.locator('#play-with-it')).toBeHidden();
    await expect(visualizer.locator('[data-role="status"]')).toHaveText('Live · analysis');
    // The demo island is still mounted with its playing state intact.
    await expect(demo).toHaveAttribute('data-playing', 'true');
  });
});

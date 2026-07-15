// Build-time Markdown rendering for the code viewer's repo-doc case.
//
// When a "Build it" checkpoint points at a Markdown file (e.g. a lab README),
// the viewer does NOT show it as raw highlighted source — it RENDERS it, the way
// Markdown is meant to be read. This module turns trusted repo Markdown TEXT into
// static HTML at build time, so the site stays self-contained: nothing is fetched
// or parsed in the browser.
//
// It reuses Astro's OWN Markdown pipeline (`@astrojs/markdown-remark`, already a
// transitive dependency) so the output matches the rest of the site: GFM tables/
// autolinks, heading ids, and — crucially — code fences highlighted by the SAME
// Shiki theme the code screens use, so an embedded ```cpp block reads like the
// instrument screens around it.
//
// Dependency invariant (mirrors load-source): the Markdown is treated as
// DOCUMENTATION TEXT. Rendering it is a lexical/structural pass (Markdown ->
// HTML); nothing here resolves program semantics.

import { createMarkdownProcessor } from '@astrojs/markdown-remark';

// Match the code screens' Shiki theme so fences INSIDE a rendered doc are
// cohesive with the standalone code viewer (see highlight.ts CODE_THEME).
const DOC_CODE_THEME = 'github-dark-dimmed';

/** The concrete renderer `createMarkdownProcessor` resolves to (its own type). */
type MarkdownRenderer = Awaited<ReturnType<typeof createMarkdownProcessor>>;

// The processor is comparatively expensive to construct (it wires up remark +
// rehype + Shiki). Build it once and reuse it across every rendered doc in the
// build.
let processorPromise: Promise<MarkdownRenderer> | null = null;

function getProcessor(): Promise<MarkdownRenderer> {
  if (processorPromise === null) {
    processorPromise = createMarkdownProcessor({
      gfm: true,
      smartypants: false,
      shikiConfig: { theme: DOC_CODE_THEME },
    });
  }
  return processorPromise;
}

/**
 * Render trusted repo Markdown `text` to static HTML at build time.
 *
 * The input is our own repository content (a curated excerpt, or a whole README
 * for the expanded view), so this renders it as-is; a range-limited excerpt may
 * begin mid-section, which is acceptable — it renders whatever is in range.
 */
export async function renderMarkdown(text: string): Promise<string> {
  const processor = await getProcessor();
  const rendered = await processor.render(text);
  return rendered.code;
}

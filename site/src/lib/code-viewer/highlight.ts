// Build-time syntax highlighting for the code viewer (Shiki).
//
// Astro bundles Shiki, so this adds NO new runtime dependency: `codeToHtml`
// runs during `astro build` and emits static, self-contained HTML with inline
// token colors — the browser fetches nothing to render it.
//
// Highlighting is a purely LEXICAL pass (tokenize by grammar, colorize) — it
// never resolves symbols or evaluates the program, upholding the load-source
// dependency invariant that repo files are read as documentation text only.
//
// The `line` transformer stamps each rendered line with its REAL file line
// number (`data-ln`), so the gutter matches GitHub even though the excerpt does
// not start at line 1. The container is tuned to the palette in CodeViewer's
// styles; Shiki's own token colors are left intact (the theme below).

import { codeToHtml } from 'shiki';

import type { SourceLang } from '@lib/code-viewer/load-source';

// A muted dark theme cohesive with the deep blue-graphite faceplate. Its token
// colors read calmly on the panel; the panel overrides only the background so
// the "screen behind glass" gradient shows through (see CodeViewer styles).
const CODE_THEME = 'github-dark-dimmed';

/**
 * Highlight `code` to static HTML. `startLine` is the excerpt's real first line
 * so the gutter shows true file line numbers via `data-ln` on each `.line`.
 */
export async function highlightSource(code: string, lang: SourceLang, startLine: number): Promise<string> {
  return codeToHtml(code, {
    lang,
    theme: CODE_THEME,
    transformers: [
      {
        line(node, line) {
          // `line` is 1-based within this block; map to the real file line.
          node.properties['data-ln'] = String(startLine + line - 1);
        },
      },
    ],
  });
}

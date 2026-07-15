import { defineCollection } from 'astro:content';
import { glob } from 'astro/loaders';
import { z } from 'astro/zod';

// The `lessons` collection (specs/svf-training-site plan.md Phase 4/5:
// src/content/svf/lesson.mdx). The MDX frontmatter carries only the lesson's
// HERO copy — the display title, a one-line thesis, and the monospace eyebrow;
// the six-part body is authored in the MDX itself, and the effect id / roadmap
// node / repo anchors live in the strictly-typed `lesson.meta.ts` (consumed by
// the doc auto-resolver), not in this content schema.
const lessons = defineCollection({
  loader: glob({ pattern: '**/*.mdx', base: './src/content/svf' }),
  schema: z.object({
    /** Display title (hero + <title>). */
    title: z.string(),
    /** Meta description + hero thesis line. */
    description: z.string(),
    /** Monospace eyebrow above the hero title. */
    eyebrow: z.string(),
  }),
});

export const collections = {
  lessons,
};

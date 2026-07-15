import { defineCollection } from 'astro:content';
import { glob } from 'astro/loaders';
import { z } from 'astro/zod';

// Minimal `lessons` collection anticipating the SVF lesson
// (specs/svf-training-site plan.md Phase 4/5: src/content/svf/lesson.mdx +
// lesson.meta.ts). This schema is intentionally minimal — it will be
// extended in a later task (T024, lesson.meta.ts) with the full LessonMeta
// contract (effect id, roadmap node, repo anchors) from
// specs/svf-training-site/contracts/artifact-registry.md.
const lessons = defineCollection({
  loader: glob({ pattern: '**/*.mdx', base: './src/content/svf' }),
  schema: z.object({
    title: z.string(),
    description: z.string(),
  }),
});

export const collections = {
  lessons,
};

// Typed SVF lesson metadata (design §4.5, contract: specs/svf-training-site/
// contracts/artifact-registry.md — the `LessonMeta` shape).
//
// The lesson declares its `effect` id, its roadmap node, and the repository
// anchors it maps to. The build-time doc auto-resolver (`@lib/repo-refs/
// resolver`) turns these anchors into the "Go deeper" links (§3.6); a bad field
// here is a compile error (Principle IX), and an anchor that no longer resolves
// fails `astro build` (FR-009).
//
// `sourcePaths` lists REAL implementation files this lesson references; each is
// verified to exist by the resolver at build time.

import type { LessonMeta } from '@lib/repo-refs/resolver';

export const svfLessonMeta: LessonMeta = {
  effect: 'svf',
  roadmapNode: 'design:feature/svf-training-site',
  repoAnchors: {
    specDir: 'specs/svf-training-site',
    featureSlug: 'svf-training-site',
    sourcePaths: [
      'core/effects/svf/svf-effect.h',
      'core/primitives/filters/svf-primitive.h',
      'core/labs/state-variable-filter/harness/svf-harness.cpp',
      'adapters/web/svf-web-abi.h',
    ],
  },
};

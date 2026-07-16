// Typed SVF lesson metadata (design §4.5, contract: specs/svf-training-site/
// contracts/artifact-registry.md — the `LessonMeta` shape).
//
// `sourcePaths` lists the real implementation files this lesson references; the
// build-time resolver (`@lib/repo-refs/resolver`) turns them (plus the effect's
// host test) into the "Go deeper" source links. A bad field here is a compile
// error (Principle IX), and an anchor that no longer resolves fails `astro
// build` (FR-009) rather than shipping a dead link.

import type { LessonMeta } from '@lib/repo-refs/resolver';

export const svfLessonMeta: LessonMeta = {
  effect: 'svf',
  repoAnchors: {
    sourcePaths: [
      'core/labs/state-variable-filter/harness/svf-harness.cpp',
      'core/primitives/filters/svf-primitive.h',
      'core/effects/svf/svf-effect.h',
      'adapters/web/svf-web-abi.h',
    ],
  },
};

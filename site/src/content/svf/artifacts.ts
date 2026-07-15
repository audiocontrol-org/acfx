// Typed artifact declarations for the SVF lesson (FR-007).
//
// The lesson embeds interactive artifacts by DECLARATION, never by importing a
// component directly: each constant here is a fully-typed `ArtifactDeclaration`
// (discriminated union on `kind`), so its `props` are checked per-kind at
// author time (Principle IX). `lesson.mdx` imports these and hands them to
// `<Artifact declaration={…} />`, which resolves `kind -> component` through the
// registry. Authoring the declarations here (rather than as inline object
// literals in MDX) keeps them under strict TypeScript checking.

import type { ArtifactDeclaration } from '@lib/artifacts/registry';

/**
 * "Observe it" — the live analysis bench (frequency response / pole-zero /
 * impulse), painted from the real compiled analysis ABI. Opens near the
 * resonant preset (800 Hz, moderate resonance) so the first thing the learner
 * sees already shows the pole approaching the unit circle.
 */
export const svfVisualizerDeclaration: ArtifactDeclaration = {
  kind: 'svf-visualizer',
  assetBundle: 'svf',
  props: {
    cutoffHz: 800,
    resonance: 0.5,
    mode: 'lowpass',
  },
};

/**
 * "Play with it" — the live audio faceplate driven by the real svf.wasm on the
 * audio thread. Opens at a musical mid-cutoff with light resonance so the first
 * sweep is immediately audible without self-oscillating.
 */
export const svfDemoDeclaration: ArtifactDeclaration = {
  kind: 'svf-demo',
  assetBundle: 'svf',
  props: {
    initialCutoffHz: 1000,
    initialResonance: 0.2,
    initialMode: 'lowpass',
  },
};

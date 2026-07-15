// Interactive-artifact registry — type machinery + registration seam.
//
// Contract: specs/svf-training-site/contracts/artifact-registry.md
//
// Lessons declare artifacts by `kind`, never by importing a component
// directly; the registry resolves `kind -> component`. `ArtifactDeclaration`
// is a discriminated union on `kind` so `props` is always typed per kind at
// the consumption site — never `unknown`, never cast.
//
// The real `svf-demo` / `svf-visualizer` components do not exist yet (they
// arrive in Phase 5, `specs/svf-training-site/tasks.md` T027-T030, produced
// via `/frontend-design`). This module is deliberately just the type
// machinery + a `registerArtifactComponent` seam: resolving an unregistered
// kind throws `ArtifactNotRegisteredError` rather than rendering a stand-in
// (acfx Principle VII — no fake/mock UI outside tests).

/** The set of artifact kinds a lesson may declare. Future kinds (`spectrum`, */
/** `oscilloscope`, `sandbox`, `exercise`, ...) extend this union. */
export type ArtifactKind = 'svf-demo' | 'svf-visualizer';

/** The SVF's three filter modes (core/effects/svf/svf-effect.h, `SvfMode`). */
export type SvfFilterMode = 'lowpass' | 'highpass' | 'bandpass';

/**
 * Props for the `svf-demo` artifact (Phase 5, T027/T028): audio playback
 * with cutoff/resonance/mode controls, transport, and a response curve,
 * driven by the real compiled WASM audio path. Minimal-but-real for this
 * slice; refined in Phase 5 once the frontend design lands.
 */
export interface SvfDemoProps {
  readonly initialCutoffHz: number;
  readonly initialResonance: number;
  readonly initialMode: SvfFilterMode;
}

/**
 * Props for the `svf-visualizer` artifact (Phase 5, T029/T030): live
 * response curve, pole/zero plot, and impulse response, driven by the
 * analysis ABI. Minimal-but-real for this slice; refined in Phase 5.
 */
export interface SvfVisualizerProps {
  readonly cutoffHz: number;
  readonly resonance: number;
  readonly mode: SvfFilterMode;
}

interface ArtifactBase<K extends ArtifactKind, P> {
  readonly kind: K;
  /** Key into the lesson-asset manifest (see `@lib/lesson-assets/manifest`). */
  readonly assetBundle: string;
  readonly props: Readonly<P>;
}

/**
 * Discriminated union on `kind` — switching/narrowing on `declaration.kind`
 * narrows `declaration.props` to the matching per-kind prop type, with no
 * `unknown` and no cast required at the consumption site.
 */
export type ArtifactDeclaration = ArtifactBase<'svf-demo', SvfDemoProps> | ArtifactBase<'svf-visualizer', SvfVisualizerProps>;

/**
 * The module shape a Phase-5 artifact component loader resolves to. Typed as
 * an Astro component factory (matching the `Content`/MDX component pattern
 * Astro itself uses) since `site/src/components/artifacts/*` are plain Astro
 * components per `plan.md`'s project structure; this can be widened later if
 * Phase 5 introduces a framework island instead.
 */
export type ArtifactComponentModule = {
  readonly default: import('astro/runtime/server/index.js').AstroComponentFactory;
};

export type ArtifactComponentLoader = () => Promise<ArtifactComponentModule>;

/** Thrown by `resolveArtifactComponent` when Phase 5 hasn't registered `kind` yet. */
export class ArtifactNotRegisteredError extends Error {
  constructor(kind: ArtifactKind) {
    super(
      `artifact kind "${kind}" has no registered component yet — it is wired in Phase 5 ` +
        '(specs/svf-training-site/tasks.md T027-T030) via registerArtifactComponent(). ' +
        'This is expected before Phase 5 lands; it is a bug if it fires after.',
    );
    this.name = 'ArtifactNotRegisteredError';
  }
}

type ArtifactRegistry = Record<ArtifactKind, ArtifactComponentLoader | undefined>;

const registry: ArtifactRegistry = {
  'svf-demo': undefined,
  'svf-visualizer': undefined,
};

/**
 * Phase-5 seam: register the real component loader for `kind`. Intended to
 * be called once (at module init of the components that implement each
 * artifact) — e.g.
 * `registerArtifactComponent('svf-demo', () => import('@components/artifacts/SvfDemo/SvfDemo.astro'))`.
 */
export function registerArtifactComponent(kind: ArtifactKind, loader: ArtifactComponentLoader): void {
  registry[kind] = loader;
}

/**
 * Resolve the registered loader for `kind`. Throws `ArtifactNotRegisteredError`
 * if Phase 5 hasn't registered it yet — no placeholder/fake component is
 * ever returned.
 */
export function resolveArtifactComponent(kind: ArtifactKind): ArtifactComponentLoader {
  const loader = registry[kind];
  if (loader === undefined) {
    throw new ArtifactNotRegisteredError(kind);
  }
  return loader;
}

/** True once Phase 5 has registered a real component for `kind`. */
export function isArtifactRegistered(kind: ArtifactKind): boolean {
  return registry[kind] !== undefined;
}

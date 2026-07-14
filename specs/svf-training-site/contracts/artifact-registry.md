# Contract: Interactive-artifact registry + lesson metadata

## Artifact registry (typed, metadata-driven — FR-007)

Lessons declare artifacts by `kind`, not by importing components. The registry resolves
`kind → component`; the declaration is a **discriminated union on `kind`** (no `unknown` at
consumption).

```ts
type ArtifactKind = "svf-demo" | "svf-visualizer";

interface ArtifactBase<K extends ArtifactKind, P> {
  kind: K;
  assetBundle: string;               // manifest key
  props: Readonly<P>;
}

type ArtifactDeclaration =
  | ArtifactBase<"svf-demo", SvfDemoProps>
  | ArtifactBase<"svf-visualizer", SvfVisualizerProps>;

// Usage in MDX:  <Artifact kind="svf-visualizer" lesson="svf" />
```

- This slice registers two kinds; future kinds (`spectrum`, `oscilloscope`, `sandbox`,
  `exercise`) extend the union.
- Visual design of each artifact component is produced via `/frontend-design` (Commandment IV).

## Lesson metadata + repo-doc resolver (FR-009)

```ts
interface LessonMeta {
  effect: "svf";
  roadmapNode: "design:feature/svf-training-site";
  repoAnchors: {
    specDir: string;                 // "specs/svf-training-site"
    featureSlug: string;             // "svf-training-site"
    sourcePaths: string[];           // e.g. ["core/effects/svf/svf-effect.h", ...]
  };
}
```

- The **doc auto-resolver** runs at build time, mapping `repoAnchors` → resolved links to the
  effect's current spec/plan/tasks/tests/implementation/roadmap.
- **Invariant**: reads repo paths as documentation metadata only — MUST NOT import/parse/depend
  on C++ implementation semantics (clarified dependency invariant).
- **Invariant**: an anchor that no longer resolves FAILS the build (no dead links).

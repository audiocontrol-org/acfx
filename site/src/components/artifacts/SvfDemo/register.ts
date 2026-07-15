// Registration side-effect for the `svf-demo` artifact (T022 seam, T028).
//
// Importing this module registers a LAZY loader for the SvfDemo component so
// `<Artifact kind="svf-demo" />` resolves it. The loader is `() => import(...)`
// so pulling in this registration does not eagerly bundle the component — it
// only records how to load it on demand.
import { registerArtifactComponent } from '@lib/artifacts/registry';

registerArtifactComponent('svf-demo', () => import('./SvfDemo.astro'));

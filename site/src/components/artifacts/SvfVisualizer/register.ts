// Registration side-effect for the `svf-visualizer` artifact (T022 seam, T030).
//
// Importing this module registers a LAZY loader for the SvfVisualizer component
// so `<Artifact kind="svf-visualizer" />` resolves it. The loader is
// `() => import(...)` so pulling in this registration does not eagerly bundle
// the component — it only records how to load it on demand.
import { registerArtifactComponent } from '@lib/artifacts/registry';

registerArtifactComponent('svf-visualizer', () => import('./SvfVisualizer.astro'));

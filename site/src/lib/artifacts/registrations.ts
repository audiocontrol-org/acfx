// Central artifact-registration barrel (T022 seam).
//
// Importing this module runs every artifact's `registerArtifactComponent`
// side-effect, so `<Artifact kind="…" />` can resolve any implemented kind.
// Each registration installs only a lazy `() => import(...)` loader, so this
// barrel does NOT eagerly bundle the component code — it just wires the seam.
//
// `Artifact.astro` imports this for its side effect.
import '@components/artifacts/SvfDemo/register.ts';
import '@components/artifacts/SvfVisualizer/register.ts';

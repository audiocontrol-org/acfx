// The visibility contract between the tab controller and the artifact islands
// (frontend-design — tabbed layout).
//
// Tabs show/hide panels by toggling `hidden` — the client islands are NEVER
// unmounted, so their AudioContext + instantiated WASM persist across tab
// switches (no re-fetch of the CDN wasm, no re-init). But an island in a hidden
// panel must not keep working: the audio demo must pause (never play from an
// unseen tab) and the visualizer must stop rendering (correctness + perf). The
// tab controller announces every show/hide by dispatching this typed
// CustomEvent on each island root; the island controllers listen and
// pause/resume their live state accordingly.

/** Event name dispatched on an artifact root when its tab is shown/hidden. */
export const ARTIFACT_VISIBILITY_EVENT = 'svf:visibility';

/** Payload: whether the island's tab panel is now visible. */
export interface ArtifactVisibilityDetail {
  readonly visible: boolean;
}

/** Narrow an incoming Event to our typed CustomEvent (no `any`). */
export function readVisibilityDetail(event: Event): ArtifactVisibilityDetail | undefined {
  if (!(event instanceof CustomEvent)) return undefined;
  const detail: unknown = event.detail;
  if (typeof detail !== 'object' || detail === null) return undefined;
  const visible = (detail as Record<string, unknown>)['visible'];
  return typeof visible === 'boolean' ? { visible } : undefined;
}

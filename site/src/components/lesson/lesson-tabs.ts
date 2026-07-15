// Tab controller for the six-part SVF lesson (frontend-design — tabbed layout).
//
// Turns the tablist + six tabpanels into an ARIA tabs widget with roving
// tabindex, keyboard nav, hash deep-linking, and — the hard part — show/hide by
// toggling `hidden` so the live artifact ISLANDS are never unmounted. On every
// switch it dispatches a typed visibility event on each island root in the
// panel it leaves (visible:false) and the one it enters (visible:true) so the
// audio demo pauses and the visualizer stops rendering while off-screen, then
// resume on return (artifact-visibility.ts). Pure DOM orchestration — no DSP.

import {
  ARTIFACT_VISIBILITY_EVENT,
  type ArtifactVisibilityDetail,
} from '@lib/lesson/artifact-visibility';

interface Tab {
  readonly button: HTMLButtonElement;
  readonly panel: HTMLElement;
}

interface ActivateOptions {
  /** Move keyboard focus to the newly-selected tab button. */
  readonly focus: boolean;
  /** Write the panel id to the URL hash (deep-link, no scroll jump). */
  readonly updateHash: boolean;
}

function dispatchVisibility(panel: HTMLElement, visible: boolean): void {
  const detail: ArtifactVisibilityDetail = { visible };
  panel.querySelectorAll('[data-svf-demo], [data-svf-visualizer]').forEach((island) => {
    island.dispatchEvent(new CustomEvent<ArtifactVisibilityDetail>(ARTIFACT_VISIBILITY_EVENT, { detail }));
  });
}

function prefersReducedMotion(): boolean {
  return window.matchMedia('(prefers-reduced-motion: reduce)').matches;
}

class TabController {
  private readonly tabs: readonly Tab[];
  private activeIndex = 0;

  constructor(private readonly tablist: HTMLElement) {
    const tabs: Tab[] = [];
    tablist.querySelectorAll('[role="tab"]').forEach((button) => {
      if (!(button instanceof HTMLButtonElement)) return;
      const panelId = button.getAttribute('aria-controls');
      const panel = panelId === null ? null : document.getElementById(panelId);
      if (panel instanceof HTMLElement) tabs.push({ button, panel });
    });
    this.tabs = tabs;
  }

  init(): void {
    if (this.tabs.length === 0) return;

    this.tabs.forEach((tab, index) => {
      tab.button.addEventListener('click', () => this.activate(index, { focus: false, updateHash: true }));
      tab.button.addEventListener('keydown', (event) => this.onKeydown(event, index));
    });

    // Honor a deep-link hash on load; otherwise the first tab (server-rendered
    // active) stays selected. No focus steal, no hash rewrite on initial paint.
    const fromHash = this.indexForHash(window.location.hash);
    this.activate(fromHash ?? 0, { focus: false, updateHash: false });

    // Back/forward or an in-page anchor changing the hash re-selects the tab.
    window.addEventListener('hashchange', () => {
      const index = this.indexForHash(window.location.hash);
      if (index !== undefined && index !== this.activeIndex) {
        this.activate(index, { focus: false, updateHash: false });
      }
    });
  }

  private indexForHash(hash: string): number | undefined {
    if (hash.length < 2) return undefined;
    const id = hash.slice(1);
    const index = this.tabs.findIndex((tab) => tab.panel.id === id);
    return index >= 0 ? index : undefined;
  }

  private activate(index: number, options: ActivateOptions): void {
    const target = this.tabs[index];
    if (target === undefined) return;
    const previous = this.tabs[this.activeIndex];

    this.tabs.forEach((tab, i) => {
      const selected = i === index;
      tab.button.setAttribute('aria-selected', selected ? 'true' : 'false');
      tab.button.tabIndex = selected ? 0 : -1;
      tab.panel.hidden = !selected;
    });

    // Tell the islands they left / entered view (pause/resume live state).
    if (previous !== undefined && previous !== target) {
      dispatchVisibility(previous.panel, false);
    }
    dispatchVisibility(target.panel, true);

    this.activeIndex = index;

    if (options.updateHash) {
      // replaceState (not `location.hash =`) keeps deep-links shareable without
      // yanking the scroll position to the panel top on every tab click.
      window.history.replaceState(null, '', `#${target.panel.id}`);
    }
    if (options.focus) {
      target.button.focus();
      this.revealTab(target.button);
    }
  }

  /** Keep the active tab in view inside the horizontally-scrollable bank. */
  private revealTab(button: HTMLButtonElement): void {
    const scroller = this.tablist.closest('[data-lesson-tabs-scroll]');
    if (!(scroller instanceof HTMLElement)) return;
    const bank = scroller.getBoundingClientRect();
    const tab = button.getBoundingClientRect();
    const behavior: ScrollBehavior = prefersReducedMotion() ? 'auto' : 'smooth';
    if (tab.left < bank.left) {
      scroller.scrollBy({ left: tab.left - bank.left, behavior });
    } else if (tab.right > bank.right) {
      scroller.scrollBy({ left: tab.right - bank.right, behavior });
    }
  }

  private onKeydown(event: KeyboardEvent, index: number): void {
    const count = this.tabs.length;
    let next: number | undefined;
    switch (event.key) {
      case 'ArrowRight':
      case 'ArrowDown':
        next = (index + 1) % count;
        break;
      case 'ArrowLeft':
      case 'ArrowUp':
        next = (index - 1 + count) % count;
        break;
      case 'Home':
        next = 0;
        break;
      case 'End':
        next = count - 1;
        break;
      default:
        return;
    }
    event.preventDefault();
    this.activate(next, { focus: true, updateHash: true });
  }
}

/** Boot every lesson tablist on the page (idempotent per element). */
export function initLessonTabs(): void {
  document.querySelectorAll('[data-lesson-tabs]').forEach((element) => {
    if (!(element instanceof HTMLElement)) return;
    if (element.dataset['lessonTabsReady'] === 'true') return;
    element.dataset['lessonTabsReady'] = 'true';
    new TabController(element).init();
  });
}

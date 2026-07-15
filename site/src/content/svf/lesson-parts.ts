// Lesson-part tab metadata (frontend-design — the tabbed layout).
//
// The six parts are ONE ordered procedure (Concept -> Hear -> Observe -> Play
// -> Build -> Go deeper). Converting the old vertical "signal rail" scroll to a
// TABBED layout makes the outer numbering the tab station-index (01..06) and
// removes the nested-numbering weirdness (the old station 05 held its OWN 01..04
// rail). This module is the single source of truth for the tablist the layout
// renders; each entry's `id` must match the `id=` of the matching <LessonPart>
// tabpanel in `lesson.mdx` (the controller links them by that id, and the
// Playwright test + "Go deeper" links rely on those anchors staying resolvable).
//
// Strictly typed (Principle IX): a bad field is a compile error.

/** One tab of the six-part lesson (short channel-selector label + heat flag). */
export interface LessonPartTab {
  /** Stable anchor id — matches the tabpanel `id=` and the deep-link hash. */
  readonly id: string;
  /** Short engraved label shown on the tab button. */
  readonly label: string;
  /** Hands-on part (Play / Build): the tab runs warm (resonance-heat motif). */
  readonly hot?: boolean;
}

/** The six lesson parts, in their one true order. */
export const lessonPartTabs: readonly LessonPartTab[] = [
  { id: 'concept', label: 'Concept' },
  { id: 'hear-it', label: 'Hear it' },
  { id: 'observe-it', label: 'Observe it' },
  { id: 'play-with-it', label: 'Play with it', hot: true },
  { id: 'build-it', label: 'Build it', hot: true },
  { id: 'go-deeper', label: 'Go deeper' },
];

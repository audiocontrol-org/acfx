### Workbench has no path to select the required file-player source

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:74-89

`prepareToPlay()` says that when there is no live input “the operator must point the built-in player at a file,” but this component never exposes any file-selection path or calls `source_.useFilePlayer(...)`. The only source selection in the diff is `source_.useLiveInput(inputs)` when `inputs > 0`; otherwise `source_.prepare(...)` throws, the catch posts a warning, and audio startup continues with no configured source.

That matters because the declared workbench behavior is sketch-and-hear with a built-in looping file player or live input. A downstream user on a desktop with no active input device will hit this as a functional failure: the app opens, reports “No audio source configured,” and has no in-app control to recover. The blast radius is `high` because this is a correctness defect a real adopter can hit on a fresh install, and it contradicts the “no silent fallback” discipline by leaving the stream alive after source setup failed. A reasonable fix is to add an actual setup-time file selection/default file-player path before `source_.prepare(...)`, or fail/disable audio until a source is explicitly configured instead of continuing after the exception.

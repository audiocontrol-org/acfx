### Ignored decode failure can accept an unreadable file as a valid source

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    adapters/workbench/audio-source.cpp:34-42

`useFilePlayer()` validates that a reader exists and that the reported length is positive, but it ignores the return value from `reader->read(&decoded, ...)` at line 37. If decoding fails or only partially succeeds for a corrupt/truncated file, the function still installs `fileBuffer_`, sets `hasFile_ = true`, and switches out of live mode. That directly contradicts the README promise that unreadable files are surfaced and do not produce “silent silence or placeholder audio.”

Blast radius is high because this is a shipped correctness path a user can hit by choosing a damaged but recognized audio file: the UI will believe the source switch succeeded, persistence may remember that file, and playback can become silent or invalid without an error. A reasonable fix is to check `read()`’s boolean result, clear/avoid installing the decoded buffer on failure, and throw `AudioSourceError` with the file path before changing source state.

### Reopening the async chooser can destroy the previous chooser while it is still active

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    adapters/workbench/source-bar.cpp:18-25

`SourceBar::openChooser()` assigns a new `juce::FileChooser` into `chooser_` every time the button is clicked, then launches it asynchronously. The comment correctly says the chooser “must outlive the launch,” but a second click before the first chooser completes overwrites the `unique_ptr` at line 21 and destroys the first chooser while its async dialog is still outstanding. That opens a reachable UI state through ordinary double-click/repeated-click behavior.

Blast radius is medium: it is localized to the source picker UI, but it can cause cancellation, undefined callback behavior, or a crash depending on JUCE/platform behavior. A reasonable fix is to guard `openChooser()` when `chooser_` is already non-null, disable the file button while the chooser is active, and reset `chooser_` only inside the async completion path after callbacks have run.

### Async file-chooser lambda captures `this` without a safety guard — use-after-free risk

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/source-bar.cpp:28-38

`openChooser()` stores a `[this]` capturing lambda as the async callback for `juce::FileChooser::launchAsync`. The callback fires on the message thread when the native dialog is dismissed. If the `SourceBar` is destroyed while the dialog is still open (e.g., the user quits the workbench while the picker is visible), JUCE's `FileChooser` destructor will attempt to cancel the native dialog — but whether that cancellation invokes the completion callback synchronously, asynchronously, or not at all varies by platform. On macOS, `NSOpenPanel` typically fires its completion handler inline during `orderOut`; on Linux (GTK async variant), the callback can be posted after the owning component is gone. If the callback fires after `SourceBar` is fully destructed, `this->onChooseCancelled` and `this->onChooseFile` are accessed as dangling references.

The comment at line 22 acknowledges that "the FileChooser must outlive the launch, so it is owned by the bar" — but the mirrored requirement, that the bar must outlive the callback, is unaddressed. The standard JUCE mitigation is to capture a `juce::Component::SafePointer<SourceBar>` (which becomes null when the component is destroyed) and guard the callback body: `if (auto* self = ptr.getComponent()) { ... }`. Alternatively, a destructor body that sets `chooser_.reset()` before any other member is destroyed, combined with a sentinel checked in the lambda, achieves the same effect.

Blast-radius: this path is exercised any time a user quits the application with the file-open dialog still open — not rare. The consequence is an unguarded write through a dangling pointer, which is a crash on any build with address sanitization enabled and undefined behavior otherwise.

---

### Replacing `chooser_` on a second rapid click triggers a spurious `onChooseCancelled`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    adapters/workbench/source-bar.cpp:20-38

`openChooser()` unconditionally replaces `chooser_` with a new `unique_ptr`, destroying any in-flight `FileChooser`. There is no guard on `fileButton_` while a chooser is active. If the user clicks "Load file..." a second time before dismissing the first dialog, the first `FileChooser` is destroyed mid-flight. On platforms where destroying a `FileChooser` while its dialog is open invokes the callback with an empty result (to signal cancellation), `onChooseCancelled` fires unexpectedly. The workbench's intent for `onChooseCancelled` is described as reverting to Live when no source is saved — so a double-click inadvertently switches the source to Live, then a second dialog opens, and the user picks a file in it, switching back. The net result is two audio-stop/restart cycles for a single intentional file selection.

The simplest fix is to disable `fileButton_` immediately on the first click and re-enable it in both branches of the callback (success and cancel). Alternatively, guard with `if (chooser_) return;` at the top of `openChooser()` so a second click while a dialog is pending is a no-op.

Blast-radius: requires two rapid clicks; not a crash but causes spurious audio restarts and confusing Live reversion. Low severity because it requires deliberate or accidental rapid double-click.

---

### "Live" button does not cancel an open file chooser — callback ordering is undefined

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/source-bar.h:22-26, adapters/workbench/source-bar.cpp:8-12

Clicking "Live" while a file chooser is open calls `onSelectLive()` immediately, triggering the workbench's audio-stop → configure-live → restart cycle. The in-flight `FileChooser`, however, is not cancelled — `chooser_` is not reset and `fileButton_.onClick` does nothing in this path. If the user then selects a file in the still-open dialog, `onChooseFile()` fires, triggering a second audio-stop → configure-file → restart cycle in sequence. The final state is determined by whichever callback fires last, which is whatever the user picks in the dialog after having already clicked Live. This is confusing because clicking Live should signal "I do not want a file," but the dialog remains open and can override that intent.

The simplest fix is to call `chooser_.reset()` in the `liveButton_.onClick` lambda (after triggering the callback) so the native dialog is dismissed and the `onChooseCancelled` / file-pick callback cannot fire. This completes the conceptual "cancel any pending file selection" semantics that clicking Live implies.

Blast-radius: not a crash; the workbench handles both callbacks through the same restart cycle and ends in a consistent (if surprising) state. Low severity because it requires the user to click Live during the open-dialog modal.

---

### README env-var priority claim is unverifiable from this chunk

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    README.md:78-82

The updated README states that `ACFX_WORKBENCH_FILE` is now "a first-run convenience that seeds the source when nothing has been saved yet — a saved selection always takes precedence." This is a behaviorally specific claim about evaluation order (saved settings win over env var). The implementation that would enforce this ordering lives in `adapters/workbench/workbench-app.cpp` (chunk 5420a3615ad2e99c, not included in this chunk's diff). If the implementation instead reads the env var after restoring saved settings and overwrites the restored value, the README is documenting the inverse of the actual precedence. The operator should cross-reference this claim against the env-var and persistence restore sequence in `workbench-app.cpp` to confirm the README matches the code.
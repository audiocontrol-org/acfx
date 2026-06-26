I'll audit this chunk directly — it's a focused review task anchored to the provided diff. Walking each file carefully.

### CPM.cmake re-downloads on every configure (missing EXISTS guard); contradicts its own "first configure" comment

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/CPM.cmake:24-29

The header comment states the bootstrap "downloads the pinned CPM package manager on **first** configure," but the `file(DOWNLOAD ...)` call (lines 24-29) is unconditional — there is no `if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")` guard around it. The canonical upstream CPM bootstrap wraps the download in exactly such an existence check; this diff dropped it. CMake's `file(DOWNLOAD)` does not skip based on a pre-existing file even with `EXPECTED_HASH` set — the hash is verified *after* the transfer, not used to short-circuit it. The net effect: every `cmake` configure (including incremental reconfigures and every fresh CI job whose CPM cache isn't restored) performs a network fetch of `CPM.cmake`.

Blast radius: an adopter or CI runner that configures offline — or re-configures repeatedly — will hit a network requirement the comment explicitly tells them is "first configure" only. Offline reconfigure will fail outright on the download step; online reconfigure pays an avoidable round-trip each time. This is a behavior/doc mismatch plus an offline-build regression.

A reasonable fix restores the guard: `if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}") file(DOWNLOAD ... EXPECTED_HASH ...) endif()`, matching upstream and the comment's stated intent.

### `Component::SafePointer` constructed on the MIDI thread — `WeakReference` is not thread-safe

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:168-176 (handleIncomingMidiMessage)

Inside `handleIncomingMidiMessage` (which JUCE invokes on a dedicated MIDI input thread, not the message thread), the code constructs `juce::Component::SafePointer<ParameterView> safeView(&paramView_)` directly on that MIDI thread. JUCE's `SafePointer` is backed by `WeakReference`, whose master reference is documented as **not thread-safe**: creating/clearing it must happen on the message thread. The master for `paramView_` is lazily created on first `SafePointer` construction, so the MIDI thread may be the one that allocates it — and it can do so concurrently with the message thread tearing `paramView_` down (which clears the same master). That is a data race / UB on shutdown or component reconstruction.

Blast radius: a consumer running the workbench with a live MIDI controller can hit intermittent crashes or memory corruption at teardown, precisely when CC traffic overlaps component destruction. It is rare-but-reachable rather than constant, hence medium, but it is genuine UB under normal MIDI use.

The correct pattern keeps the `SafePointer`/component access on the message thread: capture only the plain `id`/`norm` values into the `callAsync` lambda and resolve the view there (or guard with a `MessageManagerLock`), rather than constructing the `SafePointer` from the MIDI thread.

### Unconfigured-source branch sets `sourceReady_ = true` and relies on an unseen `prepare()` to throw — no explicit error path despite the comment claiming one

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:78-96 (prepareToPlay source selection); depends on adapters/workbench/audio-source.h (other chunk)

The source-selection block chooses `useFilePlayer` (if `ACFX_WORKBENCH_FILE` is set) or `useLiveInput` (if `inputs > 0`), then unconditionally calls `source_.prepare(...)` and sets `sourceReady_ = true`. The leading comment promises three branches: "the built-in file player … else the live device input, **else a surfaced error**." But there is no `else` that surfaces anything — when no env var is set and `inputs == 0`, neither `useFilePlayer` nor `useLiveInput` runs, yet `prepare()` is still called and `sourceReady_` is set to `true`. The only way the promised error materializes is if `WorkbenchAudioSource::prepare()` internally throws `AudioSourceError` on an unconfigured source — a contract that lives in `audio-source.h` (a different chunk) and is nowhere asserted here.

Blast radius: if that unstated contract doesn't hold (prepare succeeds silently on an unconfigured source), `getNextAudioBlock` will call `source_.fillBlock(region)` on a source with no mode set — reading garbage or hitting UB, the exact silent-fallback-to-noise failure mode Constitution V forbids. Even if the contract does hold today, the surfacing of this case is entirely delegated to unseen code while this file's comment asserts the surfacing happens here. This is a cross-file contract gap.

A robust fix makes the no-source case explicit in this file: an `else` that throws/surfaces `AudioSourceError("no audio source configured: set ACFX_WORKBENCH_FILE or connect a live input")` before `prepare()`, so correctness does not hinge on the downstream type's internal validation.

### `ParameterView` discrete-combo `onChange` divides by raw `discreteCount` while `setNormalized` guards `< 2` — inconsistent degenerate-descriptor handling

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:22-32 (combo onChange) vs. 55-67 (setNormalized)

The two halves of the discrete mapping disagree on how they treat `discreteCount`. The forward path in the constructor lambda computes `norm = (index + 0.5f) / static_cast<float>(count)` using the raw `d.discreteCount` with no lower bound (lines 27-29), while the inverse path in `setNormalized` defensively clamps with `const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;` (line 59). For a degenerate descriptor (`discreteCount` of 0 or 1) the forward path divides by 0 or 1 — `0` yields `inf/NaN` fed straight into `onChange`, and the asymmetry means the two directions no longer round-trip. The class advertises itself as fully generic ("there is no per-effect UI code … iterates the effect's `parameters()` table"), so it should not assume `discreteCount >= 2`.

Blast radius: the shipped SVF descriptors almost certainly use `discreteCount >= 2`, so this is latent rather than active — hence low. But `ParameterView` is the descriptor-driven surface every future effect's UI flows through (FR-003), and the next effect that registers a 0- or 1-valued discrete parameter will get a NaN normalized value or a mismatched control with no compile-time signal.

A reasonable fix applies the same lower-bound guard in the constructor lambda (and ideally validates/asserts `discreteCount >= 2` once at row construction so a degenerate descriptor fails loudly rather than producing NaN), making the forward and inverse mappings provably symmetric.
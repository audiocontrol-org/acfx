I'll audit this chunk as an independent reviewer. Walking the workbench adapter, the CMake bootstrap/toolchains, and the audio-block view carefully.

### Discrete combo ignores descriptor `defaultValue`; always initializes to index 0 (asymmetric with slider)

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:18-19 (combo init) vs. :39-41 (slider init)

The continuous path seeds the control from the descriptor's default: `row.slider->setValue(static_cast<double>(normalize(d, d.defaultValue)), …)` (parameter-view.cpp:39-41). The discrete path does **not** — it hardcodes `row.combo->setSelectedItemIndex(0, juce::dontSendNotification);` (parameter-view.cpp:19), discarding `d.defaultValue` entirely. So any discrete parameter whose default is not index 0 will render with the wrong selection at startup: the GUI shows bucket 0 while the effect's actual default is something else, and the two stay disagreed until the operator first touches the combo (which then *does* fire `onChange` and push a value to the effect).

This is a latent generic defect, not specific to today's SVF param set. The whole point of `ParameterView` per its header is "no per-effect UI code… iterates the effect's parameters() table" — it is meant to render *any* effect's descriptors faithfully. The first effect that ships a discrete parameter with a non-zero default (e.g. a filter-mode enum defaulting to bandpass) will silently mis-render. Blast radius: an operator (or an automated A/B harness reading the displayed state) trusts the shown selection as ground truth and mis-attributes the audible result. A reasonable fix mirrors the slider path: derive the index from `d.defaultValue` (e.g. `static_cast<int>(normalize(d, d.defaultValue) * discreteCount)`, clamped) before `setSelectedItemIndex`.

### `prepareToPlay` surfaces a source-init failure via dialog but does not gate the audio callback against an unprepared source

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:66-86 (catch) and :88-110 (getNextAudioBlock)

When `source_.useLiveInput(inputs)` or `source_.prepare(...)` throws `AudioSourceError`, the catch block (workbench-app.cpp:79-85) shows a message box and returns from `prepareToPlay` normally. Nothing records that the source is *not* ready. JUCE then starts the device and `getNextAudioBlock` unconditionally calls `source_.fillBlock(region)` (workbench-app.cpp:99) every callback, against a source that was never (or only partially) prepared — note `prepare()` is skipped entirely if `useLiveInput` throws first. There is no `sourceReady_` flag, no early-out, no buffer clear.

The inline comment claims "no silent fallback to silence (Constitution V)", but surfacing a dialog does not enforce that invariant for the audio path: depending on `WorkbenchAudioSource::fillBlock`'s behavior on an unprepared instance (not visible in this chunk), the callback either emits silence — exactly the silent-output outcome the comment disclaims — or reads uninitialized internal state. Since `fillBlock` is documented `noexcept`, a throw can't propagate, so the failure mode is quiet garbage/silence rather than a loud crash. Blast radius: an operator dismisses the dialog and continues to drive a workbench whose audio path is in an undefined state, with no further signal. A reasonable fix sets a `std::atomic<bool> sourceReady_{false}` on success, and has `getNextAudioBlock` clear the region and return when it is false.

### `teensy.cmake` comments describe C++-standard auto-negotiation the file does not perform

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:6-9, 26-31

The header comment states "ACFX_TEENSY_CXX_STANDARD is set to the highest standard that toolchain supports (>= 17)" and the inline comment says "Raised here if the installed toolchain supports more" (teensy.cmake:30). No such logic exists: the file only does `if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD) set(ACFX_TEENSY_CXX_STANDARD 17) endif()` (teensy.cmake:28-30). There is no compiler-feature probe, no detection, no raising — it is a static default of 17 plus a manual override variable. The `__cpp_concepts`-guarded degradation path referenced at lines 8-9 therefore depends entirely on whoever configures the build remembering to pass `-DACFX_TEENSY_CXX_STANDARD=20`.

This is documentation drift with a behavioral consequence. An adopter (or an unattended agent building from this toolchain) reads "set to the highest standard that toolchain supports" and reasonably assumes the Teensy build auto-selects C++20 when the installed `arm-none-eabi-g++` supports it — so they expect the concepts-based `Effect` contract to be active. In reality they get C++17 and the duck-typed fallback, silently, with the stronger compile-time checks disabled. Blast radius: contract violations that concepts would have caught at compile time slip through on the Teensy target. Fix: either implement the probe the comment promises (e.g. `check_cxx_compiler_flag(-std=gnu++20 …)` and raise the standard accordingly), or rewrite the comment to say the standard is a manual override defaulting to 17.

### `setNormalized` combo path uses a clamped count inconsistent with the constructor's `discreteCount`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:62-69 vs. :15-25

The constructor populates the combo with exactly `d.discreteCount` items and encodes values as center-of-bucket `(index + 0.5) / discreteCount` (parameter-view.cpp:16-23). `setNormalized` decodes with a *different* count: `const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;` then `index = normalized * count` (parameter-view.cpp:63-66). For `discreteCount >= 2` the two agree and the round-trip is correct; for `discreteCount == 1` they diverge (decode uses 2 while the combo has 1 item, so an externally-driven `setNormalized` can compute `index == 1` and call `setSelectedItemIndex(1)` on a single-item combo); for `discreteCount == 0` the constructor's encode divides by zero (`/ static_cast<float>(count)` with count 0 → NaN).

These are malformed-descriptor edges rather than mainline failures, which is why this is low rather than medium — a sane effect won't ship a 0- or 1-value discrete parameter. But the two code paths having independent, inconsistent notions of the bucket count is a latent trap: the clamp in `setNormalized` exists to dodge a divide-by-zero that the constructor does *not* dodge, so the guard is in the wrong place. Fix: validate `discreteCount >= 2` once at descriptor-ingest time (or in the constructor) and use the same single source of truth for count in both encode and decode.

### Discrete combo items are labelled with bare indices, not semantic names; header comment overstates "labelled from the descriptor"

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:16-18; parameter-view.h:11-15

The combo populates items via `row.combo->addItem(juce::String(i), i + 1)` (parameter-view.cpp:17) — the visible text is the integer index ("0", "1", "2"…). Only the *row* label (the parameter name) comes from the descriptor (parameter-view.cpp:13). The header comment claims the view "builds a slider (continuous) or combo (discrete) for each, labelled from the descriptor" (parameter-view.h:11-13), which reads as if the discrete *choices* are descriptor-labelled. They are not — the descriptor model (`ParameterDescriptor`: name/kind/discreteCount/defaultValue, per the includes) carries no per-value names, so the auto-generated UI for, e.g., a filter-mode selector shows "0/1/2" instead of "LP/BP/HP".

Blast radius is contained to UX legibility of the sketch-and-hear workbench, hence low. But it is worth surfacing as a design gap the vertical slice exposes: the descriptor table — the same one every adapter consumes (FR-003) — has no slot for discrete value names, so *no* adapter can render meaningful enum labels. If semantic discrete labels are a goal, the fix is in the descriptor contract (add an optional `span<const string_view>` of value names), not in this view; if they are out of scope for the slice, the header comment should be narrowed so a future reader doesn't assume the labels are descriptor-driven.

### `params_` member is assigned in the constructor body but never read

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:38 (assignment); :167 (declaration)

`params_ = node_->parameters();` (workbench-app.cpp:38) stores the descriptor span in the `span<const ParameterDescriptor> params_;` member (workbench-app.cpp:167), but no other code in this component reads `params_` — the `ParameterView` was already constructed from `node_->parameters()` directly in the initializer list (workbench-app.cpp:33-34), and MIDI/processing paths use `node_` and `midi_` rather than `params_`. The member is dead state.

This is hygiene, not a defect — the span is cheap and harmless. I flag it only because a stored-but-unread span invites a future reader to assume it is the authoritative parameter list and to mutate `node_` without realizing `params_` could go stale (it is a non-owning view into whatever `parameters()` returned). Fix: drop the member and the assignment, or, if it is meant to be the component's parameter handle, route the other consumers through it so it is actually load-bearing.
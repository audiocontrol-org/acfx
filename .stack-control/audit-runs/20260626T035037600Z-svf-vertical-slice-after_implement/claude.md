### prepareToPlay never reaches the promised "surfaced error" branch — unconfigured source still sets sourceReady_ = true

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp (prepareToPlay source-selection try/catch, ~lines 90–118)

The comment block directly above the source selection promises three outcomes: file player "when ACFX_WORKBENCH_FILE points at an audio file … else the live device input, else a surfaced error." The code implements only the first two:

```cpp
if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
    source_.useFilePlayer(...);
} else if (inputs > 0) {
    source_.useLiveInput(inputs);
}                         // <-- no else; nothing configured if !path && inputs==0
source_.prepare(sampleRate, blockSize);
sourceReady_ = true;      // <-- set true unconditionally on the success path
```

When there is no `ACFX_WORKBENCH_FILE` **and** the device exposes zero input channels (an output-only interface — a common, reachable configuration), neither `useFilePlayer` nor `useLiveInput` is called, yet `source_.prepare()` runs and `sourceReady_ = true` is set. There is no "else a surfaced error" branch anywhere in this function. Whether this becomes a silent fallback depends on the unseen `WorkbenchAudioSource::prepare()` (in audio-source.h, another chunk): if `prepare()` on a default-constructed/unconfigured source throws `AudioSourceError`, the catch surfaces it and the comment is merely misplaced; if it succeeds as a no-op, the workbench reports `sourceReady_ = true` and `getNextAudioBlock` calls `fillBlock` on an unconfigured source with no error — exactly the silent fallback Constitution V forbids, and exactly the failure mode the comment claims to prevent. Blast radius: an adopter on an output-only device, or an agent reading the comment as a guarantee, takes the "no silent fallback" promise at face value while the code does not enforce it in this function. Fix: add the explicit `else { throw AudioSourceError("no audio source: set ACFX_WORKBENCH_FILE or enable a live input"); }` so the promised error is in this function rather than presumed in a callee, and only set `sourceReady_` after a source is actually selected.

### SafePointer<ParameterView> is constructed on the MIDI thread, off the message thread

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp (handleIncomingMidiMessage, ~lines 178–188)

`handleIncomingMidiMessage` runs on JUCE's MIDI input thread, not the message thread. Inside it:

```cpp
juce::Component::SafePointer<ParameterView> safeView(&paramView_);
juce::MessageManager::callAsync([safeView, id, norm] { ... });
```

The intent — guard the async GUI update so a callback queued before teardown can't touch a destroyed component — is correct, but constructing the `SafePointer` here does the guarding on the wrong thread. `Component::SafePointer` is backed by `juce::WeakReference`, whose `WeakReference::Master` is **not** synchronized; JUCE's contract is that `Component` and its weak-reference machinery are touched only on the message thread. Building the `SafePointer` on the MIDI thread reads/writes `paramView_`'s master weak-reference concurrently with whatever the message thread is doing to that component (including its destruction during teardown), which is a data race — the very race the guard was meant to close is reopened by where the guard object is created. The `node_->setParameter(...)` call on the line above is fine (it documents the core as atomic-pending and lock-free), so the fix is narrow: capture only the plain values (`id`, `norm`) into the `callAsync` lambda and construct/check the `SafePointer` **inside** that lambda, which executes on the message thread. Blast radius: an intermittent, hard-to-reproduce crash/UAF during shutdown while MIDI is arriving — low frequency, high cost, and invisible to the test suite.

### Discrete parameters render combo items labeled with numeric indices, not value names

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:18–21

For a discrete parameter the combo is populated with the integer index as its visible text:

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    row.combo->addItem(juce::String(i), i + 1); // item ids are 1-based
```

The only discrete parameter in this feature is the SVF filter mode (lowpass / bandpass / highpass / etc.). The operator using the "sketch-and-hear" workbench therefore sees a dropdown of `0`, `1`, `2` rather than the mode names. The header for `ParameterView` states the contract as "labelled from the descriptor … the same descriptor table every other adapter consumes (FR-003, SC-006)" — but only the *row* gets a descriptor-derived label (the param name); the discrete *values* get none. This isn't a correctness defect (the normalized round-trip is fine), but it undercuts the stated usability goal of auto-rendered controls: a human cannot tell which mode `1` is without reading source. If `ParameterDescriptor` already carries per-value names, they should be used here; if it does not, that is a gap in the descriptor surface that the auto-rendering contract implies. Blast radius: degraded but functional UX in the one adapter whose entire purpose is interactive auditioning; compounds because every future discrete parameter inherits the same nameless rendering.

### dependencies.cmake pins doctest to v2.5.2 and claims it was verified in-session, but that tag does not appear to exist

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    cmake/dependencies.cmake (header ledger ~lines 14–16; CPMAddPackage ~lines 36–40)

The ledger comment splits pins into "verified by an in-session fetch+build (the `test` preset path)" — listing `doctest v2.5.2` — versus pins merely "captured from the upstream repos." doctest's published releases run through the 2.4.x line (latest `2.4.11`); there is no `2.5.x` series that I am aware of. If `v2.5.2` is not a real doctest tag, two things follow: (1) the "verified by an in-session fetch+build" claim is false for this pin, which is precisely the fabricated/over-stated-pin pattern the surrounding comment is at pains to disclaim ("never a fabricated version number"); and (2) the `test` preset — the single path the ledger asserts was actually exercised — would fail at configure time because `CPMAddPackage(NAME doctest GIT_TAG v2.5.2)` cannot resolve. Because doctest is gated behind `ACFX_BUILD_TESTS` and is the test runner itself, a wrong tag here breaks the whole host-side test build, not one optional target. This should be cross-checked against the upstream tag list and corrected to the real verified tag (e.g. `v2.4.11`); if `v2.5.2` genuinely resolved in-session, the finding is void, but the claimed-verified-yet-suspect combination warrants confirmation before relying on it. Blast radius: the one build path advertised as proven may not configure at all on a clean checkout.

### setNormalized and the combo onChange use inconsistent discreteCount, producing out-of-range selection for degenerate counts

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp (combo onChange ~line 24; setNormalized ~lines 60–66)

The forward path (combo → normalized) divides by the raw descriptor count:

```cpp
const std::uint8_t count = d.discreteCount;
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```

The inverse path (normalized → combo) silently rewrites the count:

```cpp
const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;
int index = static_cast<int>(normalized * static_cast<float>(count));
if (index >= count) index = count - 1;
```

For any normal discrete parameter (`discreteCount >= 2`) the two agree and the round-trip is exact, so this is harmless in the present feature (SVF mode has ≥2 values). But the asymmetry is a latent trap: with `discreteCount == 1` the inverse uses `count = 2` and can compute `index = 1` and call `setSelectedItemIndex(1)` on a combo that holds only item index 0 (out of range); with `discreteCount == 0` the forward path divides by zero (NaN/Inf normalized) while only the inverse guards against it. The `<2 ? 2` clamp papers over a degenerate descriptor in one direction only. Fix: pick one definition of the effective bucket count (reject/҅assert `discreteCount >= 2` at the descriptor boundary, or apply the same clamp in both directions) so forward and inverse cannot diverge. Blast radius is currently nil for this feature, but it is a correctness asymmetry that the next discrete parameter (or a malformed descriptor) would expose.
---
slug: svf-vertical-slice
targetVersion: ""
---

# Audit log — svf-vertical-slice

## 2026-06-26 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260626-01 — CPM bootstrap writes into a directory it never creates

Finding-ID: AUDIT-20260626-01
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    cmake/CPM.cmake:9-23

`CPM_DOWNLOAD_LOCATION` is always placed under a subdirectory, either `${CPM_SOURCE_CACHE}/cpm/...` or `${CMAKE_BINARY_DIR}/cmake/...`, but the bootstrap never creates that parent directory before `file(DOWNLOAD ...)`. On a clean checkout, the default cache from the top-level build is `external/.cpm-cache`, so the destination becomes `external/.cpm-cache/cpm/CPM_0.40.5.cmake`; neither `.cpm-cache` nor `cpm` is guaranteed to exist. CMake’s download step will then fail before dependencies can be configured.

The blast radius is high because a downstream adopter running the documented fresh configure path can hit a hard configure failure before any feature target builds. A reasonable fix is to compute the parent directory with `get_filename_component(... DIRECTORY)` and call `file(MAKE_DIRECTORY ...)` before `file(DOWNLOAD ...)`, covering both the repo cache and binary-dir fallback paths.

### AUDIT-20260626-02 — Queued MIDI UI callback can outlive the workbench component

Finding-ID: AUDIT-20260626-02
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:137-143

`handleIncomingMidiMessage()` schedules `juce::MessageManager::callAsync([this, id, norm] { paramView_.setNormalized(id, norm); });`. That lambda captures a raw `this` and can run after `WorkbenchComponent` destruction; the destructor removes the MIDI callback and shuts down audio, but it does not cancel already queued message-thread work. Closing the app while MIDI traffic is arriving can therefore dereference a destroyed component or `paramView_`.

The blast radius is high because this is a concrete lifetime bug in an interactive adapter path, not just cosmetic UI drift. A reasonable fix is to use JUCE’s safe weak-reference pattern for components, or otherwise gate the async callback on component liveness before touching `paramView_`.

### AUDIT-20260626-03 — Logarithmic parameter math: missing precondition guard produces silent NaN in audio path

Finding-ID: AUDIT-20260626-03
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   single-model (gate-counted high)
Surface:    core/dsp/parameter.h:57-60 (denormalize), core/dsp/parameter.h:77-79 (normalize)

`denormalize` for `ParamSkew::logarithmic` computes `d.min * std::pow(d.max / d.min, norm)`. When `d.min == 0`, this evaluates to `0 * pow(inf, norm)` which is `NaN`. When `d.min == d.max`, `pow(1.0, norm) = 1.0` but the matching `normalize` path divides by `std::log(d.max / d.min) = std::log(1.0) = 0`, producing `NaN` on the round-trip. Both failures are silent: no assertion fires, no error is thrown, and `NaN` propagates through every subsequent `process()` call without any visible indicator until the adapter detects silence or distortion.

`ParameterDescriptor` is a plain aggregate with no constructor validation, no `static_assert` constraints, and no `isValid()` utility. A future effect that accidentally sets `min = 0.0f` with `skew = logarithmic` (a plausible mistake for a cutoff frequency where "minimum" might naively be set to 0 Hz) will silently corrupt the audio stream. The comment on line 55 says "Requires min, max > 0" but this is documentation, not an enforced invariant. A debug-mode `assert(d.min > 0.0f && d.max > d.min)` at the top of the logarithmic branches (both functions) would catch this early without violating real-time constraints. Alternatively, a `static_assert`-backed `ParameterDescriptor::validate()` called in `prepare()` would push the failure to a detectable phase.

---

### AUDIT-20260626-04 — T035 closed as [X] while its own body documents the MCU firmware link as blocked — the adjacent Phase 5 checkpoint still claims "build + link on both MCUs"

Finding-ID: AUDIT-20260626-04 (claude-01 + codex-02; cross-model)
Status:     open
Severity:   blocking
Per-lane:   claude=medium, codex=blocking
Decision:   adjudicated (gate-counted blocking) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — blocking retained.
Surface:    specs/svf-vertical-slice/tasks.md:118-121 (T035 + unchanged Phase 5 checkpoint at line 125)

T035 flips to `[X]` with a body that is candid: "**Blocked / on-board checkpoint:** full firmware ELF link — the installed `arm-none-eabi-gcc` is C-only (ships no libstdc++), so linking + flashing is the proper-toolchain/hardware checkpoint." So the link step did **not** happen. But the Phase 5 checkpoint line immediately below (unchanged context) still reads "**Checkpoint**: US3 done — the cross-platform claim is real (build + link on both MCUs)." The implementer updated the task body to be honest but left the summarizing checkpoint asserting that link succeeded. A reader who trusts the checkbox + checkpoint summary (the two most skimmable signals) concludes MCU linking is verified; only the dense body paragraph corrects that. This is documentation drift: the checkpoint should have been rewritten to "build (compile) on both MCUs; link is the on-hardware checkpoint" when the C-only-toolchain blocker was discovered.

Blast radius: a downstream agent grafting onto the "cross-platform claim is real" line would treat MCU link/flash as proven and skip re-validating it on a real toolchain, shipping an unlinkable firmware path as "done." The body does disclose the blocker in-place, so an attentive reader resolves it — hence medium, not high. A reasonable fix is to edit the checkpoint to match T035's body (compile-verified, link/flash deferred to hardware) so the checkbox, checkpoint, and body all agree.

### AUDIT-20260626-05 — Completion ledger marks acceptance tasks done while their own acceptance remains unverified

Finding-ID: AUDIT-20260626-05
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    specs/svf-vertical-slice/tasks.md:88, specs/svf-vertical-slice/tasks.md:104

T027 and T031 are checked `[X]` as “Run quickstart Scenario B/C end-to-end and confirm all acceptance scenarios,” but both lines then state that the manual acceptance portions still need confirmation: US1 needs live sweep, MIDI CC, and A/B listening; US2 needs DAW instantiation, cutoff automation, and audible parity. Those are not optional extras; they are the independent tests and acceptance scenarios in `spec.md` and `quickstart.md`.

The blast radius is high because this task ledger is an input to unattended downstream agents and release/governance checks. A consumer can reasonably read `[X]` plus the “independently shippable” checkpoints as completion, even though the same line says the actual end-to-end acceptance is still unconfirmed. A reasonable fix is to leave implementation/build tasks checked, but split the manual acceptance tasks into explicit unchecked/manual-verification items or mark the parent story checkpoint as partially verified rather than complete.

### AUDIT-20260626-06 — `WorkbenchAudioSource::fillBlock` contains a throwing path on the audio callback thread

Finding-ID: AUDIT-20260626-06 (claude-03 + codex-02 + claude-02; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=medium, codex=high, sonnet=high
Decision:   agreement (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:46-58 (fillBlock)

`fillBlock(juce::AudioBuffer<float>&)` is the per-block source pump — by name, signature, and role it runs on the audio device callback thread. It opens with `if (!configured_) throw AudioSourceError("fillBlock called before prepare().")` (audio-source.cpp:47-48). `AudioSourceError`'s constructor calls `what.toStdString()` (audio-source.h:33-34), a heap allocation, and a `throw` triggers exception unwinding — both forbidden in the audio path (CLAUDE.md "Real-time safety: no heap allocation or locks in any process()/audio-callback path"; Constitution V). Even though the guard is rarely taken, a `throw` in the RT path is exactly the kind of latent allocation the constitution bans, and it differs in kind from the legitimate throws in the *setup* methods (`useFilePlayer`, `prepare`), which are not on the audio thread.

Secondarily, `transport_.start()` is invoked lazily from this same path (audio-source.cpp:53-54); auto-starting transport from the audio thread is a questionable RT call best done in `prepare()`. Blast radius: this is the workbench's playback hot path, so any adopter who copies this adapter as the "thin adapter" reference (the explicit framing in CLAUDE.md) inherits an RT-unsafe pattern. Fix: replace the `fillBlock` precondition throw with a non-throwing early-return (or assert in debug only), and move the transport start out of the callback into `prepare()`.

### AUDIT-20260626-07 — Workbench runtime source switching installs an unprepared file transport

Finding-ID: AUDIT-20260626-07
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:15-36

`useFilePlayer()` replaces `readerSource_` and calls `transport_.setSource()` on lines 15-17, but only `prepare()` calls `transport_.prepareToPlay()` on lines 30-36. The class and comments advertise runtime selection between a looping file player and live input, so the normal “audio device already running, user picks a file” path installs a new transport source after preparation without preparing it.

The blast radius is high because a downstream workbench UI built against this API will naturally call `useFilePlayer()` from the message thread while audio is already active, and the next audio callback will read from a source that was never prepared for the current sample rate/block size. A reasonable fix is to make source changes go through an explicit prepared-state transition: store the current sample rate/block size, prepare the newly installed source when `configured_` is already true, and release/stop the previous source in a defined order.

### AUDIT-20260626-08 — Concurrent parameter writes from `loop()` race against audio ISR in Teensy adapter

Finding-ID: AUDIT-20260626-08
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=low/latent, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/teensy/teensy-main.cpp:82–87 (loop body) and :34–39 (AcfxSvfNode::update)

`loop()` calls `svf.setParameter(...)` from the Arduino main-loop context while `AcfxSvfNode::update()` calls `svf.process(audioBlock)` from the Teensy Audio library's interrupt-driven audio ISR. These run concurrently with no synchronization. If `SvfEffect` stores parameters as plain `float` fields (the most natural implementation), reads in `process()` and writes in `setParameter()` form a data race — undefined behavior in C++, and practically observable as filter coefficient tearing mid-block on IMXRT1062 which lacks hardware memory ordering for non-atomic stores.

The diff shows no `std::atomic<float>` or spinlock in `teensy-main.cpp` itself, and the effect's parameter storage is in `core/` (not in this chunk), so it cannot be verified here. The fix must be one of: (a) mark parameter fields `std::atomic<float>` with relaxed or acquire/release semantics; (b) use `noInterrupts()`/`interrupts()` guards around writes in `loop()`; or (c) use a lock-free parameter ring. Until one of these is confirmed, this is a latent UB that will only manifest as an occasional audio artifact under load — the hardest class of embedded bugs to reproduce.

---

### AUDIT-20260626-09 — `SvfEffect::setParameter` mutates filter state with no audio-thread ownership boundary

Finding-ID: AUDIT-20260626-09 (codex-01 + claude-02; cross-model)
Status:     open
Severity:   high
Per-lane:   codex=high, sonnet=high
Decision:   agreement (gate-counted high)
Surface:    core/effects/svf/svf-effect.h:64-76, core/effects/svf/svf-effect.h:110-121, core/primitives/svf-primitive.h:27-50

`SvfEffect::setParameter()` immediately calls `applyCutoff()`, `applyResonance()`, or `applyMode()`, and those methods write directly into each per-channel `SvfPrimitive` while `process()` reads and advances the same DaisySP state. The primitive setters at `core/primitives/svf-primitive.h:27-32` mutate the same `svf_`/`mode_` state that `process()` uses at `core/primitives/svf-primitive.h:40-50`, with no documented invariant that `setParameter()` must only run on the audio thread and no internal handoff/snapshot mechanism enforcing that boundary.

The blast radius is high because this core effect is advertised as the shared one-source-many-targets surface. A downstream adapter can reasonably call `setParameter()` from a control/UI loop while audio is processing, which creates a real data race / torn coefficient update against the filter state. The workbench happens to queue GUI/MIDI edits onto the audio callback, and the plugin applies automation inside `processBlock`, but the core contract itself does not encode that requirement; MCU adapters are especially likely to sample controls outside the audio callback.

A reasonable fix is to make the ownership invariant explicit and mechanically hard to violate: either require and document that `setParameter()` is audio-thread-only and update all adapters to honor that, or change `SvfEffect` so `setParameter()` only writes atomic/plain pending parameter values and the audio thread applies them at the start of `process()`.

## 2026-06-26 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260626-10 — `setParameter` thread ownership undocumented despite commit bd79479 claiming to address it

Finding-ID: AUDIT-20260626-10
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   single-model (gate-counted high)
Surface:    host/processor-node/processor-node.h:21,37

`ProcessorNode::setParameter` and `EffectNode::setParameter` carry no documentation about which thread may call them. The commit `bd79479` is titled "Address govern findings: RT-safety, thread ownership, doc drift," but the only apparent path for parameter updates — the `setParameter` virtual on `ProcessorNode` — is still silent on thread ownership. In a DAW or workbench context, `processBlock` fires on the real-time audio thread while parameter changes originate from the UI or MIDI thread. A caller who reads this header gets no signal that calling `setParameter` concurrently with `processBlock` is either safe or unsafe, and `SvfEffect::setParameter` (in the unauditable `svf-effect.h`) contains no visible synchronization. The blast-radius: every adapter that uses `EffectNode` is implicitly racy on `setParameter` without realizing it. A one-line doc comment on the `ProcessorNode` method stating the required thread contract (e.g., "must be called from the same thread as `processBlock`, or under external synchronization") would close the gap.

---

### AUDIT-20260626-11 — MIDI inputs are registered but never enabled

Finding-ID: AUDIT-20260626-11
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:37-46, adapters/workbench/workbench-app.cpp:110-122

The workbench advertises default MIDI CC bindings, and `handleIncomingMidiMessage()` is implemented, but the constructor only calls `deviceManager.addMidiInputDeviceCallback({}, this)` after `setAudioChannels(2, 2)`. In JUCE, adding a MIDI callback does not by itself enable any MIDI input devices; inputs generally need to be enumerated and enabled with `setMidiInputDeviceEnabled(...)` or selected through an audio/MIDI settings UI. On a fresh install, the callback can therefore remain inert even though the app claims MIDI CC control is wired.

Blast radius is high because a consumer running the workbench as written will reasonably expect CC 74/71 to control cutoff/resonance, but the feature silently does not work on the default path. A reasonable fix is to either enable available MIDI inputs explicitly during startup or provide a settings surface that enables/selects them before relying on the callback.

### AUDIT-20260626-12 — `prepareToPlay` catches `AudioSourceError` and stores it in a field that is never read

Finding-ID: AUDIT-20260626-12
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=high, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/workbench/workbench-app.cpp:68–73, 156

`prepareToPlay` wraps the audio-source setup in a try/catch and stores the error message in `lastSourceError_`:

```cpp
} catch (const AudioSourceError& e) {
    lastSourceError_ = juce::String(e.what());
}
```

`lastSourceError_` is declared as a private `juce::String` field and never read anywhere in the file — there is no `paint()`, no label, no alert, no logger call, no assertion. The operator has no indication that audio source setup failed; the workbench just silently produces silence. Per the project rule in CLAUDE.md: *"Never implement fallbacks or mock data outside of test code. Throw errors with a description of the missing functionality. Errors let us know that something isn't implemented. Fallbacks and mock data are bug factories."* A field that stores an error and then discards it is a fallback that hides failures. The fix is to surface the error on the message thread — e.g., via a `juce::AlertWindow::showMessageBoxAsync` or a persistent error label in the UI — rather than swallowing it into an unread string.

---

### AUDIT-20260626-13 — `getNextAudioBlock` silently falls back to cleared buffer on `AudioSourceError`

Finding-ID: AUDIT-20260626-13
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=high, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/workbench/workbench-app.cpp:89–95

Inside the real-time audio callback:

```cpp
try {
    // ...
    source_.fillBlock(region);
} catch (const AudioSourceError&) {
    buffer.clear(startSample, numSamples);
    return;
}
```

The exception message is not stored, not surfaced on the message thread, and the workbench produces silence with zero operator feedback. This is a stricter violation than Finding-01: at least `prepareToPlay` stored the string. Here the error is discarded entirely. The pattern used in `handleIncomingMidiMessage` — `juce::MessageManager::callAsync` with a `SafePointer` — is the correct mechanism to bridge RT-callback failures to the GUI thread. The fix is to post the error to the message thread for display rather than silently clearing the buffer.

---

### AUDIT-20260626-14 — `setParameter` called unconditionally every audio block inside Daisy ISR — new surface of prior RT-safety finding

Finding-ID: AUDIT-20260626-14
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   single-model (gate-counted high)
Surface:    adapters/daisy/daisy-main.cpp:26-28

`AudioCallback` calls `svf.setParameter()` three times per block, unconditionally, on every invocation:

```cpp
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},    hw.adc.GetFloat(kAdcCutoff));
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance},  hw.adc.GetFloat(kAdcResonance));
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},       hw.adc.GetFloat(kAdcMode));
```

The prior govern finding `e8e701847d91847a::AUDIT-BARRAGE-codex-01+AUDIT-BARRAGE-claude-02` flagged "`SvfEffect::setParameter` mutates filter state with no audio-thread ownership boundary." The Daisy adapter is a new file introduced in this diff; it exhibits the same structural pattern on a new surface. Commit bd79479 claims to "Address govern findings: RT-safety, thread ownership, doc drift" but this file was not guarded.

There is no race condition in the Daisy adapter because the main loop (`for(;;){}`) does nothing — all parameter writes come from within the callback itself. However, two concerns remain: (1) if `setParameter` recomputes SVF coefficients (typically involving `tan()` or similar transcendental math), that computation happens inside the hardware ISR at every block boundary (≈1 ms at 48 kHz / 48-sample block), adding unbounded CPU overhead that can cause audio underruns; (2) the pattern, once established in adapters, communicates to future adapter authors that calling `setParameter` from the ISR is the correct idiom, regardless of whether a future implementation makes it expensive. A minimal fix is to read ADC values into temporaries, compare to the last known value, and call `setParameter` only when the value changes by more than a dead-band threshold.

---

### AUDIT-20260626-15 — Runtime audio-source switch frees the reader before rebinding the transport — message-thread/audio-thread use-after-free

Finding-ID: AUDIT-20260626-15 (claude-01 + codex-01 + codex-02; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=high, codex=high
Decision:   agreement (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:8-26 (useFilePlayer), :32-38 (useLiveInput)

`useFilePlayer` is documented as a "runtime source switch from the message thread" (line 18-20) while `fillBlock` runs concurrently on the audio thread calling `transport_.getNextAudioBlock`. The switch is unsynchronized against the audio thread, and the statement ordering is itself unsafe: `readerSource_ = std::make_unique<...>(reader, true)` (line 14) **destroys the old `AudioFormatReaderSource` before** `transport_.setSource(readerSource_.get())` (line 16) re-points the transport. Between those two lines the transport's internal source pointer still references the just-freed object. JUCE's `AudioTransportSource` guards `setSource`/`getNextAudioBlock` with an internal `CriticalSection`, but the `unique_ptr` reassignment that frees the old source happens **outside** that lock — so an audio callback firing in that window dereferences freed memory (use-after-free). `useLiveInput` has the same shape: `readerSource_.reset()` (line 35) frees the source the audio thread may still be pulling from. Additionally, the lock `getNextAudioBlock` takes on the audio thread is itself an RT-safety violation (priority inversion under the no-locks-in-audio-callback rule).

The blast radius: a downstream operator who switches source mid-playback (the documented use case) gets intermittent crashes/corruption that are nearly impossible to reproduce deterministically. A reasonable fix sequences the swap so the audio thread never sees a freed source — clear the transport source under its lock first (`transport_.setSource(nullptr)`), then destroy the old reader, then install and bind the new one — or, better, post the swap to the audio thread via a lock-free single-slot handoff and let the audio thread perform the pointer exchange.

### AUDIT-20260626-16 — Race condition: `svf.setParameter()` in `loop()` is not guarded against audio ISR

Finding-ID: AUDIT-20260626-16
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/teensy/teensy-main.cpp:83-89 (`loop()`) vs :35 (`svf.process()` in `update()`)

On Teensy 4.x the Teensy Audio Library fires `AcfxSvfNode::update()` from a hardware interrupt at ~344 Hz (44100 / 128). `loop()` runs on the main (non-interrupt) thread. The global `svf` object (`acfx::SvfEffect svf`, line ~18) is accessed from both contexts without synchronization:

- `loop()` lines 83-89 call `svf.setParameter(...)` three times in sequence.
- `update()` line 35 calls `svf.process(audioBlock)`, which reads the same parameter state the loop is writing.

There is no `AudioNoInterrupts()` / `AudioInterrupts()` guard around the `setParameter` calls in `loop()`. On ARM Cortex-M7 (IMXRT1062), non-atomic multi-word writes to struct members are not ISR-safe — the audio interrupt can fire between any two `setParameter` calls and observe a torn state. This is the standard Teensy Audio Library data-race failure mode.

The fix is to wrap the three `setParameter` calls in `loop()` with `AudioNoInterrupts()` / `AudioInterrupts()`, which disables the audio interrupt for the duration of the parameter update:

```cpp
AudioNoInterrupts();
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff}, ...);
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance}, ...);
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode}, ...);
AudioInterrupts();
```

---

## 2026-06-26 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260626-17 — Thread-ownership comment over-claims: prepare()/reset() mutate coefficients off the audio thread with no synchronization

Finding-ID: AUDIT-20260626-17
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    core/effects/svf/svf-effect.h:16-22, 98-119, 152-156

The class header comment claims (lines 16-22): *"the actual filter coefficients are mutated on exactly one thread, with no torn-coefficient data race against process() — an invariant the core encodes itself rather than asking every adapter to honor."* The member comment (lines 152-153) reinforces it: applied state is *"owned by the audio thread (read/written only in prepare/reset/applyPending)."*

This is only true for the `setParameter` → `applyPending` path. The actual coefficient mutation happens in `applyCutoff/applyResonance/applyMode` (lines 110-119), and those are **also** called from `prepare()` (line 89, via `applyAll()`) and `reset()` (line 95). `prepare()` is documented in `process-context.h` as running *"before audio starts and on any device change"* — i.e. on the audio-device/message thread, **not** the audio thread. So on a device change while audio is live, `prepare()`/`reset()` mutate `svf_` internal state, `sampleRate_`, and `numChannels_` concurrently with `process()` — a genuine data race the comment claims the core has eliminated.

Blast radius: the audit's unattended-agent framing makes this high. An adapter author who reads "the core encodes this invariant itself" reasonably concludes `reset()` is safe to wire to a UI "reset filter" button or a settings change while the stream runs. The setParameter path was hardened (round-2 govern findings), but the comment now overstates the guarantee to cover prepare/reset, which remain adapter-discipline-dependent. Fix: scope the claim explicitly to `setParameter`, and state the real invariant for prepare/reset ("must be called only when the audio stream is stopped — the adapter owns this") rather than asserting single-thread ownership the code doesn't enforce.

### AUDIT-20260626-18 — Non-lock-free float atomics leak into the embedded audio/control path

Finding-ID: AUDIT-20260626-18
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    core/effects/svf/svf-effect.h:83-89, core/effects/svf/svf-effect.h:107-123, core/effects/svf/svf-effect.h:167-169

`SvfEffect` makes the cross-thread parameter handoff depend on `std::atomic<float>` for normalized values, and those atomics are used by both `setParameter()` and `process()` via `applyPending()`. The surrounding comments claim this is the core RT-safe boundary for UI/MIDI/MCU callbacks and the audio thread, but the code never proves that `std::atomic<float>` is lock-free on the target toolchains. On Cortex-M/embedded standard libraries this can degrade to compiler-runtime atomic calls or a locking/critical-section implementation, and in some configurations it can fail at link time if the atomic runtime is not present.

The blast radius is high because this surface is the shared core path for Daisy/Teensy and desktop adapters: a downstream adopter can correctly call `setParameter()` from a control callback and still end up with a non-RT-safe or non-portable primitive inside the feature’s advertised RT-safe handoff. A reasonable fix would store the normalized value in a lock-free integer representation, for example fixed-point `std::atomic<std::uint32_t>` with explicit encode/decode, and add compile-time checks such as `static_assert(decltype(pendingNorm_)::value_type::is_always_lock_free)` or equivalent target-specific portability tests.

### AUDIT-20260626-19 — Workbench parameter edits bypass the claimed RT-safe handoff

Finding-ID: AUDIT-20260626-19
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:26-35

The file-level contract says GUI/MIDI parameter edits are “handed to the audio thread through a small lock-free queue so process() stays RT-safe,” but the GUI callback wired into `ParameterView` calls `node_->setParameter(id, norm)` directly from the message thread. If `ProcessorNode::setParameter` is anything other than a trivial atomic store for every current and future effect, this creates a cross-thread mutation path racing the audio callback; even if today’s SVF happens to use atomic pending state, the adapter boundary is documenting and implementing different contracts.

Blast radius is high because downstream adapter authors can copy this workbench pattern as the host boundary and assume the queue exists. A reasonable fix is to either implement the described queue for both GUI and MIDI paths, or update the boundary so `ProcessorNode::setParameter` is explicitly the only RT-safe cross-thread ingress and enforce that contract in the processor/effect interface.

### AUDIT-20260626-20 — Non-atomic `fileBuffer_` swap in `useFilePlayer()` races with the audio thread's `fillBlock()`

Finding-ID: AUDIT-20260626-20 (claude-01 + codex-01 + codex-02 + claude-02 + claude-08; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=high, codex=high, sonnet=high
Decision:   agreement (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:28-31 (the swap) vs. :63-75 (the reader)

`useFilePlayer()` performs `fileBuffer_ = std::move(decoded);` (line 28) — a non-atomic move-assignment that destroys the previous `AudioBuffer<float>`'s heap storage — while the audio thread may be inside `fillBlock()` calling `fileBuffer_.getReadPointer(...)` and dereferencing `src[pos]` (lines 63-67). The `std::atomic` flags (`hasFile_`, `playPos_`) are published *after* the move (lines 29-31), so they order the *flag* but provide no protection for the buffer storage itself: an in-flight reader holding a `getReadPointer` into the old buffer gets a use-after-free the moment the move frees it. Notably the code does not even set `hasFile_=false` *before* the swap, which would at least let a fresh `fillBlock` bail out.

The header (audio-source.h:11-18) makes an *absolute* RT-safety claim and specifically says it eliminated "no transport object whose source pointer the audio thread could see freed mid-swap." That fix removed the transport but moved the same race onto the buffer reassignment — the channel was relocated, not closed. For a "sketch-and-hear workbench," loading a new file *during live playback* is the obvious use case, and `useFilePlayer()` exposes no precondition guard or documented stop-the-audio contract. Blast radius: an adopter wiring a "load file" button to this method while audio runs gets intermittent UAF crashes that won't reproduce deterministically. A correct fix double-buffers (hold both old and new, swap an `atomic<int>` active-index) or requires the caller to quiesce the audio graph first and states that as an enforced precondition, not a comment.

## 2026-06-26 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260626-21 — Completed tasks still contain pending or blocked acceptance

Finding-ID: AUDIT-20260626-21
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    specs/svf-vertical-slice/tasks.md:88, specs/svf-vertical-slice/tasks.md:107, specs/svf-vertical-slice/tasks.md:127

`T027`, `T031`, and `T035` are marked `[X]`, but their own text says parts of the acceptance remain pending or blocked: US1 still needs live sweep/MIDI/A-B listening, US2 still needs DAW instantiation/automation/parity, and US3 says the firmware ELF link is blocked by a C-only `arm-none-eabi-gcc`. This contradicts the task-list completion signal: an unattended downstream agent or release gate will read `[X]` as complete and close the feature despite acceptance gaps.

The blast radius is high because these are the user-story acceptance tasks, not cosmetic notes. A reasonable correction is to split each item into completed automated/build verification versus explicit unchecked manual/hardware acceptance tasks, or leave the parent acceptance task unchecked until the stated independent test is actually satisfied.

### AUDIT-20260626-22 — `clamp01` passes NaN/non-finite values straight through, poisoning RT filter state irrecoverably

Finding-ID: AUDIT-20260626-22
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    core/dsp/parameter.h:27 (`detail::clamp01`), consumed by `denormalize` at :34-58 and `SvfEffect::applyPending`/`applyCutoff`

`clamp01` is `x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x)`. For `x = NaN`, both `NaN < 0.0f` and `NaN > 1.0f` evaluate false, so the function **returns NaN unchanged** — the one input class a 0..1 clamp exists to neutralize is the one it lets through. The path is reachable end-to-end: `SvfEffect::setParameter` stores the caller's float bits verbatim (`svf-effect.h:floatBits`), `applyPending` reads them back and calls `denormalize(kParams[kCutoff], NaN)`, whose logarithmic branch computes `min * std::pow(max/min, NaN) = NaN` (parameter.h:52-53). That NaN reaches `SvfPrimitive::setFreq` → `daisysp::Svf::SetFreq`, and a NaN coefficient propagates into the filter's recursive state on the very next sample, corrupting **all future output on that channel until `reset()`** — there is no self-healing.

Blast radius: a single malformed automation value (buggy host automation curves, a denormalized/uninitialized control read, an MCU control loop with a divide-by-zero) silently and permanently destroys the audio path with no error surfaced — exactly the "fallback that hides a failure mode" the project guidelines forbid, except here it's a guard that fails open. An unattended adopter wiring up parameter automation will trust the clamp and never see the corruption coming. Fix: make `clamp01` non-finite-safe, e.g. `return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f;` — because `NaN >= 0.0f` is false, NaN maps to 0, and `±inf` clamp correctly. This is the round-0 self-red-team driver applied: the round-3 atomic hardening is correct, but it faithfully *transports* a poisoned value into the hot path.

### AUDIT-20260626-23 — ProcessContext is prepared for a hardcoded 2 channels but process() drives up to 8 channels from the live buffer

Finding-ID: AUDIT-20260626-23
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:60 (prepare) vs :97-103 (process)

`prepareToPlay` builds `const ProcessContext ctx{sampleRate, blockSize, 2}` with a literal channel count of 2 (line 60). `getNextAudioBlock` then derives the actual channel count from the live device buffer: `numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels)` (line 90, `kMaxChannels == 8`) and constructs `AudioBlock block(chans.data(), numChannels, numSamples)` (line 102) passed to `node_->processBlock`. `setAudioChannels(2, 2)` is a *request*, not a guarantee — JUCE may open a device with a different active channel count, in which case `buffer.getNumChannels()` can exceed 2.

If `SvfEffect::prepare` sizes its per-channel state (filter z-state, atomics) to `ctx.numChannels()`, and `process()` iterates `block.numChannels()`, a device that opens with >2 channels yields out-of-bounds reads/writes on the per-channel state — in the RT callback, the worst place for it. Even short of OOB, preparing for 2 while processing N is a contract violation the effect can't defend against. The prepared channel count must be the same quantity the process path uses: derive it from the device (`numInputChannels()`/output count) or clamp `processBlock` to `ctx.numChannels()`. I can't see `SvfEffect` in this chunk to confirm the OOB, so the blast radius is conditional on its state sizing — but the inconsistency itself is real and the failure mode is severe, hence high.

### AUDIT-20260626-24 — Teensy mode-pin normalization produces `norm = 1.0`, potentially yielding out-of-range discrete index

Finding-ID: AUDIT-20260626-24
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/teensy/teensy-main.cpp:86

`analogRead()` returns values in `[0, 1023]`. The mapping `static_cast<float>(analogRead(kModePin)) / 1023.0f` yields exactly `1.0f` when the pin reads 1023 — its documented maximum. If `SvfEffect::setParameter` discretizes mode using the common idiom `floor(norm * count)` (count = 3 for LP/HP/BP), then `floor(1.0f * 3) = 3`, which is an out-of-range index. This is a 1-in-1024 frequency hardware bug that would manifest as unpredictable filter behaviour when the cutoff knob sits at its physical maximum. The project constitution forbids defensive clamping in the core ("no fallbacks outside test code"), so the adapter must guard this boundary. The fix is `std::min(static_cast<float>(analogRead(kModePin)) / 1023.0f, std::nextbelow(1.0f))` or `analogRead(kModePin) * count / 1024` in integer arithmetic, depending on how the effect's discretization is defined.

---

## 2026-06-26 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260626-25 — Logarithmic-descriptor invariant (0 < min < max) is enforced only by a debug `assert`, so a malformed descriptor produces NaN straight into the audio path in release builds

Finding-ID: AUDIT-20260626-25 (claude-03 + claude-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=low, sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=high, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    core/dsp/parameter.h:62-66 (denormalize logarithmic), :91-93 (normalize logarithmic)

The same file that added `clamp01` specifically because a NaN "poison[s] the filter state irrecoverably" guards the logarithmic mapping's `0 < min < max` precondition with only `assert` (lines 64-65, 92), which compiles out in release. If a descriptor is authored with `min == 0` and `skew == logarithmic`, release-mode `denormalize` evaluates `0.0f * std::pow(max/0, norm)` → `0 * inf` → **NaN**, flowing directly into the audio path the round-4 work was hardening — the identical "guard that fails open" the file's own comment criticizes, just relocated from the `norm` channel to the descriptor channel.

Blast radius is genuinely low because `ParameterDescriptor`s are static, developer-authored, compile-time constants, not runtime input — a bad descriptor is a build-time author error a debug test run would catch, not an adversary-reachable state. But the asymmetry is worth noting: the runtime input (`norm`) is now belt-and-suspenders NaN-safe while the equally-NaN-generating descriptor invariant relies entirely on someone running a debug build. A `static_assert` at the descriptor-table definition site (or a release-safe finite-and-ordered check that returns a clamped fallback) would close the channel without re-introducing a hot-path branch, matching the stated NaN-safety posture consistently across both inputs to the mapping.

### AUDIT-20260626-26 — Workbench has no path to select the required file-player source

Finding-ID: AUDIT-20260626-26
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:74-89

`prepareToPlay()` says that when there is no live input “the operator must point the built-in player at a file,” but this component never exposes any file-selection path or calls `source_.useFilePlayer(...)`. The only source selection in the diff is `source_.useLiveInput(inputs)` when `inputs > 0`; otherwise `source_.prepare(...)` throws, the catch posts a warning, and audio startup continues with no configured source.

That matters because the declared workbench behavior is sketch-and-hear with a built-in looping file player or live input. A downstream user on a desktop with no active input device will hit this as a functional failure: the app opens, reports “No audio source configured,” and has no in-app control to recover. The blast radius is `high` because this is a correctness defect a real adopter can hit on a fresh install, and it contradicts the “no silent fallback” discipline by leaving the stream alive after source setup failed. A reasonable fix is to add an actual setup-time file selection/default file-player path before `source_.prepare(...)`, or fail/disable audio until a source is explicitly configured instead of continuing after the exception.

### AUDIT-20260626-27 — `source_` used in audio callback after `prepareToPlay` exception skips `prepare`

Finding-ID: AUDIT-20260626-27
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:74–97

`prepareToPlay` wraps both `source_.useLiveInput(inputs)` and `source_.prepare(sampleRate, blockSize)` in a single `try` block. If `useLiveInput` throws an `AudioSourceError`, execution jumps to the catch handler and `source_.prepare` is never called. The catch handler posts an async dialog but does not set any flag that would gate `getNextAudioBlock`. As a result, `source_.fillBlock(region)` at line ~97 runs on a `WorkbenchAudioSource` that was never prepared — a use-before-prepare that the RT callback cannot recover from. Whether `WorkbenchAudioSource::fillBlock` on an unprepared instance crashes, silently outputs garbage, or accesses freed memory is not visible in this diff, but the state is unconditionally reachable: live input present + driver-level error opening it. The constitution (Commandment V) says "raise descriptive errors for missing functionality instead of silently falling back" — the current code shows a dialog but continues running with a broken source, which is a soft fallback rather than a clean stop. A minimal fix: set `bool sourcePrepared_ = false` in the catch path and guard `source_.fillBlock` in `getNextAudioBlock` behind it, or re-throw to abort audio initialisation.

---

### AUDIT-20260626-28 — CI "Build plugin (VST3 / AU / CLAP)" builds the shared-code target, not the plugin formats

Finding-ID: AUDIT-20260626-28 (claude-01 + claude-05 + codex-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=high, codex=high
Decision:   agreement (gate-counted high)
Surface:    .github/workflows/ci.yml:44-47 (cross-ref adapters/plugin/CMakeLists.txt:1-44)

The desktop-build job's final step is labeled "Build plugin (VST3 / AU / CLAP)" and runs `cmake --build --preset desktop --target acfx_plugin -j`. But `juce_add_plugin(acfx_plugin ...)` (adapters/plugin/CMakeLists.txt:1) does **not** make `acfx_plugin` the plugin binary — JUCE creates `acfx_plugin` as the *shared-code* static library and emits the actual format artifacts as separate targets (`acfx_plugin_VST3`, `acfx_plugin_AU`, the CLAP target, and the aggregate `acfx_plugin_All`). Building the bare `acfx_plugin` target compiles the plugin sources but never links the VST3/AU/CLAP wrappers or produces any plugin bundle.

The blast radius: this is the only CI gate that purports to prove the DAW-plugin slice (Phase 4, e74b0db) builds. A linker error in the JUCE format wrappers, a broken CLAP-extension registration (`clap_juce_extensions_plugin`, CMakeLists.txt:42), or a missing AU/VST3 symbol would all pass CI green while the step claims all three formats built. A downstream adopter trusting the green check would believe the plugin formats compile and link when CI never exercised them. A correct fix targets `acfx_plugin_All` (or enumerates the format targets explicitly) so the wrappers actually link.

### AUDIT-20260626-29 — Daisy mode-knob normalization reproduces the lifted Teensy out-of-range exposure on an unaudited sibling

Finding-ID: AUDIT-20260626-29
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    adapters/daisy/daisy-main.cpp:33-47

The convergence ledger lifts `d58…claude-01`: "Teensy mode-pin normalization produces `norm = 1.0`, potentially yielding out-of-range discrete index." The daisy adapter has the identical exposure on a different surface, and nothing in this chunk shows it was covered by the round-4 fix. `maybeSet` (line 33) reads `hw.adc.GetFloat(adc)` — a value in the **closed** range [0.0, 1.0] — and passes it verbatim as the normalized parameter value via `svf.setParameter(... v)` (line 38). For `kMode` (line 47) the effect denormalizes a normalized value into a discrete mode index; if that mapping is `floor(norm * modeCount)` (the natural discrete denormalization), an ADC reading of exactly `1.0` yields index `modeCount`, one past the last valid mode.

Applying the channel-enumeration / fix-review driver: the lifted finding was dispositioned on the Teensy path, but the *value channel* it opens (any adapter feeding a raw [0,1]-inclusive control into the mode descriptor) was not enumerated. The daisy path is precisely that channel and carries no fixture proving the `norm == 1.0` boundary is safe. If the descriptor clamps the discrete index this is benign; if it does not (which the existence of a Teensy-specific fix implies), a knob at full deflection selects an out-of-range mode and reads/writes past the mode table in the real-time callback. A correct fix clamps the discrete index in the descriptor (one fix covering all adapters) rather than per-adapter, and adds a `norm == 1.0` fixture.

### AUDIT-20260626-30 — `processBlock` constructs a `std::function` every block — "Allocation-free" claim relies on unguaranteed SBO

Finding-ID: AUDIT-20260626-30 (claude-02 + codex-01 + claude-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=medium, codex=high, sonnet=high
Decision:   agreement (gate-counted high)
Surface:    adapters/plugin/plugin-processor.cpp:30-34; adapters/plugin/plugin-parameters.h:21 (ApplyFn = std::function), plugin-parameters.cpp:76 (apply signature)

`processBlock` (cpp:30-34) calls `parameters_.apply([this](ParamId id, float normalized){ node_.setParameter(id, normalized); })`, and `apply` takes `const ApplyFn&` where `ApplyFn = std::function<void(ParamId,float)>` (header:21). Every audio callback therefore constructs a fresh `std::function` from the lambda on the realtime thread. The inline comment explicitly claims *"Allocation-free."* That claim is only true by small-buffer-optimization luck: `std::function` is *not* guaranteed by the standard to avoid heap allocation. For a single `this`-pointer capture, libc++ and libstdc++ both SBO it, so on JUCE's actual targets it happens to be allocation-free today — but the guarantee is the library's, not the code's.

Given this feature's entire govern history is RT-safety hardening (commits 2fef393/bd79479: "no heap allocation in process()"), a comment asserting allocation-freedom via a non-guaranteed mechanism is a fragility worth closing. The latent failure mode: anyone who adds a second capture to that lambda (or builds against a stdlib without SBO) silently introduces a per-block heap allocation on the audio thread, and the allocation sentinel test (`tests/core/no-allocation-test.cpp`, other chunk) almost certainly does not exercise this JUCE plugin path. Fix: pass the apply target as a non-owning function-ref type or a template/concrete callable instead of constructing `std::function` per block, or at minimum static_assert/document the SBO dependency.

## 2026-06-26 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260626-31 — T035 claims Cortex-M7 C++ compile-verification with a toolchain it simultaneously describes as C-only

Finding-ID: AUDIT-20260626-31
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    specs/svf-vertical-slice/tasks.md (T035 task line + Phase 5 checkpoint)

T035 (now marked `[X]`) asserts two things in one breath: "the identical `core/effects/svf` cross-compiles for Cortex-M7 at both C++17 (concept degraded) and C++20 (named concept), the lock-free `is_always_lock_free` static_assert holds on-target" — and then, as the reason the *link* is deferred, "blocked in this environment by a C-only `arm-none-eabi-gcc` with no libstdc++." These contradict each other. A `static_assert(... is_always_lock_free ...)` is a *compile-time* gate inside a C++ translation unit; for it to "hold on-target" the C++ source must have been compiled by an ARM C++ frontend with `<atomic>` available. A genuinely "C-only `arm-none-eabi-gcc` with no libstdc++" cannot compile `core/effects/svf` at all — `#include <atomic>`/`<cstdint>` fail at the compile step, not the link step. So either the compile happened (and the toolchain is not C-only) or it didn't (and the "compile-verifies at C++17 and C++20" + "static_assert holds on-target" claims are unsupported).

Blast radius: an agent or adopter reading the Phase 5 checkpoint ("US3 compile-verified — the identical core cross-compiles for Cortex-M7 at both C++17 and C++20") will treat the MCU portability claim (SC-007, the central thesis of the feature) as machine-proven and build downstream work on it. The artifact's own toolchain description says that verification was impossible here, so the agent inherits an unverified claim presented as verified. A reasonable fix: state precisely *what compiler actually performed the compile-verify* (e.g. a host `clang --target=arm-none-eabi -nostdlib` syntax/semantic check, or a different g++), and if no ARM C++ compile actually ran, downgrade the claim from "compile-verified for Cortex-M7" to "host-compiled with ARM-target flags" or mark it unverified.

### AUDIT-20260626-32 — T035 is marked complete while the required MCU link is explicitly unverified

Finding-ID: AUDIT-20260626-32
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    specs/svf-vertical-slice/tasks.md:116-132

Phase 5 still defines the user-story goal and independent test as “compiles and links” for Daisy/Teensy, and says Scenario D should “build & link” the same core. But T035 is checked off while its new text narrows the completed work to compile-only verification and explicitly says the “full firmware ELF link + flashing” is an unchecked operator/on-hardware checkpoint blocked by the current toolchain.

The blast radius is high because a downstream consumer or unattended agent reading task completion mechanically will conclude US3’s build-and-link acceptance is done, even though the file itself says the link has not been performed. A reasonable fix is to split T035 into an automated compile/portability task that can be checked and a separate unchecked link/flash acceptance task, or leave T035 unchecked until the stated Scenario D link criterion is satisfied.

### AUDIT-20260626-33 — README Scenario C tells users to build `acfx_plugin`, which (per CI's own comment) produces no plugin bundles

Finding-ID: AUDIT-20260626-33 (claude-01 + codex-01 + claude-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=high, codex=high, sonnet=medium
Decision:   agreement (gate-counted high)
Surface:    README.md:73-76 (Scenario C block) vs `.github/workflows/ci.yml:46-49` and `adapters/plugin/CMakeLists.txt`

The lifted finding `6a56babffbf5b038::…CI builds the shared-code target, not the plugin formats` was fixed **only in CI** — `ci.yml:49` now builds `acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP`, and the CI comment at `ci.yml:46-48` explicitly states: *"The aggregate acfx_plugin target builds only the shared code; the format wrappers are separate targets that produce the actual bundles."* The README was not updated to match. README Scenario C (`README.md:75-76`) still instructs:

```
cmake --build --preset desktop --target acfx_plugin
```

By the workflow's own documented semantics, this builds the shared-code object library and produces **no VST3/AU/CLAP bundle**. Blast radius: a user or unattended agent following the README's "Desktop plugin (VST3 / AU / CLAP)" section runs a build that succeeds, sees no error, and concludes the plugin built — then finds nothing to load in a DAW. This is the round-0 self-red-team case: the fix for the CI finding closed the CI surface while leaving the sibling README surface holding the exact shape the finding identified. Fix: change README Scenario C to build the format targets (`--target acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP`), matching CI.

### AUDIT-20260626-34 — Daisy mode-knob normalization — lifted HIGH finding still open with no fix visible in this chunk

Finding-ID: AUDIT-20260626-34
Status:     open
Severity:   high
Per-lane:   sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/daisy/daisy-main.cpp:33-39

The convergence JSON (`liftedFindings`) carries this finding as unresolved:

> "Daisy mode-knob normalization reproduces the lifted Teensy out-of-range exposure on an unaudited sibling"

In `daisy-main.cpp`, `maybeSet()` forwards the raw ADC float (`hw.adc.GetFloat(adc)`, range `[0.0, 1.0]`) directly to `setParameter` for all three parameters including mode:

```cpp
void maybeSet(acfx::SvfEffect::Param param, int adc) {
    const float v = hw.adc.GetFloat(adc);
    if (v < lastKnob[adc] - kKnobDeadband || v > lastKnob[adc] + kKnobDeadband) {
        lastKnob[adc] = v;
        svf.setParameter(acfx::ParamId{static_cast<std::uint8_t>(param)}, v);
    }
}
```

Mode is a discrete enum (LP / BP / HP). If the descriptor maps `[0, 1)` to mode index 0, `[1/3, 2/3)` to index 1, and `[2/3, 1.0]` to index 2 (or any comparable scheme), values near `1.0` from a fully-clockwise knob must land in a defined bucket. The Teensy finding (in another chunk) identified that this boundary was not being clamped or validated, producing an out-of-range mode index. No fix to the Daisy adapter is visible here — the `maybeSet` implementation is unchanged — and the finding is not listed under `closedInLoopFindings`. The fix may live in a core-side denormalization path visible only in another chunk (e.g., the parameter descriptor); if so, that chunk should close this finding explicitly. As written, the Daisy adapter is an unverified sibling with the same surface.

### AUDIT-20260626-35 — useLiveInput() leaves hasFile_ set, creating asymmetric state that relies on fillBlock's check order

Finding-ID: AUDIT-20260626-35 (claude-05 + claude-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=low, sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    adapters/workbench/audio-source.cpp:33-40 (vs 17-31)

`useFilePlayer` carefully resets the *other* source's flag
(`live_.store(false, release)` at line 30) so the two selection paths are
mutually exclusive. `useLiveInput` does not do the mirror: it sets
`live_.store(true)` but never clears `hasFile_`. So the sequence
`useFilePlayer(f)` then `useLiveInput(n)` (both legal before `prepare()`, since
`configured_` is still false) leaves `hasFile_ == true` AND `live_ == true`
simultaneously.

Today this is masked because `fillBlock` checks `live_` first and returns
(line 53), so live correctly wins. But the invariant "exactly one source
selected" is not actually maintained in the state — it's maintained only by the
ordering of two reads in a different function. If that check order is ever
reordered, or `isLiveInput()` is consulted alongside `hasFile_` elsewhere, the
inconsistency becomes a bug. Add `hasFile_.store(false, std::memory_order_release)`
to `useLiveInput` to make the two selectors symmetric and the state
self-consistent regardless of who reads it.
```

### AUDIT-20260626-36 — Workbench file length overflows before validation

Finding-ID: AUDIT-20260626-36
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:19-24

`reader->lengthInSamples` is a 64-bit value, but the code casts it to `int` before validating it: `const int numSamples = static_cast<int>(reader->lengthInSamples);`. A valid long audio file whose sample count exceeds `INT_MAX` can wrap or truncate, after which the code either rejects it as “empty” or allocates/reads the wrong length.

The blast radius is high because this is a user-facing workbench source path: an adopter can hit it with a real long recording, and the failure mode is misleading or incorrect playback rather than a clear “file too large” diagnostic. A reasonable fix is to validate `reader->lengthInSamples` in its original integer width first, reject values larger than `std::numeric_limits<int>::max()` with a descriptive `AudioSourceError`, and only then cast for `AudioBuffer`.

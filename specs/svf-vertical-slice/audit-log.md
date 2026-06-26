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

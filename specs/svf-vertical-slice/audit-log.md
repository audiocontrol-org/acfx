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

# Quickstart & Validation — Workbench audio device + source + MIDI selection

Runnable scenarios that prove the feature. Each maps to a user story / success
criterion in [spec.md](./spec.md). Implementation details live in `tasks.md`; this is
the run-and-verify guide.

## Prerequisites

- A C++20 desktop toolchain and the `desktop` CMake preset (JUCE 8 is CPM-pinned).
- An audio output device; for input/source tests, an input device and/or an audio
  file. For routing another app's audio, a loopback device (e.g. BlackHole) selected
  as the input device.

## Build

```bash
cmake --preset desktop
cmake --build --preset desktop --target acfx_workbench
# launch the built app bundle
```

## Scenario A — Host unit test (the tested seam) → SC (config integrity)

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

**Expected**: all tests pass, including the new `SourceConfig` serialize/parse
round-trip and safe-default-on-garbage cases. The pre-existing core tests stay green.

## Scenario B — Choose input/output devices → US1, SC-001, SC-003

1. Launch the workbench; click **Audio Settings…**.
2. Change the **output** device to another available device → processed audio now
   plays out of the new device, no app restart, no crash or stuck stream.
3. Change the **input** device → the filter now processes the new input.
4. Select a device that fails to open → the failure is shown and the previous device
   keeps working.

## Scenario C — Choose the source from the UI → US2, SC-002, SC-003

1. With audio running on live input, set the source bar to **File** and pick a file →
   it loops through the filter; the **Process (A/B)** toggle compares dry vs filtered;
   no glitch/crash at the switch.
2. Pick a different file → it plays without a restart.
3. Choose **File** then cancel the chooser → the source stays valid (no broken
   no-source state).

## Scenario D — Persistence across launches → US3, SC-004

1. Select non-default input/output devices and a file source; quit the workbench.
2. Relaunch → the same devices and source are active on launch with no re-selection.
3. (Edge) Remove a previously selected device, relaunch → it falls back to an
   available device and surfaces that the saved device was unavailable.

## Scenario E — MIDI input selection → US4, SC-005

1. With two MIDI inputs available, open **Audio Settings…** and enable only one.
2. Move CC 74 / 71 on the enabled controller → cutoff / resonance move; the disabled
   controller has no effect.

## Scenario F — Real-time safety under change → SC-003

Repeatedly switch device and source while audio is flowing (e.g. 20× rapid toggles).
**Expected**: no audible torn/garbage buffer, no stuck stream, no lock-up, no crash —
every change is applied with the audio engine stopped.

## Scope note

Scenarios B–F are interactive (display + audio device + MIDI + files) — they are the
**Manual acceptance** for this feature. CI build-checks the workbench (compile against
real JUCE) and runs Scenario A; running B–F on a real machine is the operator
checkpoint, as with the prior workbench scenarios.

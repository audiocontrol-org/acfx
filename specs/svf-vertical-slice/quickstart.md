# Quickstart & Validation — SVF Vertical Slice

Runnable scenarios that prove the milestone. Each maps to a user story / success
criterion in [spec.md](./spec.md). Implementation details live in `tasks.md`; this
is the run-and-verify guide. Exact dependency tags are pinned in the CMake files
(Phase 0, decision 4).

## Prerequisites

- CMake (recent), a C++20 desktop toolchain, an audio device or a test audio file.
- ARM toolchains for the hardware checks: `arm-none-eabi-gcc` (Daisy) and the
  Teensy toolchain (Teensy). Not required for the desktop/test scenarios.
- First configure fetches CPM-pinned deps (JUCE, DaisySP, libDaisy, doctest, …).

## Scenario A — Host tests (core correctness, no hardware) → SC-003, SC-004

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test        # or run the doctest binary directly
```

**Expected**: all tests pass, including (a) the SVF frequency-response check
against the known-good reference for low/high/band modes within tolerance, (b)
parameter scaling/skew checks, (c) NaN/denormal stability, and (d) the
**no-heap-allocation-in-`process()`** invariant (zero allocations counted).

## Scenario B — Desktop workbench (sketch-and-hear) → US1, SC-002

```bash
cmake --preset desktop
cmake --build --preset desktop --target acfx_workbench
# launch the built workbench app
```

**Expected**: the workbench opens with auto-generated controls for cutoff /
resonance / mode. Selecting the built-in player or a live input device routes audio
through the SVF. Lowering cutoff (low-pass) audibly attenuates highs in real time;
a bound MIDI CC moves cutoff; the A/B control toggles dry vs processed. Editing a
DSP constant, rebuilding, and relaunching reflects the change within a few seconds.

## Scenario C — Desktop plugin (VST3 / AU / CLAP) → US2, SC-006

```bash
cmake --preset desktop
cmake --build --preset desktop --target acfx_plugin
# load the built plugin in a plugin host, in each format
```

**Expected**: the plugin instantiates as VST3, AU, and CLAP. Its host-automation
parameters (cutoff/resonance/mode) appear with names, ranges, and defaults derived
from the *same* descriptor table the workbench uses. Automating cutoff in the host
produces the same audible sweep as the workbench for identical settings.

## Scenario D — Hardware cross-compile + link → US3, SC-001, SC-005, SC-007

```bash
cmake --preset daisy   && cmake --build --preset daisy     # arm-none-eabi-gcc
cmake --preset teensy  && cmake --build --preset teensy    # Teensy toolchain
```

**Expected**: the **same** `core/effects/svf` source compiles and links into a
Daisy firmware artifact and a Teensy artifact, each with audio-callback glue
invoking `effect.process` and ADC/encoder (or analog/MIDI) inputs mapped to the
parameters. Inspecting each MCU build's dependency graph shows core + that target's
adapter only — **no JUCE**, no desktop-only stubs. (Flashing and listening on a
physical board is a separate checkpoint when hardware is in hand.)

## Scenario E — One-source-many-targets invariant → SC-001, SC-005

Confirm that Scenarios B, C, and D all built from the identical, unmodified
`core/effects/svf` sources — no `#ifdef`-per-target forks of the effect, and no
change to its parameter declaration between targets. Only adapter glue differs.

## CI (desktop + tests) → SC-008

CI runs Scenario A (build + tests) and the desktop build of Scenario B/C on every
change. Hardware presets are build-checked where toolchains are available; the
authoritative hardware gate is local until CI ARM toolchains are provisioned (a
deferred follow, noted so the boundary isn't silently dropped).

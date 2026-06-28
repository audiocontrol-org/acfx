# Quickstart & Validation: Modulated Delay

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-28

A runnable validation guide proving the feature works end-to-end, one block per user
story, mapped to acceptance scenarios and Success Criteria in [spec.md](./spec.md).
Implementation details live in `tasks.md`; this file is a run/validation guide only.

## Prerequisites

- The acfx toolchain as used by the SVF slice (CMake + presets + CPM-pinned deps).
- No new dependencies for this feature (reuses DaisySP + the existing core).

## Host-side tests (primary gate — hardware-free)

The core is validated host-side with doctest, exactly as the SVF effect:

```sh
cmake --preset test
cmake --build --preset test
ctest --preset test        # or run the doctest binary directly
```

Expected: the new suites pass —
- `delay-line-test` — fractional-read accuracy, in-range bounds at/beyond max and at 0,
  integer round-trip, reset silence.
- `lfo-test` — sine/triangle/saw values at known phases, all shapes in [-1,1],
  sample-rate-independent period, deterministic click-free `random`.
- `modulated-delay-test` — progressive feedback-filter shaping; delay-time modulation
  movement and depth-0 equivalence; cutoff/resonance modulation movement; wow & flutter
  pitch instability and depth-0 passthrough; feedback-stability at the max bound; and
  the no-heap-allocation-in-`process()` invariant (allocation sentinel).

## Story 1 — Filtered-feedback delay (P1, SC-001/002)

1. Build + launch the workbench with the modulated-delay effect loaded:
   ```sh
   cmake --preset desktop
   cmake --build --preset desktop --target acfx_workbench
   ```
2. Route audio through it; set an audible delay time and high feedback.
3. Lower the feedback-filter cutoff in low-pass mode → each successive echo is darker
   (acceptance 1). Switch to band-pass → tail collapses toward a narrow band
   (acceptance 2). Sweep mix dry→wet → smooth, click-free (acceptance 3). Hold high
   feedback → no uncommanded runaway (acceptance 4). Change delay time → retimes
   without clicks (acceptance 5).

## Story 2 — Modulated delay time & feedback filter (P2, SC-003/004)

1. With Story 1 working, set the delay-mod depth > 0 and a slow delay-mod rate →
   tail develops periodic pitch/time warble at the rate (acceptance 1).
2. Set cutoff-mod depth > 0 → tail brightness rises/falls periodically at its rate
   (acceptance 2). Try different shapes {sine, triangle, saw, random}.
3. Set any mod depth to 0 → that destination is static, indistinguishable from
   Story 1 (acceptance 3). Increase a mod rate → movement speeds up, no aliasing/clicks
   (acceptance 4). Drive delay-mod toward bounds → reads stay in range (acceptance 5).

## Story 3 — Wow & flutter on the input (P3, SC-005)

1. Set the main delay mix mostly dry so the input is audible.
2. Raise wow depth → slow periodic pitch drift (acceptance 1). Raise flutter depth →
   faster, shallower shimmer layered on top (acceptance 2).
3. Set both wow and flutter depth to 0 → input passes through with no pitch modulation
   (acceptance 3). With the delay audible, the tape instability is present in the tail
   too (acceptance 4). At any amount, reads stay in range, no clicks (acceptance 5).

## Cross-platform parity (SC-008)

Build the plugin and the MCU targets without changing the effect source:

```sh
cmake --preset desktop && cmake --build --preset desktop --target acfx_plugin
cmake --preset daisy   && cmake --build --preset daisy
cmake --preset teensy  && cmake --build --preset teensy
```

Expected: the same `core/effects/modulated-delay/` source compiles into every target;
MCU dependency graphs show no JUCE; the 2 s buffer fits device RAM (bounded if a target
cannot hold 2 s at its native rate — research Decision 8).

## Sample-rate independence (SC-009)

Run `modulated-delay-test` (and `lfo-test`) at 44.1k/48k/96k fixtures — a given delay
time and modulation rate produce the same musical result independent of sample rate.

## CI note

CI runs the host-side tests and the desktop builds on every change; hardware presets
are build-checked where toolchains are available (same policy as the SVF slice).

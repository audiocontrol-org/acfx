# Harmonic Analysis — Audit Log

## 2026-07-01 — `/code-review` stop-gap (govern-FATAL substitute)

The end-of-feature `stackctl govern --mode implement` pass **FATAL'd** on a tooling
limit, not a code defect: the `adapters/workbench/workbench-app.cpp` **diff** vs
`main` (a near-total rewrite — the necessary <500-line extraction refactor, 363+/446−)
renders ~43.9 KB, exceeding the audit fleet's 24,576-byte per-file envelope, so govern
declined to hunk-split it. Per the documented sandbox-govern ceiling, the operator chose
a `/code-review` stop-gap (real adversarial review) followed by an operator-recorded
`stackctl govern --override`.

**Review:** 4 parallel finder angles (DSP correctness, lock-free ring concurrency,
build-unverified JUCE adapters, conventions+relocation) at high effort, controller-verified.
The FFT/Goertzel/IMD math and the T007 relocation (F1) were confirmed correct/clean.

### Fixed (host-side, `make test` 261/261 green — commit f069ee6)

- **D1 (silent wrong result, HIGH)** — `host/analysis/alias-sweep.h`: `aliasSweep` fed
  non-integer-cycle sweep points to `aliasingMeasure` (which requires integer-cycle),
  scalloping the Goertzel and reporting massive false "aliasing" for a linear passthrough.
  Fix: snap each swept frequency to the nearest bin (100 Hz) before measuring; report the
  snapped frequency in `frequencyHz[]`.
- **D2 (fail-loud, Constitution V)** — `imd.h` + `alias-sweep.h` Effect overloads silently
  accepted a `ctx.sampleRate` != the fixed internal rate they document as a caller error.
  Fix: throw `std::invalid_argument`.
- **D3 (fabricated 0.0 vs NaN, Constitution V)** — `spectrum.h` `harmonicSpectrum` returned
  magnitude 0.0 for an empty input, contradicting its own "never a fabricated 0.0" banner.
  Fix: empty input → NaN sentinels (mirrors `thdPlusN`'s n==0 guard).
- **D6 (fail-loud, Constitution V)** — `live-readout.h` `LiveReadout` silently analyzed the
  DC bin when `fundamentalHz` was left at its 0.0 default. Fix: constructor throws on
  `fundamentalHz <= 0` (both adapters already pass a positive default).

### Flagged for follow-up (not auto-fixed — concurrency / embedded / build-unverified)

- **C1 (overrun torn-window race, HIGH)** — `core/primitives/analysis/capture-probe.h`: on
  overrun the producer can overwrite slots the consumer is mid-copy (data race on the
  non-atomic buffer); `drain()` does not re-check `writeIndex_` after the copy, so a lapped
  window is a torn old/new mix, contradicting the "coherent recent window" contract. Impact:
  an occasional glitched analysis frame *only while the consumer is behind*. Fix needs a
  seqlock/post-copy recheck (careful concurrency work) — deferred to a focused pass.
- **C2 (embedded RT-safety, HIGH)** — same file: the three `std::atomic<std::uint64_t>` are
  not guaranteed lock-free on the 32-bit ARM (Daisy/Teensy) targets this portable core
  serves; `push()` could lower to a locked `libatomic` call, violating the no-lock audio-path
  contract. No `static_assert(is_always_lock_free)`. Fix: 32-bit indices or a compile-time
  guard, validated on the embedded toolchain (unavailable in this sandbox).
- **C3 (LOW)** — overrun count is approximate under real concurrency (producer snapshots the
  read index once). Diagnostic-only.
- **D4 (LOW/plausible)** — `thdn.h` residual clamp `if (<0) =0` absorbs *any* negative, so a
  contract-violating capture where the Goertzel over-estimates the fundamental could report
  `thd=0 / snr=+inf`. Consider clamping only sub-ULP.
- **D5 (LOW)** — `spectrum.h` per-harmonic `phaseRad` is anchored per-harmonic (ω·(N−1)
  offset); cross-harmonic phase relationships aren't physically meaningful for waveform
  reconstruction. Document in the `HarmonicSpectrum` contract.
- **A1 (LOW)** — `adapters/plugin/plugin-editor.*`: the SPSC ring is safe only under JUCE's
  single-live-editor assumption; a multi-editor host would give two consumers. Add a comment.
- **A2 (LOW)** — `adapters/workbench/workbench-app.cpp` push uses `getReadPointer(0)` without
  the channel-count guard the plugin has; OOB on a 0-output-channel device (pre-existing meter
  line already assumes ch0). Build-unverified adapter — fold into the T030/T031 operator pass.
- **A3 (LOW)** — `live-readout.h` drains one window per timer tick; at ≥176.4 kHz production
  outpaces the ~20 Hz × 8192 consume rate, so the ring permanently overruns (graceful, shown).

### Status

- Substantive host-side findings fixed + test-verified. Concurrency (C1/C2) and the
  build-unverified adapter items (A1/A2) are recorded for a focused follow-up; the adapter
  live-readout is already `- [~]` operator-acceptance (needs a JUCE desktop/DAW build).
- Terminal: operator records `stackctl govern --override` to close the governance step,
  with this stop-gap review as the evidence in lieu of the size-FATAL'd barrage.

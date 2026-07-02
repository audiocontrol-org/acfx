# core/primitives/analysis — RT-safe analysis primitives

Portable, embeddable, **real-time-safe** primitives that bridge the audio thread to
off-thread/offline harmonic analysis. This is the **only** part of the
harmonic-analysis feature that may run on an audio callback. It depends on nothing
host-side; the host analysis engine (`host/analysis/`) is **not** reachable from
here (Constitution IV; enforced by `scripts/check-portability.sh` gate `C-AN-DIR`).

## `capture-probe.h` — `acfx::CaptureProbeRing<Capacity>`

A single-producer / single-consumer (SPSC) lock-free ring buffer. The audio thread
produces; a host UI/analysis thread consumes.

### Real-time-safety contract (Constitution VI)

The audio-thread method `push(acfx::span<const float>)` performs **only** a bounded
copy plus one release store of the write index:

- **no heap allocation**, **no locks**, **no analysis math** — asserted by the
  allocation sentinel (`tests/core/no-allocation-test.cpp`, SC-004).
- fixed compile-time `Capacity` (a `std::array<float, Capacity>` value member) — no
  dynamic sizing, so embedded RAM is statically known (Daisy/Teensy-safe).
- header includes only `<array>`, `<atomic>`, `<cstddef>`, `<cstdint>` and
  `dsp/span.h` — no platform/JUCE/host headers.

### Overrun / underrun semantics (FR-013)

- **Overrun** (consumer fell behind, producer would lap the unread region): the
  producer advances anyway and increments `overrunCount()` — it **never blocks and
  never allocates**. The consumer's next `drain()` returns the most-recent
  `Capacity` samples (a coherent recent window, never a torn/stale read).
- **Underrun** (`available() < requested`): `drain()` copies what is available and
  returns that count — it never blocks and never fabricates data; the caller's
  unfilled tail is left untouched.
- Both conditions are **observable** (`overrunCount()`, `available()`), never silent
  corruption.

### Consumer surface (host thread; lock-free, off the audio path)

`available()`, `drain(acfx::span<float>)`, `overrunCount()`, `reset()`.

See `specs/harmonic-analysis/contracts/capture-probe-api.md` for the full contract.

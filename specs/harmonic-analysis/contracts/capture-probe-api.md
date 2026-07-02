# Contract — RT Capture Probe (`core/primitives/analysis/capture-probe.h`)

Portable, real-time-safe, embeddable. The **only** audio-path unit in this feature. Namespace `acfx` (portable core). No host/platform headers, no heap, no locks.

## Type

`template <std::size_t Capacity> class CaptureProbeRing` — `Capacity` is a compile-time constant, chosen ≥ (default FFT window 8192) + margin. Fixed-size value buffer; embedded RAM statically known.

## Audio-thread surface (RT-safe — Constitution VI)

```
void push(AudioBlock block) noexcept;   // bounded copy + one release store; NO alloc, NO lock, NO math
```

- Copies up to `block.size()` samples into the ring, advancing the atomic write index (release).
- On overrun (would lap the unread read index): advances anyway, increments `overrunCount()` — **never blocks, never allocates**. The dropped span is observable.
- MUST be callable from an audio callback with zero heap allocation (asserted by the allocation sentinel, SC-004).

## Analysis-thread surface (host thread; may run longer, still lock-free)

```
std::size_t available() const noexcept;              // samples ready to drain
std::size_t drain(span<float> out) noexcept;         // copy a COHERENT window; advance read index (acquire) — or 0 (dropped-torn-frame)
std::uint64_t overrunCount() const noexcept;         // observable overrun state (FR-013), producer-owned
std::uint64_t droppedFrameCount() const noexcept;    // observable torn-window drops, consumer-owned
void reset() noexcept;                               // clear indices + counters (not called concurrently with push)
```

- **Underrun** (`available() < window`): the caller holds and skips the update; `drain` returns fewer than requested — never blocks.
- **Coherent-window-or-dropped-frame** (overrun torn-window semantics): because the producer overwrites the oldest unread slots on overrun, it can overwrite the exact slots the consumer is mid-copy. `drain` therefore *validates* every window: it snapshots the write index, copies the chosen window, then **re-reads** the write index; if the producer advanced far enough during the copy to overwrite the oldest slot copied (`finalWrite - r > Capacity`), the window is **torn** and is **not** returned. `drain` retries a bounded number of times and, failing that, **drops** the frame — returns `0` and increments `droppedFrameCount()` — rather than hand a spliced old/new mix to the FFT. A dropped-torn-frame is the honest outcome for a live analyzer: the caller simply skips one update. This is *best-effort* by construction: `push()` writes its buffer slots before publishing a single release store, so a torn read of the boundary slot by an *in-flight* (not-yet-published) push is inherent to overwrite-on-overrun SPSC and cannot be fully eliminated without cooperation from `push()`; the re-read reliably rejects the dominant case (a lapping push that *completed* during the copy).

## Invariants

- Single producer (audio thread) + single consumer (analysis thread); no other concurrency.
- No dynamic allocation anywhere; no locks; no analysis math in this unit.
- Platform-free: gate-asserted by `scripts/check-portability.sh` over `core/primitives/analysis/**`.

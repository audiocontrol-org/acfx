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
std::size_t drain(span<float> out) noexcept;         // copy up to out.size() ready samples; advance read index (acquire)
std::uint64_t overrunCount() const noexcept;         // observable overrun state (FR-013)
void reset() noexcept;                               // clear indices (not called concurrently with push)
```

- **Underrun** (`available() < window`): the caller holds and skips the update; `drain` returns fewer than requested — never blocks.

## Invariants

- Single producer (audio thread) + single consumer (analysis thread); no other concurrency.
- No dynamic allocation anywhere; no locks; no analysis math in this unit.
- Platform-free: gate-asserted by `scripts/check-portability.sh` over `core/primitives/analysis/**`.

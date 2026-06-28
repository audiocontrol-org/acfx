# Contract — Interpolated DelayLine primitive (core/primitives)

A platform-independent, allocation-free circular delay buffer with fractional
(linear-interpolated) reads. Used by the main feedback delay and (a separate
instance) the wow & flutter stage. No platform headers; standard library only.

## Type

```cpp
namespace acfx {

class DelayLine {
public:
    // Bind preallocated storage (owned by the effect, sized in prepare()). No
    // allocation here. capacity must be >= maxDelaySamples + 1.
    void prepare(float* storage, int capacity, float sampleRate) noexcept;

    // Zero the buffer and reset the write position. Stream-stopped (or first use).
    void reset() noexcept;

    // Push one input sample (advances and wraps the write position).
    void write(float x) noexcept;

    // Read `delaySamples` (fractional) in the past via linear interpolation.
    // delaySamples is clamped to [0, capacity-1] so the read is ALWAYS in range.
    float readFractional(float delaySamples) const noexcept;

    int   capacity() const noexcept;
    float sampleRate() const noexcept;
    float maxDelaySamples() const noexcept; // capacity - 1
};

} // namespace acfx
```

## Normative behavior

- **Allocation-free / RT-safe**: `write`, `readFractional`, `reset` perform no heap
  allocation, take no locks, and do bounded work (Constitution VI).
- **In-range guarantee (FR-007)**: for ANY `delaySamples` value (including modulated
  values driven beyond the buffer), the interpolated read accesses only indices in
  `[0, capacity)`. Out-of-range input is clamped, never wrapped incorrectly or read
  past the end.
- **Linear interpolation (research Decision 1)**: for integer part `i` and fraction
  `f` of the (clamped) delay, the result is `(1-f)*buf[read-i] + f*buf[read-i-1]`
  (indices taken mod capacity, reading backward from the write position).
- **Sample-rate aware**: `prepare` records `sampleRate` for callers converting
  seconds↔samples; the line itself indexes in samples.

## Consumer obligations

| Consumer | Uses DelayLine to… |
|---|---|
| ModulatedDelayEffect | store the feedback delay; read the (modulated, smoothed) base delay; write `input + feedback*filtered` |
| WowFlutterStage | store a short input delay; read a tap modulated by the wow+flutter LFOs |

## Test obligations (host-side, doctest)

- A fractional read of a known buffer (ramp/impulse) equals the closed-form linear
  interpolation value.
- For delay requests at and beyond `maxDelaySamples` and at 0, reads stay in range
  (no UB, value well-defined).
- `reset()` produces silence reads; `write` then read-back round-trips at integer taps.

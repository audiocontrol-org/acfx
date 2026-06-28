# Contract — Lfo primitive (core/primitives)

A platform-independent, allocation-free low-frequency oscillator with a selectable
waveform shape. One instance per modulation destination (delay time, filter cutoff,
filter resonance, wow, flutter). No platform headers; standard library only.

## Types

```cpp
namespace acfx {

enum class LfoShape : std::uint8_t { sine, triangle, saw, random };

class Lfo {
public:
    void prepare(float sampleRate) noexcept;  // records sr; resets phase/state
    void reset() noexcept;                     // phase -> 0, random state re-seeded deterministically
    void setRate(float hz) noexcept;           // inc_ = hz / sampleRate
    void setShape(LfoShape shape) noexcept;

    // Advance one sample and return a bipolar value in [-1, 1].
    float tick() noexcept;
};

} // namespace acfx
```

## Normative behavior

- **Allocation-free / RT-safe**: `tick`, `setRate`, `setShape`, `reset` perform no
  heap allocation, take no locks, do bounded work (Constitution VI).
- **Phase accumulator**: `phase_ += rate/sampleRate` each `tick()`, wrapped to [0,1).
  This makes a given `rate` produce the **same musical motion at any sample rate**
  (FR-015).
- **Output range**: every shape returns a value in `[-1, 1]` (bipolar), so a caller
  scales by a depth and adds to a center value (delay samples, cutoff Hz offset, …).
- **Shapes (research Decision 3)**:
  - `sine` — `sin(2π·phase)`.
  - `triangle` — symmetric ramp up/down.
  - `saw` — rising ramp mapped to [-1, 1].
  - `random` — **smoothed sample-and-hold**: a new random target each LFO period,
    linearly interpolated toward it (click-free, organic). Randomness from a small
    **seedable xorshift** PRNG held in the LFO (deterministic for tests; no
    `std::random`, no heap/locks in the audio path).
- **Determinism**: with a fixed seed and rate, `tick()` output is reproducible —
  required for host-side tests of the `random` shape.

## Consumer obligations

| Consumer | Uses Lfo to… |
|---|---|
| ModulatedDelayEffect | modulate delay time, filter cutoff, filter resonance (one LFO each, independent rate/depth/shape) |
| WowFlutterStage | drive the slow wow and the faster flutter components (one LFO each) |

## Test obligations (host-side, doctest)

- Sine/triangle/saw match expected values at known phases; all shapes stay within
  [-1, 1].
- A fixed rate yields the same period in samples at 44.1k/48k/96k (sample-rate
  independence).
- `random` with a fixed seed is reproducible and click-free (bounded sample-to-sample
  delta given the smoothing).
- Depth-zero usage (caller multiplies by 0) yields no modulation — verified at the
  effect level (FR-013).

# Contract — `acfx_nucleo_support` (public header API)

Headers: `adapters/nucleo/support/*.h` · Namespace: `acfx::nucleo` · C++17, header-only,
template-sized. This is the host-testable half of the adapter (**D1**, FR-001/FR-002).

Signatures below are **illustrative of the contract** — names and exact shapes may refine during
implementation. The **behavioural guarantees** are binding, and they are what
`tests/core/nucleo-*-test.cpp` asserts.

**Hard constraint on this whole surface**: it is **platform-independent**. No TinyUSB, no CMSIS,
no board headers, no `<cstdio>`. It compiles under the `test` preset with no toolchain file
(research R8). Anything that cannot satisfy that belongs in `nucleo-main.cpp` (FR-003).

---

## Sample format — `sample-format.h`

```cpp
namespace acfx::nucleo {

inline constexpr float  kInt16Scale    = 32768.0f;
inline constexpr int    kMaxPacketFrames = 49;   // D21 / FR-028
inline constexpr int    kBlockFrames     = 48;   // FR-030a / FR-036a
inline constexpr int    kChannels        = 2;    // D4

// USB -> effect: de-interleave and convert. `dst` holds kChannels contiguous
// float buffers of at least `frames` samples each.
void deinterleaveToFloat(const std::int16_t* src, float* const* dst,
                         int frames) noexcept;

// Effect -> USB: convert, round to nearest, CLAMP, and interleave.
void interleaveToInt16(const float* const* src, std::int16_t* dst,
                       int frames) noexcept;

} // namespace acfx::nucleo
```

### Guarantees

- **SF1** — Round-trip exactness. For every representable `int16_t` value,
  `deinterleaveToFloat` followed by `interleaveToInt16` recovers it exactly. (US2 AS2, FR-038)
- **SF2** — Clamping, not wrapping. A float outside [-1.0, 1.0) is clamped to
  [-32768, 32767]. It never wraps to the opposite rail. (US2 AS3, FR-038a)
- **SF3** — Exact frame counts. Exactly `frames` frames are read and written, for every
  `frames` in [0, 49]. `frames == 0` is a valid no-op, not an error. No access past the
  payload. (US2 AS4, FR-028)
- **SF3a** — Torn payloads truncate. A byte count that is not a whole number of stereo frames
  yields the whole frames plus a reportable remainder count; L/R alignment downstream is
  preserved. (FR-028a, I-SF3a)
- **SF2a** — Ties away from zero. Round-to-nearest resolves exact .5 cases away from zero, so
  the rounding is fully specified rather than platform-dependent. (FR-038a)
- **SF4** — No allocation. Neither function allocates. (US2 AS5, FR-030)
- **SF5** — `noexcept`. Both are callable from the audio path.

---

## Audio ring — `audio-ring.h`

```cpp
namespace acfx::nucleo {

template <int CapacityFrames, int Channels = kChannels>
class AudioRing {
public:
    // Push up to `frames` frames. Overflow drops the OLDEST and reports how many.
    // Returns frames dropped (0 on the normal path).
    int write(const float* const* src, int frames) noexcept;

    // Draw exactly `frames`. Underflow fills the shortfall with SILENCE and
    // reports how many frames were substituted.
    int read(float* const* dst, int frames) noexcept;

    int  occupancy() const noexcept;
    int  capacity()  const noexcept { return CapacityFrames; }
    void reset()     noexcept;
};

} // namespace acfx::nucleo
```

### Guarantees

- **AR1** — Bounded occupancy. `occupancy()` is always in `[0, capacity()]`, on every path.
  (FR-030, I-AR1)
- **AR2** — Defined underflow. `read()` always writes exactly `frames` frames; any shortfall is
  **silence**, and the count of substituted frames is returned rather than swallowed.
  (US3 AS1/AS3, FR-031)
- **AR3** — Defined overflow. `write()` drops the **oldest** frames, never the newest, and
  returns the count dropped. (US3 AS2/AS4, FR-031)
- **AR4** — Nothing silent. Every substitution is reported by return value so the caller can
  increment the matching counter. The ring does not own the counters; it reports facts.
  (FR-032 — this is how Principle VII is honoured where throwing is unavailable)
- **AR5** — No allocation, no locks. Storage is a fixed member array. Single-producer /
  single-consumer, sufficient under the single-context assumption (**D26**, FR-046). (FR-030)
- **AR6** — `noexcept` throughout; callable from the audio path.
- **AR7** — Startup wait. Until occupancy first reaches the fill target, `read()` reports the
  ring as not-yet-ready rather than manufacturing a burst of underruns on every stream open.
  (FR-030b, I-AR6)
- **AR8** — No re-centring. The ring never drops or duplicates frames to steer occupancy back
  toward a target; drift stays visible in the counters. (FR-030c, I-AR7)
- **AR9** — `reset()` clears contents and re-arms the AR7 wait, so a bus reset restarts from a
  defined state rather than draining a stale partial ring. It does **not** touch the counters.
  (FR-053, FR-054, I-AR8)

**`CapacityFrames` is deliberately a template parameter with no default.** Its value is derived
from HIL measurement per research R5 and pinned in Phase H (**D23**, FR-035). A default here
would be an invented number wearing the costume of a decision.

---

## Transport statistics — `transport-stats.h`

```cpp
namespace acfx::nucleo {

struct AudioTransportStats {
    std::uint32_t inputUnderruns  = 0;
    std::uint32_t inputOverruns   = 0;
    std::uint32_t outputUnderruns = 0;
    std::uint32_t outputOverruns  = 0;
    std::uint32_t inputStarved    = 0;   // capture-only silence (D22)
    std::uint32_t truncatedFrames = 0;   // torn-payload remainder (FR-028a)
    std::uint32_t blocksProcessed = 0;   // denominator
    std::uint32_t worstBlockMicros = 0;  // makes the CPU budget observable
};

// Error count as a rate against blocksProcessed. Returns 0 when no block has
// been processed -- a rate over zero blocks is undefined, not infinite.
double errorRate(std::uint32_t count, const AudioTransportStats&) noexcept;

} // namespace acfx::nucleo
```

### Guarantees

- **TS1** — Monotonic **modulo 2^32**. Counters wrap rather than saturating; consumers take
  deltas between snapshots. Not resettable at runtime. (FR-034a, I-TS1)
- **TS1a** — Mutually exclusive. One event increments exactly one counter; `inputStarved` and
  `outputUnderruns` cover *different* conditions and never both fire for the same silence.
  (FR-029a, I-TS1a)
- **TS2** — `blocksProcessed` increments exactly once per DSP block, so the other counters are
  interpretable as rates rather than bare totals. (FR-034, US9 AS2)
- **TS3** — `worstBlockMicros` is a maximum, never an average, and never implicitly resets.
  (I-TS3)
- **TS4** — Zero blocks yields a rate of 0, not a division by zero.
- **TS5** — Reading the record does not allocate or block. (FR-033a, I-TS5)

**Not in this header** (it would violate platform independence): the CDC emission itself and the
DWT timing source both live in the shim. The shim's obligation is I-TS4 — a `worstBlockMicros`
of 0 after blocks have run means the timing source failed to initialize and must surface loudly
(research R6).

---

## Parameter shadow — `parameter-shadow.h`

```cpp
namespace acfx::nucleo {

template <int N>
class ParameterShadow {
public:
    // Write a slot from any parameter source. Idempotent, last-write-wins.
    // Out-of-range index is ignored (never reaches setParameter).
    void set(int index, float normalized) noexcept;

    // Walk the dirty flags once per audio block, applying each to the effect,
    // then clear. `apply` is invoked as apply(acfx::ParamId{i}, value).
    template <class ApplyFn>
    void flush(ApplyFn&& apply) noexcept;

    bool dirty(int index) const noexcept;
    static constexpr int size() noexcept { return N; }
};

} // namespace acfx::nucleo
```

### Guarantees

- **PS1** — Bounded by construction at `N`. There is no overflow path, therefore no drop policy,
  therefore nothing lost. (**D25**, FR-041, I-PS1)
- **PS2** — Last-write-wins. After any burst of `set()` calls within one block period, `flush()`
  applies the **final** value per parameter — never an intermediate one. A knob sweep cannot
  strand a parameter mid-travel. (US6 AS3, FR-042, I-PS2)
- **PS3** — No cross-parameter eviction. One fast-moving control cannot displace another
  control's single pending update. (US6 AS4, FR-043, I-PS3)
- **PS4** — Exactly-once. `flush()` invokes `apply` once per dirty parameter and not at all for
  clean ones, then clears the flags. (US6 AS2, FR-042, I-PS4)
- **PS5** — `N == 0` is valid; `flush()` is a no-op. (I-PS5)
- **PS6** — No allocation, `noexcept`; callable at the block boundary in the audio path.

**Scope note** (FR-044): this is correct for **state-valued** parameters and wrong for
event-valued controls (momentary trigger, tap tempo), where collapsing to a last value destroys
the signal. acfx's parameter model has only normalized continuous values today, so nothing is
broken; such a control would need its own mechanism regardless.

---

## Parameter source seam — `parameter-source.h`

The abstraction FR-039 requires and FR-040 shapes (**D2**, **D3**). Following acfx's existing
style — `acfx::Effect` and MNA's `CompanionSupply` are both **duck-typed seams**, not base
classes — a `ParameterSource` is any type exposing:

```cpp
namespace acfx::nucleo {

// A ParameterSource is any type T for which this is valid:
//
//     void T::poll(ParameterShadow<N>& shadow) noexcept;
//
// Called once per audio block, BEFORE shadow.flush(). Implementations write
// slots; they never call setParameter themselves.

// The first implementation (D2): drains pending USB MIDI control changes,
// resolves each through MidiCcMap, and writes the mapped slot.
template <int N>
class MidiParameterSource {
public:
    void poll(ParameterShadow<N>& shadow) noexcept;
    // Called by the shim when a CC arrives; buffering is bounded and lock-free.
    void onControlChange(std::uint8_t cc, std::uint8_t value) noexcept;
};

} // namespace acfx::nucleo
```

### Guarantees

- **PSRC1** — One seam, two source kinds. A **sampled-state** source (ADC, encoder) and an
  **event-driven** source (USB MIDI) both satisfy `poll(shadow)`. They converge on **data** —
  the shadow block — not on a shared execution model, which is what makes D3's physical
  peripherals a later addition rather than a later redesign. (FR-039, FR-040)
- **PSRC2** — **Dead-banded writes.** A source MUST NOT dirty a slot when the value has not
  meaningfully changed. Without this, a sampled source marks every slot dirty every block and
  `flush()` degenerates into calling `setParameter` for every parameter at audio rate — the
  failure mode the dirty flags exist to prevent. `daisy-main.cpp`'s `kKnobDeadband` is the
  existing precedent.
- **PSRC3** — `poll()` is `noexcept` and allocation-free; it runs inside the audio path as
  delimited by FR-046a.
- **PSRC4** — Sources are **composable**: the shim may poll more than one in a block, and two
  sources writing the same slot resolve by last-write-wins (PS2), not by conflict.
- **PSRC5** — A source never calls `setParameter` directly. Only `flush()` does (PS4), which is
  what keeps parameter application to exactly once per dirty parameter per block.

**Platform boundary**: `MidiParameterSource` holds only the decoded `(cc, value)` pair — USB
MIDI packet decoding is the shim's job, since it needs the stack. This is what keeps the source
host-testable.

---

## MIDI CC mapping — `midi-cc-map.h`

```cpp
namespace acfx::nucleo {

struct CcBinding { std::uint8_t cc; std::uint8_t paramIndex; };

// Resolve a CC number to a parameter index, or none. Pure; no state.
// `paramCount` bounds the result so an out-of-range index cannot escape.
std::optional<int> mapCcToParam(std::uint8_t cc, int paramCount) noexcept;

} // namespace acfx::nucleo
```

### Guarantees

- **MC1** — Unmapped CCs are ignored, disturbing no slot. (spec Edge Cases, I-MC1)
- **MC2** — An index beyond `paramCount` is never returned, so it can never reach
  `setParameter`. (I-MC2 — `daisy-main.cpp`'s `boundKnobs()` is the existing shape)
- **MC3** — Pure and stateless, therefore trivially host-testable. (I-MC3)

**The concrete CC convention is OPEN** — open question 7, the operator's call. This contract
fixes only the *mechanism*, deliberately, so settling the convention later is a table edit rather
than a rewrite. Research R9 records a recommendation (bind CC numbers to parameter indices
generically, omni-channel, mirroring the Daisy knob binding) that is **offered and not applied**,
because OQ7 also asks whether the convention should match the workbench's existing MIDI CC
consumption — a question about a subsystem this work has not examined.

---

## What this contract deliberately does not cover

| Concern | Where it lives | Why not here |
|---|---|---|
| TinyUSB init, descriptors, ISR, `tud_task()` | `nucleo-main.cpp`, `usb-descriptors.*` | Touches silicon and the USB stack; not host-compilable (FR-003) |
| Clock bring-up, `SystemCoreClock`, LED fault pattern | `nucleo-main.cpp` | Register-level; **D6**/**D7**/**D14**/FR-015a |
| DWT `CYCCNT` timing source | `nucleo-main.cpp` | Cortex-M core peripheral; feeds `worstBlockMicros` |
| CDC telemetry emission | `nucleo-main.cpp` | Needs the USB stack; format specified in research R7 |
| Ring capacity / water marks / startup fill | Phase H, post-measurement | **D23** / FR-035 — measurement-derived, never invented |

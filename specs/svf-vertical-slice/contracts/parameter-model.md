# Contract — The Parameter model (core/dsp)

One `constexpr` declaration per effect, consumed by every adapter (FR-003).

## Types

```cpp
struct ParamId { std::uint8_t value; };

enum class ParamUnit  : std::uint8_t { none, hz, decibels, percent, ratio };
enum class ParamSkew  : std::uint8_t { linear, logarithmic };
enum class ParamKind  : std::uint8_t { continuous, discrete };

struct ParameterDescriptor {
    ParamId      id;
    std::string_view name;        // static storage (string literal)
    ParamUnit    unit;
    float        min;
    float        max;
    float        defaultValue;    // in plain units, within [min, max]
    ParamSkew    skew;
    ParamKind    kind;
    std::uint8_t discreteCount;   // >=2 when kind==discrete, else 0
};
```

## Declaration site

```cpp
// inside an Effect type:
static constexpr std::array<ParameterDescriptor, N> kParams = { /* ... */ };
static constexpr std::span<const ParameterDescriptor> parameters() { return kParams; }
```

## Mapping contract (normative)

- **normalize(plain) / denormalize(norm)** are pure functions of a descriptor:
  - `linear`: `norm = (plain - min) / (max - min)`.
  - `logarithmic`: log-mapping between `min` and `max` (both `> 0` required).
  - `discrete`: `norm` quantizes to `floor(norm * discreteCount)` clamped to
    `[0, discreteCount)`; index ↔ enum is the effect's responsibility.
- `setParameter(id, norm)` receives normalized `0..1`; the effect denormalizes via
  the descriptor before use.
- All mappings are allocation-free and usable from the audio thread.

## Consumer obligations

| Consumer | Uses the descriptor to… |
|---|---|
| workbench | draw a control per parameter (range/skew/default), bind a MIDI CC, label with `name`/`unit` |
| plugin | register a host-automation parameter (name, range, default) generated from the descriptor |
| daisy/teensy | map an ADC pin / encoder / analog input to `setParameter(id, norm)` |

All consumers read the **same** `parameters()` array — there is no second source of
parameter truth (the invariant SC-006 verifies).

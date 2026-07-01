# Contract: Saturation Effect — Public C++ API

The public surface the effect exposes to host adapters (workbench, DAW plugins, MCU
mains) and the internal kernel contract. Signatures are the **contract shape**; exact
ranges/defaults and per-voicing numbers are a planning/tuning decision (open question).
This mirrors the shipped `core/effects/svf/svf-effect.h` Effect idiom — no base class,
no vtable on the processing path.

## Enums

```cpp
namespace acfx {

enum class SaturationVoicing : std::uint8_t {
    softClip = 0, tape = 1, console = 2, tubePreamp = 3
};

enum class SaturationQuality : std::uint8_t {
    naive = 0, adaa = 1,
    oversampled = 2   // RESERVED — documented but UNWIRED seam (FR-015); no dependency
                      // on the oversampling sibling. Until it lands, selecting this
                      // yields a defined, bounded fallback (see FR-015 semantics below),
                      // never a partial/aliased path.
};

} // namespace acfx
```

## SaturationEffect (host-facing Effect contract)

```cpp
namespace acfx {

class SaturationEffect {
public:
    // Stable dense parameter ids (index into kParams).
    enum Param : std::uint8_t {
        kDrive = 0, kVoicing = 1, kTone = 2, kMix = 3, kOutput = 4, kBias = 5, kQuality = 6
    };

    // Discrete option labels (single source of truth for the adapters).
    static constexpr std::array<std::string_view, 4> kVoicingLabels =
        {{"softClip", "tape", "console", "tubePreamp"}};
    static constexpr std::array<std::string_view, 2> kQualityLabels =
        {{"naive", "adaa"}};   // 'oversampled' intentionally not user-selectable yet

    // The single source of parameter truth (compile-time-validated descriptor table,
    // as SvfEffect::kParams). Ranges/skews/defaults finalized in planning/tuning.
    static constexpr std::array<ParameterDescriptor, 7> kParams = { /* … */ };
    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    SaturationEffect() noexcept;                    // init pending-param atomics

    // Audio stream must be stopped (thread-ownership boundary as in SvfEffect):
    void prepare(const ProcessContext& ctx) noexcept;  // sample rate + channels; build
                                                        // per-voicing filter coeffs here
    void reset() noexcept;                              // clear all core state

    void process(AudioBlock& io) noexcept;             // consume pending params, then
                                                        // per-channel composition; RT-safe

    // Callable from ANY thread — publishes a normalized 0..1 value via a lock-free
    // atomic; the audio thread applies it at the next process() (never mutates core
    // state here). Out-of-range id: a no-op (a programming error, no silent state change).
    void setParameter(ParamId id, float normalized) noexcept;
};

} // namespace acfx
```

**Guarantees**

- `process()` performs **no heap allocation and takes no lock** (FR-010, FR-020);
  asserted by the measurement suite's allocation sentinel.
- Parameter edits published from any thread take effect at the **next block boundary**;
  no torn reads (atomics are provably lock-free, as in `SvfEffect`).
- Per-channel state up to a supported channel maximum; no cross-channel state (FR-003).
- Switching `voicing` or `quality` carries **no stale filter/DC state** from the prior
  selection beyond documented state; `reset()` clears it (FR-008).
- `quality == oversampled` (reserved) yields a **defined, bounded fallback** (documented
  in the effect README) — never a silent partial/aliased path (FR-015; Constitution V).

## SaturationCore (RT-safe composition kernel — graduates from the lab)

```cpp
namespace acfx {

class SaturationCore {
public:
    void  prepare(float sampleRate) noexcept;         // build per-voicing SVF coeffs; no audio-path work
    void  reset() noexcept;                            // clear filter + DC state
    void  setVoicing(SaturationVoicing) noexcept;      // shape + pre/post emphasis (baked)
    void  setQuality(SaturationQuality) noexcept;      // naive/adaa path selection
    void  setDrive(float gainLinear) noexcept;         // pre-gain into the nonlinearity
    void  setBias(float) noexcept;                     // USER asymmetry (not per-voicing) — Decision 5
    void  setTone(float tilt) noexcept;                // −1..+1 post tilt
    void  setMix(float wet) noexcept;                  // 0..1 dry/wet
    void  setOutput(float gainLinear) noexcept;        // makeup trim
    float process(float x) noexcept;                   // the signal chain below
};

} // namespace acfx
```

**Signal chain (`process`)** — the normative order (FR-002):

```
wet = preEmphasis[voicing](x)              // SvfPrimitive, per-voicing fixed curve
wet = shaper(drive*wet + bias)             // Waveshaper (or ADAAWaveshaper if quality==adaa);
                                           //   shape[voicing]; internal gainComp = on
wet = postDeEmphasis[voicing](wet)         // SvfPrimitive, per-voicing fixed curve
wet = toneTilt(wet)                        // SvfPrimitive, user tone
y   = mix*wet + (1 - mix)*x                // parallel dry/wet blend (gain law: open question)
y   = output * y                           // user makeup trim
return y
```

**Kernel guarantees**: allocation-free, lock-free, bounded `process()`; silence→silence;
DC-free output for biased settings (Waveshaper DC-blocker); bounded (no NaN/Inf) under
extreme drive; coefficient/table work only in `prepare()`.

## Composition (which shipped primitives, per the prospectus "documents which it uses")

- `acfx::Waveshaper` / `acfx::ADAAWaveshaper` — `core/primitives/nonlinear/` (nonlinear
  stage + anti-aliasing; drive/bias/shape/DC-block/gain-comp reused as-is).
- `acfx::SvfPrimitive` — `core/primitives/filters/` (pre-emphasis, post-de-emphasis,
  tone tilt — three instances per channel).
- `core/dsp/` — `ParameterDescriptor`, `ParamId`, `AudioBlock`, `ProcessContext`
  (parameter substrate + block I/O), reused as-is.

No new DSP primitive is introduced (FR-001).

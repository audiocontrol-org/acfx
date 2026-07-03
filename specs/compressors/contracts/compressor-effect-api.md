# Contract — `acfx::CompressorCore` + `acfx::CompressorEffect` public API

**Feature**: `specs/compressors` | **Date**: 2026-07-02 | **Phase**: 1

The effect's contract is its public C++ interface, mirroring the shipped `SaturationEffect`/`SvfEffect`
idiom **exactly** (no base class, no vtable on the audio path). Headers:
`core/effects/compressor/compressor-core.h` and `core/effects/compressor/compressor-effect.h`
(optionally `compressor-parameters.h` per FR-028), namespace `acfx`. All audio-path methods are
`noexcept`, allocation-free, lock-free, bounded (FR-022).

## Types

```cpp
enum class Detection      : std::uint8_t { feedForward, feedBack };
enum class BallisticsSite : std::uint8_t { level, gain };
enum class StereoLink     : std::uint8_t { perChannel, linked };
// GainMode from gain-computer.h; DetectMode/DetectDomain from envelope-follower.h.
```

## `CompressorCore` (per-channel composition kernel)

```cpp
class CompressorCore {
public:
    void  prepare(float sampleRate, int maxLookaheadSamples) noexcept; // sizes the lookahead buffer
    void  reset() noexcept;                                            // clears state; prevOutput = floor

    // Configuration (recompute cached coefficients; do NOT reset runtime state).
    void  setMode(GainMode) noexcept;
    void  setThreshold(float dB) noexcept;
    void  setRatio(float ratio) noexcept;
    void  setKnee(float dB) noexcept;
    void  setRange(float dB) noexcept;
    void  setAttack(float seconds) noexcept;
    void  setRelease(float seconds) noexcept;
    void  setDetection(Detection) noexcept;
    void  setBallisticsSite(BallisticsSite) noexcept;
    void  setDetector(DetectMode) noexcept;          // peak / rms
    void  setSidechainHpf(float hz) noexcept;         // 0 = bypass
    void  setLookahead(int samples) noexcept;         // ≤ maxLookaheadSamples
    void  setMakeup(float dB) noexcept;
    void  setAutoMakeup(bool) noexcept;
    void  setMix(float wet) noexcept;                 // 0..1
    void  setOutput(float dB) noexcept;

    // Process one sample. `key` is the external sidechain (or the main input when keyless).
    float process(float x, float key) noexcept;       // returns the wet-mixed, output-trimmed sample
};
```

### `CompressorCore` behavioral contract
| Aspect | Guarantee | Spec ref |
|---|---|---|
| Static level map | Steady tone → output matches the analytic curve (compress/limit) ± tol. | SC-001 |
| Attack/release timing | Step above threshold → GR reaches ~63% within attack (± tol), per ballistics site. | SC-002 |
| Feedback fixed point | feedBack settles to the detector→curve→gain fixed point; stable for bounded input. | SC-004 |
| Ballistics site | level smooths the detected level; gain smooths the gain-reduction signal. | FR-011, SC-002 |
| Feedback tap | detector reads the **post-makeup, pre-mix** previous sample. | FR-010 |
| Sidechain HPF | key highpassed before detection; main path unaffected; 0 Hz = bypass. | FR-013, SC-006 |
| External key | detection tracks `key`; keyless callers pass `x` as `key`. | FR-014, SC-007 |
| Lookahead | main path delayed `lookaheadSamples`; buffer sized in `prepare()`. | FR-015, SC-008 |
| Makeup / auto | manual dB; auto = `−GainComputer.computeGainDb(0)`, 0 for expand/gate. | FR-016, SC-009 |
| Mix / output | `y = mix·comp + (1−mix)·x`, then `·outputGain`. | FR-016, SC-009 |
| RT-safety | zero allocation on `process()`; only the lookahead buffer, sized in `prepare()`. | FR-022, SC-012 |
| Numerical safety | no NaN/Inf for any finite input/config; feedback cold-start defined. | FR-024, SC-013 |

## `CompressorEffect` (host-facing wrapper — the `Effect` contract)

```cpp
class CompressorEffect {
public:
    static constexpr span<const ParameterDescriptor> parameters() noexcept; // single source of truth
    void  prepare(const ProcessContext&) noexcept;   // sr, channels; sizes lookahead; establishes latency
    void  reset() noexcept;
    void  process(AudioBlock& io) noexcept;           // in-place; consumes pending params at top
    void  setParameter(ParamId, float normalized) noexcept; // any thread; lock-free publish
    int   latencySamples() const noexcept;            // reported lookahead latency
};
static_assert(Effect<CompressorEffect>);              // C++20 concept (duck-typed on C++17)
```

### `CompressorEffect` behavioral contract
| Aspect | Guarantee | Spec ref |
|---|---|---|
| Effect concept | Satisfies `prepare`/`process`/`reset`/`parameters`/`setParameter`. | FR-018, SC-011 |
| Param table | One constexpr `ParameterDescriptor[]`; `static_assert` rejects a malformed descriptor. | FR-019, SC-011 |
| Thread handoff | `setParameter` any-thread, lock-free atomic; consumed at top of `process()`; no torn read. | FR-020, SC-011 |
| Prepare/reset | mutate coefficients directly (stream stopped); establish latency in `prepare()`. | FR-021, SC-008 |
| Stereo linking | linked ⇒ common gain from cross-channel max; perChannel ⇒ independent. | FR-017, SC-010 |
| RT-safety | allocation-free `process()` across all configs (allocation sentinel). | FR-022, SC-012 |

## Dependency contract
- **Allowed**: `<cmath>`, `<atomic>`, `<array>`, `<cstring>`, `core/dsp/`, and the shipped primitives
  `core/primitives/dynamics/{envelope-follower,gain-computer}.h`,
  `core/primitives/filters/svf-primitive.h`, `core/primitives/delays/delay-line.h`.
- **Forbidden**: any harness, any platform header (JUCE/libDaisy/Teensy). Enforced by
  `scripts/check-portability.sh` (FR-027).

## Out of scope (sibling/future items — FR-029)
Program-dependent / "auto" attack-release (opto/vari-mu), multiband dynamics, dynamic EQ, and a
separately graduated stateful VCA-envelope primitive.

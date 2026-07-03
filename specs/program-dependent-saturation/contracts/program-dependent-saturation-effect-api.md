# Contract — `acfx::ProgramDependentSaturationCore` + `ProgramDependentSaturationEffect` public API

**Feature**: `specs/program-dependent-saturation` | **Date**: 2026-07-03 | **Phase**: 1

Headers under `core/effects/program-dependent-saturation/`, namespace `acfx`. The effect mirrors the
shipped `SaturationEffect`/`SvfEffect` idiom **exactly**: no base class, no vtable on the audio path; one
constexpr `ParameterDescriptor` table as the single source of parameter truth; a lock-free atomic
cross-thread parameter handoff; allocation-free `prepare`/`process`/`reset`; a `static_assert`
descriptor-validity guard. Both types compose the shipped `SaturationCore`/`EnvelopeFollower`/
`SvfPrimitive` **unchanged** (FR-004/026).

## `ProgramDependentSaturationCore` (per-channel composition kernel)

```cpp
class ProgramDependentSaturationCore {
public:
    void prepare(float sampleRate) noexcept;   // prepares composed units; caches ref-window; no audio work
    void reset() noexcept;                      // clears detector/filter/saturation state + prevOutput

    // Static base parameters (the values the modulation offsets add to) — forwarded to SaturationCore.
    void setStaticDrive(float gainLinear) noexcept;
    void setVoicing(SaturationVoicing) noexcept;
    void setStaticTone(float tilt) noexcept;
    void setStaticMix(float wet) noexcept;
    void setOutput(float gainLinear) noexcept;
    void setStaticBias(float bias) noexcept;
    void setQuality(SaturationQuality) noexcept;

    // Detector (shared EnvelopeFollower) configuration.
    void setDetectorMode(DetectMode) noexcept;
    void setBallistics(Ballistics) noexcept;
    void setAttack(float seconds) noexcept;
    void setRelease(float seconds) noexcept;
    void setDetection(Detection) noexcept;      // feedForward / feedBack
    void setRefWindow(float loDb, float hiDb) noexcept;   // normalization window (default -60..0)

    // Modulation matrix — per target depth + curve.
    void setDepth(ModTarget, float signedDepth) noexcept;
    void setCurve(ModTarget, ModCurve) noexcept;

    // Sidechain.
    void setExternalKey(bool) noexcept;
    void setScHpf(float hz) noexcept;           // 0 = bypass

    // Audio path. `x` is the main sample; `key` the optional external sidechain sample.
    // drive/bias/mix modulate per-sample; tone per-block (call newBlock() at block start).
    void newBlock(float blockEnvNorm) noexcept; // applies the per-block tone offset (skipped if toneDepth==0)
    float process(float x, float key) noexcept;
};
```

### Behavioral contract

| Aspect | Guarantee | Spec ref |
|---|---|---|
| Composition | Composes `SaturationCore`/`EnvelopeFollower`/`SvfPrimitive` unchanged; adds no nonlinearity kernel. | FR-004/026 |
| Chain | source→(SC HPF)→topology fork→detect→normalize→per-target `base+offset` clamp→push→`SaturationCore::process`. | FR-005 |
| Matrix | four independent targets (drive/bias/tone/mix), each own depth+curve, shared envelope, no cross-talk. | FR-006, SC-003 |
| Orthogonality | all depths 0 ⇒ output == static `SaturationCore` at the same base params (byte-for-byte where paths coincide). | FR-007, SC-002 |
| Feedback | `feedBack` reads the previous **final output `y`**; defined cold-start floor; stable for bounded input. | FR-008, SC-006 |
| Update rate | drive/bias/mix per-sample; tone per-block via `newBlock()`. | FR-010a |
| Clamp | every modulated param clamped into `SaturationCore`'s valid range. | FR-010, SC-014 |
| RT-safety | `process()` allocation-free, lock-free, bounded; single shared detector. | FR-019/020, SC-013 |

## `ProgramDependentSaturationEffect` (host-facing wrapper)

```cpp
class ProgramDependentSaturationEffect {
public:
    enum Param : std::uint8_t { /* dense ids per data-model.md parameter table (0..23) */ };
    static constexpr span<const ParameterDescriptor> parameters() noexcept;   // the constexpr table
    void  prepare(const ProcessContext&) noexcept;   // stream stopped
    void  reset() noexcept;                            // stream stopped
    void  process(AudioBlock&) noexcept;               // consumes pending edits at top; calls newBlock() once
    void  setParameter(ParamId, float normalized) noexcept;   // any thread; lock-free publish
};
```

### Behavioral contract

| Aspect | Guarantee | Spec ref |
|---|---|---|
| Effect concept | Satisfies `prepare`/`process`/`reset`/`parameters`/`setParameter`; no base class / vtable on audio path. | FR-015, SC-012 |
| Single source of truth | One constexpr `ParameterDescriptor` table (the ~24-param set in data-model.md); `static_assert` rejects malformed descriptors. | FR-016, SC-012 |
| Thread handoff | `setParameter` callable any thread; lock-free atomic pending value consumed at top of `process()`; no race, no torn read. | FR-017, SC-012 |
| Lifecycle | `prepare`/`reset` mutate coefficients directly (stream stopped); allocation-free throughout. | FR-018, SC-013 |
| Presets | `dynamicPreset` applies a documented matrix configuration; `none` = neutral (orthogonality baseline). | FR-014, SC-008 |
| Block handling | `process()` calls `ProgramDependentSaturationCore::newBlock()` once per block for the per-block tone offset. | FR-010a |
| Default = static | Default params (preset none, depths 0, feedForward, perChannel, no key/HPF) reproduce the static saturator. | US3, SC-002 |

## Dependency contract
- **Allowed**: `<cmath>`, `<atomic>`, `<array>`, `<cstring>`, `core/dsp/`, the shipped
  `core/effects/saturation/*` and `core/primitives/{dynamics,filters}/*`. **Forbidden**: any harness, any
  platform header (JUCE/libDaisy/Teensy). Enforced by `scripts/check-portability.sh` (FR-024).
- If the ~24-parameter table pushes `program-dependent-saturation-effect.h` past ~300–500 lines, split
  the descriptor table + denormalize logic into `program-dependent-saturation-parameters.h` (FR-025).

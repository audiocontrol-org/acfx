# Phase 1 Data Model — Harmonic Analysis

Entities are C++ value types (host-side, may allocate at construction/`init()`) except the capture probe, which is a fixed-size portable RT value type. No entity persists to disk; the CSV report surface (deferred) is the only serialization seam.

## CaptureProbeRing (portable core — `core/primitives/analysis/capture-probe.h`)

The audio↔analysis bridge. Single-producer (audio thread) / single-consumer (analysis thread).

| Field | Type | Notes |
|---|---|---|
| `buffer` | fixed-size value array `float[Capacity]` | Compile-time `Capacity` ≥ one FFT window + margin; no heap |
| `writeIndex` | atomic index | Advanced by the producer with release semantics |
| `readIndex` | atomic index | Advanced by the consumer with acquire semantics |
| `overrunCount` | atomic counter | Incremented when the producer laps the consumer (observable) |

- **Invariants**: audio-path `push(block)` performs only a bounded copy + one release store — no alloc, no lock, no math. Capacity is a power-of-two-friendly compile-time constant.
- **State transitions**: normal → overrun (consumer behind; producer advances, counts dropped span) → normal; normal → underrun (consumer sees < window; holds, skips update) → normal. Both states are observable, never corrupt.

## Window (host — `host/analysis/window.h`)

| Field | Type | Notes |
|---|---|---|
| `kind` | enum `{ BlackmanHarris, Hann, FlatTop }` | Default `BlackmanHarris` (FR-025) |
| `coeffs` | value array sized to the transform | Precomputed at `init()`; applied before FFT |

- **Rule**: the retained integer-cycle Goertzel path does **not** use a Window (rectangular/leakage-free).

## Fft (host — `host/analysis/fft.h`)

| Field | Type | Notes |
|---|---|---|
| `size` | int (power of two) | Default 8192; **non-pow2 → descriptive error** (FR-026) |
| `twiddles` | value array | Precomputed at `init()` |

- **Output**: complex spectrum → magnitude + phase per bin.

## HarmonicSpectrum (host — `host/analysis/spectrum.h`)

| Field | Type | Notes |
|---|---|---|
| `fundamentalHz` | double | Analyzed fundamental |
| `numHarmonics` | int | Arbitrary N requested |
| `magnitude[i]` | double | Per-harmonic magnitude at (i+1)·fundamental; out-of-band → not-measured |
| `phaseRad[i]` | double | Per-harmonic phase; below the amplitude floor → NaN (FR-008) |

- **Accessor**: `at(k)` 1-based (k=1 fundamental); out-of-range → not-measured, not fabricated 0.0.

## ThdnResult (host — `host/analysis/thdn.h`)

| Field | Type | Notes |
|---|---|---|
| `thdPlusN` | double | RMS(non-fundamental) / RMS(fundamental); NaN if no fundamental (FR-008) |
| `noiseFloor` | double | Broadband residual level |
| `snr` | double | Referenced to the fundamental level |

## ImdResult (host — `host/analysis/imd.h`)

| Field | Type | Notes |
|---|---|---|
| `method` | enum `{ SMPTE, CCIF }` | 60+7000 Hz (4:1) / 19+20 kHz (1:1) |
| `differenceProducts[]` | double | e.g. CCIF 1 kHz; SMPTE 7000±60·k sidebands |
| `sumProducts[]` | double | Sum-frequency products |
| `imdRatio` | double | Product energy vs carrier(s) |

- **Rule**: a product bin coinciding with a harmonic is attributed unambiguously (spec edge case), not double-counted.

## AliasSweepCurve (host — `host/analysis/alias-sweep.h`)

| Field | Type | Notes |
|---|---|---|
| `frequencyHz[]` | double | Swept tone frequencies |
| `inharmonicEnergy[]` | double | Folded/inharmonic energy per step (integer-cycle measure reused) |

## DriveSeries (host — `host/analysis/drive-series.h`)

| Field | Type | Notes |
|---|---|---|
| `driveValue[]` | double | Swept drive/control values |
| `thd[]` | double | THD per drive point (drive→THD) |
| `harmonic[k][]` | double | Per-harmonic amplitude curve across drive (drive→harmonic) |

## AnalysisEngine (host — `host/analysis/analysis-engine.h`)

The single entry surface shared by tests and the workbench/plugin readouts (FR-006/014/015). Composes Window + Fft + the metric reducers + the retained Goertzel path. Operates on any `Effect`-contract implementation or per-sample callable. Holds precomputed twiddles/window (init-time alloc, off the audio thread). Provides both the FFT-based breadth metrics and the exact single-bin Goertzel known-bin readout (FR-007/010).

## LiveReadout (host adapters — `adapters/workbench/`, `adapters/plugin/`)

| Field | Type | Notes |
|---|---|---|
| `probe` | `CaptureProbeRing&` | Fed by the audio thread |
| `engine` | `AnalysisEngine` | The shared host engine |
| `refreshHz` | double | ~15–30 Hz overlapping-window updates |

- **Rule**: one implementation shared by both hosts; desktop-only; never embedded (FR-016).

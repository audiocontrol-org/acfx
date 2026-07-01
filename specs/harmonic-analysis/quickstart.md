# Quickstart — Harmonic Analysis

Validation/run guide. Implementation detail lives in `tasks.md`; API surfaces in `contracts/`.

## Prerequisites

- The acfx host toolchain (CMake + CPM, doctest) as used by the existing measurement suites.
- Offline CPM cache for sandbox builds: `export CPM_SOURCE_CACHE=external/.cpm-cache` (repo convention).

## Build & run the offline analysis suites

```bash
cmake -B build -S .
cmake --build build --target acfx_core_tests
ctest --test-dir build --output-on-failure -R 'analysis-|capture-probe|no-allocation'
```

Expected: all new suites green —
- `analysis-fft-test` — FFT reconstructs analytic tones; **non-power-of-two length is rejected with a descriptive error** (FR-009/026).
- `analysis-window-test` — default window is Blackman-Harris; Hann/flat-top selectable (FR-025).
- `analysis-spectrum-test` — full spectrum magnitude+phase matches analytic harmonic signatures (symmetric→odd only; biased→even+odd+DC) (FR-001).
- `analysis-thdn-test` — THD+N residual + noise-floor/SNR match analytic references; no fundamental → NaN (FR-002/008).
- `analysis-imd-test` — SMPTE + CCIF difference/sum products within tolerance (FR-003).
- `analysis-alias-sweep-test` — inharmonic energy rises past Nyquist; a band-limited arm is lower (FR-004).
- `analysis-drive-series-test` — drive→THD monotonic where the model predicts; per-harmonic curves match (FR-005).
- `analysis-goertzel-parity-test` — retained Goertzel reproduces exact known-bin values (FR-007, zero regression).
- `capture-probe-test` — SPSC ring correctness + deterministic overrun/underrun (FR-011/013).
- `analysis-live-offline-parity-test` — a metric captured through the probe equals the offline engine result within tolerance (FR-015).
- `no-allocation-test` (extended) — `CaptureProbeRing::push()` allocates nothing on the audio path (SC-004).

## Consolidation check (US4)

```bash
cmake --build build --target waveshaping_harness saturation_harness oversampling_harness
# harnesses repointed at host/analysis; run each and confirm identical harmonic tables / aliasing figures as before
grep -rn "Goertzel" core/labs/*/harness/   # expect: NO self-contained Goertzel left; all via host/analysis
```

Expected: every previously green harmonic/aliasing suite stays green; no per-lab Goertzel copy remains (SC-003).

## Portability gate

```bash
scripts/check-portability.sh
```

Expected: PASS with `core/primitives/analysis/**` covered — platform-free, harness-free, and neither `host/analysis` nor any adapter reachable from portable `core/` (SC-006).

## Live readout (workbench + plugin)

```bash
# workbench
cmake --build build --target acfx_workbench
# run the workbench, load a nonlinearity, enable the harmonic readout
```

Expected: a broadband spectrum + running-THD readout updates at ~15–30 Hz while audio plays glitch-free; the audio callback does only a bounded lock-free ring push. The same readout, driven by the same portable capture probe, is available in the plugin build (`acfx_plugin`). A live figure matches the offline figure for the same stimulus within tolerance (FR-015).

## Amendment note (FR-019)

Confirm `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` carries the appended note recording the Phase-2 off-thread FFT amending its Decision A.

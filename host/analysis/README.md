# host/analysis — the shared harmonic-analysis engine

Host-only harmonic characterization engine. **May allocate; runs only off the audio
thread** (host UI/analysis thread, or offline tests). It is the single source of
every harmonic metric, consumed identically by the doctest suites and by the
workbench/plugin live readouts.

## Boundary (Constitution IV)

- **Host-only.** Never reachable from portable `core/` — the portability gate
  (`scripts/check-portability.sh`, `C-AN-DIR`) fails if any `core/` file includes
  `analysis/…`. The one audio-path unit (the RT capture probe) lives in
  `core/primitives/analysis/` and depends on nothing here.
- **Dependency direction:** `tests/support → host/analysis` and
  `adapters/{workbench,plugin} → host/analysis`, never the reverse. The reusable
  building blocks (`stimulus.h`, `analyzers.h` GoertzelAnalyzer, `aliasing.h`) live
  here; `tests/support/measurement/` re-exports them (analyze finding F1).
- Exposed to host targets via the header-only `acfx_analysis` INTERFACE CMake lib
  (puts `host/` on the include path → `#include "analysis/…"`).

## One-engine guarantee (FR-014 / FR-015)

The workbench and the plugin drain the RT capture probe and call the **same**
`host/analysis` functions the offline suites call. A live-measured number and a
test-measured number agree within a named tolerance —
`tests/core/analysis-live-offline-parity-test.cpp` asserts this (SC-005). There is
no second engine and no re-derived spectral math anywhere.

## Hybrid spectral engine: FFT for breadth, Goertzel for exactness

The design resolved the FFT-vs-Goertzel fork as a **hybrid** (they are
complementary, not competing):

- **`fft.h`** — a self-contained windowed radix-2 FFT (default 4-term
  Blackman-Harris window, `window.h`) for **breadth**: broadband magnitude+phase
  spectrum, THD+N, IMD, the live display. Power-of-two only; a non-power-of-two
  length is **rejected with a descriptive error**, never silently zero-padded
  (Constitution V, FR-026). This introduces an off-thread FFT in Phase 2, amending
  the measurement-infrastructure design's Decision A (which deferred a general FFT
  to Phase 8); Phase 8 (partitioned convolution) reuses/supersedes it.
- **`analyzers.h` GoertzelAnalyzer** (via `goertzelBin` in `analysis-engine.h`) —
  the **exact, leakage-free** single-bin readout on integer-cycle known tones,
  retained for regression tests. The FFT does not replace it there (FR-007/010).

## Surface

`window.h`, `fft.h`, `stimulus.h`, `analyzers.h`, `aliasing.h`, `spectrum.h`
(per-harmonic magnitude+phase), `thdn.h` (residual THD+N + noise-floor/SNR),
`imd.h` (SMPTE/CCIF twin-tone), `alias-sweep.h` (alias-vs-frequency), `drive-series.h`
(drive→THD / drive→harmonic), `live-readout.h` (the shared drain+analyze readout),
`analysis-engine.h` (the single entry seam). Unmeasurable quantities return the NaN
sentinel, never a fabricated `0.0` (FR-008).

See `specs/harmonic-analysis/contracts/analysis-engine-api.md`.

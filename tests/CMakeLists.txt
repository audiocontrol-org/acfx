# Host-side doctest suite for the platform-independent core (Constitution VIII,
# FR-013). Builds one binary from all tests/core/* sources + the allocation
# sentinel, links the core spine + the host boundary, and registers with ctest so
# `ctest --preset test` runs everything.

add_executable(acfx_core_tests
  core/test-main.cpp
  core/parameter-test.cpp
  core/svf-test.cpp
  core/measurement-stimulus-test.cpp
  core/measurement-response-test.cpp
  core/measurement-distortion-test.cpp
  core/measurement-stability-test.cpp
  core/measurement-report-test.cpp
  core/delay-line-test.cpp
  core/lfo-test.cpp
  core/modulated-delay-test.cpp
  core/modulated-delay-us2-test.cpp
  core/modulated-delay-wowflutter-test.cpp
  core/no-allocation-test.cpp
  core/workbench-settings-test.cpp
  # The SourceConfig serde seam is JUCE-free (std::string only), so its TU links
  # into this JUCE-free test target directly (contracts/source-config.md).
  "${CMAKE_SOURCE_DIR}/adapters/workbench/workbench-settings.cpp"
  support/allocation-sentinel.cpp
  # ---------------------------------------------------------------------------
  # Waveshaper test suites (waveshapers feature): the six suites below are the
  # actual authored set (T007/T008 wrapper+harmonics, T012 shapes, T019
  # antiderivatives, T015 LUT, T018 ADAA).
  # ---------------------------------------------------------------------------
  core/waveshaper-test.cpp
  core/waveshaper-harmonics-test.cpp
  core/waveshaper-shapes-test.cpp
  core/waveshaper-antiderivatives-test.cpp
  core/waveshaper-lut-test.cpp
  core/waveshaper-adaa-test.cpp
  # ---------------------------------------------------------------------------
  # Saturation test suites (saturation feature): appended by their authoring
  # tasks (T007 core, T011 voicings, T014 effect, T008 harmonics, T017
  # aliasing). Each task adds its own core/saturation-*-test.cpp line below.
  # ---------------------------------------------------------------------------
  core/saturation-core-test.cpp
  core/saturation-harmonics-test.cpp
  core/saturation-voicings-test.cpp
  core/saturation-effect-test.cpp
  core/saturation-effect-rt-test.cpp
  core/saturation-aliasing-test.cpp
  # ---------------------------------------------------------------------------
  # Oversampling primitive test suites (oversampling feature): appended by their
  # authoring tasks. T007 transparency, T008 aliasing (+ T012 response, T015
  # latency added by their tasks).
  # ---------------------------------------------------------------------------
  core/oversampler-transparency-test.cpp
  core/oversampler-aliasing-test.cpp
  core/oversampler-response-test.cpp
  core/oversampler-latency-test.cpp
  # ---------------------------------------------------------------------------
  # harmonic-analysis feature: host-only analysis engine suites (host/analysis/).
  # T003 FFT (windowed radix-2 tone reconstruction; non-pow2 rejection; FR-009/026).
  # T005 window (default Blackman-Harris; Hann/flat-top selectable; FR-025).
  # T008 spectrum (full harmonic magnitude+phase; out-of-band/sub-floor NaN; FR-001/008).
  # T010 thdn (THD+N residual method + noise-floor/SNR; no-fundamental NaN; FR-002/008).
  # T012 goertzel parity (retained exact known-bin Goertzel path; FR-007/010).
  # T014 imd (twin-tone SMPTE+CCIF difference/sum products; FR-003, US2).
  # ---------------------------------------------------------------------------
  core/analysis-fft-test.cpp
  core/analysis-window-test.cpp
  core/analysis-spectrum-test.cpp
  core/analysis-thdn-test.cpp
  core/analysis-goertzel-parity-test.cpp
  core/analysis-imd-test.cpp
)

# tests/ as an include root so test files write #include "support/...".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# adapters/workbench on the include path so the serde test (and its TU) resolve
# #include "workbench-settings.h".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/adapters/workbench")
target_link_libraries(acfx_core_tests PRIVATE acfx_core acfx_host acfx_analysis doctest::doctest)
target_compile_features(acfx_core_tests PRIVATE cxx_std_20)

# Per-test-case ctest registration when doctest ships its CMake helper; otherwise
# register the whole binary as a single test (still satisfies the test preset).
set(_doctest_module "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
if(EXISTS "${_doctest_module}")
  include("${_doctest_module}")
  doctest_discover_tests(acfx_core_tests)
else()
  add_test(NAME acfx_core_tests COMMAND acfx_core_tests)
endif()

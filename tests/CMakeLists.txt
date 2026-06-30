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
  # Waveshaper test suites (T007/T008/T012/T015/T018): add each new .cpp below
  # as it is authored, one line per suite, e.g.:
  #   core/waveshaper-shape-test.cpp
  #   core/waveshaper-adaa-test.cpp
  #   core/waveshaper-dc-block-test.cpp
  #   core/waveshaper-drive-test.cpp
  #   core/waveshaper-integration-test.cpp
  # ---------------------------------------------------------------------------
  core/waveshaper-test.cpp
)

# tests/ as an include root so test files write #include "support/...".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# adapters/workbench on the include path so the serde test (and its TU) resolve
# #include "workbench-settings.h".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/adapters/workbench")
target_link_libraries(acfx_core_tests PRIVATE acfx_core acfx_host doctest::doctest)
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

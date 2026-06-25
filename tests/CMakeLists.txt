# Host-side doctest suite for the platform-independent core (Constitution VIII,
# FR-013). Builds one binary from all tests/core/* sources + the allocation
# sentinel, links the core spine + the host boundary, and registers with ctest so
# `ctest --preset test` runs everything.

add_executable(acfx_core_tests
  core/test-main.cpp
  core/parameter-test.cpp
  core/svf-test.cpp
  core/no-allocation-test.cpp
  support/allocation-sentinel.cpp
)

# tests/ as an include root so test files write #include "support/...".
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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

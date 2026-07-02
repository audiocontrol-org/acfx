cmake_minimum_required(VERSION 3.21)

# acfx — cross-platform audio DSP platform.
# One platform-independent core, four thin target adapters. Dependencies point
# only inward (adapters -> core; core -> nothing platform-specific).
project(acfx VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Build-surface options. Each preset (CMakePresets.json) flips exactly the set
# of targets it needs; dependencies are fetched lazily to match (see
# cmake/dependencies.cmake), so the `test` preset stays lightweight.
# ---------------------------------------------------------------------------
option(ACFX_BUILD_TESTS   "Build the host-side doctest suite for core/"      OFF)
option(ACFX_BUILD_DESKTOP "Build the JUCE workbench + plugin adapters"       OFF)
option(ACFX_BUILD_DAISY   "Build the Daisy (libDaisy) adapter"               OFF)
option(ACFX_BUILD_TEENSY  "Build the Teensy adapter"                         OFF)

# Keep CPM's source cache inside the repo (gitignored) for reproducible fetches.
if(NOT DEFINED CPM_SOURCE_CACHE)
  set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/external/.cpm-cache")
endif()

# ---------------------------------------------------------------------------
# C++ standard. The core compiles under both C++17 and C++20; the Effect/Generator
# concept contract-checks are guarded by __cpp_concepts in the headers, so the
# same source compiles either way. Desktop/test/Daisy build at C++20 (named
# concept checks); Teensy may be pinned lower by its toolchain file.
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake)

# ---------------------------------------------------------------------------
# acfx_core — the platform-independent spine. Header-only (INTERFACE): the DSP
# core, the Effect concept, the parameter model, and the SVF effect all live in
# headers under core/. It links DaisySP because core/primitives wraps DaisySP's
# Svf (a pure-DSP, platform-independent math library — Constitution IV permits
# the inward wrap).
# ---------------------------------------------------------------------------
add_library(acfx_core INTERFACE)
add_library(acfx::core ALIAS acfx_core)
target_include_directories(acfx_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_compile_features(acfx_core INTERFACE cxx_std_17)
if(TARGET DaisySP)
  target_link_libraries(acfx_core INTERFACE DaisySP)
endif()

# ---------------------------------------------------------------------------
# acfx_analysis -- the host-only harmonic-analysis engine (harmonic-analysis
# feature, host/analysis/). Header-only (INTERFACE), like acfx_core: puts
# host/ on the include path so headers are included as
# "#include "analysis/window.h"" (mirroring acfx_core's core/ ->
# "primitives/..."). HOST-ONLY: never linked by MCU/core targets, only by
# acfx_core_tests here; desktop adapters link it when the live readout lands.
# ---------------------------------------------------------------------------
add_library(acfx_analysis INTERFACE)
add_library(acfx::analysis ALIAS acfx_analysis)
target_include_directories(acfx_analysis INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/host")
target_compile_features(acfx_analysis INTERFACE cxx_std_17)

# ---------------------------------------------------------------------------
# Subdirectories, gated by the build-surface options.
# ---------------------------------------------------------------------------
if(ACFX_BUILD_DESKTOP)
  add_subdirectory(host/processor-node)
  # The reusable desktop effect-target factory (workbench + plugin per effect).
  # "We are going to make many effects": each adapter CMakeLists below calls the
  # factory once per effect, recompiling the shared adapter sources with the
  # concrete effect type/header injected via compile definitions.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/acfx-effect-targets.cmake)
  add_subdirectory(adapters/workbench)
  add_subdirectory(adapters/plugin)
endif()

if(ACFX_BUILD_DAISY)
  # The reusable effect-target factory (firmware per effect). Include-guarded, so
  # it is safe to include here as well as in the desktop branch above.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/acfx-effect-targets.cmake)
  add_subdirectory(adapters/daisy)
endif()

if(ACFX_BUILD_TEENSY)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/acfx-effect-targets.cmake)
  add_subdirectory(adapters/teensy)
endif()

if(ACFX_BUILD_TESTS)
  enable_testing()
  # host/processor-node is desktop-only host code, but it is pure C++ (no JUCE),
  # so the test build includes it to exercise the EffectNode boundary too.
  add_subdirectory(host/processor-node)
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Lab harnesses — host-only diagnostic executables. Built under any host
# configuration (test or desktop). Never built under MCU configurations
# (daisy / teensy) because they have their own toolchains and no stdio.
# ---------------------------------------------------------------------------
if(ACFX_BUILD_TESTS OR ACFX_BUILD_DESKTOP)
  add_executable(acfx_lab_svf_harness
    core/labs/state-variable-filter/harness/svf-harness.cpp
  )
  target_link_libraries(acfx_lab_svf_harness PRIVATE acfx_core)
  target_compile_features(acfx_lab_svf_harness PRIVATE cxx_std_20)

  # Waveshaping lab harness — mirrors acfx_lab_svf_harness exactly (same
  # linkage and feature). The harness is a required, completed deliverable;
  # a missing source file should fail configure, not silently drop the target.
  add_executable(acfx_lab_waveshaping_harness
    core/labs/waveshaping/harness/waveshaping-harness.cpp
  )
  target_link_libraries(acfx_lab_waveshaping_harness PRIVATE acfx_core acfx_analysis)
  target_compile_features(acfx_lab_waveshaping_harness PRIVATE cxx_std_20)

  # Saturation lab harness — same linkage as the two harnesses above, plus the
  # tests/ include root: unlike the SVF/waveshaping harnesses (self-contained
  # per the SVF-harness precedent), this harness drives its measurements
  # through the SHARED measurement infrastructure
  # (tests/core/measurement-support.h — meastest::harmonicSignature /
  # meastest::aliasingMeasure) per spec.md US5's acceptance scenario 1. That
  # header and its support/measurement/*.h dependencies are header-only, so
  # only the include path is needed — no test-target linkage.
  add_executable(acfx_lab_saturation_harness
    core/labs/saturation/harness/saturation-harness.cpp
  )
  target_include_directories(acfx_lab_saturation_harness PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  # acfx_analysis: this harness includes core/measurement-support.h, which
  # includes the tests/support/measurement/*.h re-export shims that now
  # forward to host/analysis/*.h (harmonic-analysis T007, analyze finding F1).
  target_link_libraries(acfx_lab_saturation_harness PRIVATE acfx_core acfx_analysis)
  target_compile_features(acfx_lab_saturation_harness PRIVATE cxx_std_20)

  # Oversampling lab harness — same linkage as acfx_lab_saturation_harness,
  # plus the tests/ include root: like the saturation harness, this harness
  # drives its aliasing-reduction evidence through the SHARED measurement
  # infrastructure (tests/core/measurement-support.h — meastest::aliasingMeasure)
  # per the lab README's "Measured evidence" section, rather than reimplementing
  # a second spectral engine. That header and its support/measurement/*.h
  # dependencies are header-only, so only the include path is needed — no
  # test-target linkage.
  add_executable(acfx_lab_oversampling_harness
    core/labs/oversampling/harness/oversampling-harness.cpp
  )
  target_include_directories(acfx_lab_oversampling_harness PRIVATE "${CMAKE_SOURCE_DIR}/tests")
  # acfx_analysis: this harness includes core/measurement-support.h, which
  # includes the tests/support/measurement/*.h re-export shims that now
  # forward to host/analysis/*.h (harmonic-analysis T007, analyze finding F1).
  target_link_libraries(acfx_lab_oversampling_harness PRIVATE acfx_core acfx_analysis)
  target_compile_features(acfx_lab_oversampling_harness PRIVATE cxx_std_20)
endif()

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
  add_subdirectory(adapters/daisy)
endif()

if(ACFX_BUILD_TEENSY)
  add_subdirectory(adapters/teensy)
endif()

if(ACFX_BUILD_TESTS)
  enable_testing()
  # host/processor-node is desktop-only host code, but it is pure C++ (no JUCE),
  # so the test build includes it to exercise the EffectNode boundary too.
  add_subdirectory(host/processor-node)
  add_subdirectory(tests)
endif()

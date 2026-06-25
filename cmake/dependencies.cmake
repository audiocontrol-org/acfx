# acfx external dependencies — CPM-pinned to explicit, known-good refs.
#
# Per research.md (Phase 0, decision 4): every dependency is fetched by CPM and
# pinned to an explicit ref. The pin is a real, reproducible tag/commit captured
# when the dependency is first fetched and verified to build — never a fabricated
# version number.
#
# Pins verified by an in-session fetch+build (the `test` preset path):
#   - DaisySP   599511b740f8f3a9b8db72a0642aa45b8a23c3a3   (core SVF primitive)
#   - doctest   v2.5.2                                      (host-side test runner)
#
# Pins captured from the upstream repos (real refs); first-fetch verification
# happens the first time each target's preset is configured on a machine with the
# matching toolchain (desktop / daisy / teensy):
#   - JUCE                    8.0.14    (workbench + plugin)
#   - clap-juce-extensions    16e9d4c   (CLAP export, plugin only)
#   - libDaisy                c02245d   (daisy adapter)
#   - Teensy cores            a664eff   (teensy adapter)
#   - Teensy Audio Library    3039be2   (teensy adapter)
#
# Dependencies are fetched lazily: a dependency is only declared when a target
# that needs it is enabled, so the `test` preset pulls only DaisySP + doctest.

include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

# --- Core: DaisySP (platform-independent pure-DSP math; wrapped by core/primitives)
# Needed by core/ on every target, so it is always declared.
CPMAddPackage(
  NAME DaisySP
  GITHUB_REPOSITORY electro-smith/DaisySP
  GIT_TAG 599511b740f8f3a9b8db72a0642aa45b8a23c3a3
  DOWNLOAD_ONLY YES
)

if(DaisySP_ADDED)
  # DaisySP's own CMake assumes an ARM cross-build; for a portable host build we
  # compile its sources into a small static lib directly against the wrapped
  # primitives we use. The full library is available, but we only need the DSP
  # translation units, which are platform-independent C++.
  file(GLOB_RECURSE _daisysp_sources CONFIGURE_DEPENDS "${DaisySP_SOURCE_DIR}/Source/*.cpp")
  add_library(DaisySP STATIC ${_daisysp_sources})
  target_include_directories(DaisySP PUBLIC "${DaisySP_SOURCE_DIR}/Source")
  target_compile_features(DaisySP PUBLIC cxx_std_17)
  set_target_properties(DaisySP PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# --- Host-side tests: doctest
if(ACFX_BUILD_TESTS)
  CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.5.2
  )
endif()

# --- Desktop (workbench + plugin): JUCE 8, plus clap-juce-extensions for CLAP.
if(ACFX_BUILD_DESKTOP)
  CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.14
  )
  CPMAddPackage(
    NAME clap-juce-extensions
    GITHUB_REPOSITORY free-audio/clap-juce-extensions
    GIT_TAG 16e9d4ca7b1e86c76e04584b2c08e85a764bcda8
  )
endif()

# --- Daisy: libDaisy (provides the STM32 HAL + audio callback glue).
if(ACFX_BUILD_DAISY)
  CPMAddPackage(
    NAME libDaisy
    GITHUB_REPOSITORY electro-smith/libDaisy
    GIT_TAG c02245d22b38acad3916d9c2f156bcba34fa15af
    DOWNLOAD_ONLY YES
  )
endif()

# --- Teensy: Teensy cores + Audio Library.
if(ACFX_BUILD_TEENSY)
  CPMAddPackage(
    NAME teensy_cores
    GITHUB_REPOSITORY PaulStoffregen/cores
    GIT_TAG a664effb008d1ac8d8f00f3f19b47c0d1ea46e3b
    DOWNLOAD_ONLY YES
  )
  CPMAddPackage(
    NAME teensy_audio
    GITHUB_REPOSITORY PaulStoffregen/Audio
    GIT_TAG 3039be2773e86daf1f381a1e8bdc1e6a55ed11f1
    DOWNLOAD_ONLY YES
  )
endif()

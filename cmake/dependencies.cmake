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
# Pins verified by an in-session fetch that resolved the pinned ref (the `nucleo`
# preset configured against the Arm GNU Toolchain). Firmware build verification
# follows once the adapter declares targets:
#   - TinyUSB                 0.21.0    (nucleo adapter — USB audio/MIDI/CDC)
#   - cmsis_device_f4         v2.6.11   (nucleo adapter — STM32F4 device headers)
#   - CMSIS_5                 5.9.0     (nucleo adapter — CMSIS core, e.g. core_cm4.h)
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
# Needed by core/ on every target, so it is always declared. DaisySP ships its own
# CMakeLists that defines the `DaisySP` static-lib target with the correct (bare-
# name) include directories for its sources; we use it directly rather than
# re-globbing. It is portable C++ (no platform headers), so it builds host-side.
CPMAddPackage(
  NAME DaisySP
  GITHUB_REPOSITORY electro-smith/DaisySP
  GIT_TAG 599511b740f8f3a9b8db72a0642aa45b8a23c3a3
  # DaisySP's fmax()/fmin() emit FPv5-only VMAXNM/VMINNM inline asm gated on bare
  # `#ifdef __arm__`, which HardFaults the Cortex-M4F (FPv4) Nucleo the first time
  # an effect's prepare() runs float min/max. Re-gate that asm on an explicit FPv5
  # opt-in macro (DSY_FPV5_MAXMIN, set only by the M7 daisy/teensy toolchains);
  # every other target uses DaisySP's own portable path. Idempotent -> warm-cache /
  # offline-reconfigure safe. See cmake/patches/daisysp-fpv5-maxmin.cmake.
  PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/patches/daisysp-fpv5-maxmin.cmake
)

if(TARGET DaisySP)
  set_target_properties(DaisySP PROPERTIES POSITION_INDEPENDENT_CODE ON)
  # DaisySP's sources use POSIX/GNU math constants (M_E, M_PI, ...), which newlib
  # hides under a strict -std=c++NN (__STRICT_ANSI__) on the ARM toolchain. The
  # repo-wide CMAKE_CXX_EXTENSIONS OFF therefore breaks the embedded DaisySP build;
  # DaisySP upstream itself compiles with GNU extensions. Re-enable them for this
  # dependency target only (no effect on the acfx core/adapters, which stay strict;
  # desktop math.h exposes these constants regardless).
  set_target_properties(DaisySP PROPERTIES CXX_EXTENSIONS ON)
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

# --- Nucleo: TinyUSB (USB audio/MIDI/CDC stack), cmsis_device_f4 (STM32F4 device headers), and
# CMSIS_5 (ARM CMSIS core headers, e.g. core_cm4.h, that cmsis_device_f4's headers require but do
# not vendor themselves). All three compile their sources directly into the adapter rather than
# being consumed as submodules (FR-010/FR-011).
if(ACFX_BUILD_NUCLEO)
  CPMAddPackage(
    NAME TinyUSB
    GITHUB_REPOSITORY hathach/tinyusb
    GIT_TAG 0.21.0
    DOWNLOAD_ONLY YES
  )
  CPMAddPackage(
    NAME cmsis_device_f4
    GITHUB_REPOSITORY STMicroelectronics/cmsis_device_f4
    GIT_TAG v2.6.11
    DOWNLOAD_ONLY YES
  )
  CPMAddPackage(
    NAME CMSIS_5
    GITHUB_REPOSITORY ARM-software/CMSIS_5
    GIT_TAG 5.9.0
    DOWNLOAD_ONLY YES
  )
endif()

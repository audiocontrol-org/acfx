# Teensy firmware targets (T033/T017 embedded). Compiles the SAME core effects into
# Teensy 4.x firmware via the Teensy core + Audio Library — the only
# platform-specific code is teensy-main.cpp (AudioStream node + analog reads),
# recompiled per effect with the concrete effect type/header injected by the
# acfx_add_effect_teensy factory (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER). No JUCE,
# no ProcessorNode (Constitution IV; SC-007). Add a new effect's firmware with one
# more factory call below.
#
# The C++ standard is the highest the installed Teensy toolchain supports (>=17,
# T034 / research.md decision 3); on C++17 the Effect concept degrades to a
# duck-typed template and the same effect compiles unchanged. Requires the Teensy
# ARM toolchain WITH the C++ standard library; a C-only arm-none-eabi is a hard
# error, never a fallback.

if(NOT DEFINED teensy_cores_SOURCE_DIR OR NOT DEFINED teensy_audio_SOURCE_DIR)
  message(FATAL_ERROR
    "Teensy cores/Audio not fetched. Configure with the `teensy` preset so "
    "cmake/dependencies.cmake pins and downloads them.")
endif()

set(_teensy4 "${teensy_cores_SOURCE_DIR}/teensy4")

# One-time platform setup: Teensy core (Arduino runtime for IMXRT1062) + Audio
# Library, built ONCE as the platform substrate the adapters link against.
file(GLOB _teensy_core_sources CONFIGURE_DEPENDS "${_teensy4}/*.c" "${_teensy4}/*.cpp")
file(GLOB _teensy_audio_sources CONFIGURE_DEPENDS "${teensy_audio_SOURCE_DIR}/*.cpp")

add_library(teensy_platform STATIC ${_teensy_core_sources} ${_teensy_audio_sources})
target_include_directories(teensy_platform PUBLIC "${_teensy4}" "${teensy_audio_SOURCE_DIR}")
target_compile_definitions(teensy_platform PUBLIC ARDUINO=10813 TEENSYDUINO=159 ARDUINO_TEENSY40)

# Teensy linker script (IMXRT1062), shared by all firmwares.
set(_teensy_lds "${_teensy4}/imxrt1062.ld")
set(_teensy_main "${CMAKE_CURRENT_SOURCE_DIR}/teensy-main.cpp")

# SVF — preserve the existing target name + behavior exactly (no regression).
acfx_add_effect_teensy(
  NAME acfx_teensy
  EFFECT_TYPE acfx::SvfEffect
  EFFECT_HEADER effects/svf/svf-effect.h
  SOURCE ${_teensy_main}
  LINKER_SCRIPT ${_teensy_lds}
)

# Modulated delay — firmware for the second effect.
acfx_add_effect_teensy(
  NAME acfx_teensy_delay
  EFFECT_TYPE acfx::ModulatedDelayEffect
  EFFECT_HEADER effects/modulated-delay/modulated-delay-effect.h
  SOURCE ${_teensy_main}
  LINKER_SCRIPT ${_teensy_lds}
)

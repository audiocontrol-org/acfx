# Daisy firmware targets (T032/T010 embedded). Compiles the SAME core effects into
# Daisy firmware via libDaisy — the only platform-specific code is daisy-main.cpp
# (audio callback + ADC), recompiled per effect with the concrete effect
# type/header injected by the acfx_add_effect_daisy factory (ACFX_EFFECT_TYPE /
# ACFX_EFFECT_HEADER). No JUCE, no ProcessorNode (Constitution IV; SC-007). Add a
# new effect's firmware with one more factory call below.
#
# Requires the ARM embedded toolchain WITH the C++ standard library (ARM's
# gcc-arm-embedded or the Daisy toolchain). A C-only arm-none-eabi (no libstdc++)
# cannot build the C++ core — that is surfaced as a hard error, never a fallback.

if(NOT DEFINED libDaisy_SOURCE_DIR)
  message(FATAL_ERROR
    "libDaisy not fetched. Configure with the `daisy` preset so cmake/dependencies.cmake "
    "pins and downloads libDaisy.")
endif()

# One-time platform setup: libDaisy ships its own CMake that defines the `daisy`
# static library + the STM32H750 build flags and linker script. Added ONCE here,
# not per effect.
add_subdirectory("${libDaisy_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libDaisy" EXCLUDE_FROM_ALL)

# Daisy bootloader linker script (STM32H750IB internal flash), shared by all firmwares.
set(_daisy_lds "${libDaisy_SOURCE_DIR}/core/STM32H750IB_flash.lds")
set(_daisy_main "${CMAKE_CURRENT_SOURCE_DIR}/daisy-main.cpp")

# SVF — preserve the existing target name + behavior exactly (no regression).
acfx_add_effect_daisy(
  NAME acfx_daisy
  EFFECT_TYPE acfx::SvfEffect
  EFFECT_HEADER effects/svf/svf-effect.h
  SOURCE ${_daisy_main}
  LINKER_SCRIPT ${_daisy_lds}
)

# Modulated delay — firmware for the second effect.
acfx_add_effect_daisy(
  NAME acfx_daisy_delay
  EFFECT_TYPE acfx::ModulatedDelayEffect
  EFFECT_HEADER effects/modulated-delay/modulated-delay-effect.h
  SOURCE ${_daisy_main}
  LINKER_SCRIPT ${_daisy_lds}
)

# Daisy firmware target (T032). Compiles the SAME core/effects/svf into a Daisy
# firmware via libDaisy — the only platform-specific code is daisy-main.cpp
# (audio callback + ADC). No JUCE, no ProcessorNode (Constitution IV; SC-007).
#
# Requires the ARM embedded toolchain WITH the C++ standard library (ARM's
# gcc-arm-embedded or the Daisy toolchain). A C-only arm-none-eabi (no libstdc++)
# cannot build the C++ core — that is surfaced as a hard error, never a fallback.

if(NOT DEFINED libDaisy_SOURCE_DIR)
  message(FATAL_ERROR
    "libDaisy not fetched. Configure with the `daisy` preset so cmake/dependencies.cmake "
    "pins and downloads libDaisy.")
endif()

# libDaisy ships its own CMake that defines the `daisy` static library + the
# STM32H750 build flags and linker script.
add_subdirectory("${libDaisy_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libDaisy" EXCLUDE_FROM_ALL)

add_executable(acfx_daisy daisy-main.cpp)
target_compile_features(acfx_daisy PRIVATE cxx_std_20)
target_link_libraries(acfx_daisy PRIVATE acfx_core daisy)

# Daisy bootloader linker script (STM32H750IB internal flash).
set(_daisy_lds "${libDaisy_SOURCE_DIR}/core/STM32H750IB_flash.lds")
target_link_options(acfx_daisy PRIVATE
  -T "${_daisy_lds}"
  -Wl,-Map=acfx_daisy.map,--cref
  -Wl,--gc-sections
)
set_target_properties(acfx_daisy PROPERTIES SUFFIX ".elf")

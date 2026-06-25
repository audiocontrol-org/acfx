# Teensy firmware target (T033). Compiles the SAME core/effects/svf into a Teensy
# 4.x firmware via the Teensy core + Audio Library — the only platform-specific
# code is teensy-main.cpp (AudioStream node + analog reads). No JUCE, no
# ProcessorNode (Constitution IV; SC-007).
#
# The C++ standard is the highest the installed Teensy toolchain supports (>=17,
# T034 / research.md decision 3); on C++17 the Effect concept degrades to a
# duck-typed template and the same SvfEffect compiles unchanged. Requires the
# Teensy ARM toolchain WITH the C++ standard library; a C-only arm-none-eabi is a
# hard error, never a fallback.

if(NOT DEFINED teensy_cores_SOURCE_DIR OR NOT DEFINED teensy_audio_SOURCE_DIR)
  message(FATAL_ERROR
    "Teensy cores/Audio not fetched. Configure with the `teensy` preset so "
    "cmake/dependencies.cmake pins and downloads them.")
endif()

set(_teensy4 "${teensy_cores_SOURCE_DIR}/teensy4")

# Teensy core (Arduino runtime for IMXRT1062) + Audio Library, built as the
# platform substrate the adapter links against.
file(GLOB _teensy_core_sources CONFIGURE_DEPENDS "${_teensy4}/*.c" "${_teensy4}/*.cpp")
file(GLOB _teensy_audio_sources CONFIGURE_DEPENDS "${teensy_audio_SOURCE_DIR}/*.cpp")

add_library(teensy_platform STATIC ${_teensy_core_sources} ${_teensy_audio_sources})
target_include_directories(teensy_platform PUBLIC "${_teensy4}" "${teensy_audio_SOURCE_DIR}")
target_compile_definitions(teensy_platform PUBLIC ARDUINO=10813 TEENSYDUINO=159 ARDUINO_TEENSY40)

add_executable(acfx_teensy teensy-main.cpp)
target_link_libraries(acfx_teensy PRIVATE acfx_core teensy_platform)

set(_teensy_lds "${_teensy4}/imxrt1062.ld")
target_link_options(acfx_teensy PRIVATE
  -T "${_teensy_lds}"
  -Wl,-Map=acfx_teensy.map,--cref
  -Wl,--gc-sections
)
set_target_properties(acfx_teensy PROPERTIES SUFFIX ".elf")

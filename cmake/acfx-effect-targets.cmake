# ============================================================================
# THE acfx COMMANDMENTS (repeated by design): commit & push early/often; ZERO git
# hooks ever; descriptive names, never numeric/ordinal prefixes (datestamps OK).
# ============================================================================
#
# Reusable desktop effect-target factory.
#
# Operator directive: "we are going to make many effects." Each effect that
# satisfies the acfx::Effect contract gets its own desktop targets (a JUCE
# standalone workbench + a VST3/AU/CLAP plugin) from a few lines of CMake — the
# SAME shared adapter sources are recompiled per effect with the concrete effect
# type/header injected via compile definitions:
#
#   ACFX_EFFECT_TYPE   -> a fully-qualified C++ type (e.g. acfx::SvfEffect),
#                         consumed as `using AppEffect = ACFX_EFFECT_TYPE;`.
#   ACFX_EFFECT_HEADER -> a quoted include path, consumed as
#                         `#include ACFX_EFFECT_HEADER`. The value must reach the
#                         preprocessor WITH its quotes, so the quotes are escaped
#                         in the compile definition below.
#
# Two functions, one per adapter, so each adapter subdirectory owns and compiles
# its own sources in its own scope (idiomatic CMake):
#
#   acfx_add_effect_workbench(NAME <target> EFFECT_TYPE <type> EFFECT_HEADER <path>
#                             PRODUCT <"Display Name"> SOURCES <src>...)
#   acfx_add_effect_plugin(NAME <target> EFFECT_TYPE <type> EFFECT_HEADER <path>
#                          PRODUCT <"Display Name"> PLUGIN_CODE <4char>
#                          CLAP_ID <reverse.dns> CLAP_FEATURES <feat>... SOURCES <src>...)

# Idempotent: a build that enables more than one adapter surface (e.g. desktop +
# daisy) includes this file from each branch; the guard defines the functions once.
include_guard(GLOBAL)

# Inject the effect type + header onto a target as compile definitions. The header
# path is double-quoted so `#include ACFX_EFFECT_HEADER` sees a quoted string; the
# type is passed bare so it expands to a C++ type.
function(_acfx_inject_effect target effect_type effect_header)
  target_compile_definitions(${target} PRIVATE
    "ACFX_EFFECT_TYPE=${effect_type}"
    "ACFX_EFFECT_HEADER=\"${effect_header}\""
  )
endfunction()

# Create a standalone JUCE workbench app for one effect.
function(acfx_add_effect_workbench)
  set(options "")
  set(oneValue NAME EFFECT_TYPE EFFECT_HEADER PRODUCT)
  set(multiValue SOURCES)
  cmake_parse_arguments(ARG "${options}" "${oneValue}" "${multiValue}" ${ARGN})

  juce_add_gui_app(${ARG_NAME}
    PRODUCT_NAME "${ARG_PRODUCT}"
    COMPANY_NAME "acfx"
    # Live input is a core workbench use (filter the live device input). On macOS an
    # app that opens an audio input MUST declare NSMicrophoneUsageDescription or the
    # OS silently denies/zeros the input (TCC). Enabling this adds that Info.plist
    # key so macOS prompts for — and can grant — audio-input access.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "${ARG_PRODUCT} needs audio-input access to filter your live input device."
  )

  target_sources(${ARG_NAME} PRIVATE ${ARG_SOURCES})
  target_compile_features(${ARG_NAME} PRIVATE cxx_std_20)

  target_compile_definitions(${ARG_NAME} PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    "JUCE_APPLICATION_NAME_STRING=\"${ARG_PRODUCT}\""
  )
  _acfx_inject_effect(${ARG_NAME} "${ARG_EFFECT_TYPE}" "${ARG_EFFECT_HEADER}")

  target_link_libraries(${ARG_NAME} PRIVATE
    acfx_core
    acfx_host
    juce::juce_audio_utils      # pulls audio_basics/devices/formats/processors + gui
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
  )
endfunction()

# Create a JUCE plugin (VST3 + AU-on-Apple + CLAP) for one effect.
function(acfx_add_effect_plugin)
  set(options "")
  set(oneValue NAME EFFECT_TYPE EFFECT_HEADER PRODUCT PLUGIN_CODE CLAP_ID)
  set(multiValue SOURCES CLAP_FEATURES)
  cmake_parse_arguments(ARG "${options}" "${oneValue}" "${multiValue}" ${ARGN})

  # AU is Apple-only; request it only on Apple so the desktop preset stays buildable
  # on Linux/Windows (VST3 + CLAP there, VST3 + AU + CLAP on macOS).
  set(_formats VST3)
  if(APPLE)
    list(APPEND _formats AU)
  endif()

  juce_add_plugin(${ARG_NAME}
    PRODUCT_NAME "${ARG_PRODUCT}"
    COMPANY_NAME "acfx"
    PLUGIN_MANUFACTURER_CODE Acfx
    PLUGIN_CODE ${ARG_PLUGIN_CODE}
    FORMATS ${_formats}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
  )

  target_sources(${ARG_NAME} PRIVATE ${ARG_SOURCES})
  target_compile_features(${ARG_NAME} PRIVATE cxx_std_20)

  target_compile_definitions(${ARG_NAME} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
  )
  _acfx_inject_effect(${ARG_NAME} "${ARG_EFFECT_TYPE}" "${ARG_EFFECT_HEADER}")

  target_link_libraries(${ARG_NAME} PRIVATE
    acfx_core
    acfx_host
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
  )

  # Add the CLAP format to the same target (research.md decision 7).
  clap_juce_extensions_plugin(TARGET ${ARG_NAME}
    CLAP_ID "${ARG_CLAP_ID}"
    CLAP_FEATURES ${ARG_CLAP_FEATURES}
  )
endfunction()

# ============================================================================
# Embedded effect-target factory (U7b).
#
# Same per-effect injection pattern as the desktop functions above, applied to
# the MCU adapters: each effect that satisfies acfx::Effect gets its own firmware
# executable from the SAME shared adapter source (daisy-main.cpp / teensy-main.cpp)
# recompiled with the concrete effect type/header injected via ACFX_EFFECT_TYPE /
# ACFX_EFFECT_HEADER.
#
# One-time platform setup (libDaisy add_subdirectory, the teensy_platform static
# lib, fetch guards) stays in the adapter CMakeLists and runs ONCE; these factory
# functions add ONLY the per-effect executable + its link/linker-script glue.
#
#   acfx_add_effect_daisy(NAME <target> EFFECT_TYPE <type> EFFECT_HEADER <path>
#                         SOURCE <daisy-main.cpp> LINKER_SCRIPT <lds>)
#   acfx_add_effect_teensy(NAME <target> EFFECT_TYPE <type> EFFECT_HEADER <path>
#                          SOURCE <teensy-main.cpp> LINKER_SCRIPT <lds>)
# ============================================================================

# Create a Daisy (STM32H750) firmware executable for one effect. Assumes the
# `daisy` static library already exists (added once by the adapter CMakeLists).
function(acfx_add_effect_daisy)
  set(options "")
  set(oneValue NAME EFFECT_TYPE EFFECT_HEADER SOURCE LINKER_SCRIPT)
  set(multiValue "")
  cmake_parse_arguments(ARG "${options}" "${oneValue}" "${multiValue}" ${ARGN})

  add_executable(${ARG_NAME} ${ARG_SOURCE})
  target_compile_features(${ARG_NAME} PRIVATE cxx_std_20)
  target_link_libraries(${ARG_NAME} PRIVATE acfx_core daisy)
  _acfx_inject_effect(${ARG_NAME} "${ARG_EFFECT_TYPE}" "${ARG_EFFECT_HEADER}")

  target_link_options(${ARG_NAME} PRIVATE
    -T "${ARG_LINKER_SCRIPT}"
    -Wl,-Map=${ARG_NAME}.map,--cref
    -Wl,--gc-sections
  )
  set_target_properties(${ARG_NAME} PROPERTIES SUFFIX ".elf")
endfunction()

# Create a Teensy 4.x (IMXRT1062) firmware executable for one effect. Assumes the
# `teensy_platform` static library already exists (added once by the adapter
# CMakeLists). C++ standard is left at the highest the Teensy toolchain provides;
# on C++17 the Effect concept degrades to a duck-typed template unchanged.
function(acfx_add_effect_teensy)
  set(options "")
  set(oneValue NAME EFFECT_TYPE EFFECT_HEADER SOURCE LINKER_SCRIPT)
  set(multiValue "")
  cmake_parse_arguments(ARG "${options}" "${oneValue}" "${multiValue}" ${ARGN})

  add_executable(${ARG_NAME} ${ARG_SOURCE})
  target_link_libraries(${ARG_NAME} PRIVATE acfx_core teensy_platform)
  _acfx_inject_effect(${ARG_NAME} "${ARG_EFFECT_TYPE}" "${ARG_EFFECT_HEADER}")

  target_link_options(${ARG_NAME} PRIVATE
    -T "${ARG_LINKER_SCRIPT}"
    -Wl,-Map=${ARG_NAME}.map,--cref
    -Wl,--gc-sections
  )
  set_target_properties(${ARG_NAME} PROPERTIES SUFFIX ".elf")
endfunction()

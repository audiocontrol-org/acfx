# Nucleo (STM32F446RE) firmware targets. Compiles the SAME core effects into
# NUCLEO-F446RE firmware — the only platform-specific code is nucleo-main.cpp,
# recompiled per effect with the concrete effect type/header injected by the
# acfx_add_effect_nucleo factory (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER). No JUCE,
# no ProcessorNode (Constitution IV; SC-007). Add a new effect's firmware with one
# more factory call below.
#
# Unlike libDaisy/teensy_platform, there is no single upstream "board support"
# CMake target to add_subdirectory: this adapter assembles its own platform
# substrate from the pinned CPM packages (cmsis_device_f4 device headers,
# CMSIS_5 core headers) plus the in-tree C-runtime startup + vector table. That
# substrate is built ONCE below as acfx_nucleo_platform and linked into each
# firmware target via LINK_LIBRARIES, alongside acfx::nucleo_support (the
# platform-independent sample-format/ring-buffer/parameter/MIDI-CC half of the
# adapter, declared unconditionally at the top level — see root CMakeLists.txt).
#
# TinyUSB is partially wired in: acfx_nucleo_usb below compiles the descriptor
# set (usb-descriptors.cpp) against TinyUSB's HEADERS. TinyUSB's own .c sources
# are still NOT compiled — the task that calls tusb_init(), installs the
# OTG_FS ISR, and services tud_task() owns that, and pulling the stack's
# sources into the link before anything calls them would add compile surface
# nothing references.

if(NOT DEFINED cmsis_device_f4_SOURCE_DIR OR NOT DEFINED CMSIS_5_SOURCE_DIR)
  message(FATAL_ERROR
    "cmsis_device_f4/CMSIS_5 not fetched. Configure with the `nucleo` preset so "
    "cmake/dependencies.cmake pins and downloads them.")
endif()
if(NOT DEFINED TinyUSB_SOURCE_DIR)
  message(FATAL_ERROR
    "TinyUSB not fetched. Configure with the `nucleo` preset so "
    "cmake/dependencies.cmake pins and downloads it.")
endif()

# One-time platform setup: the C-runtime startup (Reset_Handler) + the interrupt
# vector table, built ONCE as the platform substrate every firmware target links
# against. Both need the CMSIS device-selection macro and the two CPM-pinned
# CMSIS include directories (device headers + core headers) — verified empirically
# required for the vector table (its stm32f446xx.h chain) to compile at all. Use
# the CPM-provided *_SOURCE_DIR variables rather than any hardcoded path under
# external/.cpm-cache — those cache paths carry content hashes and are not stable.
#
# MUST be an OBJECT library, not STATIC. A STATIC archive member is only
# extracted by the linker when something has an UNDEFINED reference to a symbol
# it defines. Nothing in nucleo-main.cpp (or anywhere else) references
# g_vectorTable, so vector-table.o was silently dropped from the link — the
# firmware built and linked "successfully" with a zero-size .isr_vector and no
# vector table at 0x08000000, i.e. a board that cannot boot. KEEP(.isr_vector)
# in the linker script cannot fix this: KEEP only protects a section from
# --gc-sections AFTER it has been pulled into the link; it has no power over
# archive-member extraction in the first place. An OBJECT library's objects are
# added directly to each consuming executable via target_link_libraries (CMake
# 3.12+ propagates both the objects and the usage requirements below through the
# alias), so vector-table.o is always present — archive-extraction semantics
# never apply. Do not revert this to STATIC.
add_library(acfx_nucleo_platform OBJECT
  startup/startup.cpp
  startup/vector-table.cpp
)
target_compile_definitions(acfx_nucleo_platform PUBLIC STM32F446xx)
target_include_directories(acfx_nucleo_platform PUBLIC
  "${cmsis_device_f4_SOURCE_DIR}/Include"
  "${CMSIS_5_SOURCE_DIR}/CMSIS/Core/Include"
)
target_compile_features(acfx_nucleo_platform PRIVATE cxx_std_20)
add_library(acfx::nucleo_platform ALIAS acfx_nucleo_platform)

# USB descriptor set — the composite device's device/configuration/string
# descriptors, plus the UAC2 clock-source control replies that carry the half
# of the advertised format (the 48 kHz) which UAC2 keeps out of the descriptor
# bytes entirely. Two sources, one subject. Built ONCE and linked
# into every firmware image, because FR-018b requires every effect firmware to
# present the SAME device shape (audio + MIDI + CDC telemetry); a per-target
# copy would be a way for two images to drift apart.
#
# MUST be an OBJECT library, for the same reason acfx_nucleo_platform above is
# and then some. Nothing in the firmware REFERENCES these callbacks yet — the
# only caller is TinyUSB's usbd.c, which is not compiled into the link until
# the task that initialises the stack lands. A STATIC archive member with no
# undefined reference pulling it in is silently dropped by the linker, so the
# descriptors would vanish from the image without a word. An OBJECT library's
# objects are added directly to each executable, so they are always present.
#
# (--gc-sections will still discard the individual unreferenced functions and
# arrays from the final image until the stack is linked in. That is fine and
# expected: the point of compiling them now is that every descriptor
# static_assert in usb-descriptors.h/.cpp is evaluated on every build.)
add_library(acfx_nucleo_usb OBJECT
  usb-descriptors.cpp
  usb-audio-controls.cpp
)
target_compile_features(acfx_nucleo_usb PRIVATE cxx_std_20)
target_compile_definitions(acfx_nucleo_usb PRIVATE STM32F446xx)
target_include_directories(acfx_nucleo_usb PRIVATE
  # tusb_config.h lives here, alongside the descriptors it must agree with;
  # TinyUSB's headers #include it by bare name. NOT a SYSTEM include — we do
  # want warnings from our own sources.
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
# TinyUSB's own headers, as SYSTEM (-isystem) so their warnings do not become
# ours. Compiled as C++20, src/osal/osal_none.h trips -Wvolatile on its
# `sem_hdl->count++` (deprecated compound access to a volatile in C++20). That
# is upstream's code at a pinned revision, not something this adapter can fix
# or should carry noise for — and a warning we cannot act on is a warning that
# trains us to ignore the ones we can. Note -isystem only demotes warnings; it
# changes nothing about what is compiled.
target_include_directories(acfx_nucleo_usb SYSTEM PRIVATE
  "${TinyUSB_SOURCE_DIR}/src"
  # UID_BASE, for the serial-number string.
  "${cmsis_device_f4_SOURCE_DIR}/Include"
  "${CMSIS_5_SOURCE_DIR}/CMSIS/Core/Include"
)
# support/sample-format.h — kMaxPacketFrames and kChannels, so the advertised
# wMaxPacketSize is asserted against the conversion path's own constants
# rather than restated. INTERFACE library: no objects, nothing to duplicate.
target_link_libraries(acfx_nucleo_usb PRIVATE acfx::nucleo_support)
add_library(acfx::nucleo_usb ALIAS acfx_nucleo_usb)

# Nucleo linker script (STM32F446RE internal flash), shared by all firmwares.
set(_nucleo_lds "${CMAKE_CURRENT_SOURCE_DIR}/startup/nucleo-f446.ld")
set(_nucleo_main "${CMAKE_CURRENT_SOURCE_DIR}/nucleo-main.cpp")

# SVF — mirrors the Daisy/Teensy first-effect target.
acfx_add_effect_nucleo(
  NAME acfx_nucleo
  EFFECT_TYPE acfx::SvfEffect
  EFFECT_HEADER effects/svf/svf-effect.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
)

# Modulated delay — firmware for the second effect.
acfx_add_effect_nucleo(
  NAME acfx_nucleo_delay
  EFFECT_TYPE acfx::ModulatedDelayEffect
  EFFECT_HEADER effects/modulated-delay/modulated-delay-effect.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
)

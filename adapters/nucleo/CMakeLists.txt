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
# TinyUSB is now fully wired in (T030): acfx_nucleo_usb compiles the descriptor
# set (usb-descriptors.cpp) against TinyUSB's HEADERS, and acfx_nucleo_tinyusb
# below compiles the STACK'S OWN .c sources — core, the device-class drivers
# this composite actually enables, and the Synopsys DWC2 device-controller
# driver R14 established this part uses. nucleo-main.cpp calls tusb_init(),
# installs the OTG_FS ISR, and services tud_task().

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
  # T053-T055 (US10): bus suspend/resume/mount lifecycle callbacks
  # (tud_suspend_cb/tud_resume_cb/tud_mount_cb). Same weak-callback linkage
  # concern as usb-audio-controls.cpp above, and the same reason it lives in
  # its own translation unit rather than nucleo-main.cpp — see this file's
  # own header comment.
  usb-lifecycle-callbacks.cpp
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

# TinyUSB's OWN sources (T030). Nothing in the tree before this task compiled a
# single TinyUSB .c file — tusb_config.h (T026) and usb-descriptors.* (T027)
# were compiled in isolation against the stack's HEADERS only. This is the
# library that actually puts the stack in the image.
#
# THE FILE LIST BELOW WAS READ OFF THE PINNED TREE, NOT ASSUMED. Two upstream
# CMake helpers exist and were considered first:
#   - src/CMakeLists.txt's tinyusb_sources_get()/tinyusb_target_add(): pulls in
#     EVERY class driver (HID, MSC, DFU, MTP, net, printer, USBTMC, vendor,
#     video, MIDI2 — none of which this composite enables) AND the entire host
#     stack (usbh.c, hub.c, four *_host.c files) AND typec/usbc.c. Every one of
#     those is compiled out to an empty translation unit by its own `#if
#     CFG_TUD_x`/`CFG_TUH_ENABLED` guard (checked line-by-line against this
#     pinned tree), so using it would not be WRONG, but it would compile ~20
#     files' worth of dead guard-checks into every firmware image for no
#     benefit — the opposite of this repo's "no compile surface nothing
#     references" policy stated above. Rejected for that reason.
#   - hw/bsp/stm32f4/family.cmake's family_configure_example(): the actual
#     per-family reference for which DWC2 portable files an STM32F4 build
#     needs — dcd_dwc2.c, hcd_dwc2.c, dwc2_common.c. hcd_dwc2.c is the HOST
#     controller driver; this device-only composite (CFG_TUH_ENABLED unset,
#     defaulting to 0) never needs it, so it is deliberately NOT added below —
#     the one intentional deviation from that file's list.
# So: an explicit list, hand-picked from BOTH upstream helpers' contents,
# trimmed to exactly what CFG_TUD_AUDIO/CFG_TUD_MIDI/CFG_TUD_CDC (tusb_config.h)
# actually enable. Note this list does NOT include a device/usbd_control.c —
# that file does not exist in the pinned 0.21.0 tree; usbd.c owns control-
# transfer handling itself in this version (confirmed by reading usbd.c, which
# defines usbd_control_xfer_cb() inline).
add_library(acfx_nucleo_tinyusb OBJECT
  # --- Core (src/CMakeLists.txt's tinyusb_sources_get(), "common"/"device" groups) ---
  "${TinyUSB_SOURCE_DIR}/src/tusb.c"
  "${TinyUSB_SOURCE_DIR}/src/common/tusb_fifo.c"
  "${TinyUSB_SOURCE_DIR}/src/device/usbd.c"
  # --- The three class drivers CFG_TUD_AUDIO/CFG_TUD_MIDI/CFG_TUD_CDC enable
  # (tusb_config.h) — everything else in tinyusb_sources_get()'s class/ list is
  # left out because this composite's tusb_config.h leaves it at 0. ---
  "${TinyUSB_SOURCE_DIR}/src/class/audio/audio_device.c"
  "${TinyUSB_SOURCE_DIR}/src/class/midi/midi_device.c"
  "${TinyUSB_SOURCE_DIR}/src/class/cdc/cdc_device.c"
  # --- Device-controller driver: Synopsys DWC2, device side only (R14) ---
  "${TinyUSB_SOURCE_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c"
  "${TinyUSB_SOURCE_DIR}/src/portable/synopsys/dwc2/dwc2_common.c"
)
target_compile_definitions(acfx_nucleo_tinyusb PRIVATE STM32F446xx)
# CFG_TUD_MIDI_RX_BUFSIZE / CFG_TUD_MIDI_TX_BUFSIZE — GENUINELY MISSING from
# tusb_config.h (T026), discovered by this task's first real compile of
# TinyUSB's midi_device.c against it (previously that file was never
# compiled at all — see the top-of-file banner comment). Unlike
# CFG_TUD_MIDI_RX_EPSIZE/TX_EPSIZE (which midi_device.h falls back to
# TUD_EPSIZE_BULK_MAX for when unset — midi_device.h:37-49), midi_device.c
# uses CFG_TUD_MIDI_RX_BUFSIZE/TX_BUFSIZE directly with NO #ifndef fallback
# anywhere in the pinned tree (midi_device.c:65-66): an undefined-macro
# compile error, not a silent default. tusb_config.h defines the matching
# BUFSIZE/EPSIZE pair for every OTHER class it configures (CDC's
# CFG_TUD_CDC_RX_BUFSIZE/TX_BUFSIZE sit right next to its EPSIZE macros) but
# omits MIDI's BUFSIZE pair entirely — an omission, not a decision.
#
# THIS TASK DOES NOT EDIT tusb_config.h (out of T030's scope per its brief).
# Reported instead as a finding for tusb_config.h's owner to fold in
# properly. Set here, PRIVATE to this OBJECT library only (nothing else
# needs it), to the same value as the already-decided
# CFG_TUD_MIDI_RX_EPSIZE/TX_EPSIZE (64, the full-speed bulk max both
# explicitly state) — the value these macros would already resolve to had
# tusb_config.h defined them the same way it defines CDC's pair.
target_compile_definitions(acfx_nucleo_tinyusb PRIVATE
  CFG_TUD_MIDI_RX_BUFSIZE=64
  CFG_TUD_MIDI_TX_BUFSIZE=64
)
# tusb_config.h's directory, PUBLIC (unlike acfx_nucleo_usb's PRIVATE copy of
# this same include): both this library's own C sources AND nucleo-main.cpp
# (a source of the executable target, not of this OBJECT library — it never
# gets acfx_nucleo_usb's PRIVATE include at all) `#include "tusb.h"`, which
# `#include`s "tusb_config.h" by bare name. PUBLIC on an OBJECT library
# propagates as a normal (non-SYSTEM) usage requirement to whatever links this
# library, which is exactly what nucleo-main.cpp needs to resolve that include.
target_include_directories(acfx_nucleo_tinyusb PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
# TinyUSB's own headers + the CMSIS headers its DWC2/STM32 port needs
# (dwc2_stm32.h does `#include "stm32f4xx.h"`, the CMSIS umbrella that
# dispatches to stm32f446xx.h — confirmed NOT to require ST's HAL: reading
# cmsis_device_f4's stm32f4xx.h shows the HAL include is gated on
# USE_HAL_DRIVER, which nothing here defines). SYSTEM + PUBLIC for the same
# -Wvolatile reason as acfx_nucleo_usb above, and PUBLIC so nucleo-main.cpp's
# own `#include "tusb.h"` also gets -isystem treatment, not just this
# library's own sources.
target_include_directories(acfx_nucleo_tinyusb SYSTEM PUBLIC
  "${TinyUSB_SOURCE_DIR}/src"
  "${cmsis_device_f4_SOURCE_DIR}/Include"
  "${CMSIS_5_SOURCE_DIR}/CMSIS/Core/Include"
)
add_library(acfx::nucleo_tinyusb ALIAS acfx_nucleo_tinyusb)

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
                 acfx::nucleo_tinyusb
)

# Modulated delay: the old acfx_nucleo_delay target (acfx::ModulatedDelayEffect<>,
# the untemplated-default 96000-sample/float/8-channel instantiation, ~3 MB)
# NEVER booted on hardware — it cannot link into the F446RE's 128 KB SRAM
# (backlog TASK-34 lists dropping this target as one option) — and broke
# `cmake --build --preset nucleo` outright once this file's static_assert-free
# link was actually exercised. Removed entirely; acfx_nucleo_lofi_delay below
# (SRAM-bounded: 14400 samples/ch, int16, stereo) is its working replacement.
#
# Bounded lo-fi delay — firmware for the SRAM-bounded (14400 samples/ch,
# int16, stereo) ModulatedDelayEffect instantiation (nucleo-modulated-delay.h,
# T4-T5's templated core). EFFECT_HEADER is a bare filename, not a
# core/-relative path like acfx_nucleo's EFFECT_HEADER above:
# nucleo-modulated-delay.h lives in THIS directory, and effect-instance.h's
# `#include ACFX_EFFECT_HEADER` is a quoted include, resolved relative to
# effect-instance.h's own directory (also this one) before any -I search
# path — no extra include dir needed, same as effect-instance.h itself.
acfx_add_effect_nucleo(
  NAME acfx_nucleo_lofi_delay
  EFFECT_TYPE acfx::NucleoModulatedDelay
  EFFECT_HEADER nucleo-modulated-delay.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
                 acfx::nucleo_tinyusb
)

# SPIKE / THROWAWAY — reverb feasibility probe (core/effects/spike-reverb).
# Decimated-float Freeverb; validates SRAM fit + on-device operation. Replace
# with a properly-specced effect if the probe passes; do not build on it.
acfx_add_effect_nucleo(
  NAME acfx_nucleo_spike_reverb
  EFFECT_TYPE acfx::SpikeReverb
  EFFECT_HEADER effects/spike-reverb/spike-reverb.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
                 acfx::nucleo_tinyusb
)

# SPIKE / THROWAWAY — reverse-reverb feasibility probe
# (core/effects/spike-reverse-reverb). Decimated (D=6, 8 kHz) double-buffered
# int16 capture window + mono 8 kHz Freeverb tank; validates the capture-window
# memory technique. Replace with a specced effect if the probe passes.
acfx_add_effect_nucleo(
  NAME acfx_nucleo_spike_reverse_reverb
  EFFECT_TYPE acfx::SpikeReverseReverb
  EFFECT_HEADER effects/spike-reverse-reverb/spike-reverse-reverb.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
                 acfx::nucleo_tinyusb
)

# SPIKE / THROWAWAY — "Breathing Canyon" (Eventide H3000 Swept Reverb-inspired:
# randomly-swept multi-delay cathedral + pitch-shifted shimmer feedback).
acfx_add_effect_nucleo(
  NAME acfx_nucleo_spike_breathing_canyon
  EFFECT_TYPE acfx::SpikeBreathingCanyon
  EFFECT_HEADER effects/spike-breathing-canyon/spike-breathing-canyon.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
                 acfx::nucleo_tinyusb
)

#pragma once

// TinyUSB configuration for the NUCLEO-F446RE composite USB device: UAC2
// stereo audio (48 kHz / 16-bit, one IN + one OUT streaming interface),
// USB MIDI, and a CDC serial telemetry function, all IAD-grouped into one
// composite descriptor (FR-018/FR-018a/FR-018b, D5).
//
// PROVENANCE: every macro name and default value referenced in the comments
// below was read directly from the pinned TinyUSB 0.21.0 tree at
// external/.cpm-cache/tinyusb/d34550b3aaa115e7ec09bea0c9e676531bf95dfb and is
// recorded, with file:line citations, in specs/nucleo-f446-adapter/
// research.md section R13 (task T028). Nothing here is from memory or from
// a different TinyUSB version -- where this file disagrees with an older
// research note (R1), R13 is the one that is current.
//
// THIS IS A PLAIN C HEADER. TinyUSB's own class-driver .c sources
// (src/device/usbd.c, src/class/audio/audio_device.c, etc.) #include this
// file directly, and nucleo-main.cpp (C++) will too once T030 wires the
// stack in. It therefore contains nothing but preprocessor macros -- no
// namespaces, no constexpr, no C++-only syntax -- and needs no extern "C"
// wrapper, because a #define has no linkage for extern "C" to affect.
//
// SCOPE BOUNDARY: configuration macros only.
//   - Does NOT declare USB descriptors (device/config/string descriptors,
//     the TUD_AUDIO20_DESC_* assembly) -- that is T027.
//   - Does NOT call tud_init()/tusb_init(), does NOT write the OTG_FS ISR,
//     does NOT write the main-loop service/poll calls -- that is T030.
//   - Does NOT touch PA11/PA12 pin muxing (adapters/nucleo/otg-fs-gpio-
//     init.h, T025) or the OTG_FS peripheral clock enable.
//
// BUILD STATUS, STATED PLAINLY: as of this file's authorship, nothing in
// adapters/nucleo/CMakeLists.txt compiles any TinyUSB source yet (see that
// file's own header comment: "TinyUSB is intentionally NOT wired in yet").
// So this header is NOT included by the real firmware build and has NOT
// been exercised by it -- T030 is what will make that true. It has been
// checked for self-consistency with a standalone preprocessor run against
// the pinned TinyUSB tree (see the task's verification notes); that is
// evidence the macros parse and satisfy the driver's own #error checks, NOT
// evidence that the resulting firmware links, enumerates, or works.

// ---------------------------------------------------------------------------
// Board / port configuration
// ---------------------------------------------------------------------------

// Every MCU family in tusb_option.h's CFG_TUSB_MCU switch is opted in
// individually; there is no default (unlike CFG_TUSB_OS below). The value
// must name the exact silicon: OPT_MCU_STM32F4 is TinyUSB's designation for
// the whole STM32F4 family, which is what the F446's OTG_FS core belongs to.
#define CFG_TUSB_MCU OPT_MCU_STM32F4

// No RTOS. The audio/MIDI/CDC data paths are all serviced by polling from
// the bare-metal main loop (FR-023), consistent with this adapter's single
// execution context (D26) -- there is no separate TinyUSB task to hand
// events to.
#define CFG_TUSB_OS OPT_OS_NONE

// Root-hub port 0, DEVICE mode, FULL SPEED -- set as ONE combined macro
// rather than as separate CFG_TUD_ENABLED / CFG_TUD_MAX_SPEED values. This
// is TinyUSB's own documented "RootHub Mode detection" mechanism
// (src/tusb_option.h:422-446): CFG_TUD_ENABLED and CFG_TUD_MAX_SPEED both
// fall back to being DERIVED from CFG_TUSB_RHPORTn_MODE when not set
// directly (src/tusb_option.h:438-446). Using the single documented knob
// means there is exactly one place in this file that says "device, full
// speed" instead of two or three that would have to be kept in agreement by
// hand. FULL_SPEED is not a tuning choice: the F446's OTG_FS instance has
// only an on-chip full-speed PHY, so full speed is the only speed this port
// can ever electrically run at.
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Control endpoint 0's max packet size. 64 bytes is both TinyUSB's own
// driver default (src/tusb_option.h:587-589) and the maximum a full-speed
// control endpoint is permitted to advertise per the USB 2.0 spec, so this
// value could be omitted safely -- it is written explicitly anyway, because
// enumeration hinges on it and this file's policy (driven by the VBUS trap
// below) is to never let anything that gates enumeration ride on a silent
// default.
#define CFG_TUD_ENDPOINT0_SIZE 64

// USB transfer-memory placement. Some MCUs (e.g. RP2040) restrict DMA to a
// dedicated SRAM region and need CFG_TUSB_MEM_SECTION to place buffers
// there; the F446's OTG_FS core has no such restriction, so the section
// stays empty. 4-byte alignment is still required by the FIFO/endpoint
// structures TinyUSB places in these buffers. Both are set explicitly here
// -- matching the library's own defaults at src/tusb_option.h:518-525 -- so
// the choice is a recorded decision rather than an inherited default.
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

// ---------------------------------------------------------------------------
// VBUS detection -- the headline requirement of this file (FR-022, D17)
// ---------------------------------------------------------------------------
//
// THIS BOARD'S USB-C BREAKOUT HAS ITS VBUS PIN DELIBERATELY LEFT UNWIRED.
// The Nucleo is powered and clocked over its ST-Link USB cable; the second,
// USB-C breakout cable carries only D+/D- (PA12/PA11) to the host running
// the audio/MIDI/CDC session. Feeding the breakout's VBUS into the board's
// 5 V rail would put two independent 5 V supplies -- the ST-Link's and the
// breakout host's -- into direct contention on the same rail, which is a
// hardware hazard, not a software one. This wiring was confirmed on the
// physical rig by the operator; it is not an oversight this file should
// "fix" by wiring VBUS up.
//
// CONSEQUENCE FOR THE USB CORE: the OTG_FS peripheral's session-valid logic
// normally gates enumeration on seeing VBUS actually present on its sense
// pin (PA9). With PA9 left unwired (floating or grounded, never driven to
// 5 V), that gate NEVER opens on its own: the device stack sits forever
// waiting for a "VBUS valid" event that will never arrive, and the board
// simply never enumerates -- no error, no log, nothing to grep for. This is
// almost certainly the FIRST thing to suspect if this device appears
// completely inert on the USB-C side despite the ST-Link cable being
// connected and the clock (FR-014/FR-015) having come up.
//
// CFG_TUD_VBUS_DETECT_HW controls exactly this: it selects the
// hardware-vbus_sensing field TinyUSB passes into its DWC2 (Synopsys
// OTG_FS/OTG_HS core) configuration -- see CFG_TUD_CONFIGURE_DWC2_DEFAULT at
// src/device/usbd.h:48, which sets `.vbus_sensing = CFG_TUD_VBUS_DETECT_HW`
// directly. Setting it to 0 tells the DWC2 driver this board does NOT wire
// hardware VBUS sensing, which makes the driver treat the USB session as
// valid unconditionally instead of waiting on the sense pin. Note this is
// also the library's own shipped DEFAULT (CFG_TUD_VBUS_DETECT_HW_DEFAULT is
// 0 at src/tusb_option.h:618-619) -- it is written out explicitly here
// anyway, because "0 happens to be the default" is exactly the kind of fact
// that looks safe to omit and is the single most likely thing a future
// reader will suspect first when the board fails to enumerate. Leaving the
// reasoning implicit would send them straight back to this same
// investigation.
#define CFG_TUD_VBUS_DETECT_HW 0

// ---------------------------------------------------------------------------
// Composite device class enables (FR-018/FR-018a/FR-018b, D5)
// ---------------------------------------------------------------------------

// UAC2 audio: one function, both directions (sized below).
#define CFG_TUD_AUDIO 1

// USB MIDI: CC input for parameter control (FR-045 downstream).
#define CFG_TUD_MIDI 1

// CDC serial: transport telemetry / diagnostics, required on EVERY effect
// firmware, not just instrumented builds (FR-018a/FR-018b).
#define CFG_TUD_CDC 1

// Explicitly OFF. Written out (rather than left unset, where they would
// default to 0 anyway) so the composite device's full class shape is
// visible in one place instead of implied by omission.
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_VENDOR 0

// ---------------------------------------------------------------------------
// UAC2 audio class configuration
// (FR-020/FR-021/FR-023 through FR-029, D20/D21/D22)
// ---------------------------------------------------------------------------

// Both directions: stereo IN (device -> host, "microphone" role) and stereo
// OUT (host -> device, "speaker" role) on ONE audio function (func_id 0).
// R13.2 confirmed the driver distinguishes the two directions by decoding
// each SET_INTERFACE request's own itf/alt fields, not by any macro here --
// so CFG_TUD_AUDIO stays at 1 (one function) and the `_n_` multi-instance
// entry points are not needed for this duplex-in-one-function shape.
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1   // stereo IN / microphone role
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 1  // stereo OUT / speaker role

// --- Endpoint size arithmetic (D21/FR-028) ---------------------------------
//
// The stream is 48 kHz / stereo (FR-020), full speed, so USB delivers one
// isochronous packet per 1 ms SOF frame. Two PCM sample formats are advertised
// per direction (T015/US3): alt 1 is 16-bit (2-byte subslot), alt 2 is
// packed-24-bit (3-byte subslot); the format is selected per-alt at run time,
// so the DRIVER'S single endpoint FIFO/software buffer is sized for the LARGER
// (24-bit) one. A host is allowed to vary how many stereo frames it puts in a
// packet -- D21/FR-028 pin the accepted range at 0 to 49 stereo frames
// inclusive, NOT a bare 48, because a full-speed host may legitimately deliver
// one extra frame in a given 1 ms packet to stay caught up with its own clock.
// That 49-frame bound is defined once, non-negotiably, as kMaxPacketFrames in
// adapters/nucleo/support/sample-format.h -- the constants below exist only
// because this file is plain C and cannot #include that C++ header, so the
// same three numbers (49 frames, 2 channels, 3 bytes/sample worst case) are
// restated here and must be kept in agreement with it by hand.
//
//   worst-case packet = kMaxPacketFrames * kChannels * max subslot bytes
//                     =        49         *    2      *      3        = 294 bytes
//
// 294 bytes is comfortably inside the full-speed isochronous ceiling of
// 1023 bytes/packet, and is also exactly what TinyUSB's own sizing helper
// TUD_AUDIO_EP_SIZE(is_highspeed=false, 48000, 3, 2) computes
// (((48000+999)/1000)+1)*3*2 = (48+1)*6 = 294 (src/device/usbd.h:809) -- the
// two derivations agreeing is a useful cross-check, not a coincidence: both
// are counting "worst case whole frames deliverable in one 1 ms interval".
// (The 16-bit alt's own packet is 49*2*2 = 196 B, which this envelope covers.)
#define ACFX_USB_AUDIO_MAX_PACKET_FRAMES 49  // D21/FR-028; MUST match kMaxPacketFrames
#define ACFX_USB_AUDIO_CHANNELS 2            // FR-020 stereo only; MUST match kChannels

// WORST-CASE SUBSLOT across the advertised alt settings (T015/US3, FR-005/
// FR-010). Two PCM formats are advertised per streaming interface: alt 1 is
// 16-bit (bSubslotSize=2) and alt 2 is packed-24-bit (bSubslotSize=3). The
// DRIVER'S endpoint FIFO and software buffer are allocated ONCE, from these
// sizing macros, and must hold the LARGER format's packet, so the sizing uses
// the maximum subslot (3). This is deliberately NOT a per-alt value: the
// per-alt Type-I FORMAT descriptors in usb-descriptors.cpp each declare their
// OWN bSubslotSize (2 or 3); THIS number is only the buffer envelope that
// covers both. It matches kMaxSubslotBytes in usb-descriptors.h by hand, the
// same way the frame/channel counts above mirror the C++ header.
#define ACFX_USB_AUDIO_MAX_SUBSLOT_BYTES 3   // FR-010 packed-24 is the larger format

// Hard-required by the driver: omitting either is a #error at
// src/class/audio/audio_device.h:57-59 / :73-75.
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX                                    \
  (ACFX_USB_AUDIO_MAX_PACKET_FRAMES * ACFX_USB_AUDIO_CHANNELS *              \
   ACFX_USB_AUDIO_MAX_SUBSLOT_BYTES)  // 49 * 2 * 3 = 294 bytes (24-bit worst case)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX                                   \
  (ACFX_USB_AUDIO_MAX_PACKET_FRAMES * ACFX_USB_AUDIO_CHANNELS *              \
   ACFX_USB_AUDIO_MAX_SUBSLOT_BYTES)  // 49 * 2 * 3 = 294 bytes (24-bit worst case)

// --- Software FIFO depth (also hard-required; defaults to 0, which always
// fails the driver's own `SW_BUF_SZ >= SZ_MAX` check at
// audio_device.h:110-112 / :128-130) -----------------------------------
//
// This buffer is TinyUSB's internal byte FIFO sitting between the USB
// packet transfer and our polled tud_audio_read()/tud_audio_write() calls
// (T030) -- it is NOT the application-level ring buffer from research.md R5
// (FR-030/FR-030a); it only has to survive the gap between successive main-
// loop polls, not decouple USB cadence from DSP block cadence. 4x the
// worst-case packet size is the multiple the closest shipped duplex example
// (examples/device/uac2_headset/src/tusb_config.h:148,165) uses for its
// full-speed case, and is adopted here as the SAME KIND OF STARTING POINT
// research.md R5 calls for elsewhere in this feature: generous enough not
// to be the first thing that clips under real traffic, explicitly NOT
// claimed to be a measured, final number. If HIL testing (R5's measurement
// procedure) shows this FIFO clipping independently of the R5 ring buffer's
// own counters, widen this multiple then, with the observation that
// justified it.
#define ACFX_USB_AUDIO_SW_BUF_MULTIPLE 4
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ                                 \
  (ACFX_USB_AUDIO_SW_BUF_MULTIPLE * CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX)  // 4*294=1176 (>= 4*Navg 1152 @48k/24-bit)
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ                                \
  (ACFX_USB_AUDIO_SW_BUF_MULTIPLE * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX)  // 4*294=1176

// --- IN flow-control FIFO adequacy (R5/R7; audio_device.c's flow-control
// packet-size loop, audiod_tx_packet_size()/audiod_calc_tx_packet_sz()) ---
//
// With CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1 (below), the driver's own comment
// at audio_device.c:1864-1865 states the requirement directly: "Flow
// control need[s] a FIFO size of at least 4*Navg", where Navg is the
// AVERAGE per-SOF byte count (`packet_sz_tx_norm` = sample_rate/1000 *
// channels * bytes/sample, audio_device.c:1826-1861) the loop interleaves
// packet sizes around to hold the exact fractional-rate average (e.g.
// 44/45-byte interleave at 44.1 kHz).
//
// This adapter has no separate "average sample count" macro -- adding one
// would be a second sample-rate-derived constant to keep in agreement with
// ACFX_USB_AUDIO_MAX_PACKET_FRAMES by hand, the exact trap this file's
// policy avoids elsewhere. ACFX_USB_AUDIO_MAX_PACKET_FRAMES (D21/FR-028) IS
// already the worst-case "average + 1" frames/SOF bound, so it is STRICTLY
// >= the true average frame count at any accepted rate -- using it in place
// of Navg is a deliberately CONSERVATIVE proxy: satisfying 4x the proxy
// also satisfies 4x the true (smaller) average. The check below is built
// straight from the same three macros that already define
// CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX above (MAX_PACKET_FRAMES, CHANNELS,
// MAX_SUBSLOT_BYTES), not a restated 196/784 constant, so it self-adjusts
// automatically: T015 has now resized the subslot input from 2 to 3 for
// packed-24 (ACFX_USB_AUDIO_MAX_SUBSLOT_BYTES above), and because the SZ_MAX
// macro and THIS assert's RHS are built from the SAME three macros they moved
// together (Navg proxy grew 196->294, so 4*Navg grew 784->1176, matched by the
// SW-buf's own 784->1176). The check stays exact for any future subslot/frame/
// channel change too.
//
// #if/#error rather than static_assert: this header is plain C, #include'd
// directly by TinyUSB's own .c translation units (src/device/usbd.c,
// src/class/audio/audio_device.c), and a bare `static_assert` identifier is
// only available in C after `#include <assert.h>` (C11) -- #if/#error needs
// no such dependency and matches this driver's own adequacy checks
// immediately adjacent (src/class/audio/audio_device.h:110-140).
#if CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ <                                   \
    (4 * ACFX_USB_AUDIO_MAX_PACKET_FRAMES * ACFX_USB_AUDIO_CHANNELS *        \
     ACFX_USB_AUDIO_MAX_SUBSLOT_BYTES)
#error                                                                        \
    "CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ must be >= 4*Navg (audio_device.c:1864-1865) for CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL adequacy; widen ACFX_USB_AUDIO_SW_BUF_MULTIPLE or its inputs"
#endif

// --- Soft macros: silently defaulted by the driver if omitted. Every one
// below is set EXPLICITLY, specifically because silently inheriting a
// default is the exact failure mode this feature keeps tripping over
// (R13.0's weak-stub finding is the same shape of trap, one layer up). ---

// EP0 control-request buffer. 64 matches the driver's own default
// (audio_device.h:42-44) and is restated here rather than left implicit.
#define CFG_TUD_AUDIO_CTRL_BUF_SZ 64

// Defaults to 1 (audio_device.h:147-149) if left unset. Explicitly set to 1
// here -- an OPERATOR DECISION (research.md R5), superseding an earlier
// draft of this file that set it to 0 (see git history for that draft's
// now-superseded reasoning), so nobody "reverts" it back to 0 without
// re-reading this:
//
// This device has NO local clock and asserts no sample rate of its own
// (FR-024) -- the host's SOF is the only clock, the OUT stream is a pure
// adaptive sink (FR-025), and the IN stream must deliver the SYNCHRONOUS,
// nominal-per-SOF cadence FR-002 requires, including the fractional
// 44/45-frame interleave a 44.1 kHz rate needs to hold its exact average.
// CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL=1 selects audiod_tx_packet_size()
// (audio_device.c:538-543,1863-1896): a FIFO-fill-level control loop that
// TinyUSB itself runs, off iso-transfer-completion re-arming the endpoint
// each frame -- NOT off the (absent, and still not needed -- see
// CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP below) feedback endpoint's SOF path. It
// also makes the driver scan the descriptor for flow-control parameters at
// mount time (audiod_parse_flow_control_params(), audio_device.c:
// 1198-1200). R5 established this is precisely the mechanism that lets
// TinyUSB -- not a hand-rolled SOF-driven pipeline in this adapter's own
// code -- do the exact per-SOF packet-size cadence; the app's job stays
// "keep the IN FIFO fed" (T030's polled tud_audio_write() calls,
// ServiceUsbAudioIn) with no feed-logic change. This IS a change from an
// earlier reading of this same driver code (see the CFG_TUD_AUDIO_
// ENABLE_FEEDBACK_EP comment below, which still correctly explains why NO
// feedback endpoint is needed either way -- flow control and a feedback EP
// are independent knobs). The adequacy this setting places on the IN
// software FIFO is enforced immediately above
// (CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ's #if/#error), not left implicit.
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1

// ENABLED (1). This turns on TinyUSB's UAC2 EXPLICIT-FEEDBACK endpoint --
// the INDUSTRY-STANDARD USB-audio rate-matching mechanism -- and REPLACES the
// earlier hand-rolled "synchronous, no feedback" open-loop scheme, which
// glitched on silicon: the device's OUT FIFO drained empty and injected
// silence (audible clicks, ~0.25-0.9% of samples). The failure mode is
// fundamental to synchronous operation with no shared clock -- this device
// has NO local audio clock (FR-024), only the host's SOF -- so no amount of
// buffer sizing fixes it. The standard fix is ASYNCHRONOUS operation: the OUT
// streaming endpoint is declared Asynchronous (usb-descriptors-audio-
// streaming.h) with an associated feedback IN endpoint, and TinyUSB's driver
// computes a feedback value that tells the host to speed up / slow down its
// OUT delivery so the device's OUT FIFO stays half-full -- a closed loop that
// keeps the buffers from drifting. It is well-tested on Windows/Linux/macOS.
//
// Method: AUDIO_FEEDBACK_METHOD_FIFO_COUNT, selected by the STRONG
// tud_audio_feedback_params_cb() in usb-audio-controls.cpp. FIFO_COUNT
// regulates the OUT software FIFO to half-full and needs NO SOF ISR (the
// driver recomputes off the feedback endpoint's own interval), so the
// real-time cost is one small (4-byte) IN transfer per frame. Its documented
// requirement is an OUT software FIFO of at least 4 frames -- satisfied by
// CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ below (4 * worst-case packet). The
// feedback endpoint is an additional IN endpoint; its OTG-FS TX-FIFO slot and
// device-FIFO word cost are re-budgeted in usb-descriptors.h (still fits the
// 320-word device FIFO: 301/320 used).
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1

// Defaults to 0 already; restated for the same "every soft macro gets a
// decision, not a default" policy as the two above. This adapter has no
// UAC2 control changes to push to the host asynchronously (no on-device
// volume/mute controls -- FR-020 fixes the format), so the status
// interrupt endpoint has nothing to carry. Leaving it off also keeps one
// more endpoint free against the OTG_FS endpoint budget research.md R2
// flags as the real constraint for this composite device.
#define CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP 0

// ---------------------------------------------------------------------------
// MIDI class configuration (FR-018, adjacent macros per R13.5)
// ---------------------------------------------------------------------------

// MIDI is bulk-transfer, not isochronous, so its natural size is the
// full-speed bulk maximum. Both default to exactly this
// (src/class/midi/midi_device.h:37-50, via TUD_EPSIZE_BULK_MAX = 64 for a
// full-speed-only port) -- set explicitly rather than left silently
// inherited, matching this file's policy everywhere else.
#define CFG_TUD_MIDI_RX_EPSIZE 64
#define CFG_TUD_MIDI_TX_EPSIZE 64

// ---------------------------------------------------------------------------
// CDC class configuration (FR-018a/FR-018b, adjacent macros per R13.5)
// ---------------------------------------------------------------------------

// CFG_TUD_CDC_NOTIFY gates only the OPTIONAL tud_cdc_n_notify_msg() API for
// sending extra application-defined notifications
// (src/class/cdc/cdc_device.h:159-161) -- it does NOT remove the standard
// CDC-ACM notification endpoint the USB class spec requires, which T027's
// descriptor declares independently either way. This adapter has no use for
// the optional extra API, so it stays off, explicitly, rather than by
// silent default -- and one fewer feature surface is one fewer thing for a
// future reader to wonder whether it was intentionally unused.
#define CFG_TUD_CDC_NOTIFY 0

// FIFO / endpoint sizes for the CDC telemetry channel. Like MIDI, CDC is a
// bulk transfer, so 64 bytes (the full-speed bulk maximum,
// TUD_EPSIZE_BULK_MAX) is both the driver's own default
// (src/class/cdc/cdc_device.h:39-60) and the natural size here -- set
// explicitly for the same reason as everything else in this file. R7's
// line-oriented `key=value` telemetry text is small per snapshot, so this
// depth is not expected to be a bottleneck; TinyUSB's CDC write is
// non-blocking and drops on a full FIFO by design (R7), which this adapter
// treats as acceptable telemetry loss, not an error.
#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
#define CFG_TUD_CDC_RX_EPSIZE 64
#define CFG_TUD_CDC_TX_EPSIZE 64

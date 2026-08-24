#pragma once

// The polled USB audio data path's shim half (T032; FR-024, FR-025, FR-028,
// FR-028a). Holds the adapter's ring/stats/scratch instances and the one call
// that genuinely needs the stack: tud_audio_read() into a statically sized
// packet buffer.
//
// Split out of nucleo-main.cpp for the same reason as clock-init.h and
// otg-fs-gpio-init.h: this IS logically part of the Nucleo shim with no other
// consumer, and it belongs beside them rather than under support/ because it
// touches TinyUSB — everything under support/ is the platform-independent half
// of the adapter that must compile under the `test` preset with no toolchain
// file (D1, FR-003). Keeping it here is also what keeps nucleo-main.cpp inside
// the repo's per-file line budget as T033/T035/T036/T058 add to this path.
//
// WHAT IS NOT HERE, DELIBERATELY. The truncate-count-convert-de-interleave-
// write sequence itself is acfx::nucleo::UsbOutPath in support/usb-out-path.h,
// which knows nothing of USB and is exercised directly by the host doctest
// binary (tests/core/nucleo-usb-out-path-test.cpp, FR-049). This file is the
// thin wiring between that logic and the stack — the seam D1 draws.
//
// SINGLE-INCLUDE HEADER. The definitions below have external linkage and this
// header is included exactly once per firmware image, from nucleo-main.cpp.
// It is not a general-purpose header and must not be included twice.

#include <cstdint>

// TinyUSB's public API. Resolved the same way nucleo-main.cpp resolves it —
// through acfx_nucleo_tinyusb's PUBLIC include directories, which also put
// this directory (and so tusb_config.h) on the path.
#include "tusb.h"

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-out-path.h"

namespace acfx::nucleo {

// PROVISIONAL, AND DELIBERATELY LABELLED AS SUCH. FR-035 / D23 forbid picking
// ring capacity and startup fill ahead of measurement; T062 derives them from
// an instrumented HIL run and T063 pins the measured values HERE, replacing
// both constants and recording the evidence in research.md. Until then these
// are T062's "generous capacity" instrumentation values, NOT measurements and
// NOT a design decision: 1024 frames is ~21 ms at 48 kHz, chosen only to be
// far enough from the expected working set that the ring is not the first
// thing to clip while the rest of the path is brought up, and the startup fill
// is two worst-case packets. Do not cite either number as tuned. Their cost is
// static: 1024 frames * 2 channels * 4 bytes = 8 KiB of .bss per ring.
inline constexpr int kInputRingCapacityFrames = 1024;
inline constexpr int kInputRingStartupFillFrames = 2 * kMaxPacketFrames;

// This firmware is built with -fno-exceptions (cmake/toolchains/nucleo-f446
// .cmake:63), so AudioRing's fail-loud constructor check cannot throw here and
// traps instead (see that constructor's comment). The two static_asserts below
// restate the check at compile time against the same constants the constructor
// will see, which is what makes the trap provably unreachable rather than a
// run-time hazard: a bad edit to either constant fails the BUILD instead of
// bricking the board during static initialisation, before the fault LED or USB
// exists to report anything.
static_assert(kInputRingStartupFillFrames >= 0,
              "AudioRing startup fill must not be negative");
static_assert(kInputRingStartupFillFrames <= kInputRingCapacityFrames,
              "AudioRing startup fill exceeds capacity; such a ring could never "
              "reach Running and would prime forever");

using InputRing = AudioRing<kInputRingCapacityFrames, kChannels>;

// The input (host -> DSP) ring. Namespace scope, not a local in main(): it is
// 8 KiB, which belongs in .bss rather than on the startup stack, and T033's
// block-assembly consumer reads the same object. Its dynamic initialisation
// runs from __libc_init_array() in Reset_Handler, before main().
inline InputRing g_inputRing(kInputRingStartupFillFrames);

// Lifetime transport-health counters (FR-033). One record for the whole
// adapter; the OUT path below only ever increments. T058 reads it for CDC
// telemetry; T035/T036 add the IN-path and timing fields.
inline AudioTransportStats g_transportStats;

// Holds only its de-interleave scratch; no heap, no locks.
inline UsbOutPath g_outPath;

// The OUT packet staging buffer. Sized to TinyUSB's whole OUT software FIFO
// (CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ, 784 bytes = 196 stereo frames), not
// to one maximum packet, so a single poll drains everything the FIFO holds
// even when the service loop missed a millisecond — that FIFO is a BYTE fifo
// with no packet framing (research R13.3), so "one packet" is not a boundary
// it can offer us anyway. UsbOutPath::consumePacket() consumes an arbitrary
// byte count in bounded chunks, so nothing here has to be a packet multiple.
// Typed as int16 rather than uint8 so it is correctly aligned for the
// interleaved 16-bit PCM it holds; tud_audio_read() takes void*.
//
// static_asserts rather than comments: tusb_config.h derives its audio sizing
// from ITS OWN copies of the packet-frame/channel/sample-size constants and
// already says those "MUST match" the support library's. This is the one place
// both are visible, so this is where that must-match becomes enforceable.
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX ==
                  kMaxPacketFrames * kChannels *
                      static_cast<int>(sizeof(std::int16_t)),
              "tusb_config.h's OUT packet size and support/sample-format.h's "
              "kMaxPacketFrames/kChannels have drifted apart");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ % sizeof(std::int16_t) == 0,
              "OUT software FIFO size must be a whole number of 16-bit samples");

inline std::int16_t g_outPacketBuffer[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ /
                                      sizeof(std::int16_t)];

// Service the OUT endpoint once. Called every pass of the service loop.
//
// ADAPTIVE SINK (FR-024, FR-025, D20): this asserts NO rate. It does not wait
// for a packet, does not expect a particular size, and does not skip a pass
// because "no data is due yet" — the host's SOF is the only sample clock and
// this consumes exactly what the host has paced to us, whatever that is. A
// zero-byte read is the normal idle result and is NOT a fault: research R13.3
// records that tud_audio_read() also returns 0 before the audio function is
// open, so a 0 is genuinely indistinguishable from "idle" and must never be
// counted as an error. It is still passed to consumePacket(), which forwards a
// zero-frame write to the ring so the ring's promotion rule stays evaluated in
// exactly one place (AudioRing AR7).
//
// Every packet size in [0, 49] frames takes this identical path (FR-028), and
// nothing here — or in consumePacket() — mentions 48. The DSP's 48-frame block
// cadence (FR-030a) is T033's business on the far side of the ring; that
// separation is the whole point of the ring being here.
//
// Bounded work, no blocking: one FIFO drain plus one convert-and-write over at
// most the buffer above. That is what makes it safe to call from the tud_task()
// service loop, whose USB servicing cadence depends on iterating promptly.
inline void ServiceUsbAudioOut() {
    // tud_audio_read(void* buffer, uint16_t bufsize) -> uint16_t BYTES read
    // (TinyUSB 0.21.0, src/class/audio/audio_device.h:208, defined :403-405 as
    // tud_audio_n_read(0, ...); implementation src/class/audio/audio_device.c:
    // 448-451). Verified against the pinned tree recorded in research R13.3 —
    // not recalled. Compiled in only because CFG_TUD_AUDIO_ENABLE_EP_OUT is 1
    // (audio_device.h:397-415); getting that macro wrong is a compile error
    // here, not one of the silent-link failures R13.0 warns about.
    const std::uint16_t bytesRead = tud_audio_read(
        g_outPacketBuffer, static_cast<std::uint16_t>(sizeof(g_outPacketBuffer)));

    // The result carries framesConsumed / framesDropped / wasTruncated. It is
    // discarded here because every fact it reports has ALREADY been recorded in
    // g_transportStats by consumePacket() — the discard is not a dropped error
    // path. T058's telemetry reads the counters, not this return value.
    static_cast<void>(g_outPath.consumePacket(g_outPacketBuffer,
                                              static_cast<int>(bytesRead),
                                              g_inputRing,
                                              g_transportStats));
}

}  // namespace acfx::nucleo

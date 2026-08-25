#pragma once

// Compile-time sizing / FIFO-budget cross-checks for the polled USB audio
// data path (T015, T016; US3, FR-014). Split out of usb-audio-service.h for
// the same reason clock-init.h and otg-fs-gpio-init.h are their own files:
// this is a self-contained wall of static_assert/constexpr budget logic with
// no behavior of its own, included unconditionally from usb-audio-service.h
// so every check here still fires on every build, while keeping that file
// inside the repo's per-file line budget.
//
// SAME shared-object-library constraint as usb-audio-service.h: this header
// is reachable from usb-audio-controls.cpp and usb-lifecycle-callbacks.cpp
// (both compiled into acfx_nucleo_usb, an OBJECT library built ONCE with NO
// concrete effect selected) as well as from nucleo-main.cpp. It must
// therefore stay free of any effect/DSP dependency, exactly like its
// includer.
//
// WHAT'S HERE: the OUT-side worst-case packet staging buffer (whose size the
// asserts below are checked against) and the OUT/IN static_asserts that weld
// tusb_config.h's packet/FIFO envelopes to support/usb-out-path.h's,
// support/usb-in-path.h's, and usb-descriptors.h's own sizing — including the
// FR-014 OTG-FS 320-word device-FIFO RAM re-affirmation. Moved VERBATIM: same
// namespace, same constexpr values, same asserts, same comments.

#include <cstddef>
#include <cstdint>

#include "tusb.h"

#include "audio-format.h"
#include "usb-descriptors.h"
#include "usb-in-path.h"
#include "usb-out-path.h"

namespace acfx::nucleo {

// The OUT packet staging buffer: sized for the 24-bit WORST-CASE packet, 294
// bytes, and no larger. This is the bound, not merely a buffer size. TinyUSB's
// OUT software fifo holds up to CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (1176)
// bytes and has no packet framing at all — tu_fifo_t carries a byte depth and
// two indices and no item-size field (src/common/tusb_fifo.h:119-132) — so a
// read wider than one packet merges consecutive payloads into one
// undifferentiated byte run. Reading at most one packet's worth per pass keeps
// FR-028a truncation landing on a torn payload rather than on a whole backlog.
// See the framing section at the top of support/usb-out-path.h for what that
// does and does not guarantee.
//
// WHY THE WORST CASE, NOT THE ACTIVE FORMAT'S SIZE. ServiceUsbAudioOut()
// (usb-audio-service.h) passes g_currentAudioFormat into serviceOutFifo()
// (T019 live-wired), which reads at most UsbOutPath::maxPayloadBytes(format)
// bytes per pass — 196 B for Pcm16, 294 B for Pcm24 — capped defensively at
// this buffer's own capacity. Sizing the buffer itself from the WORST case
// across formats (Pcm24, 294 B), rather than from whichever format happens to
// be active, is what makes that per-call cap a no-op instead of a live
// truncation hazard: a host that has just selected 24-bit must never find the
// buffer only large enough for 16-bit, and the buffer must not need
// reallocating on a live 16<->24 format change either (FR-010). A Pcm16
// packet simply uses the leading 196 B of this same buffer — nothing here
// shrinks for the 16-bit case.
//
// Typed as int16 rather than uint8 so it is correctly aligned for the
// interleaved PCM it holds (16-bit samples, or 24-bit packed bytes
// reinterpreted — see UsbOutPath::consumePacket()'s Pcm24 branch);
// tud_audio_read() takes void*.
//
// static_asserts rather than comments: tusb_config.h derives its audio sizing
// from ITS OWN copies of the packet-frame/channel/sample-size constants and
// already says those "MUST match" the support library's. This is the one place
// both are visible, so this is where that must-match becomes enforceable.
// == now, not >=: T015 left this >= because the OUT path was not yet
// format-aware (UsbOutPath::maxPayloadBytes() with no argument was still the
// only 16-bit figure in play). Now that ServiceUsbAudioOut() live-wires T019's
// format-aware overload, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX — the 24-bit
// worst-case envelope the driver's single OUT FIFO is sized for — and
// UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24) must agree exactly; either
// drifting from the other is now a real bug, not a pre-wiring formality.
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX ==
                  UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24),
              "tusb_config.h's OUT packet envelope must equal support/usb-out-path.h's "
              "24-bit worst-case packet size");
static_assert(UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24) % sizeof(std::int16_t) == 0,
              "OUT worst-case packet size must be a whole number of 16-bit samples");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ >=
                  UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24),
              "OUT software FIFO cannot hold one maximum (24-bit worst-case) packet");

inline std::int16_t g_outPacketBuffer[UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24) /
                                      static_cast<int>(sizeof(std::int16_t))];

// This is what welds the worst-case read bound to the staging buffer across
// the platform seam: serviceOutFifo()'s own internal cap never lets a read
// exceed this buffer's capacity for ANY format, and sizing the buffer from
// the SAME 24-bit worst case that cap is measured against is what keeps that
// cap unreachable in practice — a genuine per-call read bound, not a silent
// truncation waiting to happen. Widening the worst case without widening this
// buffer, or vice versa, fails the BUILD.
static_assert(sizeof(g_outPacketBuffer) ==
                  static_cast<std::size_t>(UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24)),
              "OUT staging buffer and the 24-bit worst-case read bound have drifted apart");

// High-water mark of what tud_audio_available() reported still queued AFTER a
// service pass, in bytes. Pure observability, and the reason the OUT path's
// framing limitation is diagnosable rather than silent: under a service loop
// keeping up with the host's 1 ms cadence this stays at or near zero, and any
// value above one maximum packet means payloads queued up behind us — the one
// state in which a torn payload can be merged with its successor and L/R
// alignment across that boundary cannot be recovered (support/usb-out-path.h).
// A high-water mark rather than the latest sample: the latest value is racy
// against the ISR, the maximum is durable evidence. T058's CDC telemetry reads
// it; nothing acts on it, and nothing here treats a backlog as an error.
inline std::uint32_t g_outFifoWorstBacklogBytes = 0;

// tusb_config.h's IN packet size and support/sample-format.h's
// kMaxPacketFrames/kChannels must agree, the same cross-check the OUT side's
// static_asserts above already perform.
// >= not ==, for the same T015 reason as the OUT side above: EP_IN_SZ_MAX is
// now the 24-bit worst-case envelope (294 B); UsbInPath::maxPayloadBytes() is
// the 16-bit packet (196 B) until the IN path goes format-aware in T019.
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX >= UsbInPath::maxPayloadBytes(),
              "tusb_config.h's IN packet envelope must cover support/usb-in-path.h's "
              "16-bit kMaxPacketFrames/kChannels packet");
static_assert(UsbInPath::maxPayloadBytes() % sizeof(std::int16_t) == 0,
              "IN packet size must be a whole number of 16-bit samples");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ >= UsbInPath::maxPayloadBytes(),
              "IN software FIFO cannot hold one maximum packet");

// FR-014 feasibility gate, audio-side cross-check (US3, T016). The OTG-FS
// 320-word device-FIFO budget itself is computed and asserted in
// usb-descriptors.h (kOtgFsFifoWordsUsed / kOtgFsFifoWordsFree, derived from
// the DWC2 dfifo_alloc()/calc_device_grxfsiz() formulas against kAudioEpSize).
// This is the SW-buffer-side check that the AUDIO endpoints the budget was
// computed against are the SAME resized envelopes the driver's own EP FIFOs
// use here: the two isochronous packets (kAudioEpSize, driving both the
// largest TX FIFO and the shared RX FIFO in that budget) equal the driver's
// CFG_TUD_AUDIO_FUNC_1_EP_*_SZ_MAX macros. If a future resize moved one but not
// the other, the FIFO budget in usb-descriptors.h would be computed against a
// stale packet size; welding them here makes that drift fail the build.
static_assert(kAudioEpSize == CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX &&
                  kAudioEpSize == CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
              "OTG-FS FIFO budget (usb-descriptors.h) is computed from kAudioEpSize; it "
              "must equal the driver's audio EP FIFO envelopes or the budget is stale");
// Re-affirm, from this translation unit, that the whole EP/descriptor config
// fits the 320-word OTG-FS device FIFO RAM. Redundant with usb-descriptors.h by
// design: both audio SW-buffer sizing (this file) and the descriptor/FIFO
// budget must be recompiled and re-checked whenever the packet envelope moves.
static_assert(kOtgFsFifoWordsUsed <= kOtgFsDfifoWords,
              "FR-014 OVERRUN: 24-bit EP config exceeds the OTG-FS 320-word device FIFO "
              "RAM; surface the FR-014 fallback table to the operator (Constitution V)");

}  // namespace acfx::nucleo

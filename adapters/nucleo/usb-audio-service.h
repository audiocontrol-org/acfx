#pragma once

// The polled USB audio data path's shim half (T032; FR-024, FR-025, FR-028,
// FR-028a). Holds the adapter's ring/stats/scratch instances and the two calls
// that genuinely need the stack: tud_audio_read() into a statically sized
// packet buffer, and tud_audio_available() to observe what the fifo still
// holds behind it.
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
// write sequence, and the decision of how many bytes one read may ask for, are
// both in support/usb-out-path.h (UsbOutPath / serviceOutFifo), which knows
// nothing of USB and is exercised directly by the host doctest binary
// (tests/core/nucleo-usb-out-path-test.cpp and its
// nucleo-usb-out-service-test.cpp sibling, FR-049). This file is the thin
// wiring between that logic and the stack — the seam D1 draws.
//
// ONE INSTANCE PER IMAGE, SAFELY. Every definition below is `inline`, which is
// the C++ mechanism that makes multiple inclusion well defined: each name has
// external linkage and the linker folds all definitions into exactly ONE
// object. Including this header from more than one translation unit is
// therefore correct, not a hazard; what it must not do is get copied, because
// two copies would be two rings.

#include <cstddef>
#include <cstdint>

// TinyUSB's public API. Resolved the same way nucleo-main.cpp resolves it —
// through acfx_nucleo_tinyusb's PUBLIC include directories, which also put
// this directory (and so tusb_config.h) on the path.
#include "tusb.h"

#include "audio-format.h"
#include "audio-ring.h"
#include "format-change.h"
#include "lifecycle-policy.h"
#include "rate-change.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-descriptors.h"
#include "usb-in-path.h"
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

// The output (DSP -> host) ring (T033). PROVISIONAL for exactly the same
// reason as the input ring's constants above, and pinned by the same tasks
// (T062/T063, FR-035 / D23): 1024 frames is not a measurement, and the startup
// fill of two worst-case packets is the same instrumentation value on the
// other side of the DSP. Its consumer is the USB IN endpoint (T035), which
// drains up to one 49-frame packet per SOF — which is why the fill is
// expressed in packets here and not in blocks. Cost: 1024 * 2 * 4 = 8 KiB of
// .bss, the same as the input ring.
//
// It lives HERE, beside g_inputRing rather than beside the block path that
// fills it, because it has two consumers on this side of the seam: the block
// path writes it (dsp-block-service.h) and the IN endpoint drains it (T035,
// which lands in this file). Keeping both rings in one place is what stops
// those two from acquiring a circular include.
inline constexpr int kOutputRingCapacityFrames = 1024;
inline constexpr int kOutputRingStartupFillFrames = 2 * kMaxPacketFrames;

// The same two call-site guards g_inputRing carries, restated for THIS ring
// rather than assumed to carry over. Backlog TASK-32 names this exact
// situation — a second ring instantiated elsewhere that copies the ring but
// not its guards — as the way AudioRing's -fno-exceptions __builtin_trap()
// stops being provably unreachable. Under -fno-exceptions a bad constant here
// would otherwise HardFault inside __libc_init_array(), before the fault LED
// or USB exists to report anything.
static_assert(kOutputRingStartupFillFrames >= 0,
              "AudioRing startup fill must not be negative");
static_assert(kOutputRingStartupFillFrames <= kOutputRingCapacityFrames,
              "AudioRing startup fill exceeds capacity; such a ring could never "
              "reach Running and would prime forever");

using OutputRing = AudioRing<kOutputRingCapacityFrames, kChannels>;

inline OutputRing g_outputRing(kOutputRingStartupFillFrames);

// Lifetime transport-health counters (FR-033). One record for the whole
// adapter; the OUT path below only ever increments. T058 reads it for CDC
// telemetry. T035's IN path (below) increments `outputUnderruns` — the same
// FR-031 counter the OUT path's shortfall convention already established, not
// a new field: no bytes this path pulls from the ring are ever lost
// uncounted at this layer (see support/usb-in-path.h's file header for the
// sink back-pressure story), so nothing here needed a ninth field. T036 adds
// `worstBlockMicros`'s timing source.
inline AudioTransportStats g_transportStats;

// T047 (US7, FR-029, FR-029a, D22): per-streaming-interface alt-setting
// state, maintained ONLY by tud_audio_set_itf_cb / tud_audio_set_itf_close_ep_cb,
// so those callbacks are the single place the host's SET_INTERFACE requests
// are turned into "is this direction actually streaming".
//
// DECLARED extern HERE, DEFINED (and written) in usb-audio-controls.cpp — NOT
// `inline` in this header. The two TinyUSB callbacks are TU_ATTR_WEAK; a weak
// default with the same signature always exists. An `inline` C++ definition of
// the callback emits only a COMDAT (weak) symbol, which the linker is free to
// resolve to TinyUSB's weak default instead — the callback then silently does
// nothing and the board never detects capture-only, despite a clean link and
// green host tests (which drive the pure logic directly). The callbacks MUST be
// STRONG definitions to override the weak default, so they live in a .cpp
// (usb-audio-controls.cpp — the adapter's established home for exactly this
// weak-audio-callback linkage concern), and the state they own is defined
// there beside them. These control requests are dispatched from tud_task() in
// the same single execution context as the service loop that reads them
// (D26), so a plain bool needs no atomicity. Default false/false matches the
// USB power-up state (every alt-bearing interface starts at alt 0).
extern bool g_outStreaming;
extern bool g_inStreaming;

// T015 (US3, FR-005/FR-010): which PCM sample format the host most recently
// SELECTED via SET_INTERFACE alt setting. alt 1 -> Pcm16, alt 2 -> Pcm24; this
// is the run-time counterpart to the two alts usb-descriptors.cpp advertises.
// The poll-loop converters (T019) read this to pick int16 vs packed-24
// conversion; the format-transition lifecycle (T017/T018) reads it to detect a
// 16<->24 change. DECLARED extern HERE, DEFINED (and written) in
// usb-audio-controls.cpp by the SAME strong tud_audio_set_itf_cb that owns
// g_outStreaming/g_inStreaming -- for exactly the weak-callback-linkage reason
// documented there and above, an `inline` definition would risk losing to
// TinyUSB's weak default. Dispatched from tud_task() (D26), so a plain enum
// needs no atomicity. Default Pcm16 matches alt 1 being the first/16-bit format
// and the power-up state where no stream is open yet.
//
// AudioFormat ITSELF is defined in support/audio-format.h (T018), not here —
// see that header's comment for why: this header pulls in "tusb.h", so it
// cannot be format-change.h's home (which must stay host-compilable), and a
// SECOND local definition here previously collided with format-change.h's
// copy the moment both headers met in one translation unit (the ODR trap
// backlog TASK-12 names). "audio-format.h" is included above.
extern AudioFormat g_currentAudioFormat;

// The currently-selected sample rate (Hz), owned by the Clock Source's
// Sampling-Frequency Control (US2, FR-004). Default kDefaultSampleRateHz
// (48000). DECLARED extern HERE, DEFINED in usb-audio-controls.cpp beside the
// strong tud_audio_set_req_entity_cb that writes it and the get callback that
// reports it — same weak-callback-linkage home as the streaming flags above.
// The SET request that updates it is dispatched from tud_task() in the same
// single execution context as the poll-loop consumers that read it (D26), so a
// plain uint32_t needs no atomicity. The EP0 SET callback only VALIDATES and
// STORES the new rate here; re-configuring the effect/rings at the new rate is
// deferred to a poll-loop service step (research §R9), never done in EP0
// context.
extern std::uint32_t g_currentSampleRateHz;

// Capture-only (D22): the IN streaming interface is open and OUT sits at its
// zero-bandwidth alt — no playback stream exists at all, not merely one that
// is momentarily idle. See support/usb-in-path.h's `captureOnly` doc comment
// for why that distinction is what keeps this counted under `inputStarved`
// rather than `outputUnderruns` (FR-029a / I-TS1a).
inline bool CaptureOnlyActive() noexcept { return g_inStreaming && !g_outStreaming; }

// T056 (US10, FR-055, contract AR9's "stream open -> reset()"): per-interface
// stream-OPEN edges, reconciled once per service pass rather than acted on
// inside the alt-setting callbacks themselves. tud_audio_set_itf_cb /
// tud_audio_set_itf_close_ep_cb (usb-audio-controls.cpp) own ONLY the
// streaming-bool tracking T047 already established and stay free of any
// ring/TinyUSB-audio coupling that resetting a ring directly from them would
// add; the ring-owning decision instead lives HERE, beside g_inputRing /
// g_outputRing, via support/lifecycle-policy.h's pure, host-tested
// reconcileStreamOpenEdges().
//
// `g_prevOutStreaming` / `g_prevInStreaming` are this reconciler's own memory
// of "was this direction streaming LAST pass" — distinct from
// g_outStreaming/g_inStreaming's "streaming RIGHT NOW" above. Comparing the
// two, once per pass, is what turns a level into an edge; both default false,
// matching g_outStreaming/g_inStreaming's own power-up default.
//
// RING <-> INTERFACE MAPPING (lifecycle-policy.h's file header has the full
// rationale): OUT (host -> device) feeds g_inputRing; IN (device -> host)
// drains g_outputRing. Opening OUT resets ONLY g_inputRing; opening IN resets
// ONLY g_outputRing — every combination of {both closed, playback only,
// capture only, both open} and every transition between them (FR-055) falls
// out of reconcileStreamOpenEdges() checking each direction independently,
// with no special-casing here.
inline bool g_prevOutStreaming = false;
inline bool g_prevInStreaming = false;

// Called once per service-loop pass, before the OUT/IN data-path services
// below: a stream that just opened this pass must find its ring already
// reset by the time ServiceUsbAudioOut()/ServiceUsbAudioIn() next touch it.
// The result is discarded — nothing here needs to know WHICH ring was reset,
// only that the reconciler ran; support/nucleo-lifecycle-policy-test.cpp is
// what asserts the mapping is not inverted.
inline void ServiceUsbLifecycle() noexcept {
    static_cast<void>(reconcileStreamOpenEdges(g_outStreaming, g_inStreaming,
                                               g_prevOutStreaming, g_prevInStreaming,
                                               g_inputRing, g_outputRing));
}

// ----------------------------------------------------------------------------
// T011 (US2, FR-006, research §R9): the rate-change latch shared between the
// USB SET callback (producer) and the poll-loop's rate-change reaction
// (consumer). ONLY the latch lives here — the reaction itself (re-preparing
// the effect at the new rate, resetting the rings) is ServiceRateChange() in
// the sibling rate-change-service.h, NOT in this file. That split mirrors
// exactly why dsp-block-service.h exists as its own file rather than folding
// ServiceDspBlock() in here too (see this file's own "WHY THIS INCLUDES
// usb-audio-service.h" comment on dsp-block-service.h, and effect-instance.h's
// header comment): this header is included by usb-audio-controls.cpp and
// usb-lifecycle-callbacks.cpp, both compiled into acfx_nucleo_usb — an OBJECT
// library built ONCE and shared by every firmware image (see
// CMakeLists.txt's acfx_nucleo_usb target) — so it must compile with NO
// concrete effect selected. ACFX_EFFECT_TYPE/ACFX_EFFECT_HEADER are compile
// definitions the acfx_add_effect_nucleo factory sets ONLY on each firmware
// executable target (nucleo-main.cpp's own translation unit), never on
// acfx_nucleo_usb. Including effect-instance.h from here (tried first, and
// reverted) breaks that shared library's build with "ACFX_EFFECT_TYPE does
// not name a type" for exactly that reason. rate-change.h itself has no such
// dependency, so the latch stays here, reachable from both sides; only the
// PrepareEffect()-calling reaction moves to the file nucleo-main.cpp alone
// includes.
//
// THE SHARED LATCH. `g_rateChangeLatch` is the ONE instance of
// support/rate-change.h's RateChangeLatch for this image, following the same
// multiple-inclusion-safe `inline` pattern as g_inputRing/g_outputRing above:
// every definition below is `inline`, so the linker folds all inclusions into
// exactly one object. usb-audio-controls.cpp's strong tud_audio_set_req_entity_cb
// (the producer) reaches it by including THIS header — the mirror image of how
// it already reaches g_currentSampleRateHz (declared extern here, DEFINED in
// that .cpp): here the definition lives in this header and that .cpp is the
// consumer of the header, not the other way around, because the latch has
// exactly one producer call site and one consumer call site and both are
// reachable from this file's include graph without a new extern.
inline RateChangeLatch g_rateChangeLatch;

// ----------------------------------------------------------------------------
// T018 (US3, FR-006, research §R9): the format-change latch, the exact mirror
// of g_rateChangeLatch above — same multiple-inclusion-safe `inline` instance
// pattern, same producer (usb-audio-controls.cpp's strong tud_audio_set_itf_cb,
// which already records g_currentAudioFormat and now also arms this latch)
// and same consumer (ServiceFormatChange(), the sibling of ServiceRateChange()
// in format-change-service.h, included only from nucleo-main.cpp for the same
// ACFX_EFFECT_TYPE reason rate-change-service.h documents). support/format-
// change.h itself has no firmware dependency, so — like rate-change.h — the
// latch lives here, reachable from both usb-audio-controls.cpp and
// nucleo-main.cpp's include graph without a new extern.
inline FormatChangeLatch g_formatChangeLatch;

// Holds only its de-interleave scratch; no heap, no locks.
inline UsbOutPath g_outPath;

// The two TinyUSB entry points the OUT path needs, behind the tiny duck-typed
// interface serviceOutFifo() expects. Both are verified against the PINNED
// 0.21.0 tree recorded in research R13.3, not recalled:
//
//   tud_audio_read(void* buffer, uint16_t bufsize) -> uint16_t BYTES read
//     src/class/audio/audio_device.h:208, defined :403-405 as
//     tud_audio_n_read(0, ...); implementation audio_device.c:448-451, which
//     is tu_fifo_read_n() on the endpoint's software fifo.
//   tud_audio_available(void) -> uint16_t BYTES currently queued
//     src/class/audio/audio_device.h:206, defined :399-401 as
//     tud_audio_n_available(0); implementation audio_device.c:443-446, which
//     is tu_fifo_count() on the same fifo.
//
// Both are compiled in only because CFG_TUD_AUDIO_ENABLE_EP_OUT is 1
// (audio_device.h:397-415); getting that macro wrong is a compile error here,
// not one of the silent-link failures R13.0 warns about.
//
// A zero from either call is the normal idle result and NEVER a fault: R13.3
// records that tud_audio_read() also returns 0 before the audio function is
// open, so a 0 is genuinely indistinguishable from "idle".
struct TinyUsbOutFifo {
    int read(std::int16_t* dst, int maxBytes) noexcept {
        return static_cast<int>(
            tud_audio_read(dst, static_cast<std::uint16_t>(maxBytes)));
    }

    int available() noexcept { return static_cast<int>(tud_audio_available()); }
};

inline TinyUsbOutFifo g_outFifo;

// The OUT packet staging buffer: ONE maximum packet, 196 bytes, and no larger.
// This is the bound, not merely a buffer size. TinyUSB's OUT software fifo
// holds up to CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ (784) bytes and has no
// packet framing at all — tu_fifo_t carries a byte depth and two indices and
// no item-size field (src/common/tusb_fifo.h:119-132) — so a read wider than
// one packet merges consecutive payloads into one undifferentiated byte run.
// Reading exactly one packet's worth per pass keeps FR-028a truncation landing
// on a torn payload rather than on a whole backlog. See the framing section at
// the top of support/usb-out-path.h for what that does and does not guarantee.
//
// Typed as int16 rather than uint8 so it is correctly aligned for the
// interleaved 16-bit PCM it holds; tud_audio_read() takes void*.
//
// static_asserts rather than comments: tusb_config.h derives its audio sizing
// from ITS OWN copies of the packet-frame/channel/sample-size constants and
// already says those "MUST match" the support library's. This is the one place
// both are visible, so this is where that must-match becomes enforceable.
// >= not ==, since T015 (US3): CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX is now the
// 24-bit WORST-CASE envelope (294 B) that the driver's single OUT FIFO is sized
// for, while UsbOutPath::maxPayloadBytes() is still the 16-bit packet (196 B) —
// the OUT path itself becomes format-aware in T019, at which point this can
// tighten back to ==. What must hold NOW is that the endpoint FIFO can hold at
// least one 16-bit packet, which >= guarantees; a driver buffer SMALLER than a
// packet is the dangerous drift this still catches.
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX >= UsbOutPath::maxPayloadBytes(),
              "tusb_config.h's OUT packet envelope must cover support/sample-format.h's "
              "16-bit kMaxPacketFrames/kChannels packet");
static_assert(UsbOutPath::maxPayloadBytes() % sizeof(std::int16_t) == 0,
              "OUT packet size must be a whole number of 16-bit samples");
static_assert(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ >= UsbOutPath::maxPayloadBytes(),
              "OUT software FIFO cannot hold one maximum packet");

inline std::int16_t g_outPacketBuffer[UsbOutPath::maxPayloadBytes() /
                                      static_cast<int>(sizeof(std::int16_t))];

// This is what welds the read bound to the staging buffer across the platform
// seam: serviceOutFifo() asks for UsbOutPath::maxPayloadBytes() bytes, and the
// buffer it writes into is exactly that size. Widening either one without the
// other fails the BUILD.
static_assert(sizeof(g_outPacketBuffer) ==
                  static_cast<std::size_t>(UsbOutPath::maxPayloadBytes()),
              "OUT staging buffer and the per-call read bound have drifted apart");

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

// Service the OUT endpoint once. Called every pass of the service loop.
//
// ADAPTIVE SINK (FR-024, FR-025, D20): this asserts NO rate. It does not wait
// for a packet, does not expect a particular size, and does not skip a pass
// because "no data is due yet" — the host's SOF is the only sample clock and
// this consumes exactly what the host has paced to us, whatever that is. A
// zero-byte read is the normal idle result and is NOT a fault; it is still
// passed to consumePacket(), which forwards a zero-frame write to the ring so
// the ring's promotion rule stays evaluated in exactly one place (AR7).
//
// Every packet size in [0, 49] frames takes this identical path (FR-028), and
// nothing here — or in consumePacket() — mentions 48. The DSP's 48-frame block
// cadence (FR-030a) is T033's business on the far side of the ring; that
// separation is the whole point of the ring being here.
//
// ONE READ PER PASS. A backlog is drained by the service loop coming back, not
// by this function looping. That keeps the work per pass bounded to one packet
// — at most 49 frames of convert-and-write, no wait of any kind — which is
// what makes it safe to call from a tud_task() loop whose USB servicing
// cadence depends on iterating promptly, and it is also the framing decision
// described above.
inline void ServiceUsbAudioOut() {
    // The pass result carries framesConsumed / framesDropped / wasTruncated,
    // all of which consumePacket() has ALREADY recorded in g_transportStats —
    // the discard below is not a dropped error path. Only backlogBytes has no
    // counter behind it, so that is the one field this reads.
    const OutServicePass pass =
        serviceOutFifo(g_outFifo, g_outPath, g_outPacketBuffer, g_inputRing, g_transportStats);

    if (pass.backlogBytes > 0 &&
        static_cast<std::uint32_t>(pass.backlogBytes) > g_outFifoWorstBacklogBytes) {
        g_outFifoWorstBacklogBytes = static_cast<std::uint32_t>(pass.backlogBytes);
    }
}

// ----------------------------------------------------------------------------
// The IN path's shim half (T035; FR-026, FR-032, FR-038a): the two TinyUSB
// calls support/usb-in-path.h's UsbInPath::service() needs, behind the same
// kind of tiny duck-typed adapter TinyUsbOutFifo provides above. Both are
// verified against the PINNED 0.21.0 tree, not recalled:
//
//   tud_audio_write(const void*, uint16_t) -> uint16_t BYTES accepted
//     src/class/audio/audio_device.h:213, defined :419-421 as
//     tud_audio_n_write(0, ...); implementation audio_device.c:500-503, which
//     is tu_fifo_write_n() (NON-overwritable mode) on the endpoint's software
//     fifo — it accepts up to what remains free and returns that count,
//     which is exactly the back-pressure support/usb-in-path.h's file header
//     describes and UsbInPath::service() retries rather than drops.
//   tud_audio_get_ep_in_ff(void) -> tu_fifo_t* (src/class/audio/
//     audio_device.h:214, :427-429, tud_audio_n_get_ep_in_ff, audio_device.c:
//     511-516) paired with tu_fifo_remaining() (src/common/tusb_fifo.h:306-
//     310), which is the ONLY way to learn the IN software fifo's free space
//     — there is no tud_audio_write_available()-named entry point in this
//     tree. Both are visible here because tusb.h itself #includes
//     "common/tusb_fifo.h" (src/tusb.h:39), so no extra include is needed
//     beyond the tusb.h already pulled in above.
//
// Both are compiled in only because CFG_TUD_AUDIO_ENABLE_EP_IN is 1
// (tusb_config.h); getting that macro wrong is a compile error here, not one
// of the silent-link failures R13.0 warns about.
//
// tud_audio_get_ep_in_ff() can return nullptr before the audio function is
// mounted (audio_device.c's own null check on `p_desc`), and tud_audio_write()
// itself returns 0 in the same circumstance (its TU_VERIFY guard). Both are
// treated as the ordinary "no room yet" idle result, never a fault — the same
// "a zero from either call is the normal idle result" convention TinyUsbOutFifo
// documents above.
struct TinyUsbInFifo {
    int write(const std::int16_t* data, int len) noexcept {
        return static_cast<int>(tud_audio_write(data, static_cast<std::uint16_t>(len)));
    }

    int writeAvailable() noexcept {
        tu_fifo_t* fifo = tud_audio_get_ep_in_ff();
        return (fifo != nullptr) ? static_cast<int>(tu_fifo_remaining(fifo)) : 0;
    }
};

inline TinyUsbInFifo g_inFifo;

// Holds only its ring-read scratch and its packet/carryover buffer; no heap,
// no locks. See support/usb-in-path.h for the full design: room-bounded pull
// size, FR-038a conversion via interleaveToInt16() (reused, not
// reimplemented), and back-pressure retried rather than dropped.
inline UsbInPath g_inPath;

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

// Service the IN endpoint once. Called every pass of the service loop, after
// ServiceUsbAudioOut() and ServiceDspBlock() have had their chance to move
// audio through the rings.
//
// ASYNCHRONOUS SOURCE (FR-026, D20): this path asserts no rate of its own —
// it is paced entirely by however much room TinyUSB's own IN software fifo
// reports free, which only grows as the host's SOF-triggered ISR drains a
// previous write. See support/usb-in-path.h's file header and
// UsbInPath::service()'s doc comment for the full account of why that is what
// keeps a fast tud_task() poll loop from over-draining the output ring, how
// much is pulled per pass, and how a partial tud_audio_write() accept is
// retried rather than silently dropped.
//
// The pass result is discarded: `outputUnderruns` and, since T047,
// `inputStarved` are already recorded inside service() itself, and every
// other field is per-pass detail with no counter behind it (see
// InServicePass's own doc comment). T058's CDC telemetry reads
// g_transportStats; nothing on this path acts on the discarded result.
//
// `CaptureOnlyActive()` is read fresh every pass rather than cached: the host
// can close the OUT stream (or the IN stream) between passes, and this must
// track that transition exactly as promptly as the ring/sink state it
// otherwise gates on.
inline void ServiceUsbAudioIn() {
    static_cast<void>(
        g_inPath.service(g_outputRing, g_inFifo, g_transportStats, CaptureOnlyActive()));
}

}  // namespace acfx::nucleo


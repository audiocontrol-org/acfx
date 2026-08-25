#pragma once

// The format-change poll-loop reaction's shim half (T018; FR-006, research
// §R9): binds usb-audio-service.h's shared g_formatChangeLatch/g_inputRing/
// g_outputRing to the single call the service loop makes,
// ServiceFormatChange(). The exact structural mirror of rate-change-
// service.h — same split, same reason: see that file's own header comment
// for the full ACFX_EFFECT_TYPE/acfx_nucleo_usb explanation (it applies here
// unchanged, since g_formatChangeLatch lives in usb-audio-service.h beside
// g_rateChangeLatch and is reachable from usb-audio-controls.cpp too).
//
// UNLIKE rate-change-service.h, this file does NOT include effect-instance.h
// and does NOT call PrepareEffect() — see ServiceFormatChange()'s own comment
// below for why a bit-depth change is not a DSP-affecting event. That is the
// one substantive difference from the rate-change reaction; everything else
// (latch shape, ring-reset call, poll-loop wiring) mirrors it deliberately.

#include <cstdint>

#include "lifecycle-policy.h"
#include "usb-audio-service.h"

namespace acfx::nucleo {

// Called once per service-loop pass, mirroring ServiceRateChange()
// (rate-change-service.h) and ServiceUsbLifecycle() (usb-audio-service.h):
// consume a latch, react by resetting ring state, all from the main loop and
// never from EP0/interrupt context (FR-006, research §R9's explicit "no
// effect re-preparation or ring reset in the EP0 callback" requirement — see
// usb-audio-controls.cpp's tud_audio_set_itf_cb comment for the producer
// side of that split). consumePendingFormatChange() is two field reads and a
// branch when nothing is pending, the same bounded, wait-free cost every
// other zero-work service step in usb-audio-service.h pays (D26: one
// execution context, so nothing here may block or loop unboundedly).
//
// ON A PENDING CHANGE: reset BOTH rings via lifecycle-policy.h's
// applyResumeOrBusReset() — the SAME function ServiceRateChange() and
// FR-052/FR-053's suspend/resume path already use to restart a ring from a
// defined Priming state rather than draining a stale partial ring (AR9). A
// format change is exactly that kind of discontinuity: audio already
// sitting in either ring was framed at the OLD subslot size (2 bytes/sample
// for Pcm16, 3 for Pcm24) and a partially-drained ring straddling the switch
// would tear frames across the boundary. Resetting both rings is this
// adapter's "flush any in-flight software FIFO across the packet-size
// change" (T018's brief): it is also, structurally, everything left to do —
// TinyUSB's OWN endpoint software FIFOs (audiod_set_interface(),
// audio_device.c, pinned tree) already call tu_fifo_clear() on ep_in_ff /
// ep_out_ff on EVERY SET_INTERFACE that closes an endpoint, which an
// alt-1<->alt-2 format switch always does (it closes the old alt's endpoint
// before opening the new one) — verified against the vendored 0.21.0 source,
// not assumed. So by the time this function ever sees a pending format
// change, TinyUSB's own EP fifos are already flushed; only OUR rings, on the
// far side of those fifos, are this function's job.
//
// "SWITCH THE RECORDED CONVERTER DEPTH CLEANLY" (T018's brief) is already
// done by the time this runs: g_currentAudioFormat (usb-audio-service.h) is
// written SYNCHRONOUSLY by the same strong tud_audio_set_itf_cb call that
// arms this latch (usb-audio-controls.cpp), before ServiceFormatChange() is
// ever invoked — there is no separate "switch" step left to perform here.
// What "cleanly" buys is ORDERING, not an extra action: the rings are reset
// on this side of the format record having already changed, so nothing
// downstream can observe a ring that still holds old-format audio alongside
// a converter that has already moved to the new format. No such
// format-aware converter exists on the OUT/IN paths YET (support/usb-out-
// path.h and usb-in-path.h are still Pcm16-only; T019 is what makes them
// read g_currentAudioFormat) — this function's contract does not change
// when T019 lands, since the ordering guarantee it provides (reset happens
// after the format record already flipped) is exactly what a future
// format-aware converter needs too.
//
// CRITICAL SEMANTIC — DELIBERATELY NOT PrepareEffect(): unlike
// ServiceRateChange(), which DOES re-prepare the compiled-in effect because
// the sample RATE changes the DSP's time-domain behaviour (filter
// coefficients, delay-line lengths, etc. are all rate-dependent), a bit-DEPTH
// change carries no such information for the DSP path to react to. The
// effect operates on de-interleaved float buffers regardless of whether the
// wire format was 16-bit or packed-24-bit — sample-format.h's
// deinterleaveToFloat()/interleaveToInt16() and
// sampleFromWire24Packed()/wireFromSample24Packed() are what absorb that
// difference, entirely on the transport side, before/after the effect ever
// sees a sample. Calling PrepareEffect() here would be wrong, not merely
// redundant: it would needlessly re-run prepare() (and, per its own
// contract, is meant to run only while the stream is stopped) for a
// transport-only event that leaves the DSP's configuration untouched.
inline void ServiceFormatChange() noexcept {
    AudioFormat newFormat = AudioFormat::Pcm16;
    if (g_formatChangeLatch.consumePendingFormatChange(newFormat)) {
        static_cast<void>(newFormat);  // Already recorded in g_currentAudioFormat by the producer.
        applyResumeOrBusReset(g_inputRing, g_outputRing);
    }
}

}  // namespace acfx::nucleo

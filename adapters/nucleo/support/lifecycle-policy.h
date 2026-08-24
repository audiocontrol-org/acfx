#pragma once

// Platform-independent USB bus-lifecycle POLICY for the Nucleo USB audio
// adapter (T053, T054, T055, T056; FR-051, FR-052, FR-053, FR-054, FR-055;
// contract AR9). No TinyUSB, no CMSIS, no board headers, no <cstdio> — this
// header compiles under the `test` preset with no toolchain file. The
// TinyUSB-visible half (the `tud_*` callbacks that call into these functions)
// lives in adapters/nucleo/usb-lifecycle-callbacks.cpp and
// adapters/nucleo/usb-audio-service.h; see those files' comments for why the
// callbacks themselves must be STRONG symbols and cannot live here.
//
// WHAT THIS FILE DECIDES, AND WHAT IT DOES NOT. Every function below is a
// pure decision over ring lifecycle transitions the ring already exposes
// (audio-ring.h's reset()/stop(), contract AR9) — it is "which ring(s), which
// method, when", nothing more. It touches no counters (AudioTransportStats):
// AR9 already guarantees reset()/stop() never do, structurally, because the
// ring owns no counters at all (AR4). That guarantee is not re-implemented
// here; it falls out of calling only reset()/stop() and never anything else.
//
// RING <-> INTERFACE MAPPING (get this right — it is the easy thing to
// invert, and the inversion is exactly the silent error a firmware-only
// wiring would hide). Per usb-descriptors.h:
//   - kItfNumAudioStreamingOut (host -> device) feeds the INPUT ring — the
//     DSP's input, filled by USB OUT packets (support/usb-out-path.h).
//   - kItfNumAudioStreamingIn  (device -> host) drains the OUTPUT ring — the
//     DSP's processed output, drained by USB IN packets
//     (support/usb-in-path.h).
// So "OUT interface opens" maps to "reset the INPUT ring", and "IN interface
// opens" maps to "reset the OUTPUT ring" — every function below that takes
// two distinctly-purposed ring references names them `inputRing`/`outputRing`
// (never `outRing`/`inRing`, which reads as though it mirrored the interface
// names and would invite exactly this inversion) to keep that mapping visible
// at every call site.
//
// TEMPLATED ON RING TYPE, not on the firmware's pinned InputRing/OutputRing
// aliases (usb-audio-service.h) — the same reasoning support/dsp-block-path.h
// gives for templating runOneBlock(): ring capacity is an HIL-derived number
// pinned later (D23, FR-035) and must not be spelled here, and this lets the
// host doctest suite drive every function below against a small
// AudioRing<48> without instantiating the firmware's 1024-frame ring or
// touching TinyUSB at all.

#include "audio-ring.h"

namespace acfx::nucleo {

// Bus suspend (FR-051, US10 AS1): stop producing and consuming audio by
// clearing BOTH rings and putting BOTH in Stopped. This is the whole of
// FR-051's "must not spin waiting for packets that will not arrive" as far as
// ring state is concerned — see usb-lifecycle-callbacks.cpp's file header for
// why nothing else is needed: the OUT/IN/block-path consumers already gate on
// state() == Running (contract AR7) and the OUT producer's own write() is a
// bounded, non-blocking call regardless of state, so a Stopped ring cannot
// make the service loop spin; suspend genuinely halting bus traffic is what
// keeps it idle, and this function's job is only to make the STATE honestly
// reflect that.
template <typename InputRing, typename OutputRing>
void applySuspend(InputRing& inputRing, OutputRing& outputRing) noexcept {
    inputRing.stop();
    outputRing.stop();
}

// Resume (FR-052, US10 AS2) and bus reset / re-enumeration (FR-053, US10
// AS3): both restart from a defined state — Priming — rather than draining a
// stale partial ring (AR9). The two triggers share one function because they
// share one policy: FR-052's "no pre-suspend audio left to replay" is
// structural, not a second mechanism bolted on here — applySuspend() above
// already cleared the contents when suspend fired, so reset() has nothing
// stale to leave behind on either trigger.
template <typename InputRing, typename OutputRing>
void applyResumeOrBusReset(InputRing& inputRing, OutputRing& outputRing) noexcept {
    inputRing.reset();
    outputRing.reset();
}

// What one call to reconcileStreamOpenEdges() below actually did, so a host
// test can assert the mapping without inferring it from ring state alone
// (occupancy/state after a reset() is indistinguishable from "never touched"
// when the ring started empty and Priming).
struct StreamEdgeResult {
    bool inputRingReset = false;
    bool outputRingReset = false;
};

// Alt-setting stream-open reconciliation (T056; FR-055, contract AR9's
// "stream open -> reset()"). Reconciled as an EDGE (false -> true), evaluated
// once per service pass, rather than acted on inside the alt-setting
// callbacks themselves (tud_audio_set_itf_cb / tud_audio_set_itf_close_ep_cb,
// usb-audio-controls.cpp): those callbacks own only the streaming-bool
// tracking T047 already established and stay free of any ring/TinyUSB
// coupling that resetting rings directly from them would add — this is
// option (b) of the two architectures considered, chosen because it keeps
// usb-audio-controls.cpp's existing, already-tested responsibility
// (SET_INTERFACE -> bool) unchanged and puts the ring-owning decision beside
// the rings it owns (usb-audio-service.h, which already declares
// g_inputRing/g_outputRing and reads g_outStreaming/g_inStreaming).
//
// `prevOutStreaming`/`prevInStreaming` are the caller's memory of "was this
// direction streaming last pass" — an in/out reference rather than an
// internal static so this function stays pure and host-testable: the caller
// (usb-audio-service.h's ServiceUsbLifecycle() in the firmware, a plain local
// in tests) owns the persisted state, and this function only compares and
// updates it.
//
// EVERY COMBINATION of {both closed, playback only, capture only, both open}
// is representable simply because this checks each direction's edge
// independently: there is no combined state to enumerate and no special case
// for any of the four combinations or the twelve ordered transitions between
// them (FR-055) — closing a direction just lets its edge fire again the next
// time that direction opens.
template <typename InputRing, typename OutputRing>
StreamEdgeResult reconcileStreamOpenEdges(bool outStreamingNow, bool inStreamingNow,
                                          bool& prevOutStreaming, bool& prevInStreaming,
                                          InputRing& inputRing,
                                          OutputRing& outputRing) noexcept {
    StreamEdgeResult result;

    // OUT interface opening (false -> true): reset the INPUT ring it feeds.
    // Per the file-header mapping, this is NOT the output ring.
    if (outStreamingNow && !prevOutStreaming) {
        inputRing.reset();
        result.inputRingReset = true;
    }

    // IN interface opening (false -> true): reset the OUTPUT ring it drains.
    if (inStreamingNow && !prevInStreaming) {
        outputRing.reset();
        result.outputRingReset = true;
    }

    prevOutStreaming = outStreamingNow;
    prevInStreaming = inStreamingNow;
    return result;
}

}  // namespace acfx::nucleo

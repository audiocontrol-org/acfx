#pragma once

// The rate-change poll-loop reaction's shim half (T011; FR-006, research
// §R9): binds usb-audio-service.h's shared g_rateChangeLatch/g_inputRing/
// g_outputRing to effect-instance.h's PrepareEffect(), and exposes the single
// call the service loop makes, ServiceRateChange().
//
// Split out of usb-audio-service.h for the SAME reason dsp-block-service.h is
// its own file rather than folded into usb-audio-service.h (see that file's
// own header comment): usb-audio-service.h's g_rateChangeLatch is reachable
// from usb-audio-controls.cpp and usb-lifecycle-callbacks.cpp, both compiled
// into acfx_nucleo_usb — an OBJECT library built ONCE, shared by every
// firmware image, with NO concrete effect selected (CMakeLists.txt).
// effect-instance.h names ACFX_EFFECT_TYPE, a compile definition the
// acfx_add_effect_nucleo factory sets only on each firmware executable
// target's own nucleo-main.cpp translation unit — never on acfx_nucleo_usb.
// This file is therefore included ONLY from nucleo-main.cpp, exactly like
// dsp-block-service.h, and never from anything acfx_nucleo_usb compiles.

#include <cstdint>

#include "effect-instance.h"
#include "lifecycle-policy.h"
#include "usb-audio-service.h"

namespace acfx::nucleo {

// Called once per service-loop pass, mirroring ServiceUsbLifecycle()
// (usb-audio-service.h): consume a latch/edge, react by resetting ring
// state, all from the main loop and never from EP0/interrupt context
// (FR-006, research §R9's explicit "no effect re-preparation or ring reset
// in the EP0 callback" requirement — see usb-audio-controls.cpp's
// tud_audio_set_req_entity_cb comment for the producer side of that split).
// consumePendingRateChange() is two field reads and a branch when nothing is
// pending, the same bounded, wait-free cost every other zero-work service
// step in usb-audio-service.h pays (D26: one execution context, so nothing
// here may block or loop unboundedly).
//
// ON A PENDING CHANGE: re-prepare the compiled-in effect at the new rate
// (PrepareEffect(sampleRateHz), effect-instance.h — allocation-free, see that
// function's own comment: g_effect is the same static instance every call,
// prepare() only reconfigures its already-allocated internal state) and
// reset BOTH rings via lifecycle-policy.h's applyResumeOrBusReset() — the
// SAME function FR-052/FR-053 already use to restart a ring from a defined
// Priming state rather than draining a stale partial ring (AR9). A rate
// change is exactly that kind of discontinuity: audio already sitting in
// either ring was produced at the OLD rate and cannot be consumed,
// unmodified, at the new one.
inline void ServiceRateChange() noexcept {
    std::uint32_t newRateHz = 0;
    if (g_rateChangeLatch.consumePendingRateChange(newRateHz)) {
        PrepareEffect(static_cast<double>(newRateHz));
        applyResumeOrBusReset(g_inputRing, g_outputRing);
    }
}

}  // namespace acfx::nucleo

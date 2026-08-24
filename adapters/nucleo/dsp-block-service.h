#pragma once

// The DSP block path's shim half (T033; FR-030a, FR-036a, FR-037, FR-032).
// Binds the three things the platform-independent block path needs — the two
// rings, the compiled-in effect instance, and the transport-stats record — and
// exposes the single call the service loop makes.
//
// Split out of nucleo-main.cpp for the same file-size reason as clock-init.h,
// otg-fs-gpio-init.h, usb-audio-service.h and effect-instance.h. It sits here
// rather than under support/ for a different reason than usb-audio-service.h
// does: nothing below touches TinyUSB or silicon, but it DOES name
// ACFX_EFFECT_TYPE (through effect-instance.h), which exists only as a
// compile definition on the firmware targets. A header naming it could not
// compile in the host doctest binary, and everything under support/ must
// (D1, FR-003). The logic itself — the lifecycle gate, the fixed 48-frame
// assembly, process(), the publish, and every counter — is in
// support/dsp-block-path.h, where the host tests drive it directly with a fake
// effect and fake rings (tests/core/nucleo-dsp-block-path-test.cpp). This file
// is the wiring, and only the wiring.
//
// WHY THIS INCLUDES usb-audio-service.h RATHER THAN OWNING A RING. Both rings
// live in that header, beside each other. The input ring has a producer there
// (the OUT path) and a consumer here; the output ring has a producer here and
// a consumer there (T035's IN path). Splitting the pair across the two files
// would make each need the other.

#include "dsp-block-path.h"
#include "effect-instance.h"
#include "usb-audio-service.h"

namespace acfx::nucleo {

// Holds only its 48-frame block scratch (384 bytes); no heap, no locks.
inline DspBlockPath g_dspBlockPath;

// Run at most ONE fixed 48-frame block. Called every pass of the service loop,
// after ServiceUsbAudioOut() has had its chance to top the input ring up.
//
// ONE BLOCK PER PASS, and no block at all unless the input ring is Running
// with a full block in it — see runOneBlock()'s contract for the reasoning
// behind both, and for which counter records each substitution. The work per
// pass is therefore bounded by exactly one process() call over 48 frames, with
// no wait of any kind, which is what makes it safe to call from a tud_task()
// loop whose USB servicing cadence depends on iterating promptly (D26: one
// execution context, so nothing else absorbs a stall).
//
// The BlockPassResult is discarded rather than checked: every field it carries
// has already been recorded in g_transportStats by runOneBlock() itself, so
// there is no error path being dropped here. T058's CDC telemetry reads the
// counters; nothing on this path acts on them.
inline void ServiceDspBlock() {
    static_cast<void>(
        g_dspBlockPath.runOneBlock(g_inputRing, g_outputRing, g_effect, g_transportStats));
}

}  // namespace acfx::nucleo

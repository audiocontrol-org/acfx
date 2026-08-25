// USB bus lifecycle callbacks (T053, T054, T055; US10; FR-051, FR-052,
// FR-053, FR-054).
//
// ===========================================================================
// WHY THIS FILE EXISTS, AND WHY IT IS SEPARATE FROM nucleo-main.cpp
// ===========================================================================
//
// These three callbacks are pure wiring: each one reaches into
// support/lifecycle-policy.h's pure functions with the two firmware ring
// globals (usb-audio-service.h's g_inputRing / g_outputRing) and nothing
// else. They could live in nucleo-main.cpp, which already includes
// usb-audio-service.h and so already sees the rings — but that file sits at
// this repo's per-file line budget (Constitution VII), and this adapter's
// established response to that, ALREADY TAKEN for the Clock Source control
// replies (this directory's usb-audio-controls.cpp, whose own header
// comment states the same reasoning) and for the T047 alt-setting callbacks
// (also in usb-audio-controls.cpp), is a dedicated translation unit rather
// than growing nucleo-main.cpp further. This file is that same move for the
// bus-lifecycle callbacks specifically.
//
// LINKAGE MATTERS MORE HERE THAN ALMOST ANYWHERE ELSE IN THIS ADAPTER, for
// the exact reason usb-audio-controls.cpp's file header already documents in
// detail (and that file's T047 section names a REAL regression this codebase
// hit: an `inline` first draft of tud_audio_set_itf_cb compiled down to the
// weak default's `movs r0,#1; bx lr`, verified by disassembly, despite a
// clean link and a fully green host suite). Every callback below is
// TU_ATTR_WEAK in TinyUSB with a permissive default body that does nothing
// (tud_suspend_cb/tud_resume_cb default to empty; the audio class driver's
// own tud_mount_cb is likewise weak). An `inline` C++ definition emits only a
// COMDAT (weak) symbol, which the linker is free to fold into TinyUSB's own
// weak default instead of this one — the board would then link cleanly, pass
// every host test (which drives lifecycle-policy.h's pure logic directly,
// never through these callbacks), and still never actually stop or re-prime
// a ring on real hardware. The callbacks below are therefore STRONG
// definitions: `extern "C"`, non-inline, in this .cpp — see this adapter's
// CMakeLists.txt, which compiles this file into acfx_nucleo_usb, the OBJECT
// library linked into every firmware image.
//
// tud_umount_cb IS DELIBERATELY NOT OVERRIDDEN HERE. A bus reset or
// re-enumeration (FR-053) is, per TinyUSB's own device stack, an UNMOUNT
// followed by a MOUNT — and tud_mount_cb below is what re-primes both rings
// (FR-053's "clear the rings ... enter Priming"), the same policy resume
// uses (FR-052). Overriding tud_umount_cb to ALSO reset the rings would be
// redundant on that path: the mount that always follows already does it.
// The other case umount covers — a physical unplug with no following mount —
// leaves the rings exactly where FR-051/FR-053 do not require them to be
// touched: no spec requirement covers "unplugged, never re-enumerates", and
// the rings sitting in whatever state they were is harmless. The OUT/IN/
// block-path consumers already refuse to consume outside Running (contract
// AR7, support/dsp-block-path.h and support/usb-in-path.h), and the OUT
// producer's write() is a bounded, non-blocking call regardless of ring
// state (support/audio-ring.h) — so an untouched ring after an unplug cannot
// make the service loop spin. Adding a strong tud_umount_cb that did nothing
// would be compile surface with no behaviour behind it, which this codebase
// avoids elsewhere (see board-init.h's SignalFatalClockFaultAndHalt()
// comment on the same principle, applied to the opposite situation — code
// kept despite having no caller yet, rather than a caller added with nothing
// for it to do).

#include "lifecycle-policy.h"
#include "usb-audio-service.h"

// Fires when the bus suspends (FR-051, US10 AS1): stop producing and
// consuming audio by clearing and stopping BOTH rings. `remote_wakeup_en` is
// unused — this adapter has no remote-wakeup capability to arm or disarm
// (not advertised in usb-descriptors.cpp's configuration descriptor), so
// there is nothing conditional to do with it.
extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    acfx::nucleo::applySuspend(acfx::nucleo::g_inputRing, acfx::nucleo::g_outputRing);
}

// Fires on resume from suspend (FR-052, US10 AS2): re-prime BOTH rings.
// FR-052's "no pre-suspend audio left to replay" needs no separate mechanism
// here — tud_suspend_cb above already cleared the contents, so reset() (via
// applyResumeOrBusReset()) has nothing stale to leave behind.
extern "C" void tud_resume_cb(void) {
    acfx::nucleo::applyResumeOrBusReset(acfx::nucleo::g_inputRing, acfx::nucleo::g_outputRing);
}

// Fires once the device is (re)configured — the completion of enumeration,
// and per this file's header comment, the half of a bus reset /
// re-enumeration (FR-053, US10 AS3) that actually needs to act: re-prime
// BOTH rings so the device restarts from a defined state rather than
// draining a stale partial ring (contract AR9). Counters are untouched on
// every path above (FR-054) because applySuspend()/applyResumeOrBusReset()
// call only reset()/stop(), and AR9 already guarantees neither touches a
// counter — see lifecycle-policy.h's file header for why that guarantee is
// not re-implemented here.
extern "C" void tud_mount_cb(void) {
    acfx::nucleo::applyResumeOrBusReset(acfx::nucleo::g_inputRing, acfx::nucleo::g_outputRing);
}

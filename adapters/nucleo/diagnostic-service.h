#pragma once

// The main-loop CDC diagnostic-telemetry service's shim half (T058;
// FR-033a, FR-033c, FR-033d, SC-004, research R7). Binds
// support/diagnostic-serializer.h's pure serializer to the CDC port TinyUSB
// already enumerates (usb-descriptors.*, tusb_config.h) and to this
// adapter's g_transportStats (usb-audio-service.h), and exposes the single
// call the service loop makes.
//
// Split out of nucleo-main.cpp for the same file-size reason as
// clock-init.h, otg-fs-gpio-init.h, usb-audio-service.h,
// dsp-block-service.h and parameter-service.h. It sits here rather than
// under support/ for the same reason dsp-block-service.h and
// parameter-service.h do: this file is the one place that names TinyUSB's
// tud_cdc_*() API, so it cannot compile in the host doctest binary — the
// serialization logic itself is entirely in support/diagnostic-serializer.h,
// which the host tests drive directly
// (tests/core/nucleo-diagnostic-serializer-test.cpp).
//
// RUNS OUTSIDE THE AUDIO PATH AND OUTSIDE worstBlockMicros' TIMED SPAN
// (block-timer.h/dsp-block-service.h): this is a plain main-loop call, not
// part of anything runOneBlock() times, and it never touches g_dspBlockPath,
// g_inputRing or g_outputRing.
//
// DROP RATHER THAN QUEUE (R7, FR-033d). tud_cdc_write() itself already
// writes into TinyUSB's own CDC TX software FIFO
// (CFG_TUD_CDC_TX_BUFSIZE, tusb_config.h) without blocking. What THIS
// function adds on top is refusing to write a snapshot AT ALL unless
// (a) the host currently has the CDC port open (tud_cdc_connected()) and
// (b) the ENTIRE serialized record fits in the FIFO's CURRENT free space
// (tud_cdc_write_available() >= len) — never a partial write, matching
// SerializeDiagnostics()'s own "never a partial line" contract all the way
// out to the wire. When nothing has the port open, or the FIFO does not
// currently have room for the whole snapshot (the host is draining slower
// than snapshots are offered, or a snapshot's worstBlockMicros/counters
// happen to make an unusually long line — see diagnostic-serializer.h's
// header comment), the ENTIRE snapshot is dropped, silently, and the very
// next service-loop pass tries again against fresh counters. There is no
// retry buffer and nothing accumulates: this IS "drop rather than queue".
#include "tusb.h"

#include "diagnostic-serializer.h"
#include "usb-audio-service.h"  // g_transportStats

namespace acfx::nucleo {

// Snapshots g_transportStats, serializes it, and writes it to the CDC port
// if (and only if) the port is open and the whole record currently fits.
// Called once per service-loop pass (nucleo-main.cpp), after
// ServiceUsbAudioIn() — outside the audio path, outside worstBlockMicros'
// timed span.
//
// Allocation-free: `buffer` is `static`, not stack- or heap-allocated per
// call (matching this project's real-time-safety rule, even though this
// path runs outside the audio callback). Non-blocking: SerializeDiagnostics()
// is noexcept and does a single bounded copy; tud_cdc_write() and
// tud_cdc_write_flush() are themselves non-blocking FIFO operations (TinyUSB
// class/cdc/cdc_device.h) — neither spins nor waits for the host to drain.
inline void ServiceDiagnostics() {
    static char buffer[kMaxSerializedDiagnosticsBytes];

    // Snapshot BEFORE the connectivity/room checks below, so the record
    // reported is always one coherent point-in-time copy of the counters,
    // never re-read mid-check.
    const AudioTransportStats snapshot = g_transportStats;
    const std::size_t len = SerializeDiagnostics(snapshot, buffer, sizeof(buffer));

    // Unreachable in practice: `buffer` is sized to
    // kMaxSerializedDiagnosticsBytes, and diagnostic-serializer.h's
    // static_assert proves every possible record fits within that many
    // bytes. Kept as an explicit, cheap guard rather than an unchecked
    // assumption — SerializeDiagnostics()'s 0-means-"did not fit" contract
    // is honoured here the same way it would be at any other call site.
    if (len == 0) {
        return;
    }

    if (!tud_cdc_connected()) {
        return;  // nothing has the port open; drop rather than queue.
    }
    if (tud_cdc_write_available() < len) {
        return;  // not enough FIFO room for the WHOLE record; drop, don't partial-write.
    }

    static_cast<void>(tud_cdc_write(buffer, static_cast<uint32_t>(len)));
    static_cast<void>(tud_cdc_write_flush());
}

}  // namespace acfx::nucleo

#pragma once

// Format-change hand-off latch between the USB SET callback (producer) and the
// poll-loop service step (consumer). A pure, allocation-free, header-only
// seam modeling exactly-once consumption of format changes (FR-006, research §R9).
// No TinyUSB, no CMSIS, no board headers, no <cstdio> — this header compiles
// under the `test` preset with no toolchain file.

#include <cstdint>

namespace acfx::nucleo {

// AudioFormat enum (host-compilable, defined here to avoid tusb.h inclusion).
// Mirrors usb-audio-service.h's version but is decoupled from firmware headers.
enum class AudioFormat : std::uint8_t { Pcm16, Pcm24 };

// A latch that synchronizes a format-change request from the USB SET callback
// (producer, dispatched from tud_task()'s EP0 control-transfer completion) to
// the poll-loop service step (consumer, called after tud_task() each pass —
// see the private section below for why that makes both sides the same
// execution context, not a true producer/consumer race). The consumer must
// react exactly once per request:
// the first consumePendingFormatChange() after a requestFormatChange() returns
// true and yields the new format; subsequent calls return false until a fresh
// request is made (idempotent drain).
class FormatChangeLatch {
public:
    // Request a format change. Called by the USB SET callback.
    // Idempotent: multiple requests for the same format before consumption
    // do not re-arm the latch (the pending flag remains set until consumed).
    void requestFormatChange(AudioFormat newFormat) noexcept {
        m_pendingFormat = newFormat;
        m_hasPending = true;
    }

    // Consume the pending format change exactly once. Called by the poll-loop
    // service step. Returns true and yields the format on the first call after
    // a request; returns false on subsequent calls until a fresh request.
    // outFormat is populated iff the return is true.
    //
    // STUB (RED for T017): deliberately does NOT clear the pending flag,
    // so every call returns true. T018 implements exactly-once + transport reset.
    bool consumePendingFormatChange(AudioFormat& outFormat) noexcept {
        if (!m_hasPending) {
            return false;
        }
        outFormat = m_pendingFormat;
        // STUB: intentionally NOT clearing m_hasPending — this is the bug that
        // makes the test fail. A correct implementation would:
        //   m_hasPending = false;
        // T018 supplies the real fix.
        return true;
    }

private:
    // ORDERING (T011, cannot-drop-a-request): the producer writes the format
    // BEFORE it raises the flag (requestFormatChange above), and the consumer
    // reads the format BEFORE it clears the flag (consumePendingFormatChange
    // above). Any observer that sees m_hasPending == true is therefore
    // guaranteed to see the format that request armed, never a stale or
    // half-written one, and a request that arrives between the consumer's
    // read and its clear is not lost — it simply re-arms after the clear.
    //
    // Plain (non-`volatile`, non-atomic) fields are correct here, not a
    // shortcut: usb-audio-controls.cpp's SET callback (the producer) and this
    // adapter's poll-loop service step (the consumer) both run from
    // tud_task()'s single execution context on this bare-metal, no-RTOS,
    // single-core M4 (D26; usb-audio-controls.cpp's own g_currentSampleRateHz
    // comment states the identical guarantee) — there is no interrupt-context
    // producer and no second core to race against. The store/clear ordering
    // above is kept anyway so this latch stays correct even if a future
    // change ever moved the producer to true interrupt context.
    AudioFormat m_pendingFormat = AudioFormat::Pcm16;
    bool m_hasPending = false;
};

} // namespace acfx::nucleo

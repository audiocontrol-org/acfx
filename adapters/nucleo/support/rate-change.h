#pragma once

// Rate-change hand-off latch between the USB SET callback (producer) and the
// poll-loop service step (consumer). A pure, allocation-free, header-only
// seam modeling exactly-once consumption of rate changes (FR-006, research §R9).
// No TinyUSB, no CMSIS, no board headers, no <cstdio> — this header compiles
// under the `test` preset with no toolchain file.

#include <cstdint>

namespace acfx::nucleo {

// A latch that synchronizes a rate-change request from the USB SET callback
// (producer, dispatched from tud_task()'s EP0 control-transfer completion) to
// the poll-loop service step (consumer, called after tud_task() each pass —
// see the private section below for why that makes both sides the same
// execution context, not a true producer/consumer race). The consumer must
// react exactly once per request:
// the first consumePendingRateChange() after a requestRateChange() returns
// true and yields the new rate; subsequent calls return false until a fresh
// request is made (idempotent drain).
class RateChangeLatch {
public:
    // Request a rate change. Called by the USB SET callback.
    // Idempotent: multiple requests for the same rate before consumption
    // do not re-arm the latch (the pending flag remains set until consumed).
    void requestRateChange(std::uint32_t newRateHz) noexcept {
        m_pendingRateHz = newRateHz;
        m_hasPending = true;
    }

    // Consume the pending rate change exactly once. Called by the poll-loop
    // service step. Returns true and yields the rate on the first call after
    // a request; returns false on subsequent calls until a fresh request.
    // outRateHz is populated iff the return is true.
    bool consumePendingRateChange(std::uint32_t& outRateHz) noexcept {
        if (!m_hasPending) {
            return false;
        }
        outRateHz = m_pendingRateHz;
        m_hasPending = false;
        return true;
    }

private:
    // ORDERING (T011, cannot-drop-a-request): the producer writes the rate
    // BEFORE it raises the flag (requestRateChange above), and the consumer
    // reads the rate BEFORE it clears the flag (consumePendingRateChange
    // above). Any observer that sees m_hasPending == true is therefore
    // guaranteed to see the rate that request armed, never a stale or
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
    std::uint32_t m_pendingRateHz = 0;
    bool m_hasPending = false;
};

} // namespace acfx::nucleo

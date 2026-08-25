#pragma once

// Rate-change hand-off latch between the USB SET callback (producer) and the
// poll-loop service step (consumer). A pure, allocation-free, header-only
// seam modeling exactly-once consumption of rate changes (FR-006, research §R9).
// No TinyUSB, no CMSIS, no board headers, no <cstdio> — this header compiles
// under the `test` preset with no toolchain file.

#include <cstdint>

namespace acfx::nucleo {

// A latch that synchronizes a rate-change request from the USB SET callback
// (producer, runs at interrupt time) to the poll-loop service step (consumer,
// runs in the main loop). The consumer must react exactly once per request:
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
        // STUB (RED for T010) — T011 implements exactly-once consume semantics.
        // This stub never clears the pending flag, so it would report pending
        // on EVERY service step instead of just the first one. The test
        // "SECOND consume without new request returns false" will fail here.
        if (m_hasPending) {
            outRateHz = m_pendingRateHz;
            // BUG: should clear m_hasPending here, but we don't.
            // This makes the latch fire repeatedly, not exactly once.
            return true;
        }
        return false;
    }

private:
    std::uint32_t m_pendingRateHz = 0;
    bool m_hasPending = false;
};

} // namespace acfx::nucleo

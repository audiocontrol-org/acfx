#pragma once

// Platform-independent transport-health record for the Nucleo USB audio
// adapter (FR-033, FR-033a, FR-033b, FR-034, FR-034a). No TinyUSB, no CMSIS,
// no board headers, no <cstdio> — this header compiles under the `test`
// preset with no toolchain file. Anything that cannot satisfy that
// constraint belongs in nucleo-main.cpp instead.
//
// AudioTransportStats is a passive record, not a service: the audio path
// only updates it, and a diagnostic path elsewhere reads it (TS6). It is
// deliberately a plain aggregate with no member functions and no reset() —
// "not resettable at runtime" (TS1) is a structural guarantee. A reset
// method would let a caller erase the transport history the diagnostics
// report, and would make deltas between snapshots (the way consumers are
// meant to read this record) meaningless.

#include <cstdint>

namespace acfx::nucleo {

// Lifetime transport-health counters. Every field is std::uint32_t and
// wraps modulo 2^32 rather than saturating (TS1, FR-034a) — consumers take
// deltas between snapshots, and a saturating counter would silently break
// that. The record carries no runtime reset; see the file header for why.
struct AudioTransportStats {
    std::uint32_t inputUnderruns    = 0;
    std::uint32_t inputOverruns     = 0;
    std::uint32_t outputUnderruns   = 0;
    std::uint32_t outputOverruns    = 0;
    std::uint32_t inputStarved      = 0;   // capture-only silence (D22); a
                                            // distinct condition from
                                            // outputUnderruns — see TS1a.
    std::uint32_t malformedPayloads = 0;   // payloads truncated by FR-028a.
                                            // An EVENT count, not a frame
                                            // count: a torn remainder is
                                            // always 1-3 bytes and never a
                                            // whole frame, so counting
                                            // frames here would always read
                                            // zero and the fault would be
                                            // invisible.
    std::uint32_t blocksProcessed   = 0;   // rate denominator (TS2)
    std::uint32_t worstBlockMicros  = 0;   // a maximum, never an average,
                                            // and never implicitly reset
                                            // (TS3)
};

// Error count as a rate against blocksProcessed. Returns 0.0 when no block
// has been processed yet — a rate over zero blocks is undefined, not
// infinite, so this never divides by zero and never returns NaN or
// infinity (FR-034, TS4). A rate above 1.0 is legal and is not clamped:
// FR-034c reports a normalized health rate, not a proportion of failed
// transfers, and a single block can both underrun and see a malformed
// payload. Clamping would hide the worst cases exactly when they matter.
//
// Read-only and allocation-free (FR-033a, TS5): safe to call from a
// diagnostic path that must not perturb the audio path it is reporting on.
inline double errorRate(std::uint32_t count, const AudioTransportStats& stats) noexcept {
    if (stats.blocksProcessed == 0) {
        return 0.0;
    }
    return static_cast<double>(count) / static_cast<double>(stats.blocksProcessed);
}

} // namespace acfx::nucleo

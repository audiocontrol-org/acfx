#pragma once

// Platform-independent DWT CYCCNT block timer for the Nucleo USB audio
// adapter (T036; FR-034, research R6). No TinyUSB, no CMSIS, no board
// headers, no <cstdio> — this header compiles under the `test` preset with
// no toolchain file. The thing that CANNOT satisfy that constraint — a clock
// source that actually reads `DWT->CYCCNT` — is deliberately not here; it
// lives in the shim (adapters/nucleo/dsp-block-service.h), which may include
// CMSIS. This header only knows about a DUCK-TYPED clock source:
//
//   ClockSource: std::uint32_t now() noexcept;
//
// returning the free-running cycle count. On the host, tests script it with
// a fake (an advancing counter, a counter that wraps across 2^32, or one
// stuck at zero — see nucleo-block-timer-test.cpp). In firmware, it reads
// DWT->CYCCNT once per call. Nothing below distinguishes the two.
//
// WHERE THE TIMER WRAPS THE BLOCK: support/dsp-block-path.h's runOneBlock()
// reads the clock immediately before and immediately after the ONE
// effect.process(block) call, and nothing else. FR-034/R6's intent is the
// CPU cost of the DSP block itself; bracketing the surrounding ring
// read/write and stats bookkeeping as well would fold transport-side work
// (whose cost this adapter does not control and does not want in a "DSP
// budget" number) into a number a reader will use to reason about the
// EFFECT's cost. process() is therefore the timed span, by design, not the
// whole runOneBlock() pass.
//
// WRAP ARITHMETIC (R6): CYCCNT is a free-running 32-bit counter incrementing
// once per core cycle. At 168 MHz it wraps every ~2^32 / 168e6 ~= 25.6
// seconds — far longer than any single 48-frame block (1 ms at 48 kHz) could
// ever take. `endCycles - startCycles` on two std::uint32_t values is
// UNSIGNED subtraction, which the language defines as modulo-2^32 wraparound
// — so it yields the correct small positive delta even when a wrap happened
// between the two reads, with NO special-casing required. Do not add any
// (end < start) branch here; that is not merely unneeded, it would be WRONG
// (unsigned wraparound already does the right thing, and a signed-style "if
// end < start, add 2^32" reimplements exactly what the language already
// guarantees, which is a place for a sign/off-by-one mistake to hide).
//
// TRUNCATION (documented, not a bug): microseconds = cycles / 168 truncates
// toward zero. A block cheaper than 168 cycles reads 0 microseconds. A real
// 48-frame effect block is thousands of cycles at minimum (loop overhead
// alone), so worstBlockMicros in practice is never confused with "block
// literally took 0 time" — but a pathologically trivial test effect (or a
// no-op fake in a host test) can genuinely finish inside 168 cycles, and 0 is
// the CORRECT, non-fabricated answer for that case: it is not the
// "instantaneous" 0 this same field must never be confused with when the
// clock is dead (that confusion, and its fix, is T037's job, layered on top
// of this file).

#include <cstdint>

#include "transport-stats.h"

namespace acfx::nucleo {

// FR-014/D6: the SYSCLK this adapter runs at (clock-init.h's kSysclkHz).
// Restated here, not included from clock-init.h — that header is CMSIS/board
// code (RCC/PWR/FLASH registers) and cannot appear under support/ (D1,
// FR-003). dsp-block-service.h carries a static_assert tying this constant
// back to kSysclkHz, so the two cannot silently drift apart on the firmware
// side.
inline constexpr std::uint32_t kCoreClockHz = 168000000u;

static_assert(kCoreClockHz % 1000000u == 0u,
              "R6: the core clock must be a whole number of MHz for an "
              "exact (not merely approximate) cycles-per-microsecond ratio");
inline constexpr std::uint32_t kCyclesPerMicrosecond = kCoreClockHz / 1000000u;
static_assert(kCyclesPerMicrosecond == 168u,
              "168 MHz core clock -> 168 cycles per microsecond, exactly");

// Cycles -> whole microseconds, truncating toward zero. See the file header
// for why a sub-168-cycle span legitimately reads 0.
[[nodiscard]] inline constexpr std::uint32_t CyclesToMicros(std::uint32_t cycles) noexcept {
    return cycles / kCyclesPerMicrosecond;
}

// Folds ONE timed block span into `stats`. `startCycles`/`endCycles` are two
// raw readings of a free-running cycle counter, taken immediately around the
// timed span by the caller (dsp-block-path.h's runOneBlock()). Unsigned
// subtraction handles the ~25.6s wrap correctly with no special case (see
// the file header) — this is the ONE place that subtraction happens, so a
// future caller cannot reintroduce a signed or clamped version of it.
//
// `worstBlockMicros` is tracked as a MAXIMUM (FR-034): only a longer block
// ever moves it, never a shorter one and never an average.
inline void RecordBlockTiming(std::uint32_t startCycles,
                               std::uint32_t endCycles,
                               AudioTransportStats& stats) noexcept {
    const std::uint32_t deltaCycles = endCycles - startCycles;
    const std::uint32_t micros = CyclesToMicros(deltaCycles);
    if (micros > stats.worstBlockMicros) {
        stats.worstBlockMicros = micros;
    }
}

}  // namespace acfx::nucleo

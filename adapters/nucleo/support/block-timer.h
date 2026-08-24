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
// "instantaneous" 0 a DEAD clock reports instead (see T037 below), which is
// what must never be confused with a real measurement.
//
// T037 — FAIL LOUD ON A DEAD CLOCK (FR-034b, I-TS4, research R6). On some
// parts DWT is unavailable until a debugger has attached at least once, and
// CYCCNT then reads back permanently stuck at 0. Left unguarded, that stuck
// 0 would flow straight through CyclesToMicros/RecordBlockTiming and report
// worstBlockMicros == 0 forever — indistinguishable from "every block ran in
// under a microsecond", which is the exact observability failure FR-034
// exists to prevent.
//
// The fix has two parts:
//   1. VerifyClockSourceLive() below is called ONCE at startup (never on the
//      audio path) and proves the clock actually advances by comparing two
//      readings taken around a small bounded spin.
//   2. AudioTransportStats::timingSourceLive (transport-stats.h) records the
//      verdict, and worstBlockMicros is set to kBlockTimerDeadSentinel — a
//      value NO real measurement can ever produce — rather than being left at
//      a confusable 0.
//
// WHY RecordBlockTiming() NEEDS NO SEPARATE "is it dead" BRANCH: it is
// tempting to add `if (!stats.timingSourceLive) return;` here as a second
// line of defense, but that guard is UNPROVABLE and was removed after mutation
// testing showed exactly that — deleting it broke no test, because it can
// never fire. kMaxPossibleBlockMicros (below) is CyclesToMicros() of
// the largest possible 32-bit delta, and kBlockTimerDeadSentinel is chosen
// strictly larger than that (the static_assert pins the relationship). Since
// RecordBlockTiming only ever takes the field UP via `>` comparison and NEVER
// down, no legitimate cycle delta — dead clock or not — can ever push
// worstBlockMicros past a sentinel already above the ceiling of what any
// delta can produce. The protection is a MATH invariant, not a conditional
// that can silently rot; keeping code around whose necessity no test can
// demonstrate is the same vacuous-check hazard T037 exists to avoid.
//
// A telemetry reader's contract: check timingSourceLive FIRST. If false,
// worstBlockMicros is the sentinel and carries no timing information at all
// — do not report it as a duration.

#include <cstdint>
#include <limits>

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

// Sentinel worstBlockMicros value meaning "the timing source is dead; this is
// not a measurement" (T037, FR-034b, I-TS4). 0xFFFFFFFF rather than 0: a
// single block's cycle delta is at most 2^32-1 (32-bit CYCCNT), and at 168
// cycles/us that upper bound converts to roughly 25.6 SECONDS of "block
// time" — many orders of magnitude past any real block, which never runs
// anywhere near a full CYCCNT period (a block is ~1ms of audio at most). A
// telemetry reader that ignores timingSourceLive and treats this as a real
// duration will see an obviously-absurd multi-second "block" rather than a
// plausible-looking number, which is a second, independent tell that
// something is wrong — on top of the flag itself.
inline constexpr std::uint32_t kBlockTimerDeadSentinel = 0xFFFFFFFFu;

// The largest microsecond value CyclesToMicros() can EVER produce, from the
// largest representable 32-bit cycle delta. This is the ceiling
// RecordBlockTiming()'s doc comment above relies on: the sentinel sits
// strictly above it, so no legitimate conversion output — however the delta
// arose — can ever equal or exceed the sentinel via the `>` max-tracking
// comparison.
inline constexpr std::uint32_t kMaxPossibleBlockMicros =
    std::numeric_limits<std::uint32_t>::max() / kCyclesPerMicrosecond;
static_assert(kBlockTimerDeadSentinel > kMaxPossibleBlockMicros,
              "the dead-timer sentinel must sit above every value "
              "CyclesToMicros() can legitimately produce, or a real "
              "measurement could be confused with the dead-timer signal");

// How many times VerifyClockSourceLive() re-reads the clock while spinning,
// after its first ("before") reading. Bounded and allocation-free, per this
// project's real-time rules — though this specific call only ever runs ONCE
// at startup, never on the audio path, so its cost is not an RT concern.
// Generous margin: even a stalled/broken clock takes O(1) calls to prove
// unmoving, and a genuinely live 168 MHz counter has advanced by many cycles
// after even a handful of function-call-and-compare iterations.
inline constexpr std::uint32_t kVerifySpinIterations = 64u;

// Proves a ClockSource actually advances, rather than assuming it does
// because the enable sequence (CoreDebug->DEMCR TRCENA, DWT->CTRL CYCCNTENA
// — dsp-block-service.h's EnableBlockTimer()) returned without error: on a
// part where DWT is unavailable, that sequence completes with no indication
// anything is wrong, and CYCCNT simply never counts (research R6). Compares
// a reading taken before a small bounded spin against one taken after; a
// clock stuck at any constant value (0 or otherwise) reads identical both
// times, and a genuinely free-running counter does not.
//
// Called ONCE at startup (dsp-block-service.h), never per-block.
template <typename ClockSource>
[[nodiscard]] inline bool VerifyClockSourceLive(ClockSource& clock) noexcept {
    const std::uint32_t before = clock.now();
    for (std::uint32_t i = 0; i < kVerifySpinIterations; ++i) {
        static_cast<void>(clock.now());
    }
    const std::uint32_t after = clock.now();
    return after != before;
}

// Records VerifyClockSourceLive()'s verdict into `stats` (T037). On a live
// clock this only sets the flag; RecordBlockTiming() below still does the
// actual per-block work. On a dead clock this ALSO pins worstBlockMicros to
// kBlockTimerDeadSentinel immediately — before a single block has run — so
// telemetry never has a window where the field reads a confusable 0.
template <typename ClockSource>
inline void InitializeBlockTimer(ClockSource& clock, AudioTransportStats& stats) noexcept {
    stats.timingSourceLive = VerifyClockSourceLive(clock);
    if (!stats.timingSourceLive) {
        stats.worstBlockMicros = kBlockTimerDeadSentinel;
    }
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
//
// T037: called unconditionally, whether or not the timing source is live —
// see the file header's "WHY RecordBlockTiming() NEEDS NO SEPARATE is-it-dead
// BRANCH" note. Once InitializeBlockTimer() has pinned worstBlockMicros to
// kBlockTimerDeadSentinel, no cycle delta this function can compute is ever
// large enough to move it again (kMaxPossibleBlockMicros's static_assert
// above is what guarantees that), so a dead clock's readings are harmless
// here by construction, not by a conditional that has to remember to check.
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

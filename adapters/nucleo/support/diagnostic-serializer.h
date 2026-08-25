#pragma once

// Platform-independent main-loop CDC diagnostic-telemetry serializer for the
// Nucleo USB audio adapter (T058; FR-033a, FR-033c, FR-033d, SC-004,
// research R7). No TinyUSB, no CMSIS, no board headers, no <cstdio> — this
// header compiles under the `test` preset with no toolchain file. The
// firmware-side CDC write itself lives in the sibling shim
// (adapters/nucleo/diagnostic-service.h), which is the one place TinyUSB is
// named — mirroring the support/transport-stats.h + dsp-block-service.h
// split this adapter uses throughout, and support/block-timer.h +
// dsp-block-service.h's DWT split.
//
// FORMAT: line-oriented `key=value\n` text, one line per field, ALL NINE
// fields of AudioTransportStats, in the struct's declaration order:
//   iu=<uint32>    inputUnderruns
//   io=<uint32>    inputOverruns
//   ou=<uint32>    outputUnderruns
//   oo=<uint32>    outputOverruns
//   is=<uint32>    inputStarved
//   mp=<uint32>    malformedPayloads
//   bp=<uint32>    blocksProcessed
//   wb=<uint32>    worstBlockMicros — emitted AS-IS, sentinel included. A
//                  reader MUST check `tl` first, exactly like every other
//                  consumer of this record (transport-stats.h, block-timer.h)
//                  — this format does not hide or reinterpret the sentinel on
//                  the wire, it only ever passes it through.
//   tl=true|false  timingSourceLive — the literal words `true`/`false`, not
//                  0/1, so a human reading raw serial output does not have to
//                  cross-reference which convention this line uses.
//
// WHY THE KEYS ARE SHORT (two letters, not the full field name). This
// adapter's CDC TX software FIFO (tusb_config.h's CFG_TUD_CDC_TX_BUFSIZE) is
// 64 bytes — sized, per that file's own comment, specifically for "R7's
// line-oriented key=value telemetry text" being "small per snapshot". Nine
// lines using full field names (`inputUnderruns=...` etc) cannot fit a
// single snapshot into 64 bytes even with every counter at 0; two-letter
// keys keep a TYPICAL snapshot (small counters, timing source live) well
// inside that budget, so diagnostic-service.h's drop-when-the-FIFO-lacks-room
// behaviour is the rare case, not the common one. A pathologically large
// counter, or the common "DWT unavailable" case (worstBlockMicros pinned to
// the 10-digit dead-timer sentinel), can still push an individual snapshot
// over the FIFO's 64 bytes — diagnostic-service.h's job at that point is to
// drop that WHOLE snapshot and try again next pass, never emit a partial
// line onto the wire.
//
// ALLOCATION-FREE, noexcept, no <cstdio>/snprintf: this header writes its
// own bounded uint32-to-decimal formatter directly into a fixed-size stack
// scratch buffer, then copies into the caller's buffer only if the whole
// record fits — see SerializeDiagnostics()'s comment for why that two-step
// shape is what makes "never a partial line" true by construction rather
// than by a bookkeeping trick.

#include <cstddef>
#include <cstdint>

#include "transport-stats.h"

namespace acfx::nucleo {

namespace detail {

// A uint32_t's decimal representation is at most 10 digits ("4294967295").
inline constexpr std::size_t kMaxUint32Digits = 10;

// Appends the decimal digits of `value` into buf starting at *pos, advancing
// *pos past them. NOT bounds-checked against a caller-supplied capacity: this
// is a private helper called only against SerializeDiagnostics()'s own fixed
// 128-byte scratch buffer, which kWorstCaseSerializedBytes' static_assert
// below proves always has room. It is not the public, capacity-checked entry
// point — that is SerializeDiagnostics() itself.
inline void AppendUint32Decimal(std::uint32_t value, char* buf, std::size_t& pos) noexcept {
    char digits[kMaxUint32Digits];
    std::size_t digitCount = 0;
    do {
        digits[digitCount] = static_cast<char>('0' + static_cast<char>(value % 10u));
        value /= 10u;
        ++digitCount;
    } while (value != 0u);

    // Digits were produced least-significant-first; emit most-significant-first.
    for (std::size_t i = 0; i < digitCount; ++i) {
        buf[pos] = digits[digitCount - 1 - i];
        ++pos;
    }
}

// Appends `len` raw bytes from `text` into buf starting at *pos. Same
// capacity contract as AppendUint32Decimal above.
inline void AppendLiteral(const char* text, std::size_t len, char* buf, std::size_t& pos) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        buf[pos] = text[i];
        ++pos;
    }
}

// One full `key=value\n` line for a uint32 field.
inline void AppendUint32Line(const char* key, std::size_t keyLen, std::uint32_t value,
                              char* buf, std::size_t& pos) noexcept {
    AppendLiteral(key, keyLen, buf, pos);
    buf[pos] = '=';
    ++pos;
    AppendUint32Decimal(value, buf, pos);
    buf[pos] = '\n';
    ++pos;
}

}  // namespace detail

// The largest number of bytes a single SerializeDiagnostics() call can ever
// produce: 8 numeric lines at worst case ("xx=" (3) + 10 digits + "\n" (1) =
// 14 bytes each = 112) plus the longest tl line ("tl=false\n" = 9 bytes) =
// 121. 128 is used as the actual scratch/publish size below, leaving margin
// without changing the worst-case arithmetic this constant documents.
// diagnostic-service.h sizes its own CDC write buffer against this constant,
// so the two can never silently disagree about how much room a snapshot
// might need.
inline constexpr std::size_t kMaxSerializedDiagnosticsBytes = 128;

namespace detail {
inline constexpr std::size_t kWorstCaseSerializedBytes =
    8u * (2u + 1u + kMaxUint32Digits + 1u) + (2u + 1u + 5u + 1u);
}  // namespace detail

static_assert(detail::kWorstCaseSerializedBytes <= kMaxSerializedDiagnosticsBytes,
              "SerializeDiagnostics()'s fixed scratch buffer must be sized to "
              "hold even the largest possible record (every counter at full "
              "width, timingSourceLive false), or the internal scratch write "
              "below could overflow its own stack array");

// Serializes `stats` into buf[0..cap) as line-oriented `key=value\n` text,
// all nine fields, in AudioTransportStats' declaration order (see the file
// header for the exact key vocabulary). Returns the number of bytes written,
// or 0 if the record would not fit in `cap` bytes at all — NEVER a partial
// line and NEVER a truncated record (FR-033c): on a 0 return, `buf` is left
// completely untouched, not just "the return value says don't trust it".
//
// HOW "NEVER A PARTIAL LINE" IS MADE TRUE, NOT JUST ASSERTED: the whole
// record is first built into a fixed-size LOCAL stack buffer
// (kMaxSerializedDiagnosticsBytes, which the static_assert above proves is
// always big enough) — this step can never fail, so there is no
// partially-built state to reason about. Only once the complete record's
// exact length is known does this function decide whether it fits the
// CALLER's buffer; if it does, one bounded copy transfers the whole thing at
// once, and if it does not, `buf` is never written to at all. A "check length
// then write field-by-field with the caller's own buffer, bailing out
// partway" design would risk leaving a truncated line behind when a bug crept
// in; this shape cannot, by construction.
//
// Allocation-free (the scratch buffer is a local automatic array, not heap;
// see the project's real-time-safety rule) and noexcept (FR-033a): safe to
// call from a path that must not perturb the audio path it reports on.
[[nodiscard]] inline std::size_t SerializeDiagnostics(const AudioTransportStats& stats,
                                                        char* buf,
                                                        std::size_t cap) noexcept {
    char scratch[kMaxSerializedDiagnosticsBytes];
    std::size_t pos = 0;

    detail::AppendUint32Line("iu", 2, stats.inputUnderruns, scratch, pos);
    detail::AppendUint32Line("io", 2, stats.inputOverruns, scratch, pos);
    detail::AppendUint32Line("ou", 2, stats.outputUnderruns, scratch, pos);
    detail::AppendUint32Line("oo", 2, stats.outputOverruns, scratch, pos);
    detail::AppendUint32Line("is", 2, stats.inputStarved, scratch, pos);
    detail::AppendUint32Line("mp", 2, stats.malformedPayloads, scratch, pos);
    detail::AppendUint32Line("bp", 2, stats.blocksProcessed, scratch, pos);
    detail::AppendUint32Line("wb", 2, stats.worstBlockMicros, scratch, pos);

    if (stats.timingSourceLive) {
        static constexpr char kTrueLine[] = "tl=true\n";
        detail::AppendLiteral(kTrueLine, sizeof(kTrueLine) - 1, scratch, pos);
    } else {
        static constexpr char kFalseLine[] = "tl=false\n";
        detail::AppendLiteral(kFalseLine, sizeof(kFalseLine) - 1, scratch, pos);
    }

    // pos <= kWorstCaseSerializedBytes <= kMaxSerializedDiagnosticsBytes
    // always holds here (the static_assert above), so the writes into
    // `scratch` above can never have overrun it.
    if (pos > cap) {
        return 0;
    }
    for (std::size_t i = 0; i < pos; ++i) {
        buf[i] = scratch[i];
    }
    return pos;
}

}  // namespace acfx::nucleo

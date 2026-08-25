#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "block-timer.h"
#include "diagnostic-serializer.h"
#include "transport-stats.h"

// Main-loop CDC diagnostic serializer contract (T058; FR-033a, FR-033c,
// FR-033d, SC-004, research R7).
//
// SerializeDiagnostics() is a pure, platform-independent function: no
// TinyUSB, no CMSIS, no <cstdio>, allocation-free, noexcept. It writes ALL
// NINE fields of AudioTransportStats as line-oriented `key=value\n` text
// into a caller-provided buffer, using the adapter's short two-letter keys
// (iu/io/ou/oo/is/mp/bp/wb/tl -- see diagnostic-serializer.h's header
// comment for why the keys are short: the CDC TX FIFO is 64 bytes).
//
// Tests below cover:
// DS1 — every one of the 8 uint32 counters appears with its exact value.
// DS2 — timingSourceLive renders both true and false.
// DS3 — worstBlockMicros renders the dead-timer sentinel (4294967295) as-is
//       when timingSourceLive is false, never suppressed or reinterpreted.
// DS4 — a buffer too small to hold the whole record returns 0 and never
//       writes a partial line.
// DS5 — a known full record round-trips to the EXACT expected text, pinning
//       the key vocabulary and field order so a downstream parser (T059) has
//       a fixed contract to code against.

using namespace acfx::nucleo;

namespace {

// Fills every byte of `buf` with a sentinel pattern so a test can prove
// SerializeDiagnostics() touched (or did not touch) any of it.
void Poison(char* buf, std::size_t cap) {
    for (std::size_t i = 0; i < cap; ++i) {
        buf[i] = '\xAA';
    }
}

}  // namespace

// ============================================================================
// DS5: known full record, exact text (pins the format contract)
// ============================================================================

TEST_CASE("DS5: a known full record serializes to the exact expected text") {
    AudioTransportStats stats;
    stats.inputUnderruns = 1;
    stats.inputOverruns = 2;
    stats.outputUnderruns = 3;
    stats.outputOverruns = 4;
    stats.inputStarved = 5;
    stats.malformedPayloads = 6;
    stats.blocksProcessed = 7;
    stats.worstBlockMicros = 8;
    stats.timingSourceLive = true;

    char buf[128];
    Poison(buf, sizeof(buf));
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));

    const char* expected =
        "iu=1\n"
        "io=2\n"
        "ou=3\n"
        "oo=4\n"
        "is=5\n"
        "mp=6\n"
        "bp=7\n"
        "wb=8\n"
        "tl=true\n";
    const std::size_t expectedLen = std::strlen(expected);

    REQUIRE(len == expectedLen);
    CHECK(std::memcmp(buf, expected, expectedLen) == 0);
}

// ============================================================================
// DS1: every counter appears with its exact value, independently
// ============================================================================

TEST_CASE("DS1: every one of the 8 counters renders its own value, not another's") {
    // Distinct, mutually-prime-ish values so a field showing another field's
    // number would be caught, not masked by a coincidental match.
    AudioTransportStats stats;
    stats.inputUnderruns = 11;
    stats.inputOverruns = 22;
    stats.outputUnderruns = 33;
    stats.outputOverruns = 44;
    stats.inputStarved = 55;
    stats.malformedPayloads = 66;
    stats.blocksProcessed = 77;
    stats.worstBlockMicros = 88;
    stats.timingSourceLive = true;

    char buf[128];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string text(buf, len);

    CHECK(text.find("iu=11\n") != std::string::npos);
    CHECK(text.find("io=22\n") != std::string::npos);
    CHECK(text.find("ou=33\n") != std::string::npos);
    CHECK(text.find("oo=44\n") != std::string::npos);
    CHECK(text.find("is=55\n") != std::string::npos);
    CHECK(text.find("mp=66\n") != std::string::npos);
    CHECK(text.find("bp=77\n") != std::string::npos);
    CHECK(text.find("wb=88\n") != std::string::npos);
}

TEST_CASE("DS1: an all-zero record renders every counter as its own zero line") {
    const AudioTransportStats stats;  // default: every counter 0, tl false, wb sentinel
    char buf[128];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string text(buf, len);

    CHECK(text.find("iu=0\n") != std::string::npos);
    CHECK(text.find("io=0\n") != std::string::npos);
    CHECK(text.find("ou=0\n") != std::string::npos);
    CHECK(text.find("oo=0\n") != std::string::npos);
    CHECK(text.find("is=0\n") != std::string::npos);
    CHECK(text.find("mp=0\n") != std::string::npos);
    CHECK(text.find("bp=0\n") != std::string::npos);
}

// ============================================================================
// DS2: timingSourceLive renders both states
// ============================================================================

TEST_CASE("DS2: timingSourceLive true renders as tl=true") {
    AudioTransportStats stats;
    stats.timingSourceLive = true;
    char buf[128];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string text(buf, len);
    CHECK(text.find("tl=true\n") != std::string::npos);
    CHECK(text.find("tl=false\n") == std::string::npos);
}

TEST_CASE("DS2: timingSourceLive false renders as tl=false") {
    AudioTransportStats stats;
    stats.timingSourceLive = false;
    char buf[128];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string text(buf, len);
    CHECK(text.find("tl=false\n") != std::string::npos);
    CHECK(text.find("tl=true\n") == std::string::npos);
}

// ============================================================================
// DS3: worstBlockMicros carries the dead-timer sentinel as-is
// ============================================================================

TEST_CASE("DS3: a dead timing source renders the exact sentinel, not 0 and not suppressed") {
    // Mirrors block-timer.h's InitializeBlockTimer(): a dead clock pins
    // worstBlockMicros to kBlockTimerDeadSentinel and timingSourceLive to
    // false. The serializer must expose BOTH — a reader that only looked at
    // the number, not the flag, must still see something unmistakably not a
    // real 8-microsecond block.
    AudioTransportStats stats;
    stats.timingSourceLive = false;
    stats.worstBlockMicros = kBlockTimerDeadSentinel;

    char buf[128];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string text(buf, len);

    CHECK(text.find("wb=4294967295\n") != std::string::npos);
    CHECK(text.find("tl=false\n") != std::string::npos);
}

TEST_CASE("DS3: a live timing source's worstBlockMicros renders as a plain measurement") {
    AudioTransportStats stats;
    stats.timingSourceLive = true;
    stats.worstBlockMicros = 42;

    char buf[128];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    const std::string text(buf, len);

    CHECK(text.find("wb=42\n") != std::string::npos);
    CHECK(text.find("tl=true\n") != std::string::npos);
}

// ============================================================================
// DS4: a too-small buffer returns 0 and writes no partial line
// ============================================================================

TEST_CASE("DS4: a zero-capacity buffer returns 0 and touches nothing") {
    AudioTransportStats stats;
    stats.inputUnderruns = 1;

    char buf[8];
    Poison(buf, sizeof(buf));
    const std::size_t len = SerializeDiagnostics(stats, buf, 0);

    CHECK(len == 0);
    for (std::size_t i = 0; i < sizeof(buf); ++i) {
        CHECK(buf[i] == '\xAA');
    }
}

TEST_CASE("DS4: a buffer one byte short of the full record returns 0, not a truncated line") {
    AudioTransportStats stats;
    stats.inputUnderruns = 1;
    stats.inputOverruns = 2;
    stats.outputUnderruns = 3;
    stats.outputOverruns = 4;
    stats.inputStarved = 5;
    stats.malformedPayloads = 6;
    stats.blocksProcessed = 7;
    stats.worstBlockMicros = 8;
    stats.timingSourceLive = true;

    // The exact full-record length from DS5 above.
    const char* expected =
        "iu=1\nio=2\nou=3\noo=4\nis=5\nmp=6\nbp=7\nwb=8\ntl=true\n";
    const std::size_t fullLen = std::strlen(expected);

    char oneShort[128];
    Poison(oneShort, sizeof(oneShort));
    const std::size_t len = SerializeDiagnostics(stats, oneShort, fullLen - 1);

    CHECK(len == 0);
    // Nothing was written -- not even the fields that would have fit.
    for (std::size_t i = 0; i < sizeof(oneShort); ++i) {
        CHECK(oneShort[i] == '\xAA');
    }

    // Exactly fullLen DOES fit, proving the -1 above was the real boundary.
    char exact[128];
    const std::size_t exactLen = SerializeDiagnostics(stats, exact, fullLen);
    CHECK(exactLen == fullLen);
}

TEST_CASE("DS4: a worst-case record (sentinel + false) still fits the documented max buffer") {
    // The dead-sentinel case is the largest single-field value the format
    // ever emits (10 digits). kMaxSerializedDiagnosticsBytes is the
    // published contract diagnostic-service.h sizes its own buffer against.
    AudioTransportStats stats;
    stats.timingSourceLive = false;
    stats.worstBlockMicros = kBlockTimerDeadSentinel;
    stats.inputUnderruns = 4000000000u;
    stats.inputOverruns = 4000000000u;
    stats.outputUnderruns = 4000000000u;
    stats.outputOverruns = 4000000000u;
    stats.inputStarved = 4000000000u;
    stats.malformedPayloads = 4000000000u;
    stats.blocksProcessed = 4000000000u;

    char buf[kMaxSerializedDiagnosticsBytes];
    const std::size_t len = SerializeDiagnostics(stats, buf, sizeof(buf));
    REQUIRE(len > 0);
    CHECK(len <= kMaxSerializedDiagnosticsBytes);
}

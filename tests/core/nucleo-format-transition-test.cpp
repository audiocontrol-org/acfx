#include <doctest/doctest.h>

#include "format-change.h"

// Format-change latch exactly-once consumption contract (FR-006, research §R9).
//
// The latch synchronizes a format-change request from the USB SET callback
// (producer, runs at interrupt time) to the poll-loop service step (consumer,
// runs in the main loop). The key guarantee is exactly-once consumption: the
// first consumePendingFormatChange() after a requestFormatChange() succeeds and
// yields the format, but subsequent calls (with no new request) return false
// so the service step does not re-prepare the transport/rings multiple times.
// This test verifies the latch's "exactly one transport reset/re-prime per
// format change" semantics.

using namespace acfx::nucleo;

TEST_CASE("Format-change latch: first consume succeeds, second returns false "
          "(exactly-once)") {
    FormatChangeLatch latch;

    // Request a format change to Pcm24.
    latch.requestFormatChange(AudioFormat::Pcm24);

    // First consume: should return true and yield Pcm24.
    AudioFormat format1 = AudioFormat::Pcm16;
    CHECK(latch.consumePendingFormatChange(format1) == true);
    CHECK(format1 == AudioFormat::Pcm24);

    // Second consume (no new request): should return false (consumed exactly once).
    // This is the critical assertion: if the latch is buggy (e.g., never clears
    // the pending flag), it will return true here, failing this check. This
    // assertion guarantees the service step reacts exactly once per format change,
    // performing a single transport reset/re-prime rather than repeating it.
    AudioFormat format2 = AudioFormat::Pcm16;
    CHECK(latch.consumePendingFormatChange(format2) == false);
}

TEST_CASE("Format-change latch: new request re-arms after consumption") {
    FormatChangeLatch latch;

    // Request and consume Pcm24.
    latch.requestFormatChange(AudioFormat::Pcm24);
    AudioFormat format1 = AudioFormat::Pcm16;
    CHECK(latch.consumePendingFormatChange(format1) == true);
    CHECK(format1 == AudioFormat::Pcm24);

    // Second consume returns false (no new request yet).
    AudioFormat format2 = AudioFormat::Pcm16;
    CHECK(latch.consumePendingFormatChange(format2) == false);

    // Fresh request for Pcm16 re-arms the latch.
    latch.requestFormatChange(AudioFormat::Pcm16);

    // Next consume should return true and yield the new format.
    AudioFormat format3 = AudioFormat::Pcm24;
    CHECK(latch.consumePendingFormatChange(format3) == true);
    CHECK(format3 == AudioFormat::Pcm16);

    // And it's consumed exactly once.
    AudioFormat format4 = AudioFormat::Pcm24;
    CHECK(latch.consumePendingFormatChange(format4) == false);
}

TEST_CASE("Format-change latch: consume on empty latch returns false") {
    FormatChangeLatch latch;

    // No request has been made yet.
    AudioFormat format = AudioFormat::Pcm16;
    CHECK(latch.consumePendingFormatChange(format) == false);
}

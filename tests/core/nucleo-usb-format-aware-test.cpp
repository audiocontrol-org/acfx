#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "support/allocation-sentinel.h"

#include "audio-format.h"
#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-in-path.h"
#include "usb-out-path.h"

// T019 (US3, FR-010): the OUT/IN paths become FORMAT-AWARE — the same
// UsbOutPath::consumePacket() / UsbInPath::service() entry points the
// sibling suites (nucleo-usb-out-path-test.cpp, nucleo-usb-out-service-
// test.cpp, nucleo-usb-in-path-test.cpp, nucleo-usb-in-path-capture-only-
// test.cpp, nucleo-inpath-startup-test.cpp) already exercise Pcm16-only —
// now also accept an explicit `AudioFormat` and select the packed-24
// converter (support/sample-format.h's deinterleaveToFloat24() /
// interleaveToInt24Packed()) instead of the int16 one. Those sibling suites
// are UNCHANGED and still pass (verified by this same ctest run) because
// every new parameter defaults to AudioFormat::Pcm16 — this file is where
// the Pcm24 branch, and the byte accounting that differs by format, get
// their own coverage.
//
// Cases:
// FA1 — maxPayloadBytes()/maxPayloadBytes(format) byte accounting: the
//       no-arg overload is untouched (196 B); the format-aware overload
//       agrees for Pcm16 and returns the 294 B packed-24 worst case for
//       Pcm24.
// FA2 — UsbOutPath::consumePacket decodes a full 49-frame Pcm24 payload.
// FA3 — a torn Pcm24 payload truncates to whole 6-byte frames, counted once.
// FA4 — UsbInPath::service() encodes a room-bounded pull as packed-24 wire
//       bytes; the room->frames math uses the ACTIVE format's 6 B/frame,
//       not the fixed 16-bit figure.
// FA5 — the T006 cold-drain guard holds when the active format is Pcm24.
// FA6 — round-trip (float -> wire -> float) through BOTH paths together,
//       once per depth, within that depth's resolution.
// FA7 — the Pcm24 branch allocates nothing (Constitution real-time safety).

using namespace acfx::nucleo;
using acfx::test::AllocationSentinel;

namespace {

constexpr int kSubslot24 = 3;
constexpr int kBpf24 = kChannels * kSubslot24;  // 6
constexpr int kBpf16 = kChannels * static_cast<int>(sizeof(std::int16_t));  // 4

using TestRing = AudioRing<256, kChannels>;

// Local, independent LE24 encode/decode helpers — deliberately NOT calling
// sample-format.h's own sampleFromWire24Packed()/wireFromSample24Packed()
// — so this file's assertions about the PATH do not become circular with
// the primitive nucleo-packed24-test.cpp already covers directly.
void encodeLE24(std::int32_t value, std::uint8_t* out) noexcept {
    const auto u24 = static_cast<std::uint32_t>(value) & 0xFFFFFFu;
    out[0] = static_cast<std::uint8_t>(u24 & 0xFFu);
    out[1] = static_cast<std::uint8_t>((u24 >> 8) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((u24 >> 16) & 0xFFu);
}

std::int32_t decodeLE24(const std::uint8_t* p) noexcept {
    std::uint32_t raw = p[0] | (static_cast<std::uint32_t>(p[1]) << 8) |
                        (static_cast<std::uint32_t>(p[2]) << 16);
    if (raw & 0x800000u) {
        raw |= 0xFF000000u;
    }
    return static_cast<std::int32_t>(raw);
}

float expected24(std::int32_t value) noexcept {
    return static_cast<float>(value) / kPacked24Scale;
}

// Interleaved packed-24 payload: left counts up from `base`, right is the
// left value negated — the same convention nucleo-usb-out-path-test.cpp's
// makePayload() uses for int16, so a de-interleave that crosses channels
// shows up as a sign flip.
std::vector<std::uint8_t> makePacked24Payload(int frames, std::int32_t base = 0) {
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(frames) *
                                      static_cast<std::size_t>(kBpf24));
    for (int frame = 0; frame < frames; ++frame) {
        const std::int32_t left = base + frame + 1;
        const std::int32_t right = -left;
        encodeLE24(left, payload.data() + kBpf24 * frame);
        encodeLE24(right, payload.data() + kBpf24 * frame + kSubslot24);
    }
    return payload;
}

template <typename Ring, typename Left, typename Right>
void pushFrames(Ring& ring, int frames, Left left, Right right) {
    std::vector<float> l(static_cast<std::size_t>(frames));
    std::vector<float> r(static_cast<std::size_t>(frames));
    for (int frame = 0; frame < frames; ++frame) {
        l[static_cast<std::size_t>(frame)] = left(frame);
        r[static_cast<std::size_t>(frame)] = right(frame);
    }
    float* channels[kChannels] = {l.data(), r.data()};
    ring.write(channels, frames);
}

// A simulated TinyUSB IN endpoint that accepts everything offered (no
// back-pressure — that behaviour is already covered format-agnostically by
// nucleo-usb-in-path-test.cpp's IN4) and records every byte in order.
class FakeInSink {
public:
    explicit FakeInSink(int roomBytes) : roomBytes_(roomBytes) {}

    int write(const std::int16_t* data, int len) noexcept {
        ++writeCalls_;
        const auto* raw = reinterpret_cast<const std::uint8_t*>(data);
        const int accepted = (len > 0) ? len : 0;
        received_.insert(received_.end(), raw, raw + accepted);
        return accepted;
    }

    int writeAvailable() noexcept { return roomBytes_; }
    int capacity() noexcept { return roomBytes_; }  // held==0: IN guard inert

    std::vector<std::uint8_t> received_;
    int writeCalls_ = 0;

private:
    int roomBytes_;
};

// A float -> wire (IN path) -> wire -> float (OUT path) round trip through
// BOTH paths together, for a given format, asserting the recovered samples
// match the originals within that format's resolution.
void checkRoundTrip(AudioFormat format, float tolerance) {
    constexpr int kFrames = 6;
    TestRing outputRing(0);
    pushFrames(
        outputRing, kFrames,
        [](int frame) { return 0.6f - 0.2f * static_cast<float>(frame); },
        [](int frame) { return -0.3f + 0.1f * static_cast<float>(frame); });
    REQUIRE(outputRing.state() == RingState::Running);

    const int bpf = (format == AudioFormat::Pcm24) ? kBpf24 : kBpf16;
    FakeInSink sink(kFrames * bpf);
    AudioTransportStats stats;
    UsbInPath inPath;
    const InServicePass inPass =
        inPath.service(outputRing, sink, stats, /*captureOnly=*/false, format);
    REQUIRE(inPass.framesRead == kFrames);
    REQUIRE(inPass.framesSubstituted == 0);
    REQUIRE(inPass.bytesWritten == kFrames * bpf);
    REQUIRE(sink.received_.size() == static_cast<std::size_t>(kFrames * bpf));

    TestRing inputRing(0);
    UsbOutPath outPath;
    const OutPacketResult outResult = outPath.consumePacket(
        reinterpret_cast<const std::int16_t*>(sink.received_.data()),
        static_cast<int>(sink.received_.size()), inputRing, stats, format);
    REQUIRE(outResult.framesConsumed == kFrames);
    REQUIRE(outResult.wasTruncated == false);

    std::vector<float> recL(static_cast<std::size_t>(kFrames));
    std::vector<float> recR(static_cast<std::size_t>(kFrames));
    float* dst[kChannels] = {recL.data(), recR.data()};
    REQUIRE(inputRing.read(dst, kFrames) == 0);

    for (int frame = 0; frame < kFrames; ++frame) {
        const float expectedL = 0.6f - 0.2f * static_cast<float>(frame);
        const float expectedR = -0.3f + 0.1f * static_cast<float>(frame);
        CHECK(std::fabs(recL[static_cast<std::size_t>(frame)] - expectedL) <= tolerance);
        CHECK(std::fabs(recR[static_cast<std::size_t>(frame)] - expectedR) <= tolerance);
    }
}

}  // namespace

// ============================================================================
// FA1: byte accounting is format-aware; the no-arg overload is untouched
// ============================================================================

TEST_CASE("FA1: maxPayloadBytes() is unchanged; maxPayloadBytes(format) agrees for Pcm16 "
          "and returns the 294 B packed-24 worst case for Pcm24") {
    CHECK(UsbOutPath::maxPayloadBytes() == 196);
    CHECK(UsbOutPath::maxPayloadBytes(AudioFormat::Pcm16) == 196);
    CHECK(UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24) == 294);

    CHECK(UsbInPath::maxPayloadBytes() == 196);
    CHECK(UsbInPath::maxPayloadBytes(AudioFormat::Pcm16) == 196);
    CHECK(UsbInPath::maxPayloadBytes(AudioFormat::Pcm24) == 294);
}

// ============================================================================
// FA2 / FA3: UsbOutPath::consumePacket, Pcm24
// ============================================================================

TEST_CASE("FA2: consumePacket decodes a full 49-frame Pcm24 payload correctly") {
    REQUIRE(kMaxPacketFrames == 49);

    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::uint8_t> payload = makePacked24Payload(kMaxPacketFrames);
    const int byteCount = kMaxPacketFrames * kBpf24;
    REQUIRE(byteCount == UsbOutPath::maxPayloadBytes(AudioFormat::Pcm24));

    const OutPacketResult result = path.consumePacket(
        reinterpret_cast<const std::int16_t*>(payload.data()), byteCount, ring, stats,
        AudioFormat::Pcm24);

    CHECK(result.framesConsumed == kMaxPacketFrames);
    CHECK(result.framesDropped == 0);
    CHECK(result.wasTruncated == false);
    CHECK(stats.malformedPayloads == 0u);
    REQUIRE(ring.occupancy() == kMaxPacketFrames);

    std::vector<float> left(static_cast<std::size_t>(kMaxPacketFrames));
    std::vector<float> right(static_cast<std::size_t>(kMaxPacketFrames));
    float* dst[kChannels] = {left.data(), right.data()};
    REQUIRE(ring.read(dst, kMaxPacketFrames) == 0);

    for (int frame = 0; frame < kMaxPacketFrames; ++frame) {
        const std::int32_t leftVal = frame + 1;
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(expected24(leftVal)));
        CHECK(right[static_cast<std::size_t>(frame)] == doctest::Approx(expected24(-leftVal)));
    }
}

TEST_CASE("FA3: a torn Pcm24 payload truncates to whole 6-byte frames and counts once") {
    TestRing ring(0);
    AudioTransportStats stats;
    UsbOutPath path;

    const std::vector<std::uint8_t> payload = makePacked24Payload(5);
    // 4 whole 6-byte frames plus a 5-byte remainder: the largest tear a
    // packed-24 stereo frame admits.
    const int byteCount = 4 * kBpf24 + 5;

    const OutPacketResult result = path.consumePacket(
        reinterpret_cast<const std::int16_t*>(payload.data()), byteCount, ring, stats,
        AudioFormat::Pcm24);

    CHECK(result.framesConsumed == 4);
    CHECK(result.wasTruncated == true);
    CHECK(stats.malformedPayloads == 1u);
    CHECK(ring.occupancy() == 4);
}

// ============================================================================
// FA4 / FA5: UsbInPath::service(), Pcm24
// ============================================================================

TEST_CASE("FA4: service() encodes a room-bounded pull as packed-24 wire bytes; room->frames "
          "uses the ACTIVE format's 6 B/frame") {
    TestRing ring(0);
    pushFrames(
        ring, 4, [](int frame) { return static_cast<float>(1000 + frame) / kPacked24Scale; },
        [](int frame) { return static_cast<float>(-(1000 + frame)) / kPacked24Scale; });
    REQUIRE(ring.state() == RingState::Running);

    // Room for exactly 4 frames at Pcm24 (6 B/frame) — smaller than
    // kMaxPacketFrames — so the pull size is bounded by ROOM computed with
    // the ACTIVE format's frame size, not the fixed 16-bit one.
    FakeInSink sink(4 * kBpf24);
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass =
        path.service(ring, sink, stats, /*captureOnly=*/false, AudioFormat::Pcm24);

    CHECK(pass.flushedCarryover == false);
    CHECK(pass.framesRead == 4);
    CHECK(pass.framesSubstituted == 0);
    CHECK(pass.bytesOffered == 4 * kBpf24);
    CHECK(pass.bytesWritten == 4 * kBpf24);
    CHECK(stats.outputUnderruns == 0u);
    REQUIRE(sink.received_.size() == static_cast<std::size_t>(4 * kBpf24));

    for (int frame = 0; frame < 4; ++frame) {
        const std::uint8_t* wireLeft = sink.received_.data() + kBpf24 * frame;
        const std::uint8_t* wireRight = wireLeft + kSubslot24;
        CHECK(decodeLE24(wireLeft) == 1000 + frame);
        CHECK(decodeLE24(wireRight) == -(1000 + frame));
    }
}

TEST_CASE("FA5: the T006 cold-drain guard also holds when the active format is Pcm24") {
    TestRing ring(48);
    REQUIRE(ring.state() == RingState::Priming);

    pushFrames(
        ring, 48, [](int) { return 0.25f; }, [](int) { return -0.25f; });
    REQUIRE(ring.state() == RingState::Running);
    REQUIRE(ring.occupancy() == 48);

    // Room for one maximum packet (49 frames) at Pcm24's 6 B/frame — the
    // first SOF-paced pull, exactly as nucleo-inpath-startup-test.cpp's RED
    // case for the 16-bit path, now under Pcm24's byte accounting.
    FakeInSink sink(kMaxPacketFrames * kBpf24);
    AudioTransportStats stats;
    UsbInPath path;

    const InServicePass pass =
        path.service(ring, sink, stats, /*captureOnly=*/false, AudioFormat::Pcm24);

    CHECK(pass.framesSubstituted == 0);
    CHECK(stats.outputUnderruns == 0u);
}

// ============================================================================
// FA6: round-trip (float -> wire -> float), both paths together, per depth
// ============================================================================

TEST_CASE("FA6: Pcm16 float->wire->float round-trips through both paths within 16-bit "
          "resolution") {
    checkRoundTrip(AudioFormat::Pcm16, 1.5f / kInt16Scale);
}

TEST_CASE("FA6: Pcm24 float->wire->float round-trips through both paths within 24-bit "
          "resolution") {
    checkRoundTrip(AudioFormat::Pcm24, 1.5f / kPacked24Scale);
}

// ============================================================================
// FA7: the Pcm24 branch allocates nothing (Constitution real-time safety)
// ============================================================================

TEST_CASE("FA7: consumePacket and service() allocate nothing on the Pcm24 branch") {
    TestRing inputRing(0);
    AudioTransportStats stats;
    UsbOutPath outPath;
    const std::vector<std::uint8_t> outPayload = makePacked24Payload(kMaxPacketFrames);

    TestRing outputRing(0);
    pushFrames(
        outputRing, kMaxPacketFrames, [](int) { return 0.1f; }, [](int) { return -0.1f; });
    UsbInPath inPath;
    FakeInSink sink(kMaxPacketFrames * kBpf24);
    sink.received_.reserve(static_cast<std::size_t>(20 * kMaxPacketFrames * kBpf24));

    AllocationSentinel::reset();
    for (int iter = 0; iter < 20; ++iter) {
        (void)outPath.consumePacket(
            reinterpret_cast<const std::int16_t*>(outPayload.data()),
            static_cast<int>(outPayload.size()), inputRing, stats, AudioFormat::Pcm24);
        (void)inPath.service(outputRing, sink, stats, /*captureOnly=*/false,
                             AudioFormat::Pcm24);
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    CHECK_MESSAGE(allocations == 0, "Pcm24 branch allocated ", allocations);
}

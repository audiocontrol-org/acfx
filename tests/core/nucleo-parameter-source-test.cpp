#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "parameter-shadow.h"
#include "parameter-source.h"

using namespace acfx::nucleo;

namespace {

// A tiny sampled-state source stub: reads a stored value, writes to a single
// slot. Used to prove the duck-typed seam admits both event-driven (MIDI) and
// sampled-state sources.
struct SampledStateSource {
    float storedValue = 0.5f;
    int targetSlot = 0;
    float lastWritten = -1.0f;

    void poll(ParameterShadow<8>& shadow) noexcept {
        if (storedValue != lastWritten) {
            shadow.set(targetSlot, storedValue);
            lastWritten = storedValue;
        }
    }
};

// A test-harness callback that records every parameter apply() call for
// verification. Captures (index, normalized value) pairs.
struct ApplyRecorder {
    std::vector<std::pair<int, float>> applies;

    void operator()(acfx::ParamId id, float value) noexcept {
        applies.push_back({id.value, value});
    }
};

}  // namespace

// ============================================================================
// PSRC1 — Duck-typed seam: two source kinds converge on shadow data
// ============================================================================

TEST_CASE("PSRC1: sampled-state source satisfies the poll(shadow) seam") {
    ParameterShadow<8> shadow;
    SampledStateSource source;
    source.storedValue = 0.75f;
    source.targetSlot = 0;

    source.poll(shadow);

    CHECK(shadow.dirty(0) == true);
    CHECK(shadow.dirty(1) == false);

    ApplyRecorder recorder;
    shadow.flush(recorder);

    REQUIRE(recorder.applies.size() == 1u);
    CHECK(recorder.applies[0].first == 0);
    CHECK(recorder.applies[0].second == doctest::Approx(0.75f));
}

TEST_CASE("PSRC1: MidiParameterSource satisfies the poll(shadow) seam") {
    ParameterShadow<8> shadow;
    MidiParameterSource<8> midiSource;

    midiSource.onControlChange(74, 100);
    midiSource.poll(shadow);

    CHECK(shadow.dirty(0) == true);

    ApplyRecorder recorder;
    shadow.flush(recorder);

    REQUIRE(recorder.applies.size() == 1u);
}

// ============================================================================
// PSRC2 — Dead-band: unchanged values do not dirty slots
// ============================================================================

TEST_CASE("PSRC2: MidiParameterSource dead-bands identical CC updates") {
    ParameterShadow<8> shadow;
    MidiParameterSource<8> midiSource;

    midiSource.onControlChange(74, 100);
    midiSource.poll(shadow);

    CHECK(shadow.dirty(0) == true);

    ApplyRecorder recorder1;
    shadow.flush(recorder1);

    CHECK(recorder1.applies.size() == 1u);

    midiSource.onControlChange(74, 100);
    midiSource.poll(shadow);

    CHECK(shadow.dirty(0) == false);
}

TEST_CASE("PSRC2: sampled-state source dead-bands unchanged values") {
    ParameterShadow<8> shadow;
    SampledStateSource source;
    source.storedValue = 0.5f;
    source.targetSlot = 0;

    source.poll(shadow);
    CHECK(shadow.dirty(0) == true);

    ApplyRecorder recorder1;
    shadow.flush(recorder1);

    CHECK(recorder1.applies.size() == 1u);

    source.poll(shadow);

    CHECK(shadow.dirty(0) == false);
}

// ============================================================================
// PSRC4 — Composability: last-write-wins across multiple sources
// ============================================================================

TEST_CASE("PSRC4: two sources writing the same slot, last write wins") {
    ParameterShadow<8> shadow;
    SampledStateSource source1;
    SampledStateSource source2;

    source1.storedValue = 0.25f;
    source1.targetSlot = 0;
    source2.storedValue = 0.75f;
    source2.targetSlot = 0;

    source1.poll(shadow);
    CHECK(shadow.dirty(0) == true);

    source2.poll(shadow);
    CHECK(shadow.dirty(0) == true);

    ApplyRecorder recorder;
    shadow.flush(recorder);

    REQUIRE(recorder.applies.size() == 1u);
    CHECK(recorder.applies[0].second == doctest::Approx(0.75f));
}

TEST_CASE("PSRC4: MIDI and sampled source to same slot, last write wins") {
    ParameterShadow<8> shadow;
    SampledStateSource sampledSource;
    MidiParameterSource<8> midiSource;

    sampledSource.storedValue = 0.3f;
    sampledSource.targetSlot = 0;

    sampledSource.poll(shadow);
    midiSource.onControlChange(74, 100);
    midiSource.poll(shadow);

    ApplyRecorder recorder;
    shadow.flush(recorder);

    REQUIRE(recorder.applies.size() == 1u);
    CHECK(recorder.applies[0].first == 0);
    CHECK(recorder.applies[0].second == doctest::Approx(100.0f / 127.0f));
}

// ============================================================================
// PSRC5 — Source never calls setParameter: verify by seam signature
// ============================================================================

TEST_CASE("PSRC5: poll takes only ParameterShadow&, not an effect or apply surface") {
    SampledStateSource source;
    ParameterShadow<8> shadow;

    static_assert(std::is_invocable_v<decltype(&SampledStateSource::poll),
                                      SampledStateSource*, ParameterShadow<8>&>);

    static_assert(std::is_invocable_v<decltype(&MidiParameterSource<8>::poll),
                                      MidiParameterSource<8>*, ParameterShadow<8>&>);
}

TEST_CASE("PSRC5: applying a parameter happens only via flush, never via poll") {
    ParameterShadow<8> shadow;
    SampledStateSource source;
    source.storedValue = 0.6f;
    source.targetSlot = 0;

    source.poll(shadow);

    CHECK(shadow.dirty(0) == true);

    ApplyRecorder recorder;
    CHECK(recorder.applies.empty());

    shadow.flush(recorder);

    REQUIRE(recorder.applies.size() == 1u);
    CHECK(recorder.applies[0].second == doctest::Approx(0.6f));
}

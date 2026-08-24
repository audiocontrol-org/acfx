#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "dsp/param-id.h"
#include "parameter-shadow.h"

// Parameter shadow contract (FR-041, FR-042, FR-043, FR-044, D25).
//
// ParameterShadow<N> is a per-ParamId dirty-flag + value pair, bounded at
// construction to the effect's parameter count. Within one audio block, any
// number of set() calls collapse to a single flush() application per dirty
// parameter, with exactly one apply() invocation per dirty slot and none for
// clean ones. After flush(), the dirty flags are cleared.
//
// Cases:
// PS1 — bounded at N: no overflow; set(i, v) with i >= N or i < 0 is a
//       silent no-op that dirties nothing and applies nothing on flush.
// PS2 — last-write-wins: after a burst of set() calls within one block,
//       flush() applies the final value per parameter, never an intermediate.
//       A knob sweep cannot strand a parameter mid-travel.
// PS3 — no cross-parameter eviction: a fast-moving control (many set() on
//       slot A) does not displace another control's single pending update
//       (one set() on slot B). After setting A many times and B once, flush
//       applies both, B's value intact.
// PS4 — exactly-once: flush() invokes apply exactly once per dirty parameter
//       and not at all for clean slots, then clears the flags. A second flush
//       with no intervening set() applies nothing.
// PS5 — N == 0 is valid; ParameterShadow<0> constructs; flush is a no-op;
//       size() == 0.
// PS6 — no allocation, noexcept; callable at the block boundary in the audio
//       path.

using namespace acfx;
using namespace acfx::nucleo;

// ============================================================================
// PS1: bounded at N; no overflow; out-of-range is a silent no-op
// ============================================================================

TEST_CASE("PS1: ParameterShadow<N> size() returns N") {
    static_assert(ParameterShadow<0>::size() == 0);
    static_assert(ParameterShadow<1>::size() == 1);
    static_assert(ParameterShadow<8>::size() == 8);
    static_assert(ParameterShadow<256>::size() == 256);
    CHECK(ParameterShadow<5>::size() == 5);
}

TEST_CASE("PS1: index >= N is silently ignored on set()") {
    ParameterShadow<3> shadow;
    std::vector<ParamId> applied;

    shadow.set(3, 0.5f);  // out of range
    shadow.set(4, 0.6f);  // out of range
    shadow.set(100, 0.7f);  // out of range

    shadow.flush([&applied](ParamId id, float) {
        applied.push_back(id);
    });

    CHECK(applied.empty());
}

TEST_CASE("PS1: index < 0 is silently ignored on set()") {
    ParameterShadow<3> shadow;
    std::vector<ParamId> applied;

    shadow.set(-1, 0.5f);
    shadow.set(-10, 0.6f);

    shadow.flush([&applied](ParamId id, float) {
        applied.push_back(id);
    });

    CHECK(applied.empty());
}

TEST_CASE("PS1: dirty() returns false for out-of-range indices") {
    ParameterShadow<2> shadow;
    shadow.set(0, 0.5f);

    CHECK(shadow.dirty(0));
    CHECK(!shadow.dirty(2));
    CHECK(!shadow.dirty(3));
    CHECK(!shadow.dirty(-1));
}

// ============================================================================
// PS2: last-write-wins within one block
// ============================================================================

TEST_CASE("PS2: a burst of set() on the same slot collapses to the final value") {
    ParameterShadow<1> shadow;
    std::vector<float> values;

    // Rapid writes on the same slot
    shadow.set(0, 0.1f);
    shadow.set(0, 0.2f);
    shadow.set(0, 0.3f);
    shadow.set(0, 0.99f);

    // Flush should apply exactly the final value
    shadow.flush([&values](ParamId, float v) {
        values.push_back(v);
    });

    CHECK(values.size() == 1);
    CHECK(values[0] == doctest::Approx(0.99f));
}

TEST_CASE("PS2: intermediate values are never applied on flush") {
    ParameterShadow<4> shadow;
    std::vector<float> values;

    // Set the same slot multiple times
    for (float v = 0.0f; v < 1.0f; v += 0.1f) {
        shadow.set(2, v);
    }

    // Flush should see only the final write (0.9f)
    shadow.flush([&values](ParamId id, float v) {
        if (id.value == 2) {
            values.push_back(v);
        }
    });

    CHECK(values.size() == 1);
    CHECK(values[0] == doctest::Approx(0.9f));
}

TEST_CASE("PS2: alternating writes to different slots still yields final values") {
    ParameterShadow<2> shadow;
    std::vector<std::pair<uint8_t, float>> applied;

    shadow.set(0, 0.1f);
    shadow.set(1, 0.2f);
    shadow.set(0, 0.3f);  // Overwrite slot 0
    shadow.set(1, 0.4f);  // Overwrite slot 1

    shadow.flush([&applied](ParamId id, float v) {
        applied.push_back({id.value, v});
    });

    CHECK(applied.size() == 2);
    // Order is unspecified, so check by value
    for (auto [idx, val] : applied) {
        if (idx == 0) {
            CHECK(val == doctest::Approx(0.3f));
        } else {
            CHECK(val == doctest::Approx(0.4f));
        }
    }
}

// ============================================================================
// PS3: no cross-parameter eviction
// ============================================================================

TEST_CASE("PS3: a fast-moving control does not displace another's pending update") {
    ParameterShadow<2> shadow;
    std::vector<std::pair<uint8_t, float>> applied;

    // Slot 0: many writes
    for (int i = 0; i < 10; ++i) {
        shadow.set(0, 0.1f + i * 0.01f);
    }
    // Slot 1: single write (this must not be evicted)
    shadow.set(1, 0.99f);

    shadow.flush([&applied](ParamId id, float v) {
        applied.push_back({id.value, v});
    });

    CHECK(applied.size() == 2);
    bool foundSlot0 = false, foundSlot1 = false;
    for (auto [idx, val] : applied) {
        if (idx == 0) {
            foundSlot0 = true;
            CHECK(val == doctest::Approx(0.19f));  // Final write on slot 0
        } else if (idx == 1) {
            foundSlot1 = true;
            CHECK(val == doctest::Approx(0.99f));
        }
    }
    CHECK(foundSlot0);
    CHECK(foundSlot1);
}

TEST_CASE("PS3: multiple fast-moving controls coexist without eviction") {
    ParameterShadow<3> shadow;
    std::vector<std::pair<uint8_t, float>> applied;

    // Slot 0: 5 writes
    for (int i = 0; i < 5; ++i) {
        shadow.set(0, 0.0f + i * 0.1f);
    }
    // Slot 1: 3 writes
    for (int i = 0; i < 3; ++i) {
        shadow.set(1, 0.5f + i * 0.1f);
    }
    // Slot 2: 1 write (must survive)
    shadow.set(2, 0.88f);

    shadow.flush([&applied](ParamId id, float v) {
        applied.push_back({id.value, v});
    });

    CHECK(applied.size() == 3);
    for (auto [idx, val] : applied) {
        if (idx == 0) {
            CHECK(val == doctest::Approx(0.4f));  // 4 * 0.1
        } else if (idx == 1) {
            CHECK(val == doctest::Approx(0.7f));  // 0.5 + 2 * 0.1
        } else if (idx == 2) {
            CHECK(val == doctest::Approx(0.88f));
        }
    }
}

// ============================================================================
// PS4: exactly-once per dirty slot, then clear flags
// ============================================================================

TEST_CASE("PS4: flush invokes apply exactly once per dirty slot") {
    ParameterShadow<4> shadow;
    std::vector<uint8_t> applyIndices;

    shadow.set(1, 0.5f);
    shadow.set(3, 0.7f);

    shadow.flush([&applyIndices](ParamId id, float) {
        applyIndices.push_back(id.value);
    });

    CHECK(applyIndices.size() == 2);
    std::sort(applyIndices.begin(), applyIndices.end());
    CHECK(applyIndices[0] == 1);
    CHECK(applyIndices[1] == 3);
}

TEST_CASE("PS4: flush does not invoke apply for clean slots") {
    ParameterShadow<3> shadow;
    int applyCount = 0;

    shadow.set(0, 0.5f);
    // Slots 1 and 2 remain clean

    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });

    CHECK(applyCount == 1);
}

TEST_CASE("PS4: second flush with no intervening set() applies nothing") {
    ParameterShadow<2> shadow;
    int applyCount = 0;

    shadow.set(0, 0.5f);
    shadow.set(1, 0.6f);

    // First flush
    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });
    CHECK(applyCount == 2);

    // Second flush without any set()
    applyCount = 0;
    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });
    CHECK(applyCount == 0);
}

TEST_CASE("PS4: flags are cleared after flush") {
    ParameterShadow<2> shadow;

    shadow.set(0, 0.5f);
    shadow.set(1, 0.6f);
    CHECK(shadow.dirty(0));
    CHECK(shadow.dirty(1));

    shadow.flush([](ParamId, float) {});

    CHECK(!shadow.dirty(0));
    CHECK(!shadow.dirty(1));
}

TEST_CASE("PS4: a new set() after flush sets the dirty flag again") {
    ParameterShadow<1> shadow;
    int applyCount = 0;

    shadow.set(0, 0.5f);
    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });
    CHECK(applyCount == 1);
    CHECK(!shadow.dirty(0));

    // Set again
    shadow.set(0, 0.7f);
    CHECK(shadow.dirty(0));

    applyCount = 0;
    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });
    CHECK(applyCount == 1);
}

// ============================================================================
// PS5: N == 0 is valid
// ============================================================================

TEST_CASE("PS5: ParameterShadow<0> constructs") {
    ParameterShadow<0> shadow;
    CHECK(shadow.size() == 0);
}

TEST_CASE("PS5: ParameterShadow<0>::flush is a no-op") {
    ParameterShadow<0> shadow;
    int applyCount = 0;

    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });

    CHECK(applyCount == 0);
}

TEST_CASE("PS5: set() on ParameterShadow<0> is a silent no-op") {
    ParameterShadow<0> shadow;
    int applyCount = 0;

    shadow.set(0, 0.5f);
    shadow.set(1, 0.6f);
    shadow.set(-1, 0.7f);

    shadow.flush([&applyCount](ParamId, float) {
        ++applyCount;
    });

    CHECK(applyCount == 0);
}

TEST_CASE("PS5: dirty() on ParameterShadow<0> always returns false") {
    ParameterShadow<0> shadow;

    CHECK(!shadow.dirty(0));
    CHECK(!shadow.dirty(-1));
    CHECK(!shadow.dirty(100));
}

// ============================================================================
// PS6: no allocation, noexcept
// ============================================================================

TEST_CASE("PS6: set() is noexcept") {
    static_assert(noexcept(std::declval<ParameterShadow<4>&>().set(0, 0.5f)));
}

TEST_CASE("PS6: flush() is noexcept") {
    static_assert(noexcept(std::declval<ParameterShadow<4>&>().flush(
        [](ParamId, float) {})));
}

TEST_CASE("PS6: dirty() is noexcept") {
    static_assert(noexcept(std::declval<const ParameterShadow<4>&>().dirty(0)));
}

TEST_CASE("PS6: size() is noexcept") {
    static_assert(noexcept(ParameterShadow<4>::size()));
}

TEST_CASE("PS6: construction is noexcept") {
    static_assert(noexcept(ParameterShadow<8>{}));
}

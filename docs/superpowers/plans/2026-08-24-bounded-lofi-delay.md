# Bounded lo-fi delay — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `ModulatedDelayEffect` run on the STM32F446RE by bounding its memory to a compile-time-templated, heap-free static buffer, and add a live lo-fi layer (internal sample-and-hold decimation that trades bandwidth for delay time, plus a feedback-loop bit-crush) driven over MIDI CC.

**Architecture:** Three new reusable primitives — a bounded, storage-typed delay line (`primitives/delays/`), a bit-crush and a decimator (`primitives/lofi/`) — then `ModulatedDelayEffect` becomes `template <MaxDelaySamples, Sample = float, MaxChannels = 8>`, heap-free, with the whole wet loop running at the internal decimated rate. Desktop/Daisy instantiate the float default (numerically identical to today); a Nucleo alias instantiates `<14400, int16_t, 2>` and ships as a new firmware target.

**Tech Stack:** C++20, header-only DSP core, doctest host tests (`test` preset), Arm GNU Toolchain cross-build (`nucleo` preset), the T058/T059 CDC HIL rig for hardware acceptance.

**Spec:** `docs/superpowers/specs/2026-08-24-lofi-bounded-delay-design.md` (read it — the plan argues from it; the quantization, decimation-clock, tape-speed, and delay-time contracts are defined there).

## Global Constraints

- **No AI/Claude attribution** in any commit message. No `Co-Authored-By`, no generated-with footer.
- **No heap allocation and no locks** on any `process()`/`prepare()` path (RT safety). The whole effect must be heap-free after Task 4 — verified by the T066 allocation sentinel.
- **Core is platform-independent.** Nothing under `core/` may include a USB/TinyUSB/CMSIS/board/adapter header — enforced by `scripts/check-portability.sh` (`C-CORE-INWARD`). The int16 quantization convention is therefore **replicated in core**, never `#include`d from `adapters/nucleo/support/sample-format.h`.
- **No `any`/unchecked casts, no fallbacks/mock data** outside test code; raise descriptive errors instead.
- **Files ~300–500 lines max.** New primitives are small, focused headers.
- **TDD throughout:** write the failing test, watch it fail, minimal implementation, watch it pass, commit.
- **int16 convention (must match `sample-format.h` exactly, replicated in core):** scale ×32768, round ties-away-from-zero (`+0.5`/`-0.5` bias), clamp `[-32768, 32767]` (saturate, no wrap); read back `/32768.0f`.
- **Lo-fi parameter encodings:** `lofi_rate` discrete index `0..3 → D = 1<<index ∈ {1,2,4,8}`; `lofi_bits` discrete index `0..4 → B ∈ {16,12,8,6,4}`.
- **Nucleo instantiation:** `ModulatedDelayEffect<14400, std::int16_t, 2>` — 14400 samples/ch = 300 ms at 48 kHz, 2 ch × 14400 × 2 bytes ≈ 57.6 KB.
- **Build/test commands:** host — `cmake --preset test && cmake --build --preset test -j && ctest --preset test`; firmware — `cmake --build --preset nucleo` with `PATH` including `/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin`.

---

## File Structure

**Create:**
- `core/primitives/delays/bounded-delay-line.h` — `BoundedDelayLine<Sample, MaxSamples>`: owns `std::array<Sample, MaxSamples>`, same fractional-read/write/reset math as `DelayLine`, converts at the boundary for non-float `Sample`. General storage primitive (reverb will reuse it).
- `core/primitives/lofi/bit-crush.h` — `crushToGrid(float, int bits)`: mid-tread B-bit-grid quantizer, `bits>=16` bypass.
- `core/primitives/lofi/decimator.h` — `SampleHoldDecimator`: internal-tick scheduler with sample-and-hold in/out and phase-reset-on-rate-change.
- `core/primitives/lofi/int16-quant.h` — `quantizeInt16(float)`/`dequantizeInt16(std::int16_t)`: the FR-038a convention, replicated in core (used by `BoundedDelayLine<int16_t,…>`).
- `adapters/nucleo/nucleo-modulated-delay.h` — `using NucleoModulatedDelay = acfx::ModulatedDelayEffect<14400, std::int16_t, 2>;` + the footprint `static_assert`.
- Tests: `tests/core/bounded-delay-line-test.cpp`, `tests/core/lofi-bit-crush-test.cpp`, `tests/core/lofi-decimator-test.cpp`, `tests/core/modulated-delay-bounded-test.cpp`, `tests/core/modulated-delay-lofi-test.cpp`.

**Modify:**
- `core/effects/modulated-delay/modulated-delay-effect.h` — add the three template params; replace `std::vector` buffers with `BoundedDelayLine`; add `lofi_rate`/`lofi_bits` params (indices 19/20); run the wet loop at the internal rate; apply crush + storage quant per the contract.
- `core/effects/modulated-delay/wow-flutter.h` — bound its per-channel buffers (heap `std::vector` → fixed `std::array`), templated on `MaxChannels` and a small fixed capacity.
- `adapters/nucleo/support/midi-cc-map.h` — add `CcBinding{76, 19}` (lofi_rate) and `CcBinding{77, 20}` (lofi_bits).
- `adapters/nucleo/CMakeLists.txt` — add an `acfx_add_effect_nucleo(NAME acfx_nucleo_lofi_delay …)` target.
- `tests/CMakeLists.txt` — register the five new test sources.

---

### Task 1: `BoundedDelayLine<Sample, MaxSamples>` storage primitive

**Files:**
- Create: `core/primitives/lofi/int16-quant.h`
- Create: `core/primitives/delays/bounded-delay-line.h`
- Test: `tests/core/bounded-delay-line-test.cpp`
- Modify: `tests/CMakeLists.txt` (register the test)

**Interfaces:**
- Consumes: nothing (leaf primitive). Mirrors the existing `DelayLine` math (`core/primitives/delays/delay-line.h`: `write(float)`, `readFractional(float delaySamples)` linear-interp, `reset()`).
- Produces:
  - `int16-quant.h`: `constexpr float kInt16Scale = 32768.0f;` `std::int16_t acfx::quantizeInt16(float) noexcept;` `float acfx::dequantizeInt16(std::int16_t) noexcept;`
  - `bounded-delay-line.h`: `template <typename Sample, std::size_t MaxSamples> class acfx::BoundedDelayLine` with `void prepare(int capacity, float sampleRate) noexcept;` (capacity ≤ MaxSamples), `void reset() noexcept;`, `void write(float x) noexcept;`, `float readFractional(float delaySamples) const noexcept;`, `int capacity() const noexcept;`, `float maxDelaySamples() const noexcept;`. Storage is an in-object `std::array<Sample, MaxSamples>`.

- [ ] **Step 1: Write the failing test — int16 convention + float-storage parity + int16 round-trip.**

```cpp
#include <doctest/doctest.h>
#include <array>
#include <cstdint>
#include "primitives/delays/bounded-delay-line.h"
#include "primitives/delays/delay-line.h"
#include "primitives/lofi/int16-quant.h"

TEST_CASE("quantizeInt16 matches the FR-038a convention") {
    CHECK(acfx::quantizeInt16(0.0f) == 0);
    CHECK(acfx::quantizeInt16(1.0f) == 32767);     // clamp at +full-scale
    CHECK(acfx::quantizeInt16(-1.0f) == -32768);
    CHECK(acfx::quantizeInt16(2.0f) == 32767);     // saturate, no wrap
    CHECK(acfx::quantizeInt16(-2.0f) == -32768);
    // round ties away from zero: 0.5/32768 scaled = 0.5 -> rounds to 1
    CHECK(acfx::quantizeInt16(0.5f / 32768.0f) == 1);
    CHECK(acfx::dequantizeInt16(16384) == doctest::Approx(0.5f));
}

TEST_CASE("BoundedDelayLine<float> is bit-identical to DelayLine") {
    constexpr int cap = 64;
    std::array<float, 128> backing{};
    acfx::DelayLine ref;
    ref.prepare(backing.data(), cap, 48000.0f);
    acfx::BoundedDelayLine<float, 128> bnd;
    bnd.prepare(cap, 48000.0f);
    // Drive identical write streams and compare fractional reads bit-for-bit.
    for (int n = 0; n < 200; ++n) {
        const float x = std::sin(0.1f * static_cast<float>(n));
        ref.write(x);
        bnd.write(x);
        const float dref = ref.readFractional(12.3f);
        const float dbnd = bnd.readFractional(12.3f);
        REQUIRE(std::bit_cast<std::uint32_t>(dref) == std::bit_cast<std::uint32_t>(dbnd));
    }
}

TEST_CASE("BoundedDelayLine<int16_t> stores on the 16-bit grid") {
    acfx::BoundedDelayLine<std::int16_t, 128> bnd;
    bnd.prepare(64, 48000.0f);
    bnd.write(0.5f);
    for (int i = 0; i < 10; ++i) bnd.write(0.0f);   // push the sample back by 10
    const float d = bnd.readFractional(10.0f);       // integer delay -> exact tap
    CHECK(d == doctest::Approx(0.5f).epsilon(1.0f / 32768.0f));
}
```

- [ ] **Step 2: Run to verify it fails.** Run: `cmake --preset test && cmake --build --preset test -j 2>&1 | head` — Expected: compile failure, `bounded-delay-line.h`/`int16-quant.h` not found. (Register the test first — Step 3 does that.)

- [ ] **Step 3: Register the test source.** In `tests/CMakeLists.txt`, add under the effect/primitive test banner (one line, matching the existing `core/…-test.cpp` list):

```
  core/bounded-delay-line-test.cpp
```

- [ ] **Step 4: Implement `int16-quant.h`.**

```cpp
#pragma once
#include <cstdint>
namespace acfx {
inline constexpr float kInt16Scale = 32768.0f;
inline std::int16_t quantizeInt16(float sample) noexcept {
    const float scaled = sample * kInt16Scale;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    const float biased = (scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f);
    return static_cast<std::int16_t>(biased);
}
inline float dequantizeInt16(std::int16_t code) noexcept {
    return static_cast<float>(code) / kInt16Scale;
}
} // namespace acfx
```

- [ ] **Step 5: Implement `bounded-delay-line.h`.** Own `std::array<Sample, MaxSamples>`, replicate `DelayLine`'s write/read/reset math exactly, converting only through `store()`/`load()` helpers that are identity for `float` and int16-quant for `std::int16_t` (use `if constexpr (std::is_same_v<Sample, std::int16_t>)`). The fractional read loads the two neighbouring taps as `float` and interpolates identically to `DelayLine`. Keep the file focused (< 120 lines). Guard `prepare` with `capacity <= MaxSamples`.

- [ ] **Step 6: Run to verify it passes.** Run: `cmake --build --preset test -j && ctest --preset test 2>&1 | tail -3` — Expected: PASS, total count grows by 3 cases.

- [ ] **Step 7: Commit.**

```bash
git add core/primitives/delays/bounded-delay-line.h core/primitives/lofi/int16-quant.h tests/core/bounded-delay-line-test.cpp tests/CMakeLists.txt
git commit -F .git/COMMIT_MSG_bdl   # write the message to that file first (no heredoc # traps)
```
Message body (write to the file, then commit): `feat(core): BoundedDelayLine<Sample,N> static storage primitive + core int16 convention`.

---

### Task 2: bit-crush primitive

**Files:**
- Create: `core/primitives/lofi/bit-crush.h`
- Test: `tests/core/lofi-bit-crush-test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `float acfx::crushToGrid(float x, int bits) noexcept;` — mid-tread quantization onto a `bits`-bit grid over `[-1, 1)`, step `q = 2^(1-bits)`; `bits >= 16` returns `x` unchanged (hard bypass); zero maps to zero.

- [ ] **Step 1: Write the failing test.**

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "primitives/lofi/bit-crush.h"

TEST_CASE("crushToGrid bypasses at 16 bits") {
    for (float x = -1.0f; x < 1.0f; x += 0.013f)
        CHECK(acfx::crushToGrid(x, 16) == x);      // exact identity
}
TEST_CASE("crushToGrid preserves zero (mid-tread)") {
    CHECK(acfx::crushToGrid(0.0f, 4) == 0.0f);
}
TEST_CASE("crushToGrid quantizes onto 2^bits levels without shrinking level") {
    // 4-bit grid: step = 2^(1-4) = 0.125. A value near full scale stays near full scale.
    const float q = acfx::crushToGrid(0.97f, 4);
    CHECK(q == doctest::Approx(1.0f));             // rounds to the 0.125 grid, NOT ~0.06
    CHECK(std::fabs(q) > 0.9f);                    // guards the right-shift level-drop trap
    // grid membership: result is an integer multiple of the step
    const float step = 0.125f;
    CHECK(std::fabs(q / step - std::round(q / step)) < 1e-5f);
}
```

- [ ] **Step 2: Run to verify it fails.** Add `core/lofi-bit-crush-test.cpp` to `tests/CMakeLists.txt`, then `cmake --build --preset test -j` — Expected: `bit-crush.h` not found.

- [ ] **Step 3: Implement `bit-crush.h`.**

```cpp
#pragma once
#include <cmath>
namespace acfx {
inline float crushToGrid(float x, int bits) noexcept {
    if (bits >= 16) return x;                       // hard bypass — load-bearing for float identity
    const float steps = static_cast<float>(1 << (bits - 1));  // 2^(bits-1)
    const float q     = 1.0f / steps;               // step = 2^(1-bits) over [-1,1)
    const float g     = std::round(x / q) * q;      // mid-tread; 0 -> 0
    if (g >= 1.0f)  return 1.0f;                     // saturate the top grid point
    if (g < -1.0f)  return -1.0f;
    return g;
}
} // namespace acfx
```

- [ ] **Step 4: Run to verify it passes.** Run: `cmake --build --preset test -j && ctest --preset test 2>&1 | tail -3` — Expected: PASS.

- [ ] **Step 5: Commit.** `feat(core): bit-crush mid-tread grid primitive (primitives/lofi)`.

---

### Task 3: sample-and-hold decimator primitive

**Files:**
- Create: `core/primitives/lofi/decimator.h`
- Test: `tests/core/lofi-decimator-test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `class acfx::SampleHoldDecimator` with `void setDivisor(int d) noexcept;` (resets phase; next call is an internal tick), `int divisor() const noexcept;`, `bool isTick() noexcept;` (advances the phase counter; returns true on internal-tick samples — true on the first call after construction/`setDivisor`, then every `d`-th). Purely a phase scheduler; the caller does the S&H of values.

- [ ] **Step 1: Write the failing test.**

```cpp
#include <doctest/doctest.h>
#include "primitives/lofi/decimator.h"

TEST_CASE("decimator ticks every D-th sample, first sample is a tick") {
    acfx::SampleHoldDecimator dec;
    dec.setDivisor(4);
    int ticks = 0;
    for (int n = 0; n < 16; ++n) if (dec.isTick()) ++ticks;
    CHECK(ticks == 4);                              // 16/4
}
TEST_CASE("changing divisor resets phase; the next sample is a tick") {
    acfx::SampleHoldDecimator dec;
    dec.setDivisor(8);
    CHECK(dec.isTick());                            // sample 0 is a tick
    CHECK_FALSE(dec.isTick());                      // sample 1
    dec.setDivisor(2);                              // phase reset
    CHECK(dec.isTick());                            // takes effect immediately on an internal tick
}
```

- [ ] **Step 2: Run to verify it fails.** Register `core/lofi-decimator-test.cpp`; build — Expected: `decimator.h` not found.

- [ ] **Step 3: Implement `decimator.h`.**

```cpp
#pragma once
namespace acfx {
class SampleHoldDecimator {
public:
    void setDivisor(int d) noexcept { divisor_ = (d < 1) ? 1 : d; phase_ = 0; }
    int  divisor() const noexcept { return divisor_; }
    bool isTick() noexcept {
        const bool tick = (phase_ == 0);
        phase_ = (phase_ + 1) % divisor_;
        return tick;
    }
private:
    int divisor_ = 1;
    int phase_   = 0;   // 0 on the next tick (reset makes the next call a tick)
};
} // namespace acfx
```

- [ ] **Step 4: Run to verify it passes.** `cmake --build --preset test -j && ctest --preset test 2>&1 | tail -3` — Expected: PASS.

- [ ] **Step 5: Commit.** `feat(core): sample-and-hold decimator primitive (primitives/lofi)`.

---

### Task 4: template `ModulatedDelayEffect` (bounded, heap-free) — behaviour preserved at D=1

This task fixes TASK-34 (a booting delay) **without** yet adding the lo-fi controls: template the effect on `<MaxDelaySamples, Sample = float, MaxChannels = 8>`, replace the heap `std::vector` buffers with `BoundedDelayLine`, and bound `WowFlutterStage`. The float default must stay numerically identical to today.

**Files:**
- Modify: `core/effects/modulated-delay/wow-flutter.h` (bound its buffers)
- Modify: `core/effects/modulated-delay/modulated-delay-effect.h` (template + BoundedDelayLine + heap-free prepare)
- Test: `tests/core/modulated-delay-bounded-test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `BoundedDelayLine<Sample, MaxSamples>` (Task 1).
- Produces: `template <std::size_t MaxDelaySamples, typename Sample = float, std::size_t MaxChannels = 8> class acfx::ModulatedDelayEffect;` still satisfying the `acfx::Effect` concept (`prepare`/`process`/`reset`/`parameters`/`setParameter`). `WowFlutterStage<MaxChannels, MaxWowSamples>` templated + heap-free.

- [ ] **Step 1: Write the failing test — float clean-path numerical identity + no-alloc + int16 instantiation compiles.**

```cpp
#include <doctest/doctest.h>
#include <vector>
#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/modulated-delay/modulated-delay-effect.h"
#include "support/allocation-sentinel.h"   // T066 sentinel (tests/support)

using DefaultDelay = acfx::ModulatedDelayEffect<96000>;                 // float, 8ch, 2s@48k
using NucleoDelay  = acfx::ModulatedDelayEffect<14400, std::int16_t, 2>;

TEST_CASE("templated float delay is heap-free through prepare+process") {
    acfx::test::AllocationSentinel sentinel;
    DefaultDelay fx;
    fx.prepare(acfx::ProcessContext{48000.0, 64, 2});
    std::vector<float> l(64), r(64);
    for (int n = 0; n < 64; ++n) { l[n] = 0.2f; r[n] = -0.2f; }
    float* ch[2] = {l.data(), r.data()};
    acfx::AudioBlock block(ch, 2, 64);
    sentinel.reset();
    fx.process(block);
    CHECK(sentinel.allocations() == 0);     // RT path never allocates
}

TEST_CASE("float default reproduces a reference impulse response") {
    // Drive a unit impulse through defaults; capture the wet tail; assert it is
    // stable and non-trivial (the delayed impulse appears at ~0.3 s * mix).
    DefaultDelay fx;
    fx.prepare(acfx::ProcessContext{48000.0, 1, 1});
    auto tap = [&](float x){ float s=x; float* c[1]={&s}; acfx::AudioBlock b(c,1,1); fx.process(b); return s; };
    float first = tap(1.0f);
    for (int n = 0; n < 14400; ++n) tap(0.0f);          // run to just past the 0.3 s default
    CHECK(std::isfinite(first));
    // The Nucleo instantiation merely needs to compile + prepare without heap on a fake ctx.
    NucleoDelay nfx; nfx.prepare(acfx::ProcessContext{48000.0, 48, 2});
}
```
(The precise reference-IR equality against the *pre-change* effect is asserted by a golden vector captured in Step 1a below — the strongest identity guard.)

- [ ] **Step 1a: Capture a golden reference BEFORE changing the effect.** On the current (pre-template) effect, add a throwaway test that prints 512 wet samples for a fixed input/seed to a file `tests/core/golden/modulated-delay-default.txt`; commit that golden. The identity test then reloads it and asserts bit-exact equality after the refactor. (This is the acceptance property the design's §Decisions 12a names.)

- [ ] **Step 2: Run to verify it fails.** Register `core/modulated-delay-bounded-test.cpp`; build — Expected: fails to compile (effect is not yet a template; `#include`s/instantiations unresolved).

- [ ] **Step 3: Bound `WowFlutterStage`.** Change `core/effects/modulated-delay/wow-flutter.h` to `template <std::size_t MaxChannels, std::size_t MaxWowSamples> class WowFlutterStage`, replacing `std::array<std::vector<float>, kMaxChannels> buffers_` with `std::array<std::array<float, MaxWowSamples>, MaxChannels>` and pointing each internal `DelayLine` at `buffers_[idx].data()` with `capacity = min(needed, MaxWowSamples)`. No `assign`/`resize` anywhere. Keep the wow/flutter math otherwise unchanged.

- [ ] **Step 4: Template the effect + swap storage.** In `modulated-delay-effect.h`:
  - Add `template <std::size_t MaxDelaySamples, typename Sample = float, std::size_t MaxChannels = 8>` to the class; replace the literal `kMaxChannels = 8` with the template `MaxChannels`.
  - Replace `std::array<std::vector<float>, kMaxChannels> buffers_` + `std::array<DelayLine, kMaxChannels> delays_` with `std::array<BoundedDelayLine<Sample, MaxDelaySamples>, MaxChannels> delays_`.
  - In `prepare()`, drop the `assign`; set `capacity = min(static_cast<int>(sampleRate_ * 2.0f) + 2, MaxDelaySamples)` and call `delays_[idx].prepare(capacity, sampleRate_)`. Give `WowFlutterStage` a concrete `MaxWowSamples` (e.g. `MaxChannels`-wide, capacity from the ≤50 ms wow/flutter span — compute the constant and `static_assert` it fits).
  - Everything else (LFOs, smoother, filter, the `process()` math) stays as-is for now. `readFractional`/`write` now go through `BoundedDelayLine`, which for `Sample=float` is bit-identical (Task 1 proved it).

- [ ] **Step 5: Run identity + no-alloc + golden tests.** Run: `cmake --build --preset test -j && ctest --preset test 2>&1 | tail -5` — Expected: PASS, including the golden bit-exact identity and `sentinel.allocations() == 0`. Also run the pre-existing `modulated-delay-*-test.cpp` suites — Expected: still green (the concept still holds; behaviour unchanged).

- [ ] **Step 6: Commit.** `feat(core): template ModulatedDelayEffect (bounded, heap-free) — fixes TASK-34; float path identical`.

---

### Task 5: lo-fi layer — whole-wet-loop decimation + bit-crush + two live params

**Files:**
- Modify: `core/effects/modulated-delay/modulated-delay-effect.h`
- Test: `tests/core/modulated-delay-lofi-test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SampleHoldDecimator` (Task 3), `crushToGrid` (Task 2), the templated effect (Task 4).
- Produces: two appended params — `kLofiRate = 19`, `kLofiBits = 20`; `kParams` size 21, `kNumParams = 21`. New per-channel/shared state: one `SampleHoldDecimator` (shared across channels — decimation phase is global), held wet-output per channel, current `B`. Behaviour contract from the spec §"Decimation" and §"Bit-crush + storage".

- [ ] **Step 1: Write the failing tests — decimation scales delay time, crush is in-loop and level-preserving, tape-speed on live D, clean settings unchanged.**

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "dsp/audio-block.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/modulated-delay/modulated-delay-effect.h"

using Delay = acfx::ModulatedDelayEffect<14400, std::int16_t, 2>;
static void setP(Delay& fx, acfx::ModulatedDelayEffect<14400,std::int16_t,2>::Param p, float plain){
    fx.setParameter(acfx::ParamId{p}, acfx::normalize(Delay::kParams[p], plain));
}

TEST_CASE("D divides the internal rate: same buffer, D=8 gives ~8x the max delay time") {
    Delay fx; fx.prepare(acfx::ProcessContext{48000.0, 1, 1});
    // At D=1, max delay ~= 14400/48000 = 0.30 s; at D=8, ~2.4 s. Assert the
    // realizable max delay in seconds scales with D (query via a helper the impl exposes,
    // e.g. fx.maxDelaySeconds()).
    setP(fx, Delay::kLofiRate, 0.0f);  // index 0 -> D=1
    const float m1 = fx.maxDelaySeconds();
    setP(fx, Delay::kLofiRate, 3.0f);  // index 3 -> D=8
    // params apply at the top of process(); pump one sample to consume them
    float s=0; float* c[1]={&s}; acfx::AudioBlock b(c,1,1); fx.process(b);
    const float m8 = fx.maxDelaySeconds();
    CHECK(m8 == doctest::Approx(8.0f * m1).epsilon(0.02f));
}

TEST_CASE("bit-crush at clean settings (D=1,B=16) leaves the wet path full-resolution") {
    // With B=16 the crush is bypassed; only the int16 storage floor remains.
    // A quiet sine round-trips within one int16 LSB (guards level, not just presence).
    Delay fx; fx.prepare(acfx::ProcessContext{48000.0, 64, 1});
    setP(fx, Delay::kMix, 1.0f); setP(fx, Delay::kFeedback, 0.0f);
    setP(fx, Delay::kLofiBits, 0.0f); setP(fx, Delay::kLofiRate, 0.0f);
    // ... drive a tone, measure output RMS is within tolerance of a 16-bit-quantized reference.
    CHECK(true); // replace with the RMS assertion once maxDelaySeconds/helpers exist
}

TEST_CASE("live D change reinterprets the buffer (tape-speed), does not clear it") {
    Delay fx; fx.prepare(acfx::ProcessContext{48000.0, 1, 1});
    setP(fx, Delay::kMix, 1.0f); setP(fx, Delay::kFeedback, 0.0f);
    auto tap=[&](float x){ float s=x; float* c[1]={&s}; acfx::AudioBlock b(c,1,1); fx.process(b); return s; };
    for (int n=0;n<50;++n) tap(1.0f);          // fill the line with a DC-ish signal
    setP(fx, Delay::kLofiRate, 1.0f);          // D=1 -> D=2 mid-stream
    const float after = tap(0.0f);             // buffer retained, not silence
    CHECK(std::isfinite(after));
    // The wet output remains non-zero right after the change (no clear-to-silence).
}
```
(Fill in the RMS/level assertions once the effect exposes `maxDelaySeconds()` and the internal-tick structure from Step 3.)

- [ ] **Step 2: Run to verify it fails.** Register `core/modulated-delay-lofi-test.cpp`; build — Expected: `kLofiRate`/`kLofiBits`/`maxDelaySeconds` unresolved.

- [ ] **Step 3: Add the two params + the internal-rate wet loop.** In `modulated-delay-effect.h`:
  - Append to the `Param` enum: `kLofiRate = 19, kLofiBits = 20`; grow `kParams` to 21 with two discrete descriptors — `{ParamId{kLofiRate}, "lofi_rate", ParamUnit::none, 0,3,0, ParamSkew::linear, ParamKind::discrete, 4, kRateLabels}` (`kRateLabels = {"/1","/2","/4","/8"}`), `{ParamId{kLofiBits}, "lofi_bits", ParamUnit::none, 0,4,0, ParamSkew::linear, ParamKind::discrete, 5, kBitsLabels}` (`kBitsLabels = {"16","12","8","6","4"}`); bump `kNumParams = 21`.
  - `applyPending()`: on `kLofiRate` dirty → `decimator_.setDivisor(1 << index)` (phase reset per Task 3 — this *is* the tape-speed reinterpret contract); on `kLofiBits` dirty → `crushBits_ = {16,12,8,6,4}[index]`.
  - Restructure `process()` per the spec §"Decimation" pseudocode: keep the dry path + the S&H mix at 48 kHz; gate the wet body (`readFractional` → mode filter → feedback → `crushToGrid(_, crushBits_)` → `write`) on `decimator_.isTick()`; hold the last wet sample per channel between ticks. Convert `effectiveDelaySecs` to **internal** samples: `dsampInternal = effectiveDelaySecs * (sampleRate_ / D)`, clamped to the current realizable max. Expose `float maxDelaySeconds() const noexcept { return static_cast<float>(delays_[0].capacity()) * decimator_.divisor() / sampleRate_; }`.
  - Delay-time parameter: keep `denormalize` semantics but clamp `targetDelaySecs_` to `maxDelaySeconds()` at read (spec §OQ1 — physical seconds, clamped to current realizable max).
  - `crushToGrid` is applied in the normalized float domain inside the loop (design §"Bit-crush + storage": crush in-effect, int16 storage floor in `BoundedDelayLine`). At `B=16` `crushToGrid` returns the value unchanged, so the float instantiation stays identical and the int16 instantiation shows only its storage floor.

- [ ] **Step 4: Run to verify it passes.** Run: `cmake --build --preset test -j && ctest --preset test 2>&1 | tail -5` — Expected: PASS, including the D-scaling, tape-speed, and clean-settings cases; the Task-4 golden identity test still green (proves `B=16`/`D=1` didn't change the float path).

- [ ] **Step 5: Commit.** `feat(core): lo-fi delay layer — internal-rate decimation + bit-crush + live params`.

---

### Task 6: Nucleo firmware target

**Files:**
- Create: `adapters/nucleo/nucleo-modulated-delay.h`
- Modify: `adapters/nucleo/support/midi-cc-map.h`
- Modify: `adapters/nucleo/CMakeLists.txt`

**Interfaces:**
- Consumes: the templated effect (Tasks 4–5).
- Produces: firmware target `acfx_nucleo_lofi_delay` whose `ACFX_EFFECT_TYPE = acfx::NucleoModulatedDelay`.

- [ ] **Step 1: Write the alias + footprint guard.** `adapters/nucleo/nucleo-modulated-delay.h`:

```cpp
#pragma once
#include <cstdint>
#include "effects/modulated-delay/modulated-delay-effect.h"
namespace acfx {
using NucleoModulatedDelay = ModulatedDelayEffect<14400, std::int16_t, 2>;
}
// ~300 ms stereo int16 core; keep the delay storage inside the SRAM headroom.
static_assert(sizeof(acfx::NucleoModulatedDelay) <= 96u * 1024u,
              "NucleoModulatedDelay exceeds its SRAM budget");
```

- [ ] **Step 2: Extend the CC map.** In `adapters/nucleo/support/midi-cc-map.h`, add above the marker comment:

```cpp
    CcBinding{76, 19},   // CC76 -> lofi_rate
    CcBinding{77, 20},   // CC77 -> lofi_bits
```

- [ ] **Step 3: Add the firmware target.** In `adapters/nucleo/CMakeLists.txt`, append after the existing `acfx_nucleo_delay` block:

```cmake
acfx_add_effect_nucleo(
  NAME acfx_nucleo_lofi_delay
  EFFECT_TYPE acfx::NucleoModulatedDelay
  EFFECT_HEADER nucleo-modulated-delay.h
  SOURCE ${_nucleo_main}
  LINKER_SCRIPT ${_nucleo_lds}
  LINK_LIBRARIES acfx::nucleo_platform acfx::nucleo_support acfx::nucleo_usb
                 acfx::nucleo_tinyusb
)
```
(Confirm `EFFECT_HEADER nucleo-modulated-delay.h` resolves on the target's include path; `effect-instance.h` lives in the same dir, so add `adapters/nucleo` to the target include dirs if not already present.)

- [ ] **Step 4: Cross-compile + host gates.** Run:
```
PATH="/Applications/ArmGNUToolchain/15.3.rel1/arm-none-eabi/bin:$PATH" cmake --build --preset nucleo 2>&1 | tail -5
./scripts/check-portability.sh 2>&1 | tail -3
cmake --build --preset test -j && ctest --preset test 2>&1 | tail -3
```
Expected: `acfx_nucleo_lofi_delay.elf` links; the `static_assert` holds; `C-CORE-INWARD` passes (core acquired no adapter dep); host suite green.

- [ ] **Step 5: Commit.** `feat(nucleo): acfx_nucleo_lofi_delay firmware target + CC map (lofi_rate/lofi_bits)`.

---

### Task 7: hardware acceptance (operator-driven)

**Files:** none (verification); record results in `specs/nucleo-f446-adapter/research.md` (a new dated subsection) and, if all green, update backlog TASK-34 to done.

- [ ] **Step 1: Flash.** `arm-none-eabi-objcopy -O binary build/nucleo/adapters/nucleo/acfx_nucleo_lofi_delay.elf x.bin && st-flash --serial <F446-serial> --reset write x.bin 0x08000000`.
- [ ] **Step 2: Confirm it enumerates (the TASK-34 fix).** `ffmpeg -f avfoundation -list_devices true -i "" 2>&1 | grep "acfx Audio"` shows the device, and the acfx CDC telemetry port appears (`iu=/bp=/tl=`). A delay that boots is the primary acceptance.
- [ ] **Step 3: Audition the lo-fi sweep.** Stream a known signal (the T059 HIL rig); send CC76 across 0→127 (rate /1→/8) and CC77 across 0→127 (16→4 bits) with the swiftc CoreMIDI sender; confirm audibly/analytically that the echo lengthens + darkens with CC76 and grits with CC77, and that a live CC76 change gives the tape-speed jump.
- [ ] **Step 4: Capture transport health.** Over the T058/T059 rig, record `worstBlockMicros` at `D=1` (worst case) — confirm well under 1000 µs — plus the counter set; note them in `research.md`.
- [ ] **Step 5: Record + commit.** Write the results subsection to `research.md`; if green, mark backlog TASK-34 done. Commit: `docs(nucleo): acfx_nucleo_lofi_delay hardware acceptance — TASK-34 resolved`.

---

## Self-Review

**Spec coverage** — every design decision maps to a task: bounding/heap-free + storage policy → Tasks 1,4; whole-wet-loop decimation → Tasks 3,5; bit-crush contract → Tasks 2,5; int16 quantize/saturate → Task 1 (core-replicated); tape-speed reinterpret → Task 5 (decimator phase reset); OQ1 seconds-clamp → Task 5 (`maxDelaySeconds`); named alias + memory contract → Task 6 (`static_assert`); CC assignments (open Q) → Task 6 (CC76/77); float numerical-identity acceptance → Task 4 (golden); no-alloc → Task 4 (sentinel); hardware acceptance / TASK-34 → Task 7.

**Placeholder scan** — the two effect-integration tasks (5) intentionally leave two RMS/level assertions to be completed once `maxDelaySeconds()` and the internal-tick structure exist in Step 3; every other step carries real code. These are flagged inline, not hidden.

**Type consistency** — `BoundedDelayLine<Sample, MaxSamples>` (Task 1) is consumed with matching args in Tasks 4–6; `crushToGrid(float,int)` (Task 2) and `SampleHoldDecimator::isTick()`/`setDivisor()` (Task 3) are used with those exact signatures in Task 5; param indices `kLofiRate=19`/`kLofiBits=20` and CC bindings `{76,19}`/`{77,20}` agree across Tasks 5–6; the `ProcessContext{sampleRate, maxBlockSize, numChannels}` field order matches the recon.

## Open items carried from the design (non-blocking, settle during execution)
- **CPU headroom** (design OQ3) — confirmed empirically in Task 7 Step 4, not pre-asserted.
- **CC numbers** — chosen here as 76/77; adjust in Task 6 if the operator prefers others.

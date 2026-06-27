### Discrete parameter out-of-range clamping is not tested

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:57-65

The `TEST_CASE("out-of-range normalized inputs clamp to the parameter bounds")` block (lines 57–65) exercises `linearRes` and `logCutoff` with out-of-range normalized values (`-0.5f`, `1.5f`, `-1.0f`, `2.0f`) but omits `discreteMode` entirely. Discrete parameters are the highest-risk case for this invariant: without clamping, `floor(n * count)` for `n = 1.0f` already produces `count` (an out-of-bounds index, as the test itself acknowledges in the comment at line 52), and for `n > 1.0f` the result is further over-range. Plugin hosts and DAW automation curves routinely send denormalized values slightly outside `[0, 1]` due to floating-point arithmetic on their end. The test suite explicitly claims to cover "out-of-range…clamp to the parameter bounds" for the parameter system as a whole, but the discrete path is entirely unexercised. If the clamping implementation for discrete parameters is ever written as a separate branch from the linear/log path (a common implementation shape since the floor-based quantization is structurally different), a regression would pass this suite undetected. A fix is to add `CHECK(denormalize(discreteMode, -0.5f) == doctest::Approx(0.0f))` and `CHECK(denormalize(discreteMode, 1.5f) == doctest::Approx(2.0f))` to the existing test case.

---

### `EffectNode` no-allocation test omits parameter changes on the audio thread

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:43-57

The `TEST_CASE("EffectNode<SvfEffect>::processBlock allocates nothing")` (lines 43–57) only calls `node.processBlock(block)` inside the measured region; it never calls any form of `setParameter` on the node. By contrast, the `SvfEffect` test at line 32 explicitly includes `fx.setParameter(...)` inside the allocation-measured loop with the comment "parameter changes on the audio thread must also be allocation-free." If `EffectNode` has its own parameter-dispatch path — e.g., a lock-free command FIFO, a ring buffer, or an `std::function` dispatch — that path's heap footprint is invisible to this test. The project's RT-safety invariant covers the entire audio-thread call chain through the host boundary, not just the bare effect. The fix is to call `node.setParameter(...)` (or however `EffectNode` exposes parameter updates) inside the `EffectNode` measured loop, mirroring the pattern already established for `SvfEffect`.

---

### `EffectNode` no-allocation test covers only a single block size

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:44-45

The `SvfEffect` no-allocation test iterates over `{16, 64, 256, 512}` (line 22), exercising block sizes an order of magnitude apart to catch conditional buffer management that might allocate on a first-seen size. The `EffectNode` test hard-codes `blockSize = 256` (lines 44–45) and runs only that one size. If `EffectNode` adds any size-adaptive bookkeeping — internal scratch buffers, channel-count re-shaping — the zero-allocation property is only verified at the single point used most commonly in tests. The fix mirrors the `SvfEffect` loop: parameterise the `EffectNode` test over the same size set `{16, 64, 256, 512}`.

---

### `ParameterDescriptor` aggregate initializers are positionally fragile

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:14-29

The three `constexpr ParameterDescriptor` objects (`linearRes`, `logCutoff`, `discreteMode`) use space-aligned positional aggregate initialization rather than C++20 designated initializers. If the field order in `ParameterDescriptor` is ever changed — reordering `min`/`max`/`default`, adding a field between existing ones, or re-ordering `ParamSkew` and `ParamKind` — these initializers silently assign values to the wrong fields; the code compiles cleanly and produces wrong tests rather than a build error. This is especially risky for a descriptor type: a `float` field misrouted to another `float` field (e.g., `min` landing in `defaultValue`) will pass template-type checks and produce subtly wrong test coverage. Designated initializers (`{.id = …, .name = …, .unit = …, …}`) would make any field reorder a compile error at the test site. The cost is a one-time rewrite; the defensive value persists across every future field addition to `ParameterDescriptor`.
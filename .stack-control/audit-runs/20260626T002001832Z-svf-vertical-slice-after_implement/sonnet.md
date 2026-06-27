I'll audit this diff carefully against the stated correctness, design, edge-case, and code-quality criteria.

---

### Float truncation in `toMode()` silently selects wrong filter mode on borderline normalized values

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:97–105 (`toMode()`) and :88–91 (`setParameter` mode branch)

The `setParameter` path for `kMode` calls `toMode(denormalize(kParams[kMode], normalized))` then casts to `int` via `static_cast<int>(index)`, which truncates toward zero. The parameter range is `[0.0f, 2.0f]` and the three legal values are 0, 1, 2. A normalized input of exactly `1.0f` should denormalize to `2.0f` (bandpass), but floating-point denormalization arithmetic can produce `1.9999998f` instead of `2.0f`, and `static_cast<int>(1.9999998f)` yields `1` (highpass), silently selecting the wrong mode. The same truncation hazard exists at the `0.0f/1.0f` boundary.

The blast-radius here is behavioral: users or DAW automation targeting bandpass would hear highpass instead, with no error or diagnostic. The fix is one line: replace `static_cast<int>(index)` with `static_cast<int>(std::roundf(index))` (or `std::lroundf`, clamped to `[0, 2]`). The `<cmath>` include is already implied by the DaisySP dependency path.

---

### `ProcessorNode` / `EffectNode` have no thread-safety contract, but audio and UI threads share mutable filter state

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    host/processor-node/processor-node.h:17–42, and core/effects/svf/svf-effect.h:57–83

In a DAW plugin host, `processBlock()` is called from the real-time audio thread and `setParameter()` is called from the message/UI thread. The `EffectNode<T>` template stores `T fx_` by value and forwards both calls directly into the same object with no synchronization. For `SvfEffect`, `setParameter` writes `cutoffHz_`, `resonance_`, `mode_` and then calls `applyCutoff()` / `applyMode()` which mutate `filters_[]` in place; `process()` reads from the same `filters_[]` in the same moment. This is a data race under the C++ memory model (undefined behavior), even though x86's strong memory ordering makes it appear to work in testing.

The `ProcessorNode` interface's docstring (and `EffectNode`) is silent on thread safety, meaning any implementation that forwards directly — exactly as `EffectNode` does — will have this race. The fix belongs in `EffectNode` (or in the plugin adapter): parameter changes should be buffered through an `std::atomic` or a lock-free queue and applied at the start of `processBlock`, before the process loop begins. The current silence in the interface is the load-bearing defect: it invites future effect implementations to build the same race without realizing it.

---

### `SvfPrimitive::reset()` silently discards frequency/resonance settings; safe only when called through `SvfEffect`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/primitives/svf-primitive.h:35–37 (`reset()`)

`SvfPrimitive::reset()` calls `svf_.Init(sampleRate_)`, which re-initializes DaisySP's internal Svf and clears its coefficient state (frequency, resonance). `SvfPrimitive` does not cache the last `setFreq`/`setRes` values, so after `reset()` the DaisySP filter reverts to its default frequency and resonance. This is safe when called through `SvfEffect::reset()` because `SvfEffect` calls `applyAll()` afterward, restoring all parameters from its own cached `cutoffHz_` / `resonance_` / `mode_`.

However, `SvfPrimitive` is a public header in `core/primitives/`, and its API (init / setFreq / setRes / process / reset) presents a self-contained abstraction. A consumer building directly on `SvfPrimitive` — e.g., a future adapter or unit test that calls `reset()` expecting "cleared state with preserved settings" — will get silently degraded audio instead of an error. The blast-radius is limited today because only `SvfEffect` uses the primitive, but the abstraction boundary leaks. A one-line fix: store `freq_` and `res_` in `SvfPrimitive` and re-apply them at the end of `reset()`.

---

### `check-portability.sh` Gate 4 linkage check matches string, not CMake target; a comment satisfies it

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:51–57

Gate 4 verifies that every adapter links `acfx_core` by running `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"`. This is a substring match: a line like `# This target does NOT use acfx_core` or a stale comment from a copy-paste passes the check even though the adapter doesn't actually link the target. The check should use a stricter pattern such as `grep -qE 'target_link_libraries\([^)]*acfx[_:]?core'` to anchor the match to an actual CMake directive. In its current form the gate can give a false-green as soon as the string appears anywhere in the file, which undermines the Scenario-E (one-source-many-targets) guarantee it is meant to enforce.

---

### `span` polyfill `Container` constructor SFINAE does not guard `size()`; substitution failures are hard compile errors

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   informational
Surface:    core/dsp/span.h:37–39

The converting constructor for the polyfill `span` uses SFINAE on `data()` only:

```cpp
template <typename Container,
          typename = decltype(static_cast<T*>(std::declval<Container&>().data()))>
constexpr span(Container& c) noexcept : data_(c.data()), size_(c.size()) {}
```

If a type has a `data()` method returning a compatible pointer but no `size()` method (e.g., a custom buffer type that exposes length by a different name), the SFINAE fires (selects this constructor) but the body then fails to compile. This is a hard error, not a substitution failure, so the compiler cannot fall back to another overload. In `std::span` (C++20) this is handled by requiring `std::ranges::sized_range`. For the subset of uses in this codebase today (only `std::array` and raw arrays) this is not a problem; it becomes one if a future `AudioBlock`-backed container that has `data()` but not `size()` is passed. No action required for the current scope, but the comment should not claim the constructor "binds to any contiguous container exposing data()/size()" when the SFINAE condition only guards `data()`.
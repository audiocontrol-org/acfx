### C++17 duck-type fallback for `Effect` concept is never compiled by the test suite

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/CMakeLists.txt:17

`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)` forces the entire test binary to C++20. T011 explicitly specifies a C++17 duck-typed fallback for the `Effect` concept, guarded by `__cpp_concepts`. That fallback path is never compiled by any test in this suite. If the fallback is broken (wrong duck-type signature, missing method, compile error under C++17), `ctest --preset test` stays green because C++20 is always used. The cross-standard portability claim is therefore untested at the host level. A separate test target compiled with `cxx_std_17` (or a CI matrix job with `-std=c++17`) is needed to exercise the fallback path.

---

### `EffectNode` allocation test omits `setParameter` call through the node boundary

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:45-57

The `EffectNode<SvfEffect>` allocation test (the second `TEST_CASE`) calls only `node.processBlock(block)` inside the sentinel-measured loop; it never calls `node.setParameter(...)`. `EffectNode` is a host-boundary wrapper and its parameter-dispatch path is distinct from `SvfEffect::setParameter` — it may involve a `std::function`, a virtual dispatch, or lock-based routing depending on its implementation. If any of those code paths allocate, this test misses it entirely. The `SvfEffect` standalone test does call `setParameter` (line 35), so the bare-effect path is covered. The gap is specifically the node's own dispatch logic. Adding `node.setParameter(ParamId{SvfEffect::kCutoff}, ...)` inside the measured loop on line 52 (mirroring what the standalone test does) would close this.

---

### Only `kCutoff` is exercised in the standalone `setParameter` allocation check

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:35

The measured loop at line 35 calls `fx.setParameter(ParamId{SvfEffect::kCutoff}, ...)` exclusively. `SvfEffect` has at minimum three parameters — cutoff (log), resonance (linear), and mode (discrete). If mode switching (a discrete parameter type) takes a different code path — e.g., triggers an internal mode-enum branch that allocates — the no-allocation invariant would not be caught. Adding two more `setParameter` calls per iteration, cycling through resonance and mode, would cover all three parameter kinds without meaningful test-complexity cost.

---

### `normalize` is not tested with out-of-range raw values

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:61-65

The clamping test block (`TEST_CASE("out-of-range normalized inputs clamp…")`) tests only `denormalize` with normalized inputs outside `[0, 1]` (e.g., `-0.5f`, `1.5f`, `-1.0f`, `2.0f`). The `normalize` direction — calling `normalize(desc, value)` with a raw value below `minValue` or above `maxValue` — is not tested. If `normalize` does not clamp, it can return a normalized result outside `[0, 1]`, which downstream callers (MIDI binding, plugin host automation) may propagate back through `setParameter` without re-clamping. Adding `CHECK(normalize(linearRes, -0.5f) == doctest::Approx(0.0f))` and `CHECK(normalize(logCutoff, 30000.0f) == doctest::Approx(1.0f))` would close this gap symmetrically with the existing `denormalize` checks.
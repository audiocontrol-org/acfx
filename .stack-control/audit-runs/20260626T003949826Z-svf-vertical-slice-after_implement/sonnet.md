### Implicit integer encoding of `SvfMode` in `MonoDriver` silently missets mode on enum change

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:39–43

`MonoDriver` converts the mode enum to an int index and feeds it through `normalize()`:

```cpp
const float modeIndex = static_cast<float>(static_cast<int>(mode));
fx.setParameter(ParamId{SvfEffect::kMode},
                normalize(SvfEffect::kParams[SvfEffect::kMode], modeIndex));
```

This embeds two implicit contracts that are not asserted anywhere in the test file: (1) the `SvfMode` enum values are exactly `{0, 1, 2}` in declaration order, and (2) `kParams[kMode]` is parameterized as a linear range over those same integer values so that `normalize(…, 0.0f)` → lowpass, `normalize(…, 1.0f)` → highpass, etc. Both assumptions are unverifiable from this chunk; neither enum definition nor `kParams` definition appears here.

If the enum gains a value, reorders, or is later given explicit integer assignments (common when syncing with a plugin host's value list), or if the parameter min/max changes, `MonoDriver` will construct the wrong mode with no compile-time or run-time diagnostic. The test will then make assertions about the wrong filter type and can trivially pass or trivially fail against an unrelated behavior. The blast radius is not a crash — it is a test that says "lowpass is correct" when it is actually measuring bandpass, or vice versa, silently masking a regression.

A reasonable fix is to use a typed `setMode()` API if one exists, or at minimum add a `static_assert` anchoring the enum integer values and a `CHECK` that normalizing index 0 produces 0.0f and normalizing index 2 produces 1.0f, so that changes in either the enum or the parameter range surface immediately.

---

### Stability test upper bound `< 100.0f` permits large unbounded output

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:97

The high-resonance stability test feeds an impulse into a bandpass at `resonanceNorm = 0.99f` and asserts:

```cpp
CHECK(maxAbs < 100.0f);
```

A correctly implemented SVF with normalised resonance approaching but not reaching 1.0 should be BIBO-stable: output should not significantly exceed the amplitude of the input impulse (1.0f). A threshold of 100 — 40 dB above the input — would pass an implementation that is numerically unstable in a moderate way, e.g. one that drifts slowly upward due to missing denormal flushing or coefficient quantization artifacts. The test's true diagnostic intent is "doesn't blow up", but the numeric sentinel chosen is 100× looser than necessary to achieve that.

The `REQUIRE(std::isfinite(out))` guard inside the loop is actually the more useful check and will catch actual divergence. The `CHECK(maxAbs < 100.0f)` adds little incremental value at its current threshold; tightening to something like `< 4.0f` (modest headroom above the impulse) would make it catch early drift without false failures on a correct self-oscillating resonator.

---

### Passband and stopband tolerances in `svf-reference.h` allow substantially degraded filters to pass

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/support/svf-reference.h:43–48

```cpp
inline constexpr double kPassbandGainMin = 0.7; // generous: SVF passband ~0 dB
inline constexpr double kStopbandGainMax = 0.25; // << passband
```

A 2nd-order SVF lowpass at 1000 Hz cutoff should exhibit:
- Gain at 100 Hz (passband): ≈ 0.998 (essentially unity)
- Gain at 8000 Hz (stopband): ≈ (1000/8000)² ≈ 0.016

The test instead accepts passband gain as low as 0.7 (about −3 dB) and stopband gain as high as 0.25 (only 12 dB of attenuation where a 2nd-order filter delivers ~32 dB). This means an implementation that is mistuned by nearly an octave, or that is first-order instead of second-order, could pass all these checks. The comment acknowledges this ("generous") but the accepted slack is wide enough that incorrect implementations are not reliably caught. This reduces the diagnostic value of the entire frequency-response test suite for catching coefficient calculation bugs.

---

### Global `operator new` replacements count doctest-internal same-thread allocations in measurement window

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    tests/support/allocation-sentinel.cpp:28–54

The sentinel replaces the global `operator new`/`delete` and tracks heap traffic in thread-local counters. Because doctest runs all assertions on the same OS thread as the test body, any allocation the doctest framework makes between `AllocationSentinel::reset()` and the final counter assertion will inflate the measured count. In practice, passing `CHECK`/`REQUIRE` macros in doctest do not allocate (failure recording does, but that path isn't taken when assertions pass), so the risk in the normal case is low. However, if doctest's `TEST_CASE` machinery — registration, result accumulation, or subcase stack — performs a deferred same-thread allocation during the measured window, the no-allocation test (in another chunk, not visible here) could produce spurious non-zero counts without any actual RT-path allocation.

The thread-local design is correct and well-suited to a multi-threaded real-time test scenario. The fragility noted here is specific to the measurement window discipline: the window must be opened (`reset()`) immediately before the first `process()` call and closed immediately after the last one, with no intervening framework calls. This is a documentation and usage concern rather than a design bug in the sentinel itself.
### `normalize` linear path divides by zero when `d.min == d.max`, unlike the guarded logarithmic path

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/dsp/parameter.h:79–97 (normalize, linear branch)

`denormalize` for the logarithmic case carries an explicit `assert(d.min > 0.0f && d.max > d.min)`. `normalize` for the logarithmic case carries the same assert. Neither function carries any guard on the linear path, which closes with:

```cpp
return (plain - d.min) / (d.max - d.min);
```

When `d.min == d.max`, `d.max - d.min` is exactly `0.0f`, so `(plain - d.min) / 0.0f` yields `NaN` (both numerator and denominator are zero after the preceding `std::clamp`). A NaN emerging here would flow directly into `setParameter`, then into the filter's internal state (`g`, `k`, `ic1`, `ic2`), where it becomes sticky and irrecoverable without a full `reset()`. The blast radius is: a single misconfigured `ParameterDescriptor` (e.g., from a copy-paste error, a merge conflict, or an in-development tweak) silently poisons the audio path. The fix is to mirror the logarithmic guard: `assert(d.max > d.min && "linear parameter requires d.min < d.max")` immediately before the linear return in both `denormalize` and `normalize`.

---

### `normalize` does not protect against NaN `plain` input; `std::clamp` is UB on NaN

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:73 (`plain = std::clamp(plain, d.min, d.max)`)

`denormalize` guards its input with `detail::clamp01`, which was specifically written (per the comment at line 35) to neutralize NaN — "a guard that fails open" is exactly what it avoids. `normalize` applies no equivalent guard. `std::clamp(NaN, lo, hi)` is undefined behaviour per the C++ standard: the internal comparisons on NaN all return false, and the standard says the behaviour is unspecified/UB when `lo > value` or `hi < value` yield unexpected results due to non-total ordering. In practice on most toolchains `std::clamp(NaN, lo, hi)` returns `NaN`, but that is an implementation detail, not a guarantee. Blast radius: a caller passing a NaN `plain` value (e.g., a DAW automation lane that sends an initial `NaN` before the host has finished its buffer fill) would produce UB in a debug build and a NaN-propagating normalized value in release. Fix: add a NaN guard analogous to `clamp01` before the `std::clamp` call in `normalize`, e.g., `if (!(plain == plain)) plain = d.defaultValue;` or a symmetric `clampFinite` helper.

---

### `core/effects/svf/svf-effect.h` listed in chunk scope but has no diff; `SvfPrimitive`'s clamping contract is unverified

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:28–30; core/effects/svf/svf-effect.h (absent from diff)

`svf-primitive.h` lines 28–30 document:

```cpp
// f in Hz. DaisySP requires 0 < f < sampleRate/3; the caller (SvfEffect)
// clamps cutoff into that range before calling.
```

`core/effects/svf/svf-effect.h` is explicitly listed as "Files in scope" for this chunk but provides no diff. The critical safety invariant — that every `setFreq` call is preceded by a clamp to `(0, sampleRate/3)` — lives entirely in that absent file. `SvfPrimitive::setFreq` performs no clamping itself; a `setFreq(0)` or `setFreq(sampleRate)` call would pass an out-of-range value directly to `daisysp::Svf::SetFreq`, which is documented to have undefined numerical behaviour outside that range. Without seeing the `SvfEffect` diff, the audit cannot confirm the contract is honoured, and a future refactor that adds a second caller of `setFreq` would break the assumption silently. Fix: either move the clamp into `SvfPrimitive::setFreq` (making the invariant local to the primitive) or add an `assert` that fires in debug when the precondition is violated, and provide the missing diff for `svf-effect.h`.

---

### `Effect` concept requires a *static* `parameters()` while `ProcessorNode` declares it as a virtual *instance* method — design mismatch at the core abstraction boundary

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/dsp/effect.h:24; host/processor-node/processor-node.h:23

The `Effect` concept in `effect.h` line 24 uses `{ T::parameters() }`, which in C++20 concept syntax tests a **static** member function call (no instance variable is used). `ProcessorNode` in `processor-node.h` line 23 declares:

```cpp
virtual span<const ParameterDescriptor> parameters() const = 0;
```

This is a **virtual instance** method. `EffectNode` bridges the two by calling `T::parameters()` from the virtual override, which compiles, but the design contract is now split: anything that satisfies `Effect` must have a static `parameters()`, while anything that satisfies `ProcessorNode` exposes an instance `parameters()`. An author writing a new `EffectNode<MyEffect>` must intuit that `MyEffect::parameters` must be `static`, but neither the concept's macro expansion (`ACFX_EFFECT_CONCEPT`) in C++17 mode (which degrades to `typename`, no enforcement) nor the `ProcessorNode` abstract interface gives that signal. The C++17 fallback also means the static requirement vanishes on Teensy, so a Teensy-only `Effect` type could accidentally provide an instance `parameters()`, compile fine, and fail only when brought to a C++20 host. Fix: document the `static` requirement explicitly in the macro comment and, if feasible, add a `static_assert(std::is_invocable_v<decltype(&T::parameters)>)` in `EffectNode`.

---

### Discrete `discreteCount < 2` silently maps to 2 rather than asserting a configuration error

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:44–46 (denormalize), parameter.h:65–68 (normalize)

`ParameterDescriptor` documents `discreteCount >= 2 when kind == discrete, else 0`. Both `denormalize` and `normalize` silently clamp an invalid `discreteCount` to `2`:

```cpp
const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);
```

A `ParameterDescriptor` constructed with `kind == discrete` but `discreteCount == 0` or `1` (a configuration mistake) would silently operate as a two-bucket discrete parameter, masking the error until a developer noticed unexpected behaviour at runtime. By contrast, the logarithmic path explicitly asserts its preconditions. An `assert(d.discreteCount >= 2 && "discrete parameter requires discreteCount >= 2")` here would surface the misconfiguration at the earliest call site in debug builds, consistent with the project's stated approach ("raise descriptive errors for missing functionality instead" per Constitution and CLAUDE.md).
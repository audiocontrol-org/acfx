I'll audit this chunk directly — it's a single review pass over the provided diffs, not a multi-agent orchestration. Let me walk the files carefully and emit findings in the required format.

I read all eight displayed diffs (plus noted what's missing), traced the parameter round-trip across the plugin/teensy/workbench adapters, and checked the RT-safety and channel claims against the descriptor contract they depend on.

---

### Mode names are config-as-code in the plugin, with a silent mislabeling fallback

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-33 (modeName) and :53-56 (build loop)

`modeName(int index)` hardcodes the SVF mode labels (`"lowpass"`/`"highpass"`/`"bandpass"`) as a `switch` inside the plugin adapter, keyed on a raw index, with `case 0: default:` collapsing to `"lowpass"`. The `ParameterDescriptor` table carries `discreteCount` but no names, so the human-readable mode vocabulary — which is a property of the *effect*, not the *plugin* — lives as code in one adapter. That is "configuration that should be data ending up as code," and it is per-adapter: the teensy and workbench adapters have no equivalent, so the names are not even shared.

The concrete trap is the `default` arm. The build loop iterates `for (int i = 0; i < d.discreteCount; ++i) choices.add(modeName(i))`. The moment the SVF gains a fourth mode (e.g. notch) and `discreteCount` becomes 4, this code silently adds a second choice literally labeled `"lowpass"` for index 3 — a wrong, duplicated UI label — while the underlying automation index is still distinct. Nothing in this file fails or warns; the descriptor count and the name table drift apart silently.

Blast radius: today, with exactly three modes, the labels are correct and the feature works, so this is latent rather than active — hence medium, not high. But an unattended agent extending the SVF would add the mode to the core descriptor (the natural place) and ship a mislabeled, duplicate-named host parameter without any signal that the plugin's name table needed updating. A reasonable fix is to source mode names from the effect/descriptor (e.g. an accessor on `SvfEffect` or a `names` span on the descriptor) so there is a single source of truth, and to make an out-of-range index an explicit error rather than a `default` fallthrough to `"lowpass"`.

---

### `ParameterDescriptor::defaultValue` carries two different meanings by kind, unenforced in this file

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:53-67

In `build()`, the same field `d.defaultValue` is interpreted two incompatible ways depending on `d.kind`. Discrete: `const int defaultIndex = static_cast<int>(d.defaultValue);` — `defaultValue` is treated as a *choice index*. Continuous: `const float defaultNorm = normalize(d, d.defaultValue);` — `defaultValue` is treated as a *plain engineering value* (Hz, dB, …) to be normalized. So whether `defaultValue` means "index 1" or "1.0 in plain units" is a function of `kind`, and this dependency is implicit — there is no comment or assertion in this file documenting it.

This matters because the truncating cast hides mistakes. If a descriptor author sets a discrete `defaultValue` of `0.5f` intending "the middle of normalized range" (the convention the continuous branch uses), `static_cast<int>(0.5f)` silently yields index 0 — no error, just the wrong default mode. The `normalize`/`denormalize` round-trip the rest of the file leans on is correct *only if* every descriptor obeys the unstated plain-vs-index rule.

Blast radius: medium. The current SVF descriptor presumably obeys the convention, so it ships correctly today; the risk is an agent adding a parameter and reaching for the wrong reading of `defaultValue`, which truncation will not catch. A fix is to either document and assert the contract at the descriptor definition (`dsp/parameter.h`, cross-file) or to give discrete descriptors an explicit `defaultIndex` distinct from continuous `defaultValue`, so the two meanings are not overloaded onto one float.

---

### Teensy int16 conversion clamps range but not NaN/Inf, leaving a UB cast at the hardware boundary

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:35-43

The output conversion does `float v = samples[i] * kFloatToInt16;` then `if (v > 32767.0f) v = 32767.0f; if (v < -32768.0f) v = -32768.0f; block->data[i] = static_cast<int16_t>(v);`. Both comparisons against a NaN evaluate false, so a NaN survives the clamp untouched and reaches `static_cast<int16_t>(NaN)`, which is undefined behavior — typically producing an arbitrary int16, i.e. a loud transient on real speakers/headphones. The range clamp is real defense against overshoot, but it is specifically the one shape (NaN) it cannot catch.

This is the MCU sibling of the NaN handling prior rounds added inside the SVF clamp. If that upstream guard fully holds, no NaN ever reaches this line and the consequence is nil — which is why this is low, not higher. But it is the only place in the chain that converts to a fixed-point hardware format, the conversion is the last line of defense before the codec, and a NaN-safe variant is one extra `if (!(v == v)) v = 0.0f;` (or `std::isnan`) before the cast — cheap insurance at the boundary rather than relying solely on an upstream invariant.

---

### `apply()` computes the discrete normalized value with a channel-count that can diverge from the effect's actual count

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-parameters.h:25-31

For discrete parameters, `apply()` does `const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;` and then `norm = (index + 0.5f) / count`. The effect, on the receiving end of `fn(id, norm)`, denormalizes using its *own* descriptor count to recover the index. For `discreteCount >= 2` the two counts agree and the `(index+0.5)/count` bucket-center round-trips exactly. But when `discreteCount == 1`, `apply()` substitutes `2` while the effect's `denormalize` still uses the real count of 1 — the two stages disagree on the divisor, and correctness is rescued only incidentally because `floor(0.25 * 1) == 0`.

The clamp-to-2 is defensive against a divide-by-something-degenerate, but it introduces a quiet inconsistency: the normalized value `apply()` emits no longer corresponds to the effect's actual bucketing; it merely happens to floor back to the right index. A single-mode discrete parameter is not in the current SVF, so the consequence today is zero — hence low. The cleaner fix is to use the descriptor's real count on both sides (the effect already owns the authoritative count) and reject `discreteCount < 1` as a malformed descriptor at `build()` time rather than papering over it with a magic `2` at apply time.

---

### `adapters/workbench/audio-source.cpp` is listed in scope but absent from the audited diff, so its load-bearing RT/enforcement claims are unverifiable here

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.h:18-29 (claims) vs missing audio-source.cpp

The chunk header lists `adapters/workbench/audio-source.cpp` in "Files in scope," and `audio-source.h` makes three strong, safety-critical promises in its header comment: `fillBlock()` "never throws, never allocates, takes no locks" (Constitution VI); the file is "decoded into an in-memory buffer before the stream starts"; and the selection precondition is "ENFORCED — a selection call while already configured throws." Every one of those guarantees is implemented in the `.cpp`, which does not appear in the displayed diffs and is not listed in any of the "Other chunks" file lists either. The header alone cannot establish that `fillBlock` is allocation-free, that the `configured_` guard actually throws, or that `playPos_`/`fileBuffer_` are accessed under the claimed discipline.

This is a chunk-boundary/coverage gap rather than a code defect, so it is low severity — but it is worth surfacing because the unverifiable claims are exactly the RT-safety and race-enforcement properties the feature's constitution treats as non-negotiable. The header's own note that `configured_` is "written on the audio/device thread (prepare/release)" is also imprecise — `prepare`/`release` are setup-lifecycle calls, not the audio callback — which is harmless wording on its own but means the actual cross-thread ordering can only be judged from the missing `.cpp`. The operator triaging this chunk should pull `audio-source.cpp` into the audited surface before signing off the RT-safety acceptance criteria.

---

### Teensy executable sets no C++ standard and inherits it from `acfx_core`; verify that does not collide with the ">=17, degrade-to-C++17" promise

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/teensy/CMakeLists.txt:26-38 (no compile-feature) vs the comment at :7-13

The CMakeLists comment promises the Teensy build uses "the highest the installed Teensy toolchain supports (>=17)" and that "on C++17 the Effect concept degrades to a duck-typed template." But `add_executable(acfx_teensy …)` sets no `target_compile_features`/`CMAKE_CXX_STANDARD`, whereas the workbench target explicitly pins `cxx_std_20`. The teensy executable therefore gets its standard entirely from whatever `acfx_core` propagates. If `acfx_core` declares `target_compile_features(acfx_core PUBLIC cxx_std_20)` (the root CMakeLists is in chunk `6a56…`, not visible here), then `acfx_teensy` *requires* C++20 — directly contradicting the "degrade to C++17" story and breaking on any Teensy toolchain that tops out at 17.

This is low and conditional: it is correct only if `acfx_core` exposes a `cxx_std_20` requirement, which this chunk cannot see. But the comment makes a portability commitment (SC-007 / Constitution IV) that a single PUBLIC compile-feature in the core target would silently invalidate, with the failure surfacing only when someone actually builds against a 17-only toolchain. The operator should confirm `acfx_core`'s declared compile feature is `cxx_std_17` (or that the requirement is INTERFACE-compatible with 17); if the core pins 20, either the comment here is wrong or the portability claim is.
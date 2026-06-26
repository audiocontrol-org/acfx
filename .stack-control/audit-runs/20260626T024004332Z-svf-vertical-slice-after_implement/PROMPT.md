# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

9ae4b58 Address round-5 govern findings: compile-time validation, RT alloc, CI, source path
3262fb3 Address round-4 govern findings: NaN-safe clamp, channel consistency, honest ledger
2406235 Address round-3 govern findings: precise contracts, lock-free atomics, enforced precondition
2fef393 Address round-2 govern findings: RT-safety, error surfacing, adapter races
bd79479 Address govern findings: RT-safety, thread ownership, doc drift
8e0e37b Replace vendored CPM.cmake with pinned auto-download bootstrap
f88525b Close acceptance tasks with honest verified/manual split (T027/T031/T035)
60b4523 Fix desktop build integration + JUCE-API bugs caught by real compilation
ee53b33 Phase 6 (polish): CI, explicit portability gates, README
ae69f91 Phase 5 (US3): Daisy + Teensy adapters; core proven ARM-portable
e74b0db Phase 4 (US2): DAW plugin (VST3 / AU / CLAP)
e27e832 Phase 3 (US1): desktop sketch-and-hear workbench (JUCE)
0ebd7d3 Phase 2 (foundational): core spine + host-side tests, all green
1b05595 Phase 1 (setup): monorepo skeleton + CMake build system


## Recent audit-log excerpt (prior findings on this feature)

Use this to avoid re-reporting findings that have already been triaged. If a finding was previously dispositioned (`closed`, `won't-fix`, `accepted-trade-off`), don't re-litigate the disposition; only surface a new instance if the underlying shape regressed.



## Under audit

The actual code under review. Read it carefully. The findings you emit must be anchored to specific files + line ranges in this diff (or call out a missing surface that should be in the diff but isn't).

Governance pass over the just-implemented work for feature 'svf-vertical-slice', diffed against ff3426a. The differentiated back half audits a plan it did not author or execute.
## Other chunks (file lists only — context for cross-file dependencies this chunk cannot see):
- 31c30149ec9faef5: tests/core/parameter-test.cpp, tests/core/svf-test.cpp, tests/core/test-main.cpp, tests/support/allocation-sentinel.cpp, tests/support/allocation-sentinel.h, tests/support/svf-reference.h
- 4cfb00d5b3480886: adapters/workbench/midi-binding.h, adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h, adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake
- 66d76089d898899c: specs/svf-vertical-slice/tasks.md, tests/CMakeLists.txt, tests/core/no-allocation-test.cpp
- 6a56babffbf5b038: .clang
…[shared context truncated to fit the fleet envelope]

## Chunk e7b284327d06692a
Files in scope: cmake/toolchains/teensy.cmake, core/dsp/audio-block.h, core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h, core/dsp/process-context.h, core/dsp/span.h, core/effects/svf/svf-effect.h

## Diffs

### cmake/toolchains/teensy.cmake
diff --git a/cmake/toolchains/teensy.cmake b/cmake/toolchains/teensy.cmake
new file mode 100644
index 0000000..8e83f2a
--- /dev/null
+++ b/cmake/toolchains/teensy.cmake
@@ -0,0 +1,41 @@
+# Toolchain — Teensy 4.x (IMXRT1062, Cortex-M7) via the ARM embedded toolchain.
+#
+# Teensy 4.x is a Cortex-M7 with a double-precision FPU. The C++ standard this
+# target builds at is verified against the installed Teensy toolchain during
+# implementation (research.md decision 3 / tasks.md T034); ACFX_TEENSY_CXX_STANDARD
+# is set to the highest standard that toolchain supports (>= 17). The same
+# core/effects/svf source compiles here; where C++20 concepts are unavailable the
+# Effect contract degrades to a duck-typed template (guarded by __cpp_concepts).
+
+set(CMAKE_SYSTEM_NAME Generic)
+set(CMAKE_SYSTEM_PROCESSOR arm)
+set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
+
+find_program(ARM_CC  arm-none-eabi-gcc)
+find_program(ARM_CXX arm-none-eabi-g++)
+if(NOT ARM_CC OR NOT ARM_CXX)
+  message(FATAL_ERROR
+    "arm-none-eabi-gcc / arm-none-eabi-g++ not found on PATH. Install the Teensy "
+    "ARM toolchain to build the teensy preset (no host-side fallback).")
+endif()
+
+set(CMAKE_C_COMPILER   "${ARM_CC}")
+set(CMAKE_CXX_COMPILER "${ARM_CXX}")
+set(CMAKE_ASM_COMPILER "${ARM_CC}")
+
+# Verified during implementation (T034); default to C++17, the level the Teensy
+# core is known to support. Raised here if the installed toolchain supports more.
+if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD)
+  set(ACFX_TEENSY_CXX_STANDARD 17)
+endif()
+set(CMAKE_CXX_STANDARD ${ACFX_TEENSY_CXX_STANDARD})
+
+set(_teensy_cpu_flags "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -DF_CPU=600000000 -D__IMXRT1062__")
+set(CMAKE_C_FLAGS_INIT   "${_teensy_cpu_flags}")
+set(CMAKE_CXX_FLAGS_INIT "${_teensy_cpu_flags} -fno-exceptions -fno-rtti")
+set(CMAKE_EXE_LINKER_FLAGS_INIT "${_teensy_cpu_flags} --specs=nano.specs --specs=nosys.specs")
+
+set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
+set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
+set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
+set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)


### core/dsp/audio-block.h
diff --git a/core/dsp/audio-block.h b/core/dsp/audio-block.h
new file mode 100644
index 0000000..549b5ca
--- /dev/null
+++ b/core/dsp/audio-block.h
@@ -0,0 +1,28 @@
+#pragma once
+
+#include "dsp/span.h"
+
+// A fixed-size, non-owning, non-allocating view of multichannel audio passed to
+// process() (FR-002). In-place processing: input and output alias. numSamples
+// varies per call but never exceeds the prepared maxBlockSize. No platform headers.
+
+namespace acfx {
+
+class AudioBlock {
+public:
+    AudioBlock(float* const* channels, int numChannels, int numSamples) noexcept
+        : channels_(channels), numChannels_(numChannels), numSamples_(numSamples) {}
+
+    int numChannels() const noexcept { return numChannels_; }
+    int numSamples() const noexcept { return numSamples_; }
+
+    // Mutable view of one channel's samples (in-place processing).
+    float* channel(int ch) const noexcept { return channels_[ch]; }
+
+private:
+    float* const* channels_; // non-owning: points at the adapter's buffers
+    int numChannels_;
+    int numSamples_;
+};
+
+} // namespace acfx


### core/dsp/effect.h
diff --git a/core/dsp/effect.h b/core/dsp/effect.h
new file mode 100644
index 0000000..38367b9
--- /dev/null
+++ b/core/dsp/effect.h
@@ -0,0 +1,65 @@
+#pragma once
+
+#include "dsp/audio-block.h"
+#include "dsp/param-id.h"
+#include "dsp/parameter.h"
+#include "dsp/process-context.h"
+#include "dsp/span.h"
+
+// The compile-time contract every effect satisfies — no base class, no vtable in
+// the audio path (FR-001, contracts/effect-concept.md). On a C++20 toolchain this
+// is a named concept; on C++17 (Teensy) the same member signatures are enforced
+// by plain template instantiation (duck typing), with a best-effort static trait
+// preserved for diagnostics. The effect code is identical either way.
+
+#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
+
+#include <concepts>
+
+namespace acfx {
+
+template <typename T>
+concept Effect =
+    requires(T fx, const ProcessContext& ctx, AudioBlock& io, ParamId id, float norm) {
+        { fx.prepare(ctx) } -> std::same_as<void>;        // set sr, maxBlock, channels
+        { fx.process(io) } -> std::same_as<void>;         // in-place; NO heap alloc
+        { fx.reset() } -> std::same_as<void>;             // clear internal state
+        { T::parameters() } -> std::convertible_to<span<const ParameterDescriptor>>;
+        { fx.setParameter(id, norm) } -> std::same_as<void>; // normalized 0..1 in
+    };
+
+} // namespace acfx
+
+// Use as: template <ACFX_EFFECT_CONCEPT T> ... — a constrained template here.
+#define ACFX_EFFECT_CONCEPT ::acfx::Effect
+
+#else // C++17 degradation: no concepts available.
+
+#include <type_traits>
+
+namespace acfx {
+
+// Best-effort detection trait (diagnostic only; never gates the build on this
+// target). Template instantiation still enforces the real member signatures.
+template <typename, typename = void>
+struct is_effect : std::false_type {};
+
+template <typename T>
+struct is_effect<
+    T, std::void_t<decltype(std::declval<T&>().prepare(std::declval<const ProcessContext&>())),
+                   decltype(std::declval<T&>().process(std::declval<AudioBlock&>())),
+                   decltype(std::declval<T&>().reset()),
+                   decltype(T::parameters()),
+                   decltype(std::declval<T&>().setParameter(std::declval<ParamId>(),
+                                                            std::declval<float>()))>>
+    : std::true_type {};
+
+template <typename T>
+inline constexpr bool is_effect_v = is_effect<T>::value;
+
+} // namespace acfx
+
+// Unconstrained on C++17 — duck typing enforces the same shape.
+#define ACFX_EFFECT_CONCEPT typename
+
+#endif


### core/dsp/param-id.h
diff --git a/core/dsp/param-id.h b/core/dsp/param-id.h
new file mode 100644
index 0000000..9ae8c1b
--- /dev/null
+++ b/core/dsp/param-id.h
@@ -0,0 +1,28 @@
+#pragma once
+
+#include <cstdint>
+
+// Strongly-typed parameter identity + the descriptor enums. No platform headers.
+// (contracts/parameter-model.md)
+
+namespace acfx {
+
+// A strongly-typed parameter identifier — not a bare int. Small, stable, dense
+// per effect; indexes into the effect's constexpr descriptor table.
+struct ParamId {
+    std::uint8_t value;
+};
+
+constexpr bool operator==(ParamId a, ParamId b) noexcept { return a.value == b.value; }
+constexpr bool operator!=(ParamId a, ParamId b) noexcept { return a.value != b.value; }
+
+// Display/semantic unit of a parameter (for labels + host metadata).
+enum class ParamUnit : std::uint8_t { none, hz, decibels, percent, ratio };
+
+// How a normalized 0..1 value maps to plain units.
+enum class ParamSkew : std::uint8_t { linear, logarithmic };
+
+// Whether a parameter is a continuous range or a discrete enumeration.
+enum class ParamKind : std::uint8_t { continuous, discrete };
+
+} // namespace acfx


### core/dsp/parameter.h
diff --git a/core/dsp/parameter.h b/core/dsp/parameter.h
new file mode 100644
index 0000000..ce589b6
--- /dev/null
+++ b/core/dsp/parameter.h
@@ -0,0 +1,113 @@
+#pragma once
+
+#include "dsp/param-id.h"
+
+#include <algorithm>
+#include <cassert>
+#include <cmath>
+#include <cstdint>
+#include <string_view>
+
+// The single declared definition of one control, consumed by every adapter
+// (FR-003), plus the pure, allocation-free normalized<->plain mapping.
+// (contracts/parameter-model.md)
+
+namespace acfx {
+
+struct ParameterDescriptor {
+    ParamId id;
+    std::string_view name; // static storage (string literal)
+    ParamUnit unit;
+    float min;
+    float max;
+    float defaultValue; // plain units, within [min, max]
+    ParamSkew skew;
+    ParamKind kind;
+    std::uint8_t discreteCount; // >= 2 when kind == discrete, else 0
+};
+
+// Compile-time descriptor validity: max>min; a logarithmic param needs min>0
+// (else denormalize yields NaN); a discrete param needs >=2 buckets. Effects
+// static_assert this over their constexpr table so a malformed descriptor is a
+// BUILD error, not a release-build NaN (the debug asserts below are defense in
+// depth for builds that compile them in).
+constexpr bool isValidDescriptor(const ParameterDescriptor& d) noexcept {
+    if (!(d.max > d.min))
+        return false;
+    if (d.skew == ParamSkew::logarithmic && !(d.min > 0.0f))
+        return false;
+    if (d.kind == ParamKind::discrete && d.discreteCount < 2)
+        return false;
+    return true;
+}
+
+namespace detail {
+// Clamp to [0,1] AND neutralize non-finite input: NaN must map to 0, not pass
+// through. Written so that NaN (for which every comparison is false) takes the
+// final else branch: NaN >= 0 is false -> 0. +inf -> 1, -inf -> 0. A plain
+// `x < 0 ? 0 : (x > 1 ? 1 : x)` would return NaN unchanged and poison the filter
+// state irrecoverably (a guard that fails open).
+constexpr float clamp01(float x) noexcept { return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f; }
+} // namespace detail
+
+// norm (0..1) -> plain units. Pure, allocation-free, audio-thread safe.
+inline float denormalize(const ParameterDescriptor& d, float norm) noexcept {
+    norm = detail::clamp01(norm);
+    switch (d.kind) {
+    case ParamKind::discrete: {
+        // Quantize to a bucket index in [0, discreteCount); the index IS the
+        // plain value (the effect maps index -> enum).
+        const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);
+        int idx = static_cast<int>(norm * static_cast<float>(count));
+        if (idx >= count)
+            idx = count - 1;
+        return static_cast<float>(idx);
+    }
+    case ParamKind::continuous:
+    default:
+        break;
+    }
+
+    switch (d.skew) {
+    case ParamSkew::logarithmic:
+        // Requires 0 < min < max (else 0*pow(inf,n) or log(1)=0 yield NaN). The
+        // invariant is enforced in debug builds; it compiles out in release, so
+        // the audio path stays branch-bounded and allocation-free.
+        assert(d.min > 0.0f && d.max > d.min &&
+               "logarithmic parameter requires 0 < min < max");
+        return d.min * std::pow(d.max / d.min, norm);
+    case ParamSkew::linear:
+    default:
+        return d.min + norm * (d.max - d.min);
+    }
+}
+
+// plain units -> norm (0..1). Inverse of denormalize for continuous params; for
+// discrete params, plain is the bucket index and the result lands mid-bucket so
+// it round-trips back to the same index.
+inline float normalize(const ParameterDescriptor& d, float plain) noexcept {
+    switch (d.kind) {
+    case ParamKind::discrete: {
+        const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);
+        int idx = static_cast<int>(plain);
+        idx = std::clamp(idx, 0, count - 1);
+        return (static_cast<float>(idx) + 0.5f) / static_cast<float>(count);
+    }
+    case ParamKind::continuous:
+    default:
+        break;
+    }
+
+    plain = std::clamp(plain, d.min, d.max);
+    switch (d.skew) {
+    case ParamSkew::logarithmic:
+        assert(d.min > 0.0f && d.max > d.min &&
+               "logarithmic parameter requires 0 < min < max");
+        return std::log(plain / d.min) / std::log(d.max / d.min);
+    case ParamSkew::linear:
+    default:
+        return (plain - d.min) / (d.max - d.min);
+    }
+}
+
+} // namespace acfx


### core/dsp/process-context.h
diff --git a/core/dsp/process-context.h b/core/dsp/process-context.h
new file mode 100644
index 0000000..76436d8
--- /dev/null
+++ b/core/dsp/process-context.h
@@ -0,0 +1,15 @@
+#pragma once
+
+// The prepared run conditions handed to Effect::prepare. Provided by the adapter
+// before audio starts and on any device change. maxBlockSize/numChannels bound
+// every subsequent process() call (no reallocation mid-stream). No platform headers.
+
+namespace acfx {
+
+struct ProcessContext {
+    double sampleRate;  // e.g. 48000.0
+    int maxBlockSize;   // upper bound on AudioBlock::numSamples
+    int numChannels;    // upper bound on AudioBlock::numChannels
+};
+
+} // namespace acfx


### core/dsp/span.h
diff --git a/core/dsp/span.h b/core/dsp/span.h
new file mode 100644
index 0000000..daabf95
--- /dev/null
+++ b/core/dsp/span.h
@@ -0,0 +1,52 @@
+#pragma once
+
+// A non-owning view over a contiguous sequence. On C++20 toolchains (desktop,
+// tests, Daisy) this is exactly std::span; on a C++17 toolchain (Teensy) it is a
+// minimal allocation-free polyfill with the same shape. One vocabulary type lets
+// the same core/ source compile under both standards (research.md decision 3).
+//
+// This is a standard-library shim, NOT a runtime fallback (Constitution V is
+// about mock data/behavior, not vocabulary types for an older standard).
+
+#include <cstddef>
+
+#if defined(__cpp_lib_span) && __cpp_lib_span >= 202002L
+#include <span>
+namespace acfx {
+template <typename T>
+using span = std::span<T>;
+} // namespace acfx
+#else
+#include <utility> // std::declval (used in the converting ctor below)
+namespace acfx {
+
+// Minimal dynamic-extent non-owning view (the subset core/ uses).
+template <typename T>
+class span {
+public:
+    constexpr span() noexcept : data_(nullptr), size_(0) {}
+    constexpr span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}
+
+    template <std::size_t N>
+    constexpr span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}
+
+    // Bind to a std::array (const or non-const) without dragging in <array>:
+    // any contiguous container exposing data()/size() converts via this ctor.
+    template <typename Container,
+              typename = decltype(static_cast<T*>(std::declval<Container&>().data()))>
+    constexpr span(Container& c) noexcept : data_(c.data()), size_(c.size()) {}
+
+    constexpr T* data() const noexcept { return data_; }
+    constexpr std::size_t size() const noexcept { return size_; }
+    constexpr bool empty() const noexcept { return size_ == 0; }
+    constexpr T& operator[](std::size_t i) const noexcept { return data_[i]; }
+    constexpr T* begin() const noexcept { return data_; }
+    constexpr T* end() const noexcept { return data_ + size_; }
+
+private:
+    T* data_;
+    std::size_t size_;
+};
+
+} // namespace acfx
+#endif


### core/effects/svf/svf-effect.h
diff --git a/core/effects/svf/svf-effect.h b/core/effects/svf/svf-effect.h
new file mode 100644
index 0000000..da44728
--- /dev/null
+++ b/core/effects/svf/svf-effect.h
@@ -0,0 +1,211 @@
+#pragma once
+
+#include <array>
+#include <atomic>
+#include <cstddef>
+#include <cstdint>
+#include <cstring>
+
+#include "dsp/audio-block.h"
+#include "dsp/param-id.h"
+#include "dsp/parameter.h"
+#include "dsp/process-context.h"
+#include "dsp/span.h"
+#include "primitives/svf-primitive.h"
+
+// The State-Variable Filter effect — the one effect proving the cross-platform
+// spine. Satisfies the Effect contract (no base class, no vtable in the hot
+// path). One constexpr parameter table (cutoff/resonance/mode) drives every
+// adapter (FR-003). Per-channel DaisySP Svf state, allocation-free process.
+//
+// Thread-ownership boundary:
+//   - setParameter() may be called from ANY thread (a UI loop, a MIDI callback,
+//     an MCU main loop). It only publishes a lock-free atomic pending value; the
+//     audio thread consumes pending values at the top of process(). So parameter
+//     edits never race process() — the core encodes that handoff itself.
+//   - prepare()/reset() DO mutate filter coefficients directly and are NOT
+//     synchronized against process(). They must be called only while the audio
+//     stream is stopped (before start, or during a device change with audio
+//     paused) — the standard prepare/process lifecycle already implies this, and
+//     that quiescence is the adapter's responsibility, not something the core can
+//     enforce.
+
+namespace acfx {
+
+class SvfEffect {
+public:
+    // Stable parameter ids — the dense index into kParams.
+    enum Param : std::uint8_t { kCutoff = 0, kResonance = 1, kMode = 2 };
+
+    // The single source of parameter truth (SC-006). cutoff: log Hz; resonance:
+    // linear 0..1; mode: discrete {lowpass, highpass, bandpass}.
+    static constexpr std::array<ParameterDescriptor, 3> kParams = {{
+        {ParamId{kCutoff}, "cutoff", ParamUnit::hz, 20.0f, 20000.0f, 1000.0f,
+         ParamSkew::logarithmic, ParamKind::continuous, 0},
+        {ParamId{kResonance}, "resonance", ParamUnit::none, 0.0f, 1.0f, 0.0f,
+         ParamSkew::linear, ParamKind::continuous, 0},
+        {ParamId{kMode}, "mode", ParamUnit::none, 0.0f, 2.0f, 0.0f,
+         ParamSkew::linear, ParamKind::discrete, 3},
+    }};
+
+    SvfEffect() noexcept {
+        for (std::size_t i = 0; i < kNumParams; ++i) {
+            pendingBits_[i].store(0u, std::memory_order_relaxed);
+            pendingDirty_[i].store(0u, std::memory_order_relaxed);
+        }
+    }
+
+    // Build-time guard: every descriptor in the table is valid (so a malformed
+    // entry — e.g. a log param with min<=0 — fails compilation, not the audio path).
+    static_assert(
+        [] {
+            for (const ParameterDescriptor& d : kParams)
+                if (!isValidDescriptor(d))
+                    return false;
+            return true;
+        }(),
+        "SvfEffect parameter table violates a descriptor invariant "
+        "(max>min; logarithmic => min>0; discrete => count>=2)");
+
+    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }
+
+    // Audio stream must be stopped — see the thread-ownership note above.
+    void prepare(const ProcessContext& ctx) noexcept {
+        sampleRate_ = static_cast<float>(ctx.sampleRate);
+        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
+        for (int ch = 0; ch < numChannels_; ++ch)
+            filters_[static_cast<std::size_t>(ch)].init(sampleRate_);
+        applyAll();
+    }
+
+    // Audio stream must be stopped — see the thread-ownership note above.
+    void reset() noexcept {
+        for (int ch = 0; ch < numChannels_; ++ch)
+            filters_[static_cast<std::size_t>(ch)].reset();
+        applyAll();
+    }
+
+    void process(AudioBlock& io) noexcept {
+        applyPending(); // consume cross-thread parameter edits on the audio thread
+        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
+        const int samples = io.numSamples();
+        for (int ch = 0; ch < channels; ++ch) {
+            float* x = io.channel(ch);
+            SvfPrimitive& f = filters_[static_cast<std::size_t>(ch)];
+            for (int n = 0; n < samples; ++n)
+                x[n] = f.process(x[n]);
+        }
+    }
+
+    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
+    // the audio thread applies it at the next process() (no immediate filter
+    // mutation here — that keeps coefficient updates single-threaded).
+    void setParameter(ParamId id, float normalized) noexcept {
+        const std::uint8_t i = id.value;
+        if (i >= kNumParams)
+            return; // out-of-range id: a programming error; no silent state change
+        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
+        pendingDirty_[i].store(1u, std::memory_order_release);
+    }
+
+private:
+    static constexpr int kMaxChannels = 8;
+    static constexpr std::size_t kNumParams = 3;
+
+    // float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy is a
+    // register move). Lets the cross-thread atomics be provably lock-free.
+    static std::uint32_t floatBits(float f) noexcept {
+        std::uint32_t u = 0;
+        std::memcpy(&u, &f, sizeof(u));
+        return u;
+    }
+    static float bitsFloat(std::uint32_t u) noexcept {
+        float f = 0.0f;
+        std::memcpy(&f, &u, sizeof(f));
+        return f;
+    }
+
+    static SvfMode toMode(float index) noexcept {
+        switch (static_cast<int>(index)) {
+        case 1:
+            return SvfMode::highpass;
+        case 2:
+            return SvfMode::bandpass;
+        case 0:
+        default:
+            return SvfMode::lowpass;
+        }
+    }
+
+    float pendingValue(Param p) const noexcept {
+        return bitsFloat(pendingBits_[p].load(std::memory_order_relaxed));
+    }
+
+    // Apply any parameter values published since the last block (audio thread).
+    void applyPending() noexcept {
+        if (pendingDirty_[kCutoff].exchange(0u, std::memory_order_acquire)) {
+            cutoffHz_ = denormalize(kParams[kCutoff], pendingValue(kCutoff));
+            applyCutoff();
+        }
+        if (pendingDirty_[kResonance].exchange(0u, std::memory_order_acquire)) {
+            resonance_ = denormalize(kParams[kResonance], pendingValue(kResonance));
+            applyResonance();
+        }
+        if (pendingDirty_[kMode].exchange(0u, std::memory_order_acquire)) {
+            mode_ = toMode(denormalize(kParams[kMode], pendingValue(kMode)));
+            applyMode();
+        }
+    }
+
+    // DaisySP's Svf requires cutoff strictly below sampleRate/3; clamp into a safe
+    // band just under that bound (and never below the descriptor minimum).
+    float clampedCutoff() const noexcept {
+        const float maxFreq = sampleRate_ * 0.32f;
+        float f = cutoffHz_;
+        if (f > maxFreq)
+            f = maxFreq;
+        if (f < 20.0f)
+            f = 20.0f;
+        return f;
+    }
+
+    void applyCutoff() noexcept {
+        const float f = clampedCutoff();
+        for (int ch = 0; ch < numChannels_; ++ch)
+            filters_[static_cast<std::size_t>(ch)].setFreq(f);
+    }
+    void applyResonance() noexcept {
+        for (int ch = 0; ch < numChannels_; ++ch)
+            filters_[static_cast<std::size_t>(ch)].setRes(resonance_);
+    }
+    void applyMode() noexcept {
+        for (int ch = 0; ch < numChannels_; ++ch)
+            filters_[static_cast<std::size_t>(ch)].setMode(mode_);
+    }
+    void applyAll() noexcept {
+        applyCutoff();
+        applyResonance();
+        applyMode();
+    }
+
+    std::array<SvfPrimitive, kMaxChannels> filters_{};
+    float sampleRate_ = 48000.0f;
+    int numChannels_ = 0;
+
+    // Applied parameter state — mutated only in prepare/reset/applyPending (the
+    // first two require a stopped stream; the third runs on the audio thread).
+    float cutoffHz_ = kParams[kCutoff].defaultValue;
+    float resonance_ = kParams[kResonance].defaultValue;
+    SvfMode mode_ = SvfMode::lowpass;
+
+    // Cross-thread pending edits: any thread publishes, the audio thread consumes.
+    // Stored as the float's bit pattern in a uint32 so the atomic is provably
+    // lock-free on every target (a bare std::atomic<float> can degrade to a libcall
+    // on some embedded runtimes).
+    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
+    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
+    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
+                  "pending-parameter atomics must be lock-free for RT safety");
+};
+
+} // namespace acfx


## What to look for

- **Correctness bugs** — logic errors, off-by-one, null/undefined paths, race conditions, missing error handling, swallowed exceptions.
- **Design issues** — coupling between layers that should be independent, leaking abstractions, primitives that should compose but don't, configuration that should be data ending up as code.
- **Missed edge cases** — what happens with empty input? Maximum input? Concurrent calls? Partial failure? Network unavailability? Operator interrupt mid-operation? What is the behavior on a fresh install vs. an upgrade?
- **Code-quality concerns** — files growing past a reasonable cap, names that don't reveal intent, dead code, duplicated logic, magic numbers without explanation, tests that don't test the contract they claim to test.
- **Cross-cutting impact** — does this diff touch a surface that other surfaces depend on? Are those other surfaces updated? Are migrations needed? Are doctor rules / schemas / validators updated to match the new shape?
- **Documentation drift** — does the README / SKILL.md / PRD describe the behavior the code actually implements? If the spec changed, did the implementation? If the implementation changed, did the spec?
- **Operator-discipline traps** — placeholder comments, swallowed errors, hardcoded paths/values that should be configurable, fallbacks that hide failure modes, mock data outside test code. These are bug-factories per project guidelines.

## Process drivers (029 US8 / FR-029)

These codify the structural drivers of myopic convergence (TASK-60), so the loop converges in fewer rounds with less fix-induced surface growth. The first three (channel-enumeration, invariant-first boundary, round-0 self-red-team) are **fix-review** drivers — apply them when the work under audit is a fix for a prior finding. The last two (fleet-degradation pricing, severity-rubric anchoring) are **general** controls that apply to every round:

- **Channel-enumeration.** When a fix ADDS to an allowlist/surface (a new flag, a new accepted value, a new parser branch, a new fold path), do not accept it on the one example it fixes — enumerate the channels it opens: the **value** channel (other inputs now accepted), the **state** channel (new reachable states), the **multiline / composition** channel (how it composes with adjacent surfaces). Flag any opened channel that lacks a fixture.
- **Invariant-first boundary.** When a finding is dispositioned as a scope boundary, state the boundary as the **mechanism's invariant plus an in-scope exception**, NOT as the exclusion of the one counterexample. "We exclude X" is a smell; "the invariant is I, and X is the in-scope exception because…" is the disposition.
- **Round-0 self-red-team.** When the work under audit is itself a FIX for a prior finding, audit the **fix diff as a fresh surface in its own right** — do not assume it is correct merely because it targets a known bug. Ask what new edge the fix opened and what it moved rather than removed; a fix that resolves one finding while opening an unaudited channel is itself a finding.
- **Fleet-degradation pricing.** A convergence claim is only as strong as the fleet that produced it. When the fleet is **degraded** (a timed-out / killed / zero-byte lane — US2 observability), price the round's "0 HIGH" accordingly: it is computed over fewer models, so cross-model agreement is weaker. Do not treat a degraded-fleet quiet round as full convergence.
- **Severity-rubric anchoring.** Rate every finding by the blast-radius rubric below (US3), not by how alarming it feels — a quietly-plausible wrong reading an unattended agent would build outranks an obvious contradiction a reader would resolve.

## Output format

For each finding you surface, emit ONE markdown block in this exact shape:

```
### <heading: one-line summary of the finding>

Finding-ID: AUDIT-BARRAGE-<your-model-name>-<NN>
Status:     open
Severity:   <blocking | high | medium | low | informational>
Surface:    <repo-relative-path:line-range> OR <description of the surface if not anchored to a single file>

<one-to-three paragraphs of body: what the finding is, why it matters, what evidence you relied on, what a reasonable fix would look like. Be specific. Cite line numbers from the diff. If the finding is structural / cross-file, name every file affected.>
```

Number the findings sequentially (`-01`, `-02`, ...).

**Severity — rate each finding by downstream blast-radius:** the consequence if a downstream consumer acts on the audited surface *as written*. The consumer may be an adopter running the code, or — especially for a spec — an AI agent building **unattended** from it, with no human to catch a wrong reading. Rate by what would actually happen if this shipped as-is, **not by how alarming the finding feels**. State the blast-radius reasoning in the finding body for every finding, at every level.

- `blocking` — acting on it as-written breaks the feature's stated goals in obvious ways; OR (for a spec) the more natural reading an agent reaches first is the wrong one, so it will likely be built wrong by default and nothing in the artifact corrects it.
- `high` — a correctness/safety defect a consumer will hit; OR a spec contradiction/ambiguity where the readings are roughly equally plausible and the artifact doesn't disambiguate — an agent might build either, including the wrong one.
- `medium` — a design issue that compounds over time; OR a spec inconsistency a reasonable consumer would resolve correctly anyway (readings barely diverge, or context makes the intended one obvious).
- `low` — hygiene; cosmetic wording with no behavioral or implementation consequence.
- `informational` — context worth seeing, not itself a defect.

**Calibrate by consequence, not by alarm.** A genuine contradiction a reader would obviously resolve the right way is at most `medium`. A quietly-plausible wrong reading an agent would actually build is `high`/`blocking` even if it looks minor. A spec's internal consistency is load-bearing — it is the input to an unattended build.

## If you find nothing — say so explicitly

If you walk the diff carefully and find no findings worth surfacing, emit ONE block in this shape instead:

```
### No findings

Finding-ID: AUDIT-BARRAGE-<your-model-name>-CLEAN
Status:     open
Severity:   informational
Surface:    (the entire diff)

I walked the diff for the feature named above and found no findings worth surfacing. My specific reasoning: <three-to-five sentences explaining what you checked, why those checks came back clean, and what you would have flagged if it had been present.>
```

**Do not pad with weak findings.** A confident "I checked X, Y, Z and they are clean for these reasons" is more useful to the operator than three vague low-severity notes. The cross-model diversity gives the operator independent signal; an empty clean report from your CLI is itself a signal when paired with findings from your siblings.

## Hard constraints

- **No deferral phrases.** Don't write phrases like "fix later", "address in a follow-up", or other commitments to deferred work. The dispatch-wrapper rejects these as bug-factories. If you spot a deferral phrase IN the diff, surface it as a finding.
- **Anchor findings to evidence.** A finding that says "this might be a problem" without naming the specific file + line is not actionable. Name the surface, quote the relevant code, explain what's wrong.
- **One issue per finding block.** Don't bundle multiple concerns into one entry; the operator triages each block as a discrete signal.
- **Provenance is your model name.** Replace `<your-model-name>` in the Finding-ID with the CLI you are (`claude`, `codex`, `gemini`, etc.). This is how the operator joins findings across models.

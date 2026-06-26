# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

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
- 2ea2449abe83c448: core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h, core/dsp/process-context.h, core/dsp/span.h, core/effects/svf/svf-effect.h, core/primitives/svf-primitive.h, external/.gitkeep, host/processor-node/CMakeLists.txt, host/processor-node/processor-node.h, scripts/check-portability.sh
- 561c01cdba330da9: adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h, adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake, cmake/toolchains/teensy.cmake, core/dsp/audio-block.h
- 6a56babffbf5b038: .clang-format, .editorconfig, .github/workflows/ci.yml, .gitignore, .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json, CMakeLists.txt, CMakePresets.json, README.md, adapters/daisy/CMakeLists.txt, adapters/daisy/daisy-main.cpp, adapters/plugin/CMakeLists.txt
- b74f59c0c4fc198b: specs/svf-vertical-slice/tasks.md, tests/CMakeLists.txt, tests/core/no-allocation-test.cpp, tests/core/parameter-test.cpp
- d58ba5050d21850a: adapters/plugin/plugin-parameters.cpp, adapters/plugin/plugin-parameters.h, adapters/plugin/plugin-processor.cpp, adapters/plugin/plugin-processor.h, adapters/teensy/CMakeLists.txt, adapters/teensy/teensy-main.cpp, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h, adapters/workbench/midi-binding.h

## Chunk 5d46bb000cdab808
Files in scope: tests/core/svf-test.cpp, tests/core/test-main.cpp, tests/support/allocation-sentinel.cpp, tests/support/allocation-sentinel.h, tests/support/svf-reference.h

## Diffs

### tests/core/svf-test.cpp
diff --git a/tests/core/svf-test.cpp b/tests/core/svf-test.cpp
new file mode 100644
index 0000000..8393e11
--- /dev/null
+++ b/tests/core/svf-test.cpp
@@ -0,0 +1,102 @@
+#include <doctest/doctest.h>
+
+#include <cmath>
+
+#include "dsp/audio-block.h"
+#include "dsp/param-id.h"
+#include "dsp/parameter.h"
+#include "dsp/process-context.h"
+#include "effects/svf/svf-effect.h"
+#include "support/svf-reference.h"
+
+// T016 — SVF effect: per-mode frequency response vs the known-good references
+// (T013), plus NaN/denormal stability at high resonance. Fails until SvfEffect
+// (T017) is implemented.
+
+using namespace acfx;
+using acfx::test::kPassbandFreqHz;
+using acfx::test::kPassbandGainMin;
+using acfx::test::kRefCutoffHz;
+using acfx::test::kRefSampleRate;
+using acfx::test::kStopbandFreqHz;
+using acfx::test::kStopbandGainMax;
+using acfx::test::measureMagnitude;
+
+namespace {
+
+// Configure a prepared mono SvfEffect at the reference cutoff, zero resonance,
+// in the requested mode. Returns a per-sample processing callable for the
+// magnitude measurement.
+struct MonoDriver {
+    SvfEffect fx;
+    float scratch = 0.0f;
+
+    explicit MonoDriver(SvfMode mode, float resonanceNorm = 0.0f) {
+        fx.prepare(ProcessContext{kRefSampleRate, 1, 1});
+        fx.setParameter(ParamId{SvfEffect::kCutoff},
+                        normalize(SvfEffect::kParams[SvfEffect::kCutoff],
+                                  static_cast<float>(kRefCutoffHz)));
+        fx.setParameter(ParamId{SvfEffect::kResonance}, resonanceNorm);
+        const float modeIndex = static_cast<float>(static_cast<int>(mode));
+        fx.setParameter(ParamId{SvfEffect::kMode},
+                        normalize(SvfEffect::kParams[SvfEffect::kMode], modeIndex));
+    }
+
+    float operator()(float in) {
+        scratch = in;
+        float* chans[1] = {&scratch};
+        AudioBlock block(chans, 1, 1);
+        fx.process(block);
+        return scratch;
+    }
+};
+
+} // namespace
+
+TEST_CASE("lowpass passes lows and attenuates highs") {
+    MonoDriver lp{SvfMode::lowpass};
+    const double passband = measureMagnitude(lp, kPassbandFreqHz, kRefSampleRate);
+    MonoDriver lp2{SvfMode::lowpass};
+    const double stopband = measureMagnitude(lp2, kStopbandFreqHz, kRefSampleRate);
+
+    CHECK(passband >= kPassbandGainMin);
+    CHECK(stopband <= kStopbandGainMax);
+    CHECK(passband > stopband);
+}
+
+TEST_CASE("highpass passes highs and attenuates lows") {
+    MonoDriver hpLow{SvfMode::highpass};
+    const double lowGain = measureMagnitude(hpLow, kPassbandFreqHz, kRefSampleRate);
+    MonoDriver hpHigh{SvfMode::highpass};
+    const double highGain = measureMagnitude(hpHigh, kStopbandFreqHz, kRefSampleRate);
+
+    CHECK(lowGain <= kStopbandGainMax);
+    CHECK(highGain >= kPassbandGainMin);
+    CHECK(highGain > lowGain);
+}
+
+TEST_CASE("bandpass emphasizes the centre relative to both edges") {
+    MonoDriver bpCentre{SvfMode::bandpass};
+    const double centre = measureMagnitude(bpCentre, kRefCutoffHz, kRefSampleRate);
+    MonoDriver bpLow{SvfMode::bandpass};
+    const double low = measureMagnitude(bpLow, kPassbandFreqHz, kRefSampleRate);
+    MonoDriver bpHigh{SvfMode::bandpass};
+    const double high = measureMagnitude(bpHigh, kStopbandFreqHz, kRefSampleRate);
+
+    CHECK(centre > low);
+    CHECK(centre > high);
+}
+
+TEST_CASE("high resonance stays NaN/denormal-free and bounded") {
+    // Near the stability limit; feed an impulse then silence and let it ring.
+    MonoDriver ring{SvfMode::bandpass, /*resonanceNorm=*/0.99f};
+    float maxAbs = 0.0f;
+    for (int n = 0; n < 200000; ++n) {
+        const float in = (n == 0) ? 1.0f : 0.0f;
+        const float out = ring(in);
+        REQUIRE(std::isfinite(out));
+        maxAbs = std::max(maxAbs, std::fabs(out));
+    }
+    // Self-oscillation must not blow up.
+    CHECK(maxAbs < 100.0f);
+}


### tests/core/test-main.cpp
diff --git a/tests/core/test-main.cpp b/tests/core/test-main.cpp
new file mode 100644
index 0000000..fa8fe8d
--- /dev/null
+++ b/tests/core/test-main.cpp
@@ -0,0 +1,4 @@
+// Single doctest entry point for the acfx core test binary. Other test
+// translation units include <doctest/doctest.h> without this implement macro.
+#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
+#include <doctest/doctest.h>


### tests/support/allocation-sentinel.cpp
diff --git a/tests/support/allocation-sentinel.cpp b/tests/support/allocation-sentinel.cpp
new file mode 100644
index 0000000..ea85292
--- /dev/null
+++ b/tests/support/allocation-sentinel.cpp
@@ -0,0 +1,54 @@
+#include "allocation-sentinel.h"
+
+#include <cstdlib>
+#include <new>
+
+// Global operator new/delete overrides that count heap traffic per thread. Kept
+// in one translation unit so the counters and the overrides share linkage. The
+// counters are thread-local: the test resets them on the audio-processing thread
+// and asserts zero across a prepare-then-N-process sequence (FR-014).
+
+namespace {
+thread_local std::size_t g_allocations = 0;
+thread_local std::size_t g_deallocations = 0;
+} // namespace
+
+namespace acfx::test {
+
+void AllocationSentinel::reset() noexcept {
+    g_allocations = 0;
+    g_deallocations = 0;
+}
+std::size_t AllocationSentinel::allocations() noexcept { return g_allocations; }
+std::size_t AllocationSentinel::deallocations() noexcept { return g_deallocations; }
+
+} // namespace acfx::test
+
+void* operator new(std::size_t size) {
+    ++g_allocations;
+    if (void* p = std::malloc(size != 0 ? size : 1))
+        return p;
+    throw std::bad_alloc();
+}
+
+void* operator new[](std::size_t size) {
+    ++g_allocations;
+    if (void* p = std::malloc(size != 0 ? size : 1))
+        return p;
+    throw std::bad_alloc();
+}
+
+void operator delete(void* p) noexcept {
+    if (p) {
+        ++g_deallocations;
+        std::free(p);
+    }
+}
+void operator delete[](void* p) noexcept {
+    if (p) {
+        ++g_deallocations;
+        std::free(p);
+    }
+}
+void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
+void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }


### tests/support/allocation-sentinel.h
diff --git a/tests/support/allocation-sentinel.h b/tests/support/allocation-sentinel.h
new file mode 100644
index 0000000..a8b1bb0
--- /dev/null
+++ b/tests/support/allocation-sentinel.h
@@ -0,0 +1,20 @@
+#pragma once
+
+#include <cstddef>
+
+// A thread-local heap-allocation counter used by the no-allocation invariant test
+// (FR-014, research.md decision 6). The global operator new/delete overrides live
+// in allocation-sentinel.cpp and bump these counters. Test-only support code.
+
+namespace acfx::test {
+
+struct AllocationSentinel {
+    // Zero the counters (call immediately before a measured region).
+    static void reset() noexcept;
+    // Number of heap allocations on this thread since the last reset().
+    static std::size_t allocations() noexcept;
+    // Number of heap deallocations on this thread since the last reset().
+    static std::size_t deallocations() noexcept;
+};
+
+} // namespace acfx::test


### tests/support/svf-reference.h
diff --git a/tests/support/svf-reference.h b/tests/support/svf-reference.h
new file mode 100644
index 0000000..7d3da05
--- /dev/null
+++ b/tests/support/svf-reference.h
@@ -0,0 +1,49 @@
+#pragma once
+
+#include <cmath>
+
+// Known-good SVF frequency-response references (T013). Rather than fabricate exact
+// magnitude numbers (false precision), this captures the *analytic* truths a
+// correct 2nd-order state-variable filter must satisfy — passband near unity,
+// stopband attenuated, bandpass emphasizing its centre — as a measurement helper
+// plus named tolerance bounds the SVF test asserts against (T016).
+
+namespace acfx::test {
+
+inline constexpr double kPi = 3.14159265358979323846;
+
+// Steady-state magnitude response |out|/|in| of a per-sample processor at a given
+// frequency. `proc` is any callable float(float). A settling prefix is discarded
+// so only the steady-state RMS ratio is measured.
+template <typename Proc>
+double measureMagnitude(Proc&& proc, double freqHz, double sampleRate, int settle = 8192,
+                        int measure = 16384) {
+    const double w = 2.0 * kPi * freqHz / sampleRate;
+    for (int n = 0; n < settle; ++n)
+        (void) proc(static_cast<float>(std::sin(w * static_cast<double>(n))));
+
+    double inSq = 0.0;
+    double outSq = 0.0;
+    for (int n = 0; n < measure; ++n) {
+        const double phase = w * static_cast<double>(settle + n);
+        const double in = std::sin(phase);
+        const double out = static_cast<double>(proc(static_cast<float>(in)));
+        inSq += in * in;
+        outSq += out * out;
+    }
+    return std::sqrt(outSq / inSq);
+}
+
+// Reference cutoff used by the SVF response test.
+inline constexpr double kRefCutoffHz = 1000.0;
+inline constexpr double kRefSampleRate = 48000.0;
+
+// Passband: a decade below cutoff should pass at roughly unity gain.
+inline constexpr double kPassbandFreqHz = 100.0;
+inline constexpr double kPassbandGainMin = 0.7; // generous: SVF passband ~0 dB
+
+// Stopband: three octaves above cutoff a 2nd-order rolloff is well attenuated.
+inline constexpr double kStopbandFreqHz = 8000.0;
+inline constexpr double kStopbandGainMax = 0.25; // << passband
+
+} // namespace acfx::test


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

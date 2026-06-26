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
- 6a56babffbf5b038: .clang-format, .editorconfig, .github/workflows/ci.yml, .gitignore, .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json, CMakeLists.txt, CMakePresets.json, README.md, adapters/daisy/CMakeLists.txt, adapters/daisy/daisy-main.cpp, adapters/plugin/CMakeLists.txt
- a70ef633c8635ce4: adapters/plugin/plugin-parameters.cpp, adapters/plugin/plugin-parameters.h, adapters/plugin/plugin-processor.cpp, adapters/plugin/plugin-processor.h, adapters/teensy/CMakeLists.txt, adapters/teensy/teensy-main.cpp, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h
- e7b284327d06692a: cmake/toolchains/teensy.cmake, core/dsp/audio-block.h, core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h, core/dsp/process-context.h, core/dsp/span.h, core/effects/svf/svf-effect.h

## Chunk a05731de7f2cca0b
Files in scope: core/primitives/svf-primitive.h, external/.gitkeep, host/processor-node/CMakeLists.txt, host/processor-node/processor-node.h, scripts/check-portability.sh

## Diffs

### core/primitives/svf-primitive.h
diff --git a/core/primitives/svf-primitive.h b/core/primitives/svf-primitive.h
new file mode 100644
index 0000000..0dc7a34
--- /dev/null
+++ b/core/primitives/svf-primitive.h
@@ -0,0 +1,59 @@
+#pragma once
+
+#include <cstdint>
+
+#include "Filters/svf.h" // DaisySP — pinned via CPM (cmake/dependencies.cmake)
+
+// A thin, allocation-free, mode-selectable wrapper over DaisySP's proven Svf
+// (research.md decision 1). DaisySP is a platform-independent pure-DSP math
+// library, so wrapping it here keeps core/ free of any platform headers
+// (Constitution IV). The wrapper owns mode selection + reset; the per-sample math
+// is DaisySP's.
+
+namespace acfx {
+
+enum class SvfMode : std::uint8_t { lowpass, highpass, bandpass };
+
+class SvfPrimitive {
+public:
+    // Prepare the filter for a sample rate. Clears internal state.
+    void init(float sampleRate) noexcept {
+        sampleRate_ = sampleRate;
+        svf_.Init(sampleRate);
+    }
+
+    // f in Hz. DaisySP requires 0 < f < sampleRate/3; the caller (SvfEffect)
+    // clamps cutoff into that range before calling.
+    void setFreq(float hz) noexcept { svf_.SetFreq(hz); }
+
+    // r in [0, 1] (DaisySP stability bound).
+    void setRes(float r) noexcept { svf_.SetRes(r); }
+
+    void setMode(SvfMode mode) noexcept { mode_ = mode; }
+    SvfMode mode() const noexcept { return mode_; }
+
+    // Re-initialize to a cleared-but-prepared state (DaisySP's Init clears state).
+    void reset() noexcept { svf_.Init(sampleRate_); }
+
+    // Process one sample, returning the currently-selected mode's output.
+    // Allocation-free, bounded work (Constitution VI).
+    float process(float in) noexcept {
+        svf_.Process(in);
+        switch (mode_) {
+        case SvfMode::highpass:
+            return svf_.High();
+        case SvfMode::bandpass:
+            return svf_.Band();
+        case SvfMode::lowpass:
+        default:
+            return svf_.Low();
+        }
+    }
+
+private:
+    daisysp::Svf svf_{};
+    float sampleRate_ = 48000.0f;
+    SvfMode mode_ = SvfMode::lowpass;
+};
+
+} // namespace acfx


### external/.gitkeep
diff --git a/external/.gitkeep b/external/.gitkeep
new file mode 100644
index 0000000..e69de29


### host/processor-node/CMakeLists.txt
diff --git a/host/processor-node/CMakeLists.txt b/host/processor-node/CMakeLists.txt
new file mode 100644
index 0000000..ab6f302
--- /dev/null
+++ b/host/processor-node/CMakeLists.txt
@@ -0,0 +1,11 @@
+# acfx_host — the desktop-only host boundary (ProcessorNode + EffectNode<T>).
+# Header-only; pure C++ (no JUCE). Shared by the workbench + plugin adapters and
+# exercised by the host-side tests. NEVER linked into the Daisy/Teensy builds.
+
+add_library(acfx_host INTERFACE)
+add_library(acfx::host ALIAS acfx_host)
+
+# Expose host/ as an include root so consumers write
+# #include "processor-node/processor-node.h".
+target_include_directories(acfx_host INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/..")
+target_link_libraries(acfx_host INTERFACE acfx_core)


### host/processor-node/processor-node.h
diff --git a/host/processor-node/processor-node.h b/host/processor-node/processor-node.h
new file mode 100644
index 0000000..28978c3
--- /dev/null
+++ b/host/processor-node/processor-node.h
@@ -0,0 +1,50 @@
+#pragma once
+
+#include "dsp/audio-block.h"
+#include "dsp/effect.h"
+#include "dsp/param-id.h"
+#include "dsp/parameter.h"
+#include "dsp/process-context.h"
+#include "dsp/span.h"
+
+// The desktop-only host boundary: the single place a virtual call touches the
+// audio path — at most once per block (FR-004 / Constitution VI). NEVER compiled
+// into the Daisy/Teensy builds. (contracts/processor-node.md)
+
+namespace acfx {
+
+struct ProcessorNode {
+    virtual ~ProcessorNode() = default;
+    virtual void prepare(const ProcessContext& ctx) = 0;
+    virtual void processBlock(AudioBlock& io) = 0; // the one virtual call / block
+    virtual void reset() = 0;
+    virtual span<const ParameterDescriptor> parameters() const = 0;
+
+    // Thread contract: setParameter may be called from any thread (UI, MIDI, an
+    // MCU control loop) concurrently with processBlock. The wrapped Effect is
+    // responsible for the cross-thread handoff — SvfEffect, for example,
+    // publishes an atomic pending value that the audio thread consumes inside
+    // process(). Callers must not assume the change takes effect synchronously;
+    // it lands at the next processBlock.
+    virtual void setParameter(ParamId id, float normalized) = 0;
+};
+
+// Stores a concrete Effect by value and forwards each virtual call to the inlined
+// concrete method — zero hot-path overhead beyond the single block dispatch.
+template <ACFX_EFFECT_CONCEPT T>
+class EffectNode final : public ProcessorNode {
+public:
+    void prepare(const ProcessContext& ctx) override { fx_.prepare(ctx); }
+    void processBlock(AudioBlock& io) override { fx_.process(io); } // inlined concrete call
+    void reset() override { fx_.reset(); }
+    span<const ParameterDescriptor> parameters() const override { return T::parameters(); }
+    void setParameter(ParamId id, float n) override { fx_.setParameter(id, n); }
+
+    T& effect() noexcept { return fx_; }
+    const T& effect() const noexcept { return fx_; }
+
+private:
+    T fx_; // stored by value — no indirection beyond the single virtual dispatch
+};
+
+} // namespace acfx


### scripts/check-portability.sh
diff --git a/scripts/check-portability.sh b/scripts/check-portability.sh
new file mode 100755
index 0000000..ab248ae
--- /dev/null
+++ b/scripts/check-portability.sh
@@ -0,0 +1,66 @@
+#!/usr/bin/env bash
+#
+# acfx explicit quality gates (T037, T038) — visible steps, NOT a git hook
+# (Constitution II). Run locally and in CI. Exits non-zero on any violation.
+#
+#   1. File-size budget (~300-500 lines; hard-fail above 500) — Constitution VII
+#   2. No platform headers in core/ — Constitution IV
+#   3. No JUCE / ProcessorNode in the MCU (daisy/teensy) dependency surface — SC-007
+#   4. One-source-many-targets: no per-target #ifdef forks of the effect, and every
+#      adapter links the same acfx_core — SC-001 / SC-005 (Scenario E)
+
+set -u
+cd "$(dirname "$0")/.." || exit 2
+
+fail=0
+note() { printf '%s\n' "$*"; }
+
+note "== 1. File-size budget (<= 500 lines) =="
+while IFS= read -r f; do
+  lines=$(wc -l < "$f" | tr -d ' ')
+  if [ "$lines" -gt 500 ]; then
+    note "  FAIL $f: $lines lines (> 500)"
+    fail=1
+  fi
+done < <(find core host adapters tests -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
+[ "$fail" -eq 0 ] && note "  OK: all source files within budget"
+
+note "== 2. No platform headers in core/ (Constitution IV) =="
+if grep -rEn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' core/ ; then
+  note "  FAIL: platform header in core/"
+  fail=1
+else
+  note "  OK: core/ is platform-independent"
+fi
+
+note "== 3. No JUCE / ProcessorNode in MCU adapters (SC-007) =="
+if grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy ; then
+  note "  FAIL: MCU adapter references JUCE or the desktop ProcessorNode"
+  fail=1
+else
+  note "  OK: daisy + teensy reference neither JUCE nor ProcessorNode"
+fi
+
+note "== 4. One-source-many-targets (Scenario E, SC-001/SC-005) =="
+if grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/ ; then
+  note "  FAIL: per-target #ifdef fork inside the effect source"
+  fail=1
+else
+  note "  OK: no per-target #ifdef forks in core/effects/"
+fi
+for adapter in workbench plugin daisy teensy; do
+  if ! grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt" 2>/dev/null; then
+    note "  FAIL: adapters/$adapter does not link acfx_core"
+    fail=1
+  fi
+done
+[ "$fail" -eq 0 ] && note "  OK: every adapter links the same acfx_core"
+
+if [ "$fail" -eq 0 ]; then
+  note ""
+  note "All portability gates passed."
+else
+  note ""
+  note "Portability gate FAILED."
+fi
+exit "$fail"


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

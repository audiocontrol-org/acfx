# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

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
- 1d366441c57c4606: scripts/check-portability.sh, specs/svf-vertical-slice/tasks.md, tests/CMakeLists.txt, tests/core/no-allocation-test.cpp
- 31c30149ec9faef5: tests/core/parameter-test.cpp, tests/core/svf-test.cpp, tests/core/test-main.cpp, tests/support/allocation-sentinel.cpp, tests/support/allocation-sentinel.h, tests/support/svf-reference.h
- 51a61c640621e280: core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h, core/dsp/process-context.h, core/dsp/span.h, core/effects/svf/svf-effect.h, core/primitives/svf-primitive.h, external/.gitkeep, host/processor-node/CMakeLists.txt, host/processor-node/processor-node.h
- 561c01cdba330da9: adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h, adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake, cmake/toolchains/teensy.cmake, core/dsp/audio-block.h
- 6a56babffbf5b038: .clang-format, .editorconfig, .github/workflows/ci.yml, .gitignore, .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json, CMakeLists.txt, CMakePresets.json, README.md, adapters/daisy/CMakeLists.txt, adapters/daisy/daisy-main.cpp, adapters/plugin/CMakeLists.txt

## Chunk d58ba5050d21850a
Files in scope: adapters/plugin/plugin-parameters.cpp, adapters/plugin/plugin-parameters.h, adapters/plugin/plugin-processor.cpp, adapters/plugin/plugin-processor.h, adapters/teensy/CMakeLists.txt, adapters/teensy/teensy-main.cpp, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h, adapters/workbench/midi-binding.h

## Diffs

### adapters/plugin/plugin-parameters.cpp
diff --git a/adapters/plugin/plugin-parameters.cpp b/adapters/plugin/plugin-parameters.cpp
new file mode 100644
index 0000000..71e83c2
--- /dev/null
+++ b/adapters/plugin/plugin-parameters.cpp
@@ -0,0 +1,96 @@
+#include "plugin-parameters.h"
+
+#include <string>
+
+namespace acfx::plugin {
+
+namespace {
+
+juce::String unitSuffix(ParamUnit unit) {
+    switch (unit) {
+    case ParamUnit::hz:
+        return " Hz";
+    case ParamUnit::decibels:
+        return " dB";
+    case ParamUnit::percent:
+        return " %";
+    case ParamUnit::ratio:
+    case ParamUnit::none:
+    default:
+        return {};
+    }
+}
+
+juce::String modeName(int index) {
+    switch (index) {
+    case 1:
+        return "highpass";
+    case 2:
+        return "bandpass";
+    case 0:
+    default:
+        return "lowpass";
+    }
+}
+
+} // namespace
+
+void PluginParameters::build(juce::AudioProcessor& processor,
+                             span<const ParameterDescriptor> descriptors) {
+    entries_.clear();
+    entries_.reserve(descriptors.size());
+
+    for (const ParameterDescriptor& d : descriptors) {
+        Entry entry;
+        entry.descriptor = d;
+        const juce::String name(std::string(d.name));
+        const juce::ParameterID paramId(name, 1);
+
+        if (d.kind == ParamKind::discrete) {
+            juce::StringArray choices;
+            for (int i = 0; i < d.discreteCount; ++i)
+                choices.add(modeName(i));
+            const int defaultIndex = static_cast<int>(d.defaultValue);
+            auto param = std::make_unique<juce::AudioParameterChoice>(paramId, name, choices,
+                                                                      defaultIndex);
+            entry.choiceParam = param.get();
+            processor.addParameter(param.release());
+        } else {
+            // Normalized 0..1 automation; the descriptor owns the skew, so the
+            // displayed plain value uses denormalize() — matching the workbench.
+            const ParameterDescriptor desc = d;
+            const float defaultNorm = normalize(d, d.defaultValue);
+            auto attributes =
+                juce::AudioParameterFloatAttributes()
+                    .withLabel(unitSuffix(d.unit))
+                    .withStringFromValueFunction([desc](float norm, int) {
+                        const float plain = denormalize(desc, norm);
+                        return juce::String(plain, 2);
+                    });
+            auto param = std::make_unique<juce::AudioParameterFloat>(
+                paramId, name, juce::NormalisableRange<float>(0.0f, 1.0f), defaultNorm,
+                attributes);
+            entry.floatParam = param.get();
+            processor.addParameter(param.release());
+        }
+
+        entries_.push_back(entry);
+    }
+}
+
+void PluginParameters::apply(const ApplyFn& fn) const {
+    if (!fn)
+        return;
+    for (const Entry& e : entries_) {
+        if (e.floatParam != nullptr) {
+            fn(e.descriptor.id, e.floatParam->get());
+        } else if (e.choiceParam != nullptr) {
+            const int index = e.choiceParam->getIndex();
+            const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;
+            const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
+            fn(e.descriptor.id, norm);
+        }
+    }
+}
+
+} // namespace acfx::plugin


### adapters/plugin/plugin-parameters.h
diff --git a/adapters/plugin/plugin-parameters.h b/adapters/plugin/plugin-parameters.h
new file mode 100644
index 0000000..619a7f9
--- /dev/null
+++ b/adapters/plugin/plugin-parameters.h
@@ -0,0 +1,40 @@
+#pragma once
+
+#include <functional>
+#include <vector>
+
+#include <juce_audio_processors/juce_audio_processors.h>
+
+#include "dsp/param-id.h"
+#include "dsp/parameter.h"
+#include "dsp/span.h"
+
+// Host-automation parameters generated from the effect's descriptor table (T030).
+// There is no hand-written parameter list: each ParameterDescriptor becomes a
+// JUCE parameter (continuous -> AudioParameterFloat in normalized 0..1 space with
+// a plain-unit display; discrete -> AudioParameterChoice). The normalized value
+// handed to setParameter is the same one the workbench produces, so the mapping
+// is identical across adapters (SC-006).
+
+namespace acfx::plugin {
+
+class PluginParameters {
+public:
+    using ApplyFn = std::function<void(ParamId, float)>;
+
+    // Create one JUCE parameter per descriptor and add it to the processor.
+    void build(juce::AudioProcessor& processor, span<const ParameterDescriptor> descriptors);
+
+    // Push each parameter's current normalized value to the effect via `fn`.
+    void apply(const ApplyFn& fn) const;
+
+private:
+    struct Entry {
+        ParameterDescriptor descriptor;
+        juce::AudioParameterFloat* floatParam = nullptr;
+        juce::AudioParameterChoice* choiceParam = nullptr;
+    };
+    std::vector<Entry> entries_;
+};
+
+} // namespace acfx::plugin


### adapters/plugin/plugin-processor.cpp
diff --git a/adapters/plugin/plugin-processor.cpp b/adapters/plugin/plugin-processor.cpp
new file mode 100644
index 0000000..502a148
--- /dev/null
+++ b/adapters/plugin/plugin-processor.cpp
@@ -0,0 +1,56 @@
+#include "plugin-processor.h"
+
+#include "dsp/audio-block.h"
+#include "dsp/process-context.h"
+
+namespace acfx::plugin {
+
+PluginProcessor::PluginProcessor()
+    : juce::AudioProcessor(BusesProperties()
+                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
+                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
+    parameters_.build(*this, node_.parameters());
+}
+
+void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
+    const ProcessContext ctx{sampleRate, samplesPerBlock, getTotalNumOutputChannels()};
+    node_.prepare(ctx);
+}
+
+bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
+    const auto out = layouts.getMainOutputChannelSet();
+    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
+        return false;
+    // Same in/out layout (in-place processing).
+    return layouts.getMainInputChannelSet() == out;
+}
+
+void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
+    juce::ScopedNoDenormals noDenormals;
+
+    // Push current host-automation values to the effect (same normalized mapping
+    // as the workbench). Allocation-free.
+    parameters_.apply([this](ParamId id, float normalized) {
+        node_.setParameter(id, normalized);
+    });
+
+    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
+    std::array<float*, kMaxChannels> chans{};
+    for (int ch = 0; ch < numChannels; ++ch)
+        chans[ch] = buffer.getWritePointer(ch);
+
+    AudioBlock block(chans.data(), numChannels, buffer.getNumSamples());
+    node_.processBlock(block);
+}
+
+juce::AudioProcessorEditor* PluginProcessor::createEditor() {
+    // Auto-generated UI from the host-automation parameters — no bespoke editor.
+    return new juce::GenericAudioProcessorEditor(*this);
+}
+
+} // namespace acfx::plugin
+
+// The plugin factory JUCE's wrappers call to instantiate the processor.
+juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
+    return new acfx::plugin::PluginProcessor();
+}


### adapters/plugin/plugin-processor.h
diff --git a/adapters/plugin/plugin-processor.h b/adapters/plugin/plugin-processor.h
new file mode 100644
index 0000000..aaf1073
--- /dev/null
+++ b/adapters/plugin/plugin-processor.h
@@ -0,0 +1,58 @@
+#pragma once
+
+#include <array>
+#include <memory>
+
+#include <juce_audio_processors/juce_audio_processors.h>
+
+#include "effects/svf/svf-effect.h"
+#include "plugin-parameters.h"
+#include "processor-node/processor-node.h"
+
+// The DAW plugin AudioProcessor (T029). Wraps the SAME host boundary the
+// workbench uses — EffectNode<SvfEffect> — adding only JUCE plugin glue and the
+// host-automation parameters generated from the effect's descriptor table. One
+// processor, exported as VST3 / AU / CLAP by the build (T028).
+
+namespace acfx::plugin {
+
+class PluginProcessor final : public juce::AudioProcessor {
+public:
+    PluginProcessor();
+    ~PluginProcessor() override = default;
+
+    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
+    void releaseResources() override {}
+    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
+    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
+
+    juce::AudioProcessorEditor* createEditor() override;
+    bool hasEditor() const override { return true; }
+
+    const juce::String getName() const override { return "acfx SVF"; }
+    bool acceptsMidi() const override { return false; }
+    bool producesMidi() const override { return false; }
+    bool isMidiEffect() const override { return false; }
+    double getTailLengthSeconds() const override { return 0.0; }
+
+    int getNumPrograms() override { return 1; }
+    int getCurrentProgram() override { return 0; }
+    void setCurrentProgram(int) override {}
+    const juce::String getProgramName(int) override { return "Default"; }
+    void changeProgramName(int, const juce::String&) override {}
+
+    // Preset/state persistence is out of scope for this milestone (plan.md:
+    // Storage N/A) — intentionally no-op, not a silent fallback.
+    void getStateInformation(juce::MemoryBlock&) override {}
+    void setStateInformation(const void*, int) override {}
+
+private:
+    static constexpr int kMaxChannels = 8;
+
+    EffectNode<SvfEffect> node_;
+    PluginParameters parameters_;
+
+    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
+};
+
+} // namespace acfx::plugin


### adapters/teensy/CMakeLists.txt
diff --git a/adapters/teensy/CMakeLists.txt b/adapters/teensy/CMakeLists.txt
new file mode 100644
index 0000000..6a821d5
--- /dev/null
+++ b/adapters/teensy/CMakeLists.txt
@@ -0,0 +1,38 @@
+# Teensy firmware target (T033). Compiles the SAME core/effects/svf into a Teensy
+# 4.x firmware via the Teensy core + Audio Library — the only platform-specific
+# code is teensy-main.cpp (AudioStream node + analog reads). No JUCE, no
+# ProcessorNode (Constitution IV; SC-007).
+#
+# The C++ standard is the highest the installed Teensy toolchain supports (>=17,
+# T034 / research.md decision 3); on C++17 the Effect concept degrades to a
+# duck-typed template and the same SvfEffect compiles unchanged. Requires the
+# Teensy ARM toolchain WITH the C++ standard library; a C-only arm-none-eabi is a
+# hard error, never a fallback.
+
+if(NOT DEFINED teensy_cores_SOURCE_DIR OR NOT DEFINED teensy_audio_SOURCE_DIR)
+  message(FATAL_ERROR
+    "Teensy cores/Audio not fetched. Configure with the `teensy` preset so "
+    "cmake/dependencies.cmake pins and downloads them.")
+endif()
+
+set(_teensy4 "${teensy_cores_SOURCE_DIR}/teensy4")
+
+# Teensy core (Arduino runtime for IMXRT1062) + Audio Library, built as the
+# platform substrate the adapter links against.
+file(GLOB _teensy_core_sources CONFIGURE_DEPENDS "${_teensy4}/*.c" "${_teensy4}/*.cpp")
+file(GLOB _teensy_audio_sources CONFIGURE_DEPENDS "${teensy_audio_SOURCE_DIR}/*.cpp")
+
+add_library(teensy_platform STATIC ${_teensy_core_sources} ${_teensy_audio_sources})
+target_include_directories(teensy_platform PUBLIC "${_teensy4}" "${teensy_audio_SOURCE_DIR}")
+target_compile_definitions(teensy_platform PUBLIC ARDUINO=10813 TEENSYDUINO=159 ARDUINO_TEENSY40)
+
+add_executable(acfx_teensy teensy-main.cpp)
+target_link_libraries(acfx_teensy PRIVATE acfx_core teensy_platform)
+
+set(_teensy_lds "${_teensy4}/imxrt1062.ld")
+target_link_options(acfx_teensy PRIVATE
+  -T "${_teensy_lds}"
+  -Wl,-Map=acfx_teensy.map,--cref
+  -Wl,--gc-sections
+)
+set_target_properties(acfx_teensy PROPERTIES SUFFIX ".elf")


### adapters/workbench/CMakeLists.txt
diff --git a/adapters/workbench/CMakeLists.txt b/adapters/workbench/CMakeLists.txt
new file mode 100644
index 0000000..58dfe55
--- /dev/null
+++ b/adapters/workbench/CMakeLists.txt
@@ -0,0 +1,31 @@
+# The JUCE standalone sketch-and-hear workbench (T021). A thin adapter over the
+# shared core: it links acfx_core + acfx_host (the same host boundary the plugin
+# uses) and adds only JUCE glue. Built under the `desktop` preset.
+
+juce_add_gui_app(acfx_workbench
+  PRODUCT_NAME "acfx Workbench"
+  COMPANY_NAME "acfx"
+)
+
+target_sources(acfx_workbench PRIVATE
+  workbench-app.cpp
+  parameter-view.cpp
+  audio-source.cpp
+)
+
+target_compile_features(acfx_workbench PRIVATE cxx_std_20)
+
+target_compile_definitions(acfx_workbench PRIVATE
+  JUCE_WEB_BROWSER=0
+  JUCE_USE_CURL=0
+  JUCE_DISPLAY_SPLASH_SCREEN=0
+  JUCE_APPLICATION_NAME_STRING="acfx Workbench"
+)
+
+target_link_libraries(acfx_workbench PRIVATE
+  acfx_core
+  acfx_host
+  juce::juce_audio_utils      # pulls audio_basics/devices/formats/processors + gui
+  juce::juce_recommended_config_flags
+  juce::juce_recommended_warning_flags
+)


### adapters/workbench/audio-source.cpp
diff --git a/adapters/workbench/audio-source.cpp b/adapters/workbench/audio-source.cpp
new file mode 100644
index 0000000..9c3ee14
--- /dev/null
+++ b/adapters/workbench/audio-source.cpp
@@ -0,0 +1,77 @@
+#include "audio-source.h"
+
+#include <memory>
+
+namespace acfx::workbench {
+
+WorkbenchAudioSource::WorkbenchAudioSource() { formatManager_.registerBasicFormats(); }
+
+void WorkbenchAudioSource::useFilePlayer(const juce::File& file) {
+    if (!file.existsAsFile())
+        throw AudioSourceError("Audio file does not exist: " + file.getFullPathName());
+
+    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
+    if (reader == nullptr)
+        throw AudioSourceError("No decoder for audio file: " + file.getFullPathName());
+
+    const int numChannels = static_cast<int>(reader->numChannels);
+    const int numSamples = static_cast<int>(reader->lengthInSamples);
+    if (numChannels <= 0 || numSamples <= 0)
+        throw AudioSourceError("Audio file is empty: " + file.getFullPathName());
+
+    // Decode the whole file into memory on this (setup) thread. The audio thread
+    // only ever reads fileBuffer_ thereafter — no reader, no transport, no lock.
+    juce::AudioBuffer<float> decoded(numChannels, numSamples);
+    reader->read(&decoded, 0, numSamples, 0, true, true);
+
+    fileBuffer_ = std::move(decoded);
+    playPos_.store(0, std::memory_order_relaxed);
+    hasFile_.store(true, std::memory_order_release);
+    live_.store(false, std::memory_order_release);
+}
+
+void WorkbenchAudioSource::useLiveInput(int availableInputChannels) {
+    if (availableInputChannels <= 0)
+        throw AudioSourceError("Live input selected but the audio device offers no "
+                               "input channels.");
+    live_.store(true, std::memory_order_release);
+}
+
+void WorkbenchAudioSource::prepare(double, int) {
+    if (!live_.load(std::memory_order_acquire) && !hasFile_.load(std::memory_order_acquire))
+        throw AudioSourceError("No audio source configured: select the built-in "
+                               "player (with a file) or a live input device.");
+    configured_ = true;
+}
+
+void WorkbenchAudioSource::release() { configured_ = false; }
+
+void WorkbenchAudioSource::fillBlock(juce::AudioBuffer<float>& block) noexcept {
+    if (live_.load(std::memory_order_relaxed))
+        return; // device input is already present in `block`
+
+    const int fileLen = fileBuffer_.getNumSamples();
+    if (!hasFile_.load(std::memory_order_relaxed) || fileLen <= 0) {
+        block.clear();
+        return;
+    }
+
+    const int fileChannels = fileBuffer_.getNumChannels();
+    const int numSamples = block.getNumSamples();
+    const int startPos = playPos_.load(std::memory_order_relaxed);
+
+    for (int ch = 0; ch < block.getNumChannels(); ++ch) {
+        const float* src = fileBuffer_.getReadPointer(ch < fileChannels ? ch : fileChannels - 1);
+        float* dst = block.getWritePointer(ch);
+        int pos = startPos;
+        for (int i = 0; i < numSamples; ++i) {
+            dst[i] = src[pos];
+            if (++pos >= fileLen)
+                pos = 0;
+        }
+    }
+
+    playPos_.store((startPos + numSamples) % fileLen, std::memory_order_relaxed);
+}
+
+} // namespace acfx::workbench


### adapters/workbench/audio-source.h
diff --git a/adapters/workbench/audio-source.h b/adapters/workbench/audio-source.h
new file mode 100644
index 0000000..5057b0c
--- /dev/null
+++ b/adapters/workbench/audio-source.h
@@ -0,0 +1,60 @@
+#pragma once
+
+#include <atomic>
+#include <stdexcept>
+
+#include <juce_audio_basics/juce_audio_basics.h>
+#include <juce_audio_formats/juce_audio_formats.h>
+
+// The workbench audio source (T025): a built-in looping file player OR live input
+// device, selectable at runtime (research.md decision 2). The player is the
+// deterministic default for reproducible A/B; live input is the real sketch use.
+// If neither is available the source raises a descriptive error — never silent
+// zeros or mock audio (Constitution V).
+//
+// RT-safety (Constitution VI): the file is decoded into an in-memory buffer at
+// setup (off the audio thread); fillBlock() reads from that buffer at an atomic
+// play position with no locks, no allocation, and no transport object whose
+// source pointer the audio thread could see freed mid-swap.
+
+namespace acfx::workbench {
+
+class AudioSourceError : public std::runtime_error {
+public:
+    explicit AudioSourceError(const juce::String& what)
+        : std::runtime_error(what.toStdString()) {}
+};
+
+class WorkbenchAudioSource {
+public:
+    WorkbenchAudioSource();
+
+    // Decode the given file into memory and select it as the source. Throws
+    // AudioSourceError if the file cannot be opened/decoded. Call at setup (off
+    // the audio thread).
+    void useFilePlayer(const juce::File& file);
+
+    // Use the live device input. `availableInputChannels` is what the device
+    // offers; throws AudioSourceError if there are none.
+    void useLiveInput(int availableInputChannels);
+
+    void prepare(double sampleRate, int blockSize);
+    void release();
+
+    // Fill `block` with the next chunk of source audio. For live input, `block`
+    // already holds the device input on entry and is passed through unchanged.
+    // Runs on the audio thread: never throws, never allocates, takes no locks.
+    void fillBlock(juce::AudioBuffer<float>& block) noexcept;
+
+    bool isLiveInput() const noexcept { return live_.load(std::memory_order_relaxed); }
+
+private:
+    juce::AudioFormatManager formatManager_;
+    juce::AudioBuffer<float> fileBuffer_; // whole file decoded into memory at setup
+    std::atomic<int> playPos_{0};
+    std::atomic<bool> live_{false};
+    std::atomic<bool> hasFile_{false};
+    bool configured_ = false;
+};
+
+} // namespace acfx::workbench


### adapters/workbench/midi-binding.h
diff --git a/adapters/workbench/midi-binding.h b/adapters/workbench/midi-binding.h
new file mode 100644
index 0000000..cacdad2
--- /dev/null
+++ b/adapters/workbench/midi-binding.h
@@ -0,0 +1,42 @@
+#pragma once
+
+#include <functional>
+#include <unordered_map>
+
+#include <juce_audio_basics/juce_audio_basics.h>
+
+#include "dsp/param-id.h"
+
+// Maps MIDI continuous-controller messages to normalized parameter changes
+// (T024). A CC value 0..127 becomes a normalized 0..1 value handed to
+// setParameter(id, normalized) — the effect denormalizes via its descriptor, so
+// the binding stays parameter-agnostic.
+
+namespace acfx::workbench {
+
+class MidiBinding {
+public:
+    using OnParam = std::function<void(ParamId, float)>;
+
+    // Bind a CC number (0..127) to a parameter id. Re-binding a CC replaces it.
+    void bind(int ccNumber, ParamId id) { bindings_[ccNumber] = id; }
+
+    // Feed a MIDI message; if it is a CC we have a binding for, invokes onParam
+    // with the normalized value and returns true.
+    bool handle(const juce::MidiMessage& msg, const OnParam& onParam) const {
+        if (!msg.isController())
+            return false;
+        const auto it = bindings_.find(msg.getControllerNumber());
+        if (it == bindings_.end())
+            return false;
+        const float normalized = static_cast<float>(msg.getControllerValue()) / 127.0f;
+        if (onParam)
+            onParam(it->second, normalized);
+        return true;
+    }
+
+private:
+    std::unordered_map<int, ParamId> bindings_;
+};
+
+} // namespace acfx::workbench


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

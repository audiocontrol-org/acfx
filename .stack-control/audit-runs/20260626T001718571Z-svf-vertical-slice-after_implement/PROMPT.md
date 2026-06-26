# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

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
- 5d46bb000cdab808: tests/core/svf-test.cpp, tests/core/test-main.cpp, tests/support/allocation-sentinel.cpp, tests/support/allocation-sentinel.h, tests/support/svf-reference.h
- 952c352cc6eab725: .clang-format, .editorconfig, .github/workflows/ci.yml, .gitignore, CMakeLists.txt, CMakePresets.json, README.md, adapters/daisy/CMakeLists.txt, adapters/daisy/daisy-main.cpp, adapters/plugin/CMakeLists.txt, adapters/plugin/plugin-parameters.cpp, adapters/plugin/plugin-parameters.h
- b04c878073d5d626: adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake, cmake/toolchains/teensy.cmake, core/dsp/audio-block.h, core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h,
…[shared context truncated to fit the fleet envelope]

## Chunk ba29de07a54f0920
Files in scope: adapters/plugin/plugin-processor.cpp, adapters/plugin/plugin-processor.h, adapters/teensy/CMakeLists.txt, adapters/teensy/teensy-main.cpp, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h, adapters/workbench/midi-binding.h, adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h

## Diffs

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


### adapters/teensy/teensy-main.cpp
diff --git a/adapters/teensy/teensy-main.cpp b/adapters/teensy/teensy-main.cpp
new file mode 100644
index 0000000..eb67344
--- /dev/null
+++ b/adapters/teensy/teensy-main.cpp
@@ -0,0 +1,92 @@
+// Teensy adapter (T033): a Teensy Audio Library AudioStream node feeds the SAME
+// core/effects/svf source, and analog inputs map to setParameter. The MCU build
+// uses the concrete SvfEffect directly — no ProcessorNode, no JUCE (Constitution
+// IV; SC-007). On a C++17 toolchain the Effect contract degrades to a duck-typed
+// template (the concept is compiled out via __cpp_concepts); the effect source is
+// identical to every other target.
+
+#include <Arduino.h>
+#include <Audio.h>
+
+#include "dsp/audio-block.h"
+#include "dsp/param-id.h"
+#include "dsp/process-context.h"
+#include "effects/svf/svf-effect.h"
+
+namespace {
+acfx::SvfEffect svf;
+constexpr float kInt16ToFloat = 1.0f / 32768.0f;
+constexpr float kFloatToInt16 = 32767.0f;
+} // namespace
+
+// A single-channel SVF node in the Teensy audio graph.
+class AcfxSvfNode : public AudioStream {
+public:
+    AcfxSvfNode() : AudioStream(1, inputQueueArray_) {}
+
+    void update() override {
+        audio_block_t* block = receiveWritable(0);
+        if (block == nullptr)
+            return;
+
+        float samples[AUDIO_BLOCK_SAMPLES];
+        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
+            samples[i] = static_cast<float>(block->data[i]) * kInt16ToFloat;
+
+        float* channels[1] = {samples};
+        acfx::AudioBlock audioBlock(channels, 1, AUDIO_BLOCK_SAMPLES);
+        svf.process(audioBlock);
+
+        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
+            float v = samples[i] * kFloatToInt16;
+            if (v > 32767.0f)
+                v = 32767.0f;
+            if (v < -32768.0f)
+                v = -32768.0f;
+            block->data[i] = static_cast<int16_t>(v);
+        }
+
+        transmit(block, 0);
+        release(block);
+    }
+
+private:
+    audio_block_t* inputQueueArray_[1];
+};
+
+// Audio graph: line in -> SVF -> line out, with the SGTL5000 codec (Audio Shield).
+namespace {
+AudioInputI2S audioIn;
+AcfxSvfNode svfNode;
+AudioOutputI2S audioOut;
+AudioControlSGTL5000 codec;
+
+AudioConnection patchIn(audioIn, 0, svfNode, 0);
+AudioConnection patchOut(svfNode, 0, audioOut, 0);
+
+constexpr int kCutoffPin = A0;
+constexpr int kResonancePin = A1;
+constexpr int kModePin = A2;
+} // namespace
+
+void setup() {
+    AudioMemory(12);
+    codec.enable();
+    codec.volume(0.6f);
+
+    const acfx::ProcessContext ctx{static_cast<double>(AUDIO_SAMPLE_RATE_EXACT),
+                                   AUDIO_BLOCK_SAMPLES, 1};
+    svf.prepare(ctx);
+}
+
+void loop() {
+    // Sample the knobs and map to normalized parameter values (the effect
+    // denormalizes via its descriptor — identical mapping to every adapter).
+    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},
+                     static_cast<float>(analogRead(kCutoffPin)) / 1023.0f);
+    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance},
+                     static_cast<float>(analogRead(kResonancePin)) / 1023.0f);
+    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},
+                     static_cast<float>(analogRead(kModePin)) / 1023.0f);
+    delay(5);
+}


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
index 0000000..67d00b9
--- /dev/null
+++ b/adapters/workbench/audio-source.cpp
@@ -0,0 +1,59 @@
+#include "audio-source.h"
+
+namespace acfx::workbench {
+
+WorkbenchAudioSource::WorkbenchAudioSource() { formatManager_.registerBasicFormats(); }
+
+void WorkbenchAudioSource::useFilePlayer(const juce::File& file) {
+    if (!file.existsAsFile())
+        throw AudioSourceError("Audio file does not exist: " + file.getFullPathName());
+
+    juce::AudioFormatReader* reader = formatManager_.createReaderFor(file);
+    if (reader == nullptr)
+        throw AudioSourceError("No decoder for audio file: " + file.getFullPathName());
+
+    readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, /*deleteReader=*/true);
+    readerSource_->setLooping(true);
+    transport_.setSource(readerSource_.get());
+    live_ = false;
+}
+
+void WorkbenchAudioSource::useLiveInput(int availableInputChannels) {
+    if (availableInputChannels <= 0)
+        throw AudioSourceError("Live input selected but the audio device offers no "
+                               "input channels.");
+    live_ = true;
+    readerSource_.reset();
+    transport_.setSource(nullptr);
+}
+
+void WorkbenchAudioSource::prepare(double sampleRate, int blockSize) {
+    if (!live_ && readerSource_ == nullptr)
+        throw AudioSourceError("No audio source configured: select the built-in "
+                               "player (with a file) or a live input device.");
+    if (!live_)
+        transport_.prepareToPlay(blockSize, sampleRate);
+    configured_ = true;
+}
+
+void WorkbenchAudioSource::release() {
+    if (!live_)
+        transport_.releaseResources();
+    configured_ = false;
+}
+
+void WorkbenchAudioSource::fillBlock(juce::AudioBuffer<float>& block) {
+    if (!configured_)
+        throw AudioSourceError("fillBlock called before prepare().");
+
+    if (live_)
+        return; // device input is already present in `block`
+
+    if (!transport_.isPlaying())
+        transport_.start();
+
+    juce::AudioSourceChannelInfo info(&block, 0, block.getNumSamples());
+    transport_.getNextAudioBlock(info);
+}
+
+} // namespace acfx::workbench


### adapters/workbench/audio-source.h
diff --git a/adapters/workbench/audio-source.h b/adapters/workbench/audio-source.h
new file mode 100644
index 0000000..1ac63a7
--- /dev/null
+++ b/adapters/workbench/audio-source.h
@@ -0,0 +1,52 @@
+#pragma once
+
+#include <memory>
+#include <stdexcept>
+
+#include <juce_audio_formats/juce_audio_formats.h>
+#include <juce_audio_utils/juce_audio_utils.h>
+
+// The workbench audio source (T025): a built-in looping file player OR live input
+// device, selectable at runtime (research.md decision 2). The player is the
+// deterministic default for reproducible A/B; live input is the real sketch use.
+// If neither is available the source raises a descriptive error — never silent
+// zeros or mock audio (Constitution V).
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
+    // Loop the given audio file as the source. Throws AudioSourceError if the
+    // file cannot be opened/decoded.
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
+    void fillBlock(juce::AudioBuffer<float>& block);
+
+    bool isLiveInput() const noexcept { return live_; }
+
+private:
+    juce::AudioFormatManager formatManager_;
+    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;
+    juce::AudioTransportSource transport_;
+    bool live_ = false;
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


### adapters/workbench/parameter-view.cpp
diff --git a/adapters/workbench/parameter-view.cpp b/adapters/workbench/parameter-view.cpp
new file mode 100644
index 0000000..d85d7fc
--- /dev/null
+++ b/adapters/workbench/parameter-view.cpp
@@ -0,0 +1,88 @@
+#include "parameter-view.h"
+
+namespace acfx::workbench {
+
+ParameterView::ParameterView(span<const ParameterDescriptor> params, OnChange onChange)
+    : onChange_(std::move(onChange)) {
+    rows_.reserve(params.size());
+    for (const ParameterDescriptor& d : params) {
+        rows_.push_back(std::make_unique<Row>());
+        Row& row = *rows_.back();
+        row.descriptor = d;
+
+        row.label.setText(juce::String(std::string(d.name)), juce::dontSendNotification);
+        addAndMakeVisible(row.label);
+
+        if (d.kind == ParamKind::discrete) {
+            row.combo = std::make_unique<juce::ComboBox>();
+            for (int i = 0; i < d.discreteCount; ++i)
+                row.combo->addItem(juce::String(i), i + 1); // item ids are 1-based
+            row.combo->setSelectedItemIndex(0, juce::dontSendNotification);
+
+            const ParamId id = d.id;
+            const std::uint8_t count = d.discreteCount;
+            juce::ComboBox* combo = row.combo.get();
+            OnChange& cb = onChange_;
+            combo->onChange = [combo, id, count, &cb] {
+                const int index = combo->getSelectedItemIndex();
+                // Centre-of-bucket normalized value for this discrete index.
+                const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
+                if (cb)
+                    cb(id, norm);
+            };
+            addAndMakeVisible(*row.combo);
+        } else {
+            row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
+                                                        juce::Slider::TextBoxRight);
+            // The slider works in normalized 0..1 space; the descriptor owns the
+            // skew, so the effect maps back to plain units on setParameter.
+            row.slider->setRange(0.0, 1.0, 0.0);
+            row.slider->setValue(static_cast<double>(normalize(d, d.defaultValue)),
+                                 juce::dontSendNotification);
+
+            const ParameterDescriptor desc = d;
+            juce::Slider* slider = row.slider.get();
+            OnChange& cb = onChange_;
+            slider->onValueChange = [slider, desc, &cb] {
+                if (cb)
+                    cb(desc.id, static_cast<float>(slider->getValue()));
+            };
+            addAndMakeVisible(*row.slider);
+        }
+    }
+}
+
+void ParameterView::setNormalized(ParamId id, float normalized) {
+    for (std::unique_ptr<Row>& rowPtr : rows_) {
+        Row& row = *rowPtr;
+        if (row.descriptor.id != id)
+            continue;
+        if (row.slider)
+            row.slider->setValue(static_cast<double>(normalized), juce::dontSendNotification);
+        else if (row.combo) {
+            const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;
+            int index = static_cast<int>(normalized * static_cast<float>(count));
+            if (index >= count)
+                index = count - 1;
+            row.combo->setSelectedItemIndex(index, juce::dontSendNotification);
+        }
+        return;
+    }
+}
+
+void ParameterView::resized() {
+    auto area = getLocalBounds().reduced(8);
+    const int rowHeight = 32;
+    for (std::unique_ptr<Row>& rowPtr : rows_) {
+        Row& row = *rowPtr;
+        auto r = area.removeFromTop(rowHeight);
+        row.label.setBounds(r.removeFromLeft(120));
+        if (row.slider)
+            row.slider->setBounds(r);
+        else if (row.combo)
+            row.combo->setBounds(r.removeFromLeft(160));
+        area.removeFromTop(6);
+    }
+}
+
+} // namespace acfx::workbench


### adapters/workbench/parameter-view.h
diff --git a/adapters/workbench/parameter-view.h b/adapters/workbench/parameter-view.h
new file mode 100644
index 0000000..f1e53d2
--- /dev/null
+++ b/adapters/workbench/parameter-view.h
@@ -0,0 +1,46 @@
+#pragma once
+
+#include <functional>
+#include <vector>
+
+#include <juce_gui_basics/juce_gui_basics.h>
+
+#include "dsp/param-id.h"
+#include "dsp/parameter.h"
+#include "dsp/span.h"
+
+// Auto-renders one control per ParameterDescriptor (T023). There is no per-effect
+// UI code: the view iterates the effect's parameters() table and builds a slider
+// (continuous) or combo (discrete) for each, labelled from the descriptor. This
+// is the same descriptor table every other adapter consumes (FR-003, SC-006).
+
+namespace acfx::workbench {
+
+class ParameterView : public juce::Component {
+public:
+    // Called when a control moves, with the normalized 0..1 value for that id.
+    using OnChange = std::function<void(ParamId, float)>;
+
+    ParameterView(span<const ParameterDescriptor> params, OnChange onChange);
+
+    // Reflect an externally-driven change (e.g. a MIDI CC) back into the control
+    // without re-firing onChange.
+    void setNormalized(ParamId id, float normalized);
+
+    void resized() override;
+
+private:
+    struct Row {
+        ParameterDescriptor descriptor;
+        juce::Label label;
+        std::unique_ptr<juce::Slider> slider; // continuous
+        std::unique_ptr<juce::ComboBox> combo; // discrete
+    };
+
+    OnChange onChange_;
+    // Heap-allocated rows: Row holds a juce::Label/Component, which is non-movable,
+    // so the vector stores pointers (not Rows) to satisfy reallocation.
+    std::vector<std::unique_ptr<Row>> rows_;
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

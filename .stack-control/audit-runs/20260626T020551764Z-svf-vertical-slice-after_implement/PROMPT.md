# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

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

Governance pass over the just-implemented work for feature 'svf-vertical-slice', diffed against ff3426a. The dif
…[shared context truncated to fit the fleet envelope]

## Chunk 561c01cdba330da9
Files in scope: adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h, adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake, cmake/toolchains/teensy.cmake, core/dsp/audio-block.h

## Diffs

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


### adapters/workbench/workbench-app.cpp
diff --git a/adapters/workbench/workbench-app.cpp b/adapters/workbench/workbench-app.cpp
new file mode 100644
index 0000000..3c4aa5d
--- /dev/null
+++ b/adapters/workbench/workbench-app.cpp
@@ -0,0 +1,194 @@
+#include <array>
+#include <atomic>
+#include <memory>
+
+#include <juce_audio_utils/juce_audio_utils.h>
+#include <juce_gui_basics/juce_gui_basics.h>
+
+#include "audio-source.h"
+#include "dsp/audio-block.h"
+#include "dsp/process-context.h"
+#include "effects/svf/svf-effect.h"
+#include "midi-binding.h"
+#include "parameter-view.h"
+#include "processor-node/processor-node.h"
+
+// The desktop sketch-and-hear workbench (T022, T026). Holds the effect behind the
+// same host boundary the plugin uses — std::unique_ptr<ProcessorNode> =
+// EffectNode<SvfEffect> — routes audio from the selected source through it, draws
+// the auto-generated controls, binds MIDI CCs, and offers a dry/processed A/B
+// toggle. Parameter edits from the GUI/MIDI threads go through
+// ProcessorNode::setParameter, the RT-safe cross-thread ingress: the effect
+// publishes each value as a lock-free atomic that process() consumes, so no
+// separate workbench-side queue is needed.
+
+namespace acfx::workbench {
+
+namespace {
+constexpr int kMaxChannels = 8;
+} // namespace
+
+class WorkbenchComponent final : public juce::AudioAppComponent,
+                                 private juce::MidiInputCallback {
+public:
+    WorkbenchComponent()
+        : node_(std::make_unique<EffectNode<SvfEffect>>()),
+          paramView_(node_->parameters(),
+                     [this](ParamId id, float norm) { node_->setParameter(id, norm); }) {
+        params_ = node_->parameters();
+
+        // Default MIDI map: CC 74 -> cutoff (the conventional filter-cutoff CC).
+        midi_.bind(74, ParamId{SvfEffect::kCutoff});
+        midi_.bind(71, ParamId{SvfEffect::kResonance});
+
+        addAndMakeVisible(paramView_);
+        abToggle_.setButtonText("Process (A/B)");
+        abToggle_.setToggleState(true, juce::dontSendNotification);
+        abToggle_.onClick = [this] { processed_.store(abToggle_.getToggleState()); };
+        addAndMakeVisible(abToggle_);
+
+        setSize(520, 220);
+        // Stereo in/out: input present for live-input mode.
+        setAudioChannels(2, 2);
+        // Enable the available MIDI inputs — registering a callback alone does
+        // not enable any device, so without this the CC bindings stay inert.
+        for (const auto& input : juce::MidiInput::getAvailableDevices())
+            deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
+        deviceManager.addMidiInputDeviceCallback({}, this);
+    }
+
+    ~WorkbenchComponent() override {
+        deviceManager.removeMidiInputDeviceCallback({}, this);
+        shutdownAudio();
+    }
+
+    void prepareToPlay(int blockSize, double sampleRate) override {
+        // Prepare for the device's ACTUAL output channel count (setAudioChannels
+        // is a request, not a guarantee), bounded by kMaxChannels — so the count
+        // the effect is prepared for matches the count process() drives.
+        const int outputs = numOutputChannels();
+        preparedChannels_ = juce::jlimit(1, kMaxChannels, outputs > 0 ? outputs : 2);
+        const ProcessContext ctx{sampleRate, blockSize, preparedChannels_};
+        node_->prepare(ctx);
+
+        // Default to live input when the device offers it; otherwise the operator
+        // must point the built-in player at a file (no silent fallback).
+        const int inputs = numInputChannels();
+        try {
+            if (inputs > 0)
+                source_.useLiveInput(inputs);
+            source_.prepare(sampleRate, blockSize);
+        } catch (const AudioSourceError& e) {
+            // Surface the failure to the operator instead of swallowing it — no
+            // silent fallback to silence (Constitution V).
+            const juce::String message(e.what());
+            juce::MessageManager::callAsync([message] {
+                juce::NativeMessageBox::showMessageBoxAsync(
+                    juce::MessageBoxIconType::WarningIcon, "Audio source unavailable", message);
+            });
+        }
+    }
+
+    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
+        // Parameter edits are consumed inside SvfEffect::process() (atomic
+        // pending), so the audio thread needs no separate apply step here.
+        juce::AudioBuffer<float>& buffer = *info.buffer;
+        const int startSample = info.startSample;
+        const int numSamples = info.numSamples;
+        // Bound to the count the effect was prepared for (never exceed it).
+        const int numChannels = juce::jmin(buffer.getNumChannels(), preparedChannels_);
+
+        // Pull the source audio into the destination region (live input is already
+        // present and passes through). fillBlock is noexcept and lock-free.
+        juce::AudioBuffer<float> region(buffer.getArrayOfWritePointers(),
+                                        buffer.getNumChannels(), startSample, numSamples);
+        source_.fillBlock(region);
+
+        if (!processed_.load())
+            return; // A/B: dry — leave the source audio untouched
+
+        std::array<float*, kMaxChannels> chans{};
+        for (int ch = 0; ch < numChannels; ++ch)
+            chans[ch] = buffer.getWritePointer(ch, startSample);
+        AudioBlock block(chans.data(), numChannels, numSamples);
+        node_->processBlock(block);
+    }
+
+    void releaseResources() override { source_.release(); }
+
+    void resized() override {
+        auto area = getLocalBounds();
+        abToggle_.setBounds(area.removeFromBottom(32).reduced(8, 4));
+        paramView_.setBounds(area);
+    }
+
+private:
+    int numInputChannels() const {
+        if (auto* device = deviceManager.getCurrentAudioDevice())
+            return device->getActiveInputChannels().countNumberOfSetBits();
+        return 0;
+    }
+
+    int numOutputChannels() const {
+        if (auto* device = deviceManager.getCurrentAudioDevice())
+            return device->getActiveOutputChannels().countNumberOfSetBits();
+        return 0;
+    }
+
+    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override {
+        midi_.handle(msg, [this](ParamId id, float norm) {
+            node_->setParameter(id, norm); // core is thread-safe (atomic pending)
+            // Reflect into the GUI on the message thread, guarded so a callback
+            // queued before teardown never touches a destroyed component.
+            juce::Component::SafePointer<ParameterView> safeView(&paramView_);
+            juce::MessageManager::callAsync([safeView, id, norm] {
+                if (safeView != nullptr)
+                    safeView->setNormalized(id, norm);
+            });
+        });
+    }
+
+    std::unique_ptr<ProcessorNode> node_;
+    span<const ParameterDescriptor> params_;
+    ParameterView paramView_;
+    MidiBinding midi_;
+    WorkbenchAudioSource source_;
+    juce::ToggleButton abToggle_;
+
+    int preparedChannels_ = 2;
+    std::atomic<bool> processed_{true};
+};
+
+class WorkbenchApplication final : public juce::JUCEApplication {
+public:
+    const juce::String getApplicationName() override { return "acfx Workbench"; }
+    const juce::String getApplicationVersion() override { return "0.1.0"; }
+
+    void initialise(const juce::String&) override {
+        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
+    }
+    void shutdown() override { mainWindow_ = nullptr; }
+
+private:
+    class MainWindow final : public juce::DocumentWindow {
+    public:
+        explicit MainWindow(const juce::String& name)
+            : juce::DocumentWindow(name, juce::Colours::darkgrey,
+                                   juce::DocumentWindow::allButtons) {
+            setUsingNativeTitleBar(true);
+            setContentOwned(new WorkbenchComponent(), true);
+            setResizable(true, true);
+            centreWithSize(getWidth(), getHeight());
+            setVisible(true);
+        }
+        void closeButtonPressed() override {
+            juce::JUCEApplication::getInstance()->systemRequestedQuit();
+        }
+    };
+
+    std::unique_ptr<MainWindow> mainWindow_;
+};
+
+} // namespace acfx::workbench
+
+START_JUCE_APPLICATION(acfx::workbench::WorkbenchApplication)


### cmake/CPM.cmake
diff --git a/cmake/CPM.cmake b/cmake/CPM.cmake
new file mode 100644
index 0000000..b635155
--- /dev/null
+++ b/cmake/CPM.cmake
@@ -0,0 +1,30 @@
+# CPM.cmake bootstrap — downloads the pinned CPM package manager on first
+# configure rather than vendoring its (large) source into the repo. The exact
+# version + content hash are pinned here, so the fetch is reproducible (research.md
+# decision 4) and the downloaded file lands in the gitignored CPM cache.
+
+set(CPM_DOWNLOAD_VERSION 0.40.5)
+set(CPM_HASH_SUM "c46b876ae3b9f994b4f05a4c15553e0485636862064f1fcc9d8b4f832086bc5d")
+
+if(CPM_SOURCE_CACHE)
+  set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
+elseif(DEFINED ENV{CPM_SOURCE_CACHE})
+  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
+else()
+  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
+endif()
+
+get_filename_component(CPM_DOWNLOAD_LOCATION "${CPM_DOWNLOAD_LOCATION}" ABSOLUTE)
+
+# Ensure the destination directory exists before downloading (defensive — current
+# CMake creates it, but this keeps the bootstrap robust across versions).
+get_filename_component(_cpm_download_dir "${CPM_DOWNLOAD_LOCATION}" DIRECTORY)
+file(MAKE_DIRECTORY "${_cpm_download_dir}")
+
+file(DOWNLOAD
+  "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
+  "${CPM_DOWNLOAD_LOCATION}"
+  EXPECTED_HASH SHA256=${CPM_HASH_SUM}
+)
+
+include("${CPM_DOWNLOAD_LOCATION}")


### cmake/dependencies.cmake
diff --git a/cmake/dependencies.cmake b/cmake/dependencies.cmake
new file mode 100644
index 0000000..b18a3b4
--- /dev/null
+++ b/cmake/dependencies.cmake
@@ -0,0 +1,88 @@
+# acfx external dependencies — CPM-pinned to explicit, known-good refs.
+#
+# Per research.md (Phase 0, decision 4): every dependency is fetched by CPM and
+# pinned to an explicit ref. The pin is a real, reproducible tag/commit captured
+# when the dependency is first fetched and verified to build — never a fabricated
+# version number.
+#
+# Pins verified by an in-session fetch+build (the `test` preset path):
+#   - DaisySP   599511b740f8f3a9b8db72a0642aa45b8a23c3a3   (core SVF primitive)
+#   - doctest   v2.5.2                                      (host-side test runner)
+#
+# Pins captured from the upstream repos (real refs); first-fetch verification
+# happens the first time each target's preset is configured on a machine with the
+# matching toolchain (desktop / daisy / teensy):
+#   - JUCE                    8.0.14    (workbench + plugin)
+#   - clap-juce-extensions    16e9d4c   (CLAP export, plugin only)
+#   - libDaisy                c02245d   (daisy adapter)
+#   - Teensy cores            a664eff   (teensy adapter)
+#   - Teensy Audio Library    3039be2   (teensy adapter)
+#
+# Dependencies are fetched lazily: a dependency is only declared when a target
+# that needs it is enabled, so the `test` preset pulls only DaisySP + doctest.
+
+include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)
+
+# --- Core: DaisySP (platform-independent pure-DSP math; wrapped by core/primitives)
+# Needed by core/ on every target, so it is always declared. DaisySP ships its own
+# CMakeLists that defines the `DaisySP` static-lib target with the correct (bare-
+# name) include directories for its sources; we use it directly rather than
+# re-globbing. It is portable C++ (no platform headers), so it builds host-side.
+CPMAddPackage(
+  NAME DaisySP
+  GITHUB_REPOSITORY electro-smith/DaisySP
+  GIT_TAG 599511b740f8f3a9b8db72a0642aa45b8a23c3a3
+)
+
+if(TARGET DaisySP)
+  set_target_properties(DaisySP PROPERTIES POSITION_INDEPENDENT_CODE ON)
+endif()
+
+# --- Host-side tests: doctest
+if(ACFX_BUILD_TESTS)
+  CPMAddPackage(
+    NAME doctest
+    GITHUB_REPOSITORY doctest/doctest
+    GIT_TAG v2.5.2
+  )
+endif()
+
+# --- Desktop (workbench + plugin): JUCE 8, plus clap-juce-extensions for CLAP.
+if(ACFX_BUILD_DESKTOP)
+  CPMAddPackage(
+    NAME JUCE
+    GITHUB_REPOSITORY juce-framework/JUCE
+    GIT_TAG 8.0.14
+  )
+  CPMAddPackage(
+    NAME clap-juce-extensions
+    GITHUB_REPOSITORY free-audio/clap-juce-extensions
+    GIT_TAG 16e9d4ca7b1e86c76e04584b2c08e85a764bcda8
+  )
+endif()
+
+# --- Daisy: libDaisy (provides the STM32 HAL + audio callback glue).
+if(ACFX_BUILD_DAISY)
+  CPMAddPackage(
+    NAME libDaisy
+    GITHUB_REPOSITORY electro-smith/libDaisy
+    GIT_TAG c02245d22b38acad3916d9c2f156bcba34fa15af
+    DOWNLOAD_ONLY YES
+  )
+endif()
+
+# --- Teensy: Teensy cores + Audio Library.
+if(ACFX_BUILD_TEENSY)
+  CPMAddPackage(
+    NAME teensy_cores
+    GITHUB_REPOSITORY PaulStoffregen/cores
+    GIT_TAG a664effb008d1ac8d8f00f3f19b47c0d1ea46e3b
+    DOWNLOAD_ONLY YES
+  )
+  CPMAddPackage(
+    NAME teensy_audio
+    GITHUB_REPOSITORY PaulStoffregen/Audio
+    GIT_TAG 3039be2773e86daf1f381a1e8bdc1e6a55ed11f1
+    DOWNLOAD_ONLY YES
+  )
+endif()


### cmake/toolchains/daisy.cmake
diff --git a/cmake/toolchains/daisy.cmake b/cmake/toolchains/daisy.cmake
new file mode 100644
index 0000000..03f3d97
--- /dev/null
+++ b/cmake/toolchains/daisy.cmake
@@ -0,0 +1,35 @@
+# Toolchain — Daisy (STM32H750, Cortex-M7) via arm-none-eabi-gcc.
+#
+# Daisy's SoC is an STM32H750IB (Cortex-M7 w/ double-precision FPU). These flags
+# match the libDaisy/Daisy bootloader expectations. The core/ sources are
+# platform-independent; this file only describes how the cross-compiler is
+# invoked for the daisy adapter target.
+
+set(CMAKE_SYSTEM_NAME Generic)
+set(CMAKE_SYSTEM_PROCESSOR arm)
+
+# A bare-metal cross toolchain cannot link a hosted test executable; build a
+# static library for CMake's compiler probe instead.
+set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
+
+find_program(ARM_CC  arm-none-eabi-gcc)
+find_program(ARM_CXX arm-none-eabi-g++)
+if(NOT ARM_CC OR NOT ARM_CXX)
+  message(FATAL_ERROR
+    "arm-none-eabi-gcc / arm-none-eabi-g++ not found on PATH. Install the ARM "
+    "embedded toolchain to build the daisy preset (no host-side fallback).")
+endif()
+
+set(CMAKE_C_COMPILER   "${ARM_CC}")
+set(CMAKE_CXX_COMPILER "${ARM_CXX}")
+set(CMAKE_ASM_COMPILER "${ARM_CC}")
+
+set(_daisy_cpu_flags "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
+set(CMAKE_C_FLAGS_INIT   "${_daisy_cpu_flags}")
+set(CMAKE_CXX_FLAGS_INIT "${_daisy_cpu_flags} -fno-exceptions -fno-rtti")
+set(CMAKE_EXE_LINKER_FLAGS_INIT "${_daisy_cpu_flags} --specs=nano.specs --specs=nosys.specs")
+
+set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
+set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
+set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
+set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)


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

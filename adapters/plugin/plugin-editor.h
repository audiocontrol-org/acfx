#pragma once

#include <cstddef>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "analysis/live-readout.h"
#include "primitives/analysis/capture-probe.h"

// The plugin's live harmonic readout editor (T031, US5, FR-014/FR-016). Mirrors
// adapters/workbench/harmonic-readout.{h,cpp}'s split for the plugin host: the
// audio thread's ONLY interaction is the RT-safe CaptureProbeRing::push()
// (PluginProcessor::processBlock, bounded copy, no analysis, no allocation); this
// message-thread editor drains the ring through the SAME SHARED
// acfx::analysis::LiveReadout (host/analysis/live-readout.h) the workbench and the
// offline suites use, and paints a broadband harmonic spectrum + running THD+N
// figure. No analysis math ever runs on the audio callback (FR-016): only push()
// does.
//
// adapters/plugin does NOT depend on adapters/workbench (adapters never depend on
// each other, only on host/ and core/) -- this is a small plugin-local editor that
// mirrors the workbench's display, both built from the SAME shared engine header.
// Factoring the JUCE display Component itself into a shared location is a
// follow-up, not required here: the one-engine guarantee (FR-015) is about
// host/analysis/live-readout.h, which both adapters already include directly.
//
// Deviation from the workbench pattern: the workbench's HarmonicReadout is a
// permanent child of a long-lived main window, so prepareToPlay can push a fresh
// sample rate into it directly (HarmonicReadout::prepare()). A plugin editor is
// created and destroyed every time the host opens/closes the GUI, and may not
// exist yet when the host calls prepareToPlay -- so instead this editor PULLS the
// processor's current sample rate on every timer tick and rebuilds the readout if
// it changed, rather than being pushed to.
//
// The plugin has no built-in test-tone generator either (US5's stimulus is
// whatever the host/DAW track feeds the plugin), so -- exactly like the workbench
// -- the fundamental is an operator-set control here, not auto-detected: feed a
// known tone and dial its frequency in for a meaningful reading.

namespace acfx::plugin {

class PluginProcessor; // full definition only needed in plugin-editor.cpp

// FR-027 default transform size (~170 ms @ 48 kHz); mirrors
// adapters/workbench/harmonic-readout.h exactly (same shared engine, same default
// window/cadence). Ring capacity adds a margin generous enough to absorb audio
// arriving between ~20 Hz timer drains without overrunning
// (contracts/capture-probe-api.md: capacity >= one window + margin).
inline constexpr std::size_t kHarmonicWindowSize = 8192;
inline constexpr std::size_t kHarmonicRingCapacity = kHarmonicWindowSize + 4096;
inline constexpr int kHarmonicNumHarmonics = 6; // mirrors the shipped fixed-6-bin convention

using HarmonicProbe = acfx::CaptureProbeRing<kHarmonicRingCapacity>;

class PluginEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    // MESSAGE THREAD: the processor owns the RT capture probe (pushed to from
    // processBlock) and the current sample rate (set in prepareToPlay); this
    // editor only ever reads them, from the Timer below.
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildReadout(); // message thread only; (re)constructs readout_ from current controls

    PluginProcessor& processor_;
    HarmonicProbe& probe_;
    double sampleRate_ = 48000.0;
    std::unique_ptr<acfx::analysis::LiveReadout<kHarmonicRingCapacity>> readout_;

    juce::Label fundamentalLabel_;
    juce::Slider fundamentalSlider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

} // namespace acfx::plugin

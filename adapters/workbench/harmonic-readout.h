#pragma once

#include <cstddef>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "analysis/live-readout.h"
#include "primitives/analysis/capture-probe.h"

// The live harmonic readout for the workbench (T030, US5, FR-014/FR-016). Mirrors
// level-meter.h's split: the audio thread's ONLY interaction is the RT-safe
// CaptureProbeRing::push() (bounded copy, no analysis, no allocation); a message-
// thread Timer drains the ring through the SHARED acfx::analysis::LiveReadout
// (host/analysis/live-readout.h) -- the SAME engine the offline suites call -- and
// repaints a broadband harmonic spectrum + running THD+N figure. No analysis math
// ever runs on the audio callback (FR-016): only push() does.
//
// The shared engine's harmonicSpectrum()/thdPlusN() (host/analysis/spectrum.h,
// thdn.h) are referenced to a known fundamental via the exact leakage-free
// Goertzel path, not a fundamental-blind broadband FFT. The workbench has no
// built-in test-tone generator -- US5's stimulus is whatever the operator plays
// through the source bar -- so the fundamental is an operator-set control here,
// not an auto-detected one: feed a known tone (e.g. a sine file via the source
// bar) and dial its frequency in for a meaningful reading.

namespace acfx::workbench {

// FR-027 default transform size (~170 ms @ 48 kHz). Ring capacity adds a margin
// generous enough to absorb audio arriving between ~20 Hz timer drains without
// overrunning (contracts/capture-probe-api.md: capacity >= one window + margin).
inline constexpr std::size_t kHarmonicWindowSize = 8192;
inline constexpr std::size_t kHarmonicRingCapacity = kHarmonicWindowSize + 4096;
inline constexpr int kHarmonicNumHarmonics = 6; // mirrors the shipped fixed-6-bin convention

using HarmonicProbe = acfx::CaptureProbeRing<kHarmonicRingCapacity>;

class HarmonicReadout final : public juce::Component, private juce::Timer {
public:
    // AUDIO THREAD: the caller (workbench-app.cpp's getNextAudioBlock) owns the
    // probe and calls its push() directly -- this component only ever reads it,
    // from the message-thread Timer below.
    explicit HarmonicReadout(HarmonicProbe& probe);
    ~HarmonicReadout() override;

    // MESSAGE THREAD: (re)configure the shared LiveReadout for the device's actual
    // sample rate. Call from prepareToPlay, mirroring ProcessorNode::prepare(ctx)
    // -- the analysis engine is never reachable from the audio path itself
    // (FR-016), only this message-thread setup touches it.
    void prepare(double sampleRate);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildReadout(); // message thread only; (re)constructs readout_ from current controls

    HarmonicProbe& probe_;
    double sampleRate_ = 48000.0;
    std::unique_ptr<acfx::analysis::LiveReadout<kHarmonicRingCapacity>> readout_;

    juce::Label fundamentalLabel_;
    juce::Slider fundamentalSlider_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicReadout)
};

} // namespace acfx::workbench
